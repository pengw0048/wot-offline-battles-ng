"""Original compiled-scene destructible metadata and state transitions."""
import struct
import unicorn
from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_EBP,UC_X86_REG_ECX,UC_X86_REG_EIP,UC_X86_REG_ESP
from stock_ground_decals_oracle import OriginalGroundDecalContacts
import stock_runtime_services_oracle as base


class OriginalStaticDestructibles(OriginalGroundDecalContacts):
    def __init__(self,executable):
        super(OriginalStaticDestructibles,self).__init__(executable)
        self.feature=base.MARKS+0x80000
        self.state=base.MARKS+0x81000
        self.models=base.MARKS+0x82000
        self.arrays=base.MARKS+0x83000
        self.vectors=base.MARKS+0x90000
        self.vector_values={}
        self.vector_storage={}
        self.events=[]
        self.native_hooks.update({0x13dac1d:self.zero,0x83f4d0:self.push,
            0x6c11a0:self.bounds,0xabab20:lambda:self.event('bounds',3),0xabacc0:lambda:self.event('changed',2),
            0x6c186b:lambda:self.u.emu_stop()})
        for address in (0x13dac1d,0x83f4d0,0x6c11a0,0xabab20,0xabacc0,0x6c186b):
            self.u.hook_add(unicorn.UC_HOOK_CODE,lambda u,a,s,d:self.native_hooks[a](),begin=address,end=address)

    def zero(self):
        self.u.mem_write(self._arg(1),bytes([self._arg(2)&255])*self._arg(3));self._return(self._arg(1))

    def stdcall(self,value,count):
        stack=self.u.reg_read(UC_X86_REG_ESP);self._return(value);self.u.reg_write(UC_X86_REG_ESP,stack+4+count*4)

    def push(self):
        target=self.u.reg_read(UC_X86_REG_ECX)
        if target not in self.vector_values:self.vector_storage[target]=self.vectors+len(self.vector_values)*0x1000
        values=self.vector_values.setdefault(target,[])
        value=struct.unpack('<I',self.u.mem_read(self._arg(1),4))[0];values.append(value)
        storage=self.vector_storage[target]
        self.u.mem_write(storage,struct.pack('<%dI'%len(values),*values))
        self.u.mem_write(target,struct.pack('<3I',storage,storage+len(values)*4,storage+0x1000))
        self.stdcall(0,1)

    def bounds(self):
        self.u.mem_write(self._arg(1),bytes(24));self._return(self._arg(1))

    def event(self,name,count):
        self.events.append((name,[self._arg(index)for index in range(1,count+1)]));self.stdcall(0,count)

    def destroy_core(self,index,activity,model_indices,types,fragile,triples,auxiliary):
        u=self.u;self.vector_values={};self.vector_storage={};self.events=[]
        self.heap=base.MARKS+0x40000
        u.mem_write(self.feature-0x100,bytes(0x300));u.mem_write(self.state,bytes(0x100));u.mem_write(self.models,bytes(0x200))
        arrays=self.arrays
        def values(offset,data):u.mem_write(self.state+offset,struct.pack('<I',data[0]));u.mem_write(data[0],data[1])
        transforms=arrays;flags=arrays+0x1000;models=arrays+0x2000;links=arrays+0x3000;aux=arrays+0x4000;info=arrays+0x5000;fragiles=arrays+0x6000
        identity=[0.]*16
        for axis in range(4):identity[axis*4+axis]=1.
        values(8,(transforms,struct.pack('<%df'%(len(activity)*16),*(identity*len(activity)))))
        values(0x14,(flags,b''.join(struct.pack('<II',0,int(v))for v in activity)))
        values(0x20,(models,struct.pack('<%dI'%len(model_indices),*model_indices)))
        values(0x80,(links,b''.join(struct.pack('<3I',*v)for v in triples)))
        values(0x8c,(aux,struct.pack('<%dI'%len(auxiliary),*auxiliary)))
        u.mem_write(self.state+0x98,struct.pack('<I',self.models));u.mem_write(self.models+0x28,struct.pack('<I',info));u.mem_write(info,b''.join(struct.pack('<2I',*v)for v in types))
        u.mem_write(self.models+0xa0,struct.pack('<I',fragiles));u.mem_write(fragiles,b''.join(struct.pack('<9I',*v)for v in fragile))
        u.mem_write(self.feature+0x60,struct.pack('<I',self.state))
        scene=base.MARKS+0x7d000;u.mem_write(self.feature-0x20,struct.pack('<I',scene));u.mem_write(scene,struct.pack('<I',scene+0x100))
        u.mem_write(0x1d59546,b'\0');u.mem_write(0x1d26733,b'\x02');u.mem_write(0x1d34599,b'\0')
        options=base.MARKS+0x7e000;u.mem_write(options,bytes(3))
        stack=base.STACK+0x80000;u.mem_write(stack,struct.pack('<3I',base.SENTINEL,index,options));u.reg_write(UC_X86_REG_ESP,stack);u.reg_write(UC_X86_REG_ECX,self.feature)
        u.emu_start(0x6c1300,0x6c186c,count=100000)
        assert u.reg_read(UC_X86_REG_EIP)==0x6c186b
        frame=u.reg_read(UC_X86_REG_EBP)
        def vector(offset):
            begin,end=struct.unpack('<2I',u.mem_read(frame+offset,8));return list(struct.unpack('<%dI'%((end-begin)//4),u.mem_read(begin,end-begin)))if end>begin else []
        return {'activity':[struct.unpack('<I',u.mem_read(flags+8*n+4,4))[0]for n in range(len(activity))],
            'models':list(struct.unpack('<%dI'%len(model_indices),u.mem_read(models,4*len(model_indices)))),
            'added':vector(-0xa4),'removed':vector(-0x98),'events':list(self.events)}

    def metadata(self,kind,entry,words,effect):
        u=self.u;u.mem_write(self.feature,bytes(0x100));u.mem_write(self.state,bytes(0x100));u.mem_write(self.models,bytes(0x200))
        selected=self.arrays;info=selected+0x100;record=selected+0x200;strings=selected+0x1000;argument=selected+0x2000;out=selected+0x3000
        u.mem_write(self.feature+0x60,struct.pack('<I',self.state))
        u.mem_write(self.state+0x20,struct.pack('<I',selected));u.mem_write(selected,bytes(4));u.mem_write(self.state+0x98,struct.pack('<I',self.models))
        u.mem_write(self.models+0x28,struct.pack('<I',info));u.mem_write(info,struct.pack('<2I',kind,0))
        u.mem_write(self.models+(0x94 if kind==1 else 0xa0),struct.pack('<I',record));u.mem_write(record,struct.pack('<%dI'%len(words),*words))
        u.mem_write(self.models+0xb8,struct.pack('<I',strings))
        text=effect.encode();u.mem_write(argument,text+b'\0'+bytes(31-len(text)));u.mem_write(argument+0x10,struct.pack('<2I',len(text),15))
        if len(text)>=16:
            u.mem_write(argument,struct.pack('<I',argument+0x40));u.mem_write(argument+0x40,text+b'\0');u.mem_write(argument+0x14,struct.pack('<I',31))
        def compare():
            a,b,n=self._arg(1),self._arg(2),self._arg(3)
            left,right=bytes(u.mem_read(a,n)),bytes(u.mem_read(b,n));self._return((left>right)-(left<right))
        if 0x613e10 not in self.native_hooks:
            self.native_hooks[0x613e10]=compare
            u.hook_add(unicorn.UC_HOOK_CODE,lambda u,a,s,d:self.native_hooks[a](),begin=0x613e10,end=0x613e10)
        else:self.native_hooks[0x613e10]=compare
        def call(address,args,float_result=False):
            target=base.SENTINEL
            if float_result:
                target=out+0x100
                # Store the actual x87 return value through original guest x86.
                u.mem_write(target,b'\xd9\x1d'+struct.pack('<I',out)+b'\xe9'+struct.pack('<i',base.SENTINEL-(target+11)))
            stack=base.STACK+0x80000;u.mem_write(stack,struct.pack('<%dI'%(len(args)+1),target,*args));u.reg_write(UC_X86_REG_ESP,stack);u.reg_write(UC_X86_REG_ECX,self.feature)
            u.emu_start(address,0,count=100000)
            assert u.reg_read(UC_X86_REG_EIP)==base.SENTINEL
            return struct.unpack('<I',u.mem_read(out,4))[0]if float_result else u.reg_read(UC_X86_REG_EAX)
        category=call(0x6c2430,[0]);name=call(0x6c2120,[0,argument]);scale=call(0x6c2290,[0],True)
        call(0x6c22f0,[out,0]);physics=bytes(u.mem_read(out,64))
        return category,(name-strings)//24 if name else 0,scale,physics
