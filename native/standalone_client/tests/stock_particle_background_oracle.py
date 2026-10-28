"""Execute #1513 MPSBackgroundTask constructor/completion/destructor code.

CPython allocation, weak-reference creation, tuple building and the final
Python call are explicit hooks. Dead/None/error decisions, result selection,
native reference adjustments and retirement branches are unchanged x86.
"""
import unicorn
from unicorn.x86_const import UC_X86_REG_ECX,UC_X86_REG_EAX,UC_X86_REG_EBX,UC_X86_REG_EBP
from stock_ballistics_oracle import Original as Base,DATA

NONE=0x1a5c798


class Original(Base):
    def __init__(self,executable):
        super(Original,self).__init__(executable)
        self.task=DATA+0x50000;self.name=self.task+0x100
        self.callback=self.task+0x200;self.weak=self.task+0x300
        self.source=self.task+0x400;self.tuple=self.task+0x500
        self.object_type=self.task+0x600
        self.events=[];self.kind='live';self.result=None
        hooks={0xfdf5a0:self.weakref,0xfc3650:self.clear,
            0x9db720:self.construct,0xfe1920:self.build,
            0x68e560:self.ask,0xfb3420:self.tuple_new,
            0x68ed50:lambda:self.ret(NONE,0),0xfb38d0:self.callable,
            0xfca730:self.invoke_python,0xfc3720:self.error,0xfc7680:self.print_error}
        for address,function in hooks.items():
            self.hooks[address]=function
            self.u.hook_add(unicorn.UC_HOOK_CODE,lambda u,a,s,d:self.hooks[a](),begin=address,end=address)

    def weakref(self):
        self.events.append(('weak',self.arg(1)))
        self.ret(0 if self.kind in ('none','nonweak') else self.weak,0)

    def clear(self):self.events.append(('clear',));self.ret(0,0)
    def error(self):self.events.append(('error',));self.ret(0,0)
    def print_error(self):self.events.append(('print',));self.ret(0,0)

    def construct(self):
        self.events.append(('wrapper',self.arg(1),self.arg(2)))
        value=self.u.reg_read(UC_X86_REG_ECX)
        self.put(value+4,'<II',10,self.object_type)
        self.result=value+4
        self.ret(value,8)

    def build(self):
        self.events.append(('tuple',self.arg(2)))
        self.put(self.tuple,'<II',20,self.object_type)
        self.ret(self.tuple,0)

    def ask(self):
        if self.kind=='not_callable':self.events.append(('error',))
        else:self.events.append(('deliver',self.result))
        self.ret(0,0)

    def tuple_new(self):
        self.put(self.tuple,'<IIII',20,self.object_type,1,0)
        self.ret(self.tuple,0)

    def callable(self):self.ret(self.kind!='not_callable',0)
    def invoke_python(self):self.events.append(('deliver',NONE));self.ret(NONE,0)

    def run(self,kind,loaded):
        self.kind=kind;self.events=[];self.result=None
        self.u.mem_write(self.task,bytes(0x700))
        self.u.mem_write(self.name,b'file\0')
        self.put(self.name+0x10,'<II',4,15)
        self.put(NONE,'<I',1000)
        self.put(self.callback,'<II',20,self.object_type)
        self.put(self.weak,'<III',20,self.object_type,self.callback)
        callback=0 if kind=='null' else NONE if kind=='none' else self.callback
        self.call(0x9ea5f0,self.task,[self.name,callback])
        constructor=list(self.events);self.events=[]
        if kind=='dead':self.put(self.weak+8,'<I',NONE)
        if loaded:
            self.put(self.task+0x3c,'<I',self.source)
            self.put(self.source+4,'<I',20)
        self.call(0x9eabd0,self.task,[0])
        complete=list(self.events);self.events=[]
        self.call(0x9ea730,self.task,[0])
        return constructor,complete

    def load_outcome(self,loaded,error):
        # Unchanged 009ea964..009ea98b is the background worker's verdict
        # after the resource loader returns and before result unwrapping.
        frame=DATA+0x90000
        self.put(self.task+0x3c,'<I',self.source)
        self.put(self.source+4,'<I',20)
        self.put(frame-0x23c,'<I',error)
        self.u.reg_write(UC_X86_REG_EAX,loaded)
        self.u.reg_write(UC_X86_REG_EBX,self.task)
        self.u.reg_write(UC_X86_REG_EBP,frame)
        self.u.emu_start(0x9ea964,0x9ea98b,count=100)
        import struct
        return struct.unpack('<I',self.u.mem_read(self.task+0x3c,4))[0]


def audit(executable):
    original=Original(executable);count=0
    for loaded in (False,True):
        for kind in ('live','dead','none','nonweak','null','not_callable'):
            constructor,complete=original.run(kind,loaded)
            if kind=='null':assert constructor==[]
            else:
                assert constructor[0][0]=='weak'
                assert (('clear',)in constructor)==(kind in ('none','nonweak'))
            wrappers=[event for event in complete if event[0]=='wrapper']
            delivered=[event for event in complete if event[0]=='deliver']
            assert len(wrappers)==int(loaded),(kind,loaded,complete)
            if kind=='live':
                assert len(delivered)==1
                assert (delivered[0][1]==NONE)==(not loaded)
            else:assert not delivered,(kind,loaded,complete)
            assert any(event[0]=='error'for event in complete)==(kind=='not_callable')
            count+=1
    for loaded in (False,True):
        for error in (False,True):
            assert original.load_outcome(loaded,error)==(original.source if loaded and not error else 0)
            count+=1
    return count
