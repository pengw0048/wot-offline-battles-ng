"""Run #1513 constructor sampling, detach bool conversion and matrix cache prefix.

The OS cursor getter and Python C API are explicit boundaries. Focus/in-window
checks (including the real 008b8630 helper), cache stores, detach flag writes,
argument guards and Script::setData(bool&) execute original instructions.
Unchanged downstream camera/D3DX lookAt/inverse is outside this state audit.
"""
import struct
import unicorn
from unicorn.x86_const import UC_X86_REG_EBP,UC_X86_REG_ECX,UC_X86_REG_EIP,UC_X86_REG_ESI,UC_X86_REG_ESP
from stock_flash_input_mode_oracle import (OriginalFlashInputMode,OBJECT,VALUE,KIND_TYPES,
    OTHER_TYPE,MARKS,STACK,INT_TYPE,LONG_TYPE,FLOAT_TYPE)


class Original(OriginalFlashInputMode):
    def __init__(self,executable):
        super(Original,self).__init__(executable)
        self.positions=[];self.reads=0;self.argc=1
        self.string_type=MARKS+0x4000;self.string=MARKS+0x5000
        self.u.mem_write(self.string_type+0x54,struct.pack('<I',0x8000000))
        compare=struct.unpack('<I',self.u.mem_read(0x1492088,4))[0]
        additions={0xfb3610:lambda:self._return(self.argc),0xfb3390:lambda:self._return(VALUE),
            0xfd64e0:lambda:self._return(self.string),compare:self.compare,
            0x68e100:self.arg_count,0x8bc2b0:lambda:self._return(MARKS+0x6000),0x8b8cd0:self.position}
        for address,function in additions.items():
            self.hooks[address]=function
            self.u.hook_add(unicorn.UC_HOOK_CODE,self._hook,begin=address,end=address)
        self.u.hook_add(unicorn.UC_HOOK_CODE,lambda u,a,s,d:u.emu_stop(),begin=0x668fbb,end=0x668fbb)
    def compare(self):
        self._return(int(self._cstr(self._arg(1)).lower()!=self._cstr(self._arg(2)).lower()))
    def arg_count(self):
        assert self._arg(2)==self._arg(3)==1
        self.trace['error']='argument count';self._return(0)
    def position(self):
        assert self.positions,'Unexpected cursor read'
        value=self.positions.pop(0);self.reads+=1
        self.u.mem_write(self._arg(1),struct.pack('<2f',*value));self._return(self._arg(1),4)
    def construct(self,point):
        self.positions=[point];self.reads=0
        self.u.reg_write(UC_X86_REG_EBP,STACK+0x90000)
        self.u.reg_write(UC_X86_REG_ESP,STACK+0x80000)
        self.u.reg_write(UC_X86_REG_ESI,OBJECT)
        self.u.emu_start(0x66714f,0x667176,count=1000)
        assert self.reads==1 and self.cached()==point and not self.detached()
    def cached(self):return tuple(struct.unpack('<2f',self.u.mem_read(OBJECT+0x10,8)))
    def detached(self):return bool(bytes(self.u.mem_read(OBJECT+0x18,1))[0])
    def detach(self,kind,value,argc=1):
        self.argc=argc;self.value=value if kind in ('int','bool','long','float') else 0
        kind_type=self.string_type if kind=='str' else KIND_TYPES[kind]
        self.u.mem_write(VALUE,struct.pack('<II',1,kind_type))
        self.u.mem_write(self.string,(value.encode('ascii') if kind=='str' else b'')+b'\0')
        result=self._call(0x667610,0,OBJECT+4,VALUE+0x300,0xbad00001)
        return result,self.trace.get('error')
    def matrix_prefix(self,focus,points):
        self.positions=list(points);self.reads=0
        self.u.mem_write(0x1d3532c,bytes([focus]))
        self.u.reg_write(UC_X86_REG_ESP,STACK+0x80000)
        self.u.mem_write(STACK+0x80000,struct.pack('<2I',0,MARKS+0x7000))
        self.u.reg_write(UC_X86_REG_ECX,OBJECT)
        self.u.emu_start(0x668f50,0,count=10000)
        assert self.u.reg_read(UC_X86_REG_EIP)==0x668fbb
        return self.cached(),self.reads


def audit(executable):
    original=Original(executable);count=0
    for point in ((.25,-.5),(2.,3.),(-1.,1.)):
        original.construct(point);count+=1
    cases=[('bool',False,False),('bool',True,True),('int',0,False),('int',-17,True),
           ('str','TrUe',True),('str','FALSE',False),('float',.5,None),('long',1,None),
           ('str','yes',None),('none',None,None)]
    for kind,value,expected in cases:
        original.construct((.25,-.5));before=original.detached()
        result,error=original.detach(kind,value)
        assert bool(error)==(expected is None),(kind,value,result,error)
        assert original.detached()==(before if expected is None else expected)
        count+=1
    for argc in (0,2,3):
        original.construct((.25,-.5));result,error=original.detach('bool',True,argc)
        assert result==0 and error and not original.detached();count+=1
    for detached in (False,True):
        for focus in (False,True):
            for point in ((.5,.75),(-1.,-1.),(1.,1.),(1.001,0.),(0.,-1.001),(float('nan'),0.)):
                original.construct((.25,-.5));original.detach('bool',detached)
                admitted=focus and not detached and all(-1<=x<=1 for x in point)
                # The native owner first checks inWindow using position(), then
                # samples position() again. Preserve this order and second value.
                second=(-.75,.125)
                expected=second if admitted else (.25,-.5)
                actual,reads=original.matrix_prefix(focus,[point,second])
                assert actual==expected,(detached,focus,point,actual,expected)
                assert reads==(2 if admitted else 1 if focus else 0)
                count+=1
    # Detach(false) changes only the flag: the next valid read refreshes it.
    original.construct((.25,-.5));original.detach('bool',True)
    assert original.matrix_prefix(True,[(.5,.75)])[0]==(.25,-.5)
    original.detach('bool',False);assert original.cached()==(.25,-.5)
    assert original.matrix_prefix(True,[(.5,.75),(-.75,.125)])[0]==(-.75,.125)
    return count+1
