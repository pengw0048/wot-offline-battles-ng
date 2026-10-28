"""Execute #1513 wg_addWaterRipples f625e0 unchanged in Unicorn.

Only Python conversions and Water::addMovement (a279d0) are hooked. The
original wrapper constructs both endpoints and forwards every registered
water in order, including when the native owner returns false.
"""
import struct
import unicorn
from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_ECX,UC_X86_REG_EIP,UC_X86_REG_ESP
import stock_runtime_services_oracle as base

WATERS=base.MARKS+0x9000
ARRAY=WATERS+0x100


class Original(base.OriginalRuntimeServices):
    def __init__(self,executable):
        super(Original,self).__init__(executable)
        for address,hook in {0x690240:self.vector,0xa2cd40:lambda:self._return(WATERS),
                0xa279d0:self.movement,0x13d809e:self.cookie}.items():
            self.hooks[address]=hook
            self.u.hook_add(unicorn.UC_HOOK_CODE,lambda u,a,s,d:self.hooks[a](),begin=address,end=address)

    def cookie(self):
        value=self.u.reg_read(UC_X86_REG_EAX);self._return(value)

    def vector(self):
        index=self._arg(1)-base.ITEMS
        self.calls.append(['setData','Vector3',index,self._cstr(self._arg(3))])
        if self.fail_at==index:return self._return(0xffffffff)
        self.u.mem_write(self._arg(2),struct.pack('<3f',*self.values[index]));self._return(0)

    def movement(self):
        self.movements.append([self.u.reg_read(UC_X86_REG_ECX),
            list(struct.unpack('<3f',self.u.mem_read(self._arg(1),12))),
            list(struct.unpack('<3f',self.u.mem_read(self._arg(2),12))),self._float(3)])
        esp=self.u.reg_read(UC_X86_REG_ESP);self._return(0)
        self.u.reg_write(UC_X86_REG_ESP,esp+16)

    def run(self,position,diameter,count=2,argc=2,fail_at=None):
        self.values=[position,diameter];self.argc=argc;self.fail_at=fail_at
        self.calls=[];self.movements=[]
        self.u.mem_write(WATERS,struct.pack('<3I',ARRAY,ARRAY+4*count,ARRAY+4*count))
        if count:self.u.mem_write(ARRAY,struct.pack('<%dI'%count,*[WATERS+0x200+0x400*i for i in range(count)]))
        esp=base.STACK+0x80000
        self.u.mem_write(esp,struct.pack('<3I',base.SENTINEL,0,base.ARGS))
        self.u.reg_write(UC_X86_REG_ESP,esp)
        self.u.emu_start(0xf625e0,base.SENTINEL+1,count=100000)
        assert self.u.reg_read(UC_X86_REG_EIP)==base.SENTINEL
        result=self.u.reg_read(UC_X86_REG_EAX)
        return ('None' if result==base.NONE else 'NULL'),self.calls,list(self.movements)


class OriginalWater(Original):
    """Run a279d0 on a real-layout Water, recording its cell virtual calls.

    D3DX transform and the BoundingBox.clip environment operation use the
    fixture's translation/box. Guards, cell selection and normalized movement
    production run as original instructions.
    """
    def __init__(self,executable):
        super(OriginalWater,self).__init__(executable)
        import math
        self.math=math
        self.hooks[0xa279d0]=lambda:None
        self.water=WATERS+0x2000;self.cells=WATERS+0x3000;self.table=WATERS+0x3400
        self.camera=WATERS+0x4000;self.points=WATERS+0x5000
        update=WATERS+0x6000;add=update+0x10;ceil=update+0x20
        self.u.mem_write(self.table+0x10,struct.pack('<2I',update,add))
        for i in range(4):
            self.u.mem_write(self.cells+4*i,struct.pack('<I',self.cells+0x100+i*0x100))
            self.u.mem_write(self.cells+0x100+i*0x100,struct.pack('<I',self.table))
        self.u.mem_write(ceil,b'\xdd\x44\x24\x04\xc3')
        self.u.mem_write(0x13dad8b,b'\xe9'+struct.pack('<i',ceil-(0x13dad8b+5)))
        for address,hook in {0x9288d0:lambda:self._return(self.camera),
                0x9288e0:lambda:self._return(self.camera),0x11c3bdc:self.transform,
                0x11bc120:self.clip,update:self.update_cell,add:self.add_cell}.items():
            if address not in self.hooks:
                self.u.hook_add(unicorn.UC_HOOK_CODE,lambda u,a,s,d:self.hooks[a](),begin=address,end=address)
            self.hooks[address]=hook
        self.u.hook_add(unicorn.UC_HOOK_CODE,self.ceil,begin=ceil,end=ceil)

    def ret_pop(self,value,pop):
        esp=self.u.reg_read(UC_X86_REG_ESP);self._return(value);self.u.reg_write(UC_X86_REG_ESP,esp+4+pop)

    def transform(self):
        point=list(struct.unpack('<3f',self.u.mem_read(self._arg(2),12)));point[1]-=20.
        self.u.mem_write(self._arg(1),struct.pack('<3f',*point));self.ret_pop(self._arg(1),12)

    def clip(self):
        a=list(struct.unpack('<3f',self.u.mem_read(self._arg(1),12)))
        b=list(struct.unpack('<3f',self.u.mem_read(self._arg(2),12)))
        delta=[b[i]-a[i] for i in range(3)];low,high=0.,1.
        for i,(minimum,maximum) in enumerate(((-50,50),(-10,0),(-40,40))):
            if not delta[i]:
                if not minimum<=a[i]<=maximum:return self.ret_pop(0,12)
                continue
            first,last=(minimum-a[i])/delta[i],(maximum-a[i])/delta[i]
            if first>last:first,last=last,first
            low,high=max(low,first),min(high,last)
            if low>high:return self.ret_pop(0,12)
        self.u.mem_write(self._arg(1),struct.pack('<3f',*[a[i]+delta[i]*low for i in range(3)]))
        self.u.mem_write(self._arg(2),struct.pack('<3f',*[a[i]+delta[i]*high for i in range(3)]))
        self.ret_pop(1,12)

    def ceil(self,u,address,size,data):
        value=struct.unpack('<d',u.mem_read(self.u.reg_read(UC_X86_REG_ESP)+4,8))[0]
        u.mem_write(self.u.reg_read(UC_X86_REG_ESP)+4,struct.pack('<d',float(self.math.ceil(value))))

    def update_cell(self):
        self.resets+=1;self._return(0)

    def add_cell(self):
        cell=(self.u.reg_read(UC_X86_REG_ECX)-(self.cells+0x100))//0x100
        a=list(struct.unpack('<3f',self.u.mem_read(self._arg(1),12)))
        b=list(struct.unpack('<3f',self.u.mem_read(self._arg(2),12)))
        self.impulses.append([cell]+a+b+[self._float(3)]);self.ret_pop(0,12)

    def add(self,position,diameter,ready=True,visible=True):
        self.impulses=[];self.resets=0
        u=self.u;water=self.water
        u.mem_write(water,b'\0'*0x400)
        u.mem_write(water+0x2a5,bytes([int(ready)]));u.mem_write(water+0x2a1,bytes([int(visible)]))
        u.mem_write(water+0x26c,struct.pack('<3I',self.cells,self.cells+16,self.cells+16))
        u.mem_write(water+0x28,struct.pack('<2f',100.,80.));u.mem_write(water+0x34,struct.pack('<f',1.))
        u.mem_write(water+0x9c,struct.pack('<f',50.));u.mem_write(self.camera+0x188,struct.pack('<4f',0.,30.,0.,1.))
        a=[position[0],position[1]+5.,position[2]];b=[position[0],position[1]-5.,position[2]]
        u.mem_write(self.points,struct.pack('<6f',*(a+b)))
        esp=base.STACK+0x80000
        u.mem_write(esp,struct.pack('<4I',base.SENTINEL,self.points,self.points+12,struct.unpack('<I',struct.pack('<f',diameter))[0]))
        u.reg_write(UC_X86_REG_ESP,esp);u.reg_write(UC_X86_REG_ECX,water)
        u.emu_start(0xa279d0,base.SENTINEL+1,count=100000)
        assert u.reg_read(UC_X86_REG_EIP)==base.SENTINEL
        return u.reg_read(UC_X86_REG_EAX)&255,self.resets,list(self.impulses)
