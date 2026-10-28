"""Execute #1513 notifier wrappers and complete 00624800 native notification.

Only CPython tuple/error services, OS dynamic linking/foreground/flash, and COM
creation/vtable calls are hooked. Window comparison, progress values/states,
FlashWindowEx layout, error branches and interface replacement run unchanged.
"""
import struct
import unicorn
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EIP, UC_X86_REG_ESP
import stock_runtime_services_oracle as base

OWNER, INTERFACE, VTABLE = base.MARKS+0x10000, base.MARKS+0x11000, base.MARKS+0x12000
STUBS = base.MARKS+0x20000
METHODS = (('onBattleBeginning',0x624430),('onInvitation',0x6244b0),('resetTaskbar',0x624530))


class OriginalNotifier(base.OriginalRuntimeServices):
    def __init__(self, executable):
        super(OriginalNotifier,self).__init__(executable)
        self.window,self.foreground=123,123
        self.library=self.procedure=self.themed=self.interface=True
        self.com_result=0
        self.u.mem_write(base.APP+0x564,struct.pack('<I',self.window))
        self.u.mem_write(OWNER,b'\0'*0x18)
        self.u.mem_write(INTERFACE,struct.pack('<I',VTABLE))
        callbacks=[(0x14913dc,lambda:self.result(7 if self.library else 0,1,'load')),
                   (0x1491318,lambda:self.result(STUBS+0x100 if self.procedure else 0,2)),
                   (0x1492294,self.create),
                   (0x14919a4,self.flash),
                   (0x1491a10,lambda:self.result(self.foreground,0))]
        for index,(iat,callback) in enumerate(callbacks):
            address=STUBS+index*0x10
            self.u.mem_write(iat,struct.pack('<I',address));self.hook(address,callback)
        self.hook(STUBS+0x100,lambda:self.result(int(self.themed),0))
        for offset,address,callback in [(8,STUBS+0x110,lambda:self.result(0,1,'release')),
                (0x24,STUBS+0x120,self.progress),(0x28,STUBS+0x130,self.state)]:
            self.u.mem_write(VTABLE+offset,struct.pack('<I',address));self.hook(address,callback)
        self.hook(0x579c00,lambda:self.result(0,0))  # PyObjectPlus destruction boundary.

    def hook(self,address,callback):
        self.u.hook_add(unicorn.UC_HOOK_CODE,lambda u,a,size,data:callback(),begin=address,end=address)

    def result(self,value,argc,event=None):
        if event:self.events.append(event)
        esp=self.u.reg_read(UC_X86_REG_ESP)
        target=self._arg(0)
        self.u.reg_write(UC_X86_REG_EAX,value&0xffffffff)
        self.u.reg_write(UC_X86_REG_EIP,target)
        self.u.reg_write(UC_X86_REG_ESP,esp+4+argc*4)

    def create(self):
        assert self._arg(3)==0x17
        self.u.mem_write(self._arg(5),struct.pack('<I',INTERFACE if self.interface else 0))
        self.result(self.com_result,5,'create')

    def progress(self):
        assert self._arg(1)==INTERFACE
        completed=self._arg(3)|(self._arg(4)<<32)
        total=self._arg(5)|(self._arg(6)<<32)
        self.result(0,6,'progress:%d:%d:%d'%(self._arg(2),completed,total))

    def state(self):
        assert self._arg(1)==INTERFACE
        self.result(0,3,'state:%d:%d'%(self._arg(2),self._arg(3)))

    def flash(self):
        size,window,flags,count,timeout=struct.unpack('<5I',self.u.mem_read(self._arg(1),20))
        assert size==20
        self.result(0,1,'flash:%d:%d:%d:%d'%(window,flags,count,timeout))

    def run(self,name,argc=0):
        self.argc=argc;self.calls=[];self.events=[]
        self.u.reg_write(UC_X86_REG_ESP,base.STACK+0x80000)
        self.u.mem_write(base.STACK+0x80000,struct.pack('<III',base.SENTINEL,OWNER+4,base.ARGS))
        self.u.emu_start(dict(METHODS)[name],base.SENTINEL+1,count=10000)
        assert self.u.reg_read(UC_X86_REG_EIP)==base.SENTINEL
        return self.u.reg_read(UC_X86_REG_EAX),self.calls,self.events

    def native_reset(self,present=True):
        self.calls=[];self.events=[]
        self.u.mem_write(0x1d20830,struct.pack('<I',OWNER if present else 0))
        self.u.reg_write(UC_X86_REG_ESP,base.STACK+0x80000)
        self.u.mem_write(base.STACK+0x80000,struct.pack('<I',base.SENTINEL))
        self.u.emu_start(0x6246f0,base.SENTINEL+1,count=10000)
        assert self.u.reg_read(UC_X86_REG_EIP)==base.SENTINEL
        return self.events

    def destroy(self):
        self.events=[]
        self.u.reg_write(UC_X86_REG_ECX,OWNER)
        self.u.reg_write(UC_X86_REG_ESP,base.STACK+0x80000)
        self.u.mem_write(base.STACK+0x80000,struct.pack('<II',base.SENTINEL,0))
        self.u.emu_start(0x6243a0,base.SENTINEL+1,count=10000)
        assert self.u.reg_read(UC_X86_REG_EIP)==base.SENTINEL
        return self.events


def audit(executable):
    original=OriginalNotifier(executable)
    # Original tp_methods at WGWinNotifier+0x74, three methods and terminator.
    pe=base.pefile.PE(str(executable));data=pe.get_memory_mapped_image();image_base=pe.OPTIONAL_HEADER.ImageBase
    table=struct.unpack_from('<I',data,0x1a07330-image_base+0x74)[0]
    entries=[]
    for index in range(4):
        name,function,flags,doc=struct.unpack_from('<4I',data,table-image_base+index*16)
        if not name:break
        label=data[name-image_base:name-image_base+60].split(b'\0',1)[0].decode('ascii')
        entries.append((label,function));assert flags==3
    assert tuple(entries)==METHODS
    for name,address in METHODS:
        if name!='resetTaskbar':assert original.run(name)==(base.NONE,[],[])
        for count in (1,2):
            assert original.run(name,count)==(0,[['argCountError',name,0,0]],[])
    for name,state in [('onBattleBeginning',4),('onInvitation',8),('resetTaskbar',0)]:
        original=OriginalNotifier(executable);original.foreground=999
        result,calls,events=original.run(name)
        value=0 if state==0 else 1
        assert (result,calls)==(base.NONE,[])
        assert events==['load','create','progress:123:%d:%d'%(value,value),'state:123:%d'%state,
                        'flash:123:%d:0:0'%(0 if state==0 else 6)]
        # The next call releases its previous COM interface before replacing it.
        assert original.run(name)[2][1:3]==['release','create']
        assert original.native_reset()[-3:]==['progress:123:0:0','state:123:0','flash:123:0:0:0']
        assert original.destroy()==['release']
        assert original.destroy()==[]
        assert original.native_reset(False)==[]
    for library,procedure,themed,result,pointer in [(False,True,True,0,True),
            (True,False,True,0,True),(True,True,False,0,True),
            (True,True,True,-1,False),(True,True,True,0,False)]:
        original=OriginalNotifier(executable);original.foreground=999
        original.library,original.procedure,original.themed=library,procedure,themed
        original.com_result,original.interface=result,pointer
        assert original.run('onBattleBeginning')[2][-1]=='flash:123:6:0:0'
