"""Execute #1513 scale conversion/wrappers, native draw and deferred/job code.

The Python C API, particle iterators/renderer submission and D3DX primitives
are explicit hook boundaries. Original scalar conversion, offsets, guards,
transform construction, per-item storage and sprite size/cull instructions run.
"""
import math
import struct
import unicorn
from unicorn.x86_const import (UC_X86_REG_EBP,UC_X86_REG_EBX,UC_X86_REG_EDX,UC_X86_REG_ESI,UC_X86_REG_EDI,
    UC_X86_REG_EIP,UC_X86_REG_ESP,UC_X86_REG_XMM5)
from stock_flash_input_mode_oracle import OriginalFlashInputMode,OBJECT,VALUE,KIND_TYPES
from stock_ballistics_oracle import Original as Base,DATA,STACK,bits,f32


class Wrappers(OriginalFlashInputMode):
    def __init__(self,exe):
        super(Wrappers,self).__init__(exe)
        self.hooks[0x68ed00]=self.get_float
        self.u.hook_add(unicorn.UC_HOOK_CODE,self._hook,begin=0x68ed00,end=0x68ed00)
    def get_float(self):
        self.result=struct.unpack('<f',self.u.mem_read(self.u.reg_read(UC_X86_REG_ESP)+4,4))[0]
        self._return(VALUE)
    def run(self,meta,kind,value):
        offset,getter,setter=(0x30,0x8e7070,0x9dcef0) if meta else (0x54,0x9de380,0x9ded90)
        self.u.mem_write(OBJECT,b'\0'*0x200)
        self.u.mem_write(OBJECT+offset,struct.pack('<f',7.25))
        self.value=value if kind in ('float','int','long','bool') else 0
        self.u.mem_write(VALUE,struct.pack('<II',1,KIND_TYPES[kind]))
        status=self._call(setter,0,OBJECT+4,VALUE,0)
        error=self.trace.get('error')
        self._call(getter,0,OBJECT+4,0)
        return status,error,self.result


class Draw(Base):
    def __init__(self,exe):
        super(Draw,self).__init__(exe)
        self.owner=DATA+0x10000;self.renderer=DATA+0x11000;self.world=DATA+0x12000
        self.entries=[];self.nonempty=True
        callbacks={0x11c3c06:self.normal,0x13d809e:lambda:self.ret(0,0),
            DATA+0x13000:lambda:self.ret(int(self.nonempty),0),DATA+0x13010:self.render,
            0x9e7340:lambda:self.iterator(0x200),0x9e6c00:lambda:self.iterator(0x100)}
        for address,function in callbacks.items():
            self.hooks[address]=function
            self.u.hook_add(unicorn.UC_HOOK_CODE,lambda u,a,s,d:self.hooks[a](),begin=address,end=address)
        self.put(0x1d20c6c,'<I',DATA+0x14000)
        self.put(DATA+0x14000+0x8c,'<B',1)
        self.put(self.owner+0x50,'<I',self.renderer)
        self.put(self.owner+0x10,'<I',DATA+0x15000)
        self.put(DATA+0x15000,'<I',DATA+0x16000)
        self.put(DATA+0x16000+0x18,'<I',DATA+0x13000)
        self.put(self.renderer,'<I',DATA+0x17000)
        self.put(DATA+0x17000+8,'<I',DATA+0x13010)
        self.put(self.owner+0x70,'<3f',1,2,3)
        self.matrix=[1,0,0,0,0,1,0,0,0,0,1,0,10,20,30,1]
        self.put(self.world,'<16f',*self.matrix)
    def normal(self):
        point=self.floats(self.arg(2),3);matrix=self.floats(self.arg(3),16)
        out=[f32(f32(f32(point[0]*matrix[i])+f32(point[1]*matrix[4+i]))+f32(point[2]*matrix[8+i])) for i in range(3)]
        self.put(self.arg(1),'<3f',*out);self.ret(self.arg(1),12)
    def iterator(self,value):
        self.put(self.arg(1),'<I',value);self.ret(self.arg(1),4)
    def render(self):
        self.entries.append((self.floats(self.arg(2),16),self.farg(3),self.arg(4),self.arg(5),self.arg(6),self.arg(7)))
        self.ret(0,28)
    def run(self,scale):
        self.entries=[];self.call(0x9e70c0,self.owner,[123,self.world,bits(scale),1,bits(0)])
        return self.entries


def equal(a,b):return math.isnan(a) and math.isnan(b) or a==b


def audit(executable):
    count=0;wrappers=Wrappers(executable)
    # Execute the original constructor's exact default stores without replacing
    # allocation/base-class construction with a guessed whole-object fixture.
    for address,register,offset in ((0x9db77f,UC_X86_REG_ESI,0x30),
                                  (0x9de6cd,UC_X86_REG_EDI,0x54),
                                  (0x9dd510,UC_X86_REG_ESI,0x54)):
        wrappers.u.reg_write(register,OBJECT)
        wrappers.u.mem_write(OBJECT+offset,b'\x00'*4)
        wrappers.u.emu_start(address,address+7,count=1)
        assert struct.unpack('<f',wrappers.u.mem_read(OBJECT+offset,4))[0]==1.0
        count+=1
    values=[('float',v) for v in (0.,1.,2.5,-2.,1e-20,1e38,float('inf'),float('-inf'),float('nan'))]
    values += [('int',v) for v in (0,-1,17,2147483647)]
    values += [('bool',True),('long',17),('long',1<<32),('long',-1),('long',1<<80),('str','3'),('none',None)]
    for meta in (False,True):
        for kind,value in values:
            status,error,result=wrappers.run(meta,kind,value)
            rejected=kind in ('str','none') or kind=='long' and not 0<=value<(1<<32)
            assert bool(status)==rejected,(meta,kind,value,status,error)
            assert bool(error)==rejected
            assert equal(result,7.25 if rejected else f32(float(value))),(meta,kind,value,result)
            count+=1
    draw=Draw(executable)
    for scale in (0.,1.,2.5,-2.,1e-10,100.):
        records=draw.run(scale);assert len(records)==1
        matrix,actual,group,begin,end,bounds=records[0]
        assert actual==f32(scale) and group==1 and begin==0x100 and end==0x200 and bounds==draw.owner+0x84
        expected=[scale,0,0,0,0,scale,0,0,0,0,scale,0,f32(10+scale),f32(20+2*scale),f32(30+3*scale),1]
        assert all(equal(a,f32(b)) for a,b in zip(matrix,expected)),(scale,matrix,expected)
        # Original DrawItem::set snapshots the scalar and all other draw inputs.
        item=DATA+0x18000
        draw.call(0xa10760,item,[draw.renderer,draw.world,bits(scale),0x100,0x200])
        assert draw.floats(item+0x50,1)==[f32(scale)]
        assert draw.floats(item+8,16)==draw.matrix
        assert struct.unpack('<2I',draw.u.mem_read(item+0x48,8))==(0x100,0x200)
        count+=2
    draw.nonempty=False;assert draw.run(2.5)==[];count+=1
    draw.nonempty=True;draw.put(DATA+0x14000+0x8c,'<B',0);assert draw.run(2.5)==[];count+=1
    # The actual sprite CPU job's size/cull fragment. Unchanged instruction
    # decoding of packed particle size, scale multiply, compare and branch.
    stop=0xa02098
    draw.u.hook_add(unicorn.UC_HOOK_CODE,lambda u,a,s,d:u.emu_stop(),begin=stop,end=stop)
    frame=STACK+0xe0000;particle=DATA+0x19000;job=DATA+0x19100
    for scale in (0.,1.,2.5,-2.,1e-10):
        draw.u.reg_write(UC_X86_REG_EBP,frame);draw.u.reg_write(UC_X86_REG_EBX,particle)
        draw.u.reg_write(UC_X86_REG_EDX,job);draw.u.reg_write(UC_X86_REG_XMM5,bits(10.))
        draw.put(particle+0xe,'<H',4096);draw.put(job+0x14,'<f',scale)
        draw.put(frame-0x164,'<f',.0005)
        draw.u.emu_start(0xa01898,0xa018cc,count=100)
        packed_factor=draw.floats(0x1500a80,1)[0]
        expected=f32(f32(4096*packed_factor)*scale)
        assert equal(draw.floats(frame-0x12c,1)[0],expected)
        assert (draw.u.reg_read(UC_X86_REG_EIP)==stop)==(f32(10*.0005)>expected)
        count+=1
    return count
