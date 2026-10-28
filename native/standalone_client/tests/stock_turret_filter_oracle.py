"""Original #1513 turret factory, scalar receipt EMA, gravity and copy dispatch.

Allocator, Python tuple parsing, base construction/settings and RTTI are recorded
boundaries. The original scalar EMA and derived gravity/copy instructions execute;
no gravity formula is reimplemented here.
"""
import random
import struct
import unicorn
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_ESP
from stock_runtime_services_oracle import OriginalRuntimeServices, MARKS, STACK, SENTINEL, ARGS

OWNER=MARKS+0x20000
SOURCE=MARKS+0x21000
WRAPPER=MARKS+0x22000
LINEAR=MARKS+0x23000
ANGULAR=MARKS+0x23100
CURRENT=MARKS+0x24000
NEXT=MARKS+0x24100


def float32(value):return struct.unpack('<f',struct.pack('<f',value))[0]

def word(value):return struct.unpack('<I',struct.pack('<f',value))[0]


class OriginalTurretFilter(OriginalRuntimeServices):
    def __init__(self,executable):
        super().__init__(executable)
        self.events=[];self.base_ok=True;self.turret=True;self.allocation=WRAPPER
        self.add_hook(0x13d809e,lambda:self._return(0))
        self.add_hook(0x13d7fe9,self.allocate)
        self.add_hook(0x6e7170,self.wrapper_base)
        self.add_hook(0x6ef850,self.native_base)
        self.add_hook(0x6e7b00,self.apply_settings)
        self.add_hook(0x6f0460,self.receipt_boundary)
        self.add_hook(0x6ef7b0,lambda:self.events.append(['gravity']))
        self.add_hook(0x6f1d50,self.copy_base)
        self.add_hook(0x13dac3b,self.rtti)

    def add_hook(self,address,callback):
        self.hooks[address]=callback
        self.u.hook_add(unicorn.UC_HOOK_CODE,lambda u,a,s,d:self.hooks[a](),begin=address,end=address)

    def ret_pop(self,value,size):
        esp=self.u.reg_read(UC_X86_REG_ESP);self._return(value)
        self.u.reg_write(UC_X86_REG_ESP,esp+4+size)

    def start(self,address,args=(),ecx=OWNER):
        esp=STACK+0x80000
        self.u.mem_write(esp,struct.pack('<%dI'%(len(args)+1),SENTINEL,*args))
        self.u.reg_write(UC_X86_REG_ESP,esp);self.u.reg_write(UC_X86_REG_ECX,ecx)
        self.u.emu_start(address,SENTINEL+1,count=100000)
        return self.u.reg_read(UC_X86_REG_EAX)

    def allocate(self):
        self.events.append(['allocate',self._arg(1)])
        self.u.mem_write(self.allocation,bytes(self._arg(1)))
        self._return(self.allocation)

    def wrapper_base(self):
        self.events.append(['wrapper-base',self.u.reg_read(UC_X86_REG_ECX),self._arg(1)])
        self.ret_pop(self.u.reg_read(UC_X86_REG_ECX),4)

    def native_base(self):
        self.events.append(['native-base',self.u.reg_read(UC_X86_REG_ECX),self._arg(1)])
        self.ret_pop(self.u.reg_read(UC_X86_REG_ECX),4)

    def apply_settings(self):
        self.events.append(['settings',self.u.reg_read(UC_X86_REG_ECX),self._arg(1)])
        self.ret_pop(0,4)

    def receipt_boundary(self):
        self.events.append(['base-receipts'])
        self.u.mem_write(OWNER+0x300,self.receipt_state)
        self.ret_pop(0,16)

    def copy_base(self):
        self.events.append(['base-copy'])
        self.ret_pop(int(self.base_ok),4)

    def rtti(self):
        self.events.append(['turret-rtti'])
        self._return(SOURCE if self.turret else 0)

    def factory(self,argc=0,requested_type=0,keywords=0):
        self.events=[];self.calls=[];self.argc=argc;self.allocation=WRAPPER
        result=self.start(0x6e6270,(requested_type,ARGS,keywords),ecx=0)
        return result,list(self.events),list(self.calls)

    def create(self,reserved=0):
        self.events=[];self.allocation=OWNER
        self.u.mem_write(WRAPPER+0x3c,bytes([reserved]))
        result=self.start(0x6e63b0,ecx=WRAPPER)
        return result,list(self.events),self.u.mem_read(OWNER+0x480,1)[0]

    def copy(self,base_ok=True,turret=True,reserved=1):
        self.events=[];self.base_ok=base_ok;self.turret=turret
        self.u.mem_write(OWNER+0x480,b'\0');self.u.mem_write(SOURCE+0x480,bytes([reserved]))
        result=self.start(0x6e6440,(SOURCE,))
        return bool(result&255),self.u.mem_read(OWNER+0x480,1)[0],list(self.events)

    def receipt(self,initial,start,end,elapsed,valid=True):
        self.u.mem_write(OWNER+0x300,struct.pack('<12f',*initial))
        if valid:
            linear=[float32(b-a) for a,b in zip(start,end)]
            self.u.mem_write(LINEAR,struct.pack('<3f',*linear))
            self.u.mem_write(ANGULAR,bytes(12))
            self.start(0x6f0060,(LINEAR,ANGULAR,word(elapsed)),ecx=OWNER+0x300)
        self.receipt_state=bytes(self.u.mem_read(OWNER+0x300,48))
        self.events=[]
        low,high=struct.unpack('<2I',struct.pack('<d',0.0))
        self.start(0x6e6330,(low,high,CURRENT,NEXT))
        assert self.events==[['base-receipts'],['gravity']],self.events
        return list(struct.unpack('<12f',self.u.mem_read(OWNER+0x300,48)))


def receipt_cases(original,count=32):
    rng=random.Random(15130633);result=[]
    for index in range(count):
        start=[float32(rng.uniform(-40,40)) for _ in range(3)]
        end=[float32(rng.uniform(-40,40)) for _ in range(3)]
        elapsed=float32([.03,.1,.25,.5,1.][index%5])
        valid=index%7!=0
        initial=[float32(rng.uniform(-10,10)) for _ in range(12)]
        expected=original.receipt(initial,start,end,elapsed,valid)
        result.append([start,end,elapsed,int(valid),expected,initial])
    return result
