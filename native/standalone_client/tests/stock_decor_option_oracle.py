"""Run bounded original #1513 decor producer/callback/getter instructions."""
import struct
from stock_ballistics_oracle import Original, DATA, STACK, SENTINEL
import unicorn
from unicorn.x86_const import UC_X86_REG_EBP, UC_X86_REG_ESP, UC_X86_REG_ECX

class DecorOracle(Original):
    def __init__(self, executable):
        super().__init__(executable)
        self.selections=[]
        self.produced=[]
        self.pipeline=0
        self.notifications=0
        self.setting=DATA+0x4100
        self.owner=DATA+0x4200
        self.registered=True
        self.extra_hooks={
            0xa36570:self.capture_producer,
            0xa37029:self.stop_producer,
            0x613c20:self.string_assign,
            0x91d590:self.registry_get,
            0x91f3c0:self.registry_select,
            0x13d809e:lambda:self.ret(None,0),
            0xa49a80:lambda:self.ret(self.pipeline,0),
            0xa390c0:self.notify,
            SENTINEL+0x100:lambda:self.ret(1,4),
        }
        self.put(0x14913a0,'<I',SENTINEL+0x100)
        for a in self.extra_hooks:
            self.u.hook_add(unicorn.UC_HOOK_CODE,lambda uc,a,size,data:self.extra_hooks[a](),begin=a,end=a)

    def capture_producer(self):
        self.produced.append(struct.unpack('<if',self.u.mem_read(self.arg(2),8)))
        self.u.emu_stop()

    def stop_producer(self):
        self.u.emu_stop()

    def string_assign(self):
        obj=self.u.reg_read(UC_X86_REG_ECX)
        data=bytes(self.u.mem_read(self.arg(1),self.arg(2)))
        assert len(data)<=15
        self.u.mem_write(obj,data+b'\0'*(16-len(data)))
        self.put(obj+16,'<II',len(data),15)
        self.ret(obj,8)

    def registry_get(self):
        self.put(self.arg(1),'<I',self.setting if self.registered else 0)
        self.ret(self.arg(1),0)

    def registry_select(self):
        value=struct.unpack('<i',struct.pack('<I',self.arg(1)))[0]
        self.selections.append(value)
        self.ret(1,4)

    def notify(self):
        self.notifications+=1
        self.ret(None,0)

    def producer(self,value,label_length=3,fallback=False):
        self.produced=[]
        ebp=STACK+0xe0000
        self.put(ebp-0x48,'<I',label_length)
        self.put(ebp-0x78,'<f',value)
        self.put(ebp-0x7c,'<I',DATA+0x4000)
        self.put(DATA+0x4000,'<I',self.setting)
        self.u.reg_write(UC_X86_REG_EBP,ebp)
        self.u.reg_write(UC_X86_REG_ESP,STACK+0xd0000)
        self.u.emu_start(0xa37138 if fallback else 0xa36fe7,0,count=100)
        return self.produced

    def callback(self,first,value,registered=True):
        self.selections=[]
        self.registered=registered
        self.put(DATA+0x4300,'<if',first,value)
        self.call(0xa374f0,DATA+0x4400,[DATA+0x4300])
        return self.selections

    def effective(self,configured,override,pipeline):
        self.pipeline=pipeline
        self.put(self.owner,'<ii',configured,override)
        return self.call(0xa38b20,self.owner,[])

    def configure(self,configured,override,pipeline,selected):
        self.pipeline=pipeline
        self.put(self.owner,'<ii',configured,override)
        self.notifications=0
        self.call(0xa39170,self.owner,[selected & 0xffffffff])
        return struct.unpack('<ii',self.u.mem_read(self.owner,8)),self.notifications
