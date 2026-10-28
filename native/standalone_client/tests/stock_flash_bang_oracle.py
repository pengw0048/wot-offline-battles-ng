"""Run #1513 flash wrappers, list retention/removal and component-max tick.

The render probe executes the original prefix through its actual draw dispatch;
device allocation, Geometrics setup, quad builder and draw are recorded seams.
"""
import struct
import unicorn
from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_ECX,UC_X86_REG_EIP,UC_X86_REG_ESP
import stock_runtime_services_oracle as base

OWNER=base.MARKS+0x9000
PIPELINE=base.MARKS+0xa000
ARRAY=base.MARKS+0xb000
PROVIDERS=base.MARKS+0xc000
OUTPUT=base.MARKS+0xd000


class OriginalFlashBang(base.OriginalRuntimeServices):
    def __init__(self,executable):
        super().__init__(executable)
        self.values={};self.events=[];self.argc=1;self.calls=[]
        self.add_hook(0x69f200,self.convert)
        self.add_hook(0x6cbfd0,lambda:self._return(PIPELINE))
        self.add_hook(OUTPUT,self.output)
        self.u.mem_write(PROVIDERS+0x800,struct.pack('<3I',0,0,OUTPUT))
        self.u.mem_write(OWNER+0x1c,struct.pack('<3I',ARRAY,ARRAY,ARRAY+0x400))
        self.u.mem_write(OWNER+4,struct.pack('<I',OWNER+0x200))
        self.add_hook(0x999530,lambda:self.ret_pop(1,0))
        self.add_hook(0xa22970,lambda:self.utility('standard2D',40))
        self.add_hook(0xa22a80,lambda:self.utility('textured2D',36))
        self.add_hook(0x94b0d0,lambda:self.ret_pop(OUTPUT+0x100,0))
        self.add_hook(0x6f8d00,lambda:self.ret_pop(OUTPUT+0x300,4))
        self.add_hook(0xa21950,self.quad)
        self.add_hook(0xa1d820,self.draw)
        self.add_hook(0xa1df25,lambda:self.u.emu_stop())
        self.u.mem_write(OUTPUT+0x100,struct.pack('<2f',640,360))
        self.u.mem_write(base.RC+0x784,struct.pack('<I',OUTPUT+0x200))
        self.u.mem_write(OUTPUT+0x300,struct.pack('<I',OUTPUT+0x400))

    def add_hook(self,address,hook):
        self.hooks[address]=hook
        self.u.hook_add(unicorn.UC_HOOK_CODE,lambda u,a,s,d:self.hooks[a](),begin=address,end=address)

    def ret_pop(self,value,count):
        esp=self.u.reg_read(UC_X86_REG_ESP);self._return(value)
        self.u.reg_write(UC_X86_REG_ESP,esp+4+count)

    def convert(self):
        self.calls.append(['convert',self._cstr(self._arg(3))])
        if self.fail:return self._return(0xffffffff)
        address=self.provider
        self.u.mem_write(self._arg(2),struct.pack('<I',address))
        if address:self.refs(address,1)
        self._return(0)

    def refs(self,address,change):
        old=struct.unpack('<I',self.u.mem_read(address+4,4))[0]
        self.u.mem_write(address+4,struct.pack('<I',old+change))

    def provider_at(self,index,colour):
        address=PROVIDERS+index*0x100
        self.u.mem_write(address,struct.pack('<2I',PROVIDERS+0x800,100))
        self.values[address]=colour
        return address

    def start(self,address,args=(),ecx=OWNER):
        esp=base.STACK+0x80000
        self.u.mem_write(esp,struct.pack('<%dI'%(len(args)+1),base.SENTINEL,*args))
        self.u.reg_write(UC_X86_REG_ESP,esp);self.u.reg_write(UC_X86_REG_ECX,ecx)
        self.u.emu_start(address,base.SENTINEL+1,count=100000)
        return self.u.reg_read(UC_X86_REG_EAX)

    def wrapper(self,remove=False,provider=0,argc=1,fail=False,pipeline='deferred',present=True):
        self.provider=provider;self.argc=argc;self.fail=fail;self.calls=[]
        table,offset=(0x14fd3b0,0x90) if pipeline=='deferred' else (0x14fd894,0xc)
        self.u.mem_write(PIPELINE,struct.pack('<I',table))
        self.u.mem_write(PIPELINE+offset,struct.pack('<I',OWNER if present else 0))
        result=self.start(0x619010 if remove else 0x6187a0,(0,base.ARGS),0)
        return ('None' if result==base.NONE else 'NULL'),list(self.calls)

    def entries(self):
        end=struct.unpack('<I',self.u.mem_read(OWNER+0x20,4))[0]
        return [struct.unpack('<I',self.u.mem_read(a,4))[0] for a in range(ARRAY,end,4)]

    def output(self):
        self.u.mem_write(self._arg(1),struct.pack('<4f',*self.values[self.u.reg_read(UC_X86_REG_ECX)]))
        self.ret_pop(0,4)

    def tick(self):
        self.start(0xa1e3f0)
        return struct.unpack('<4f',self.u.mem_read(OWNER+0xc,16))

    def utility(self,technique,pop):
        self.events.append([technique]+[self._arg(i) for i in range(2,11 if pop==40 else 10)])
        self.ret_pop(0,pop)

    def quad(self):
        self.events.append(['quad',struct.unpack('<2f',self.u.mem_read(self._arg(1),8)),
            struct.unpack('<2f',self.u.mem_read(self._arg(2),8)),
            struct.unpack('<4f',self.u.mem_read(self._arg(3),16))])
        self._return(0)

    def draw(self):
        self.events.append(['draw',self._arg(2)])
        self._return(1)

    def render_prefix(self,colour,previous=False):
        self.events=[];self.u.mem_write(OWNER+0xc,struct.pack('<4f',*colour))
        self.u.mem_write(OWNER+9,bytes([int(previous)]))
        self.start(0xa1dd30,(base.RC,))
        valid=bytes(self.u.mem_read(OWNER+9,1))[0]
        return self.events,valid
