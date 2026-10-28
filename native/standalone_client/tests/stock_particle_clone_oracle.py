"""Run #1513 PyMetaParticleSystem.clone's unchanged wrapper instructions.

The existing inverse-group query executes through 009e3990. Quality policy,
operator new and the C++ clone/constructor are the explicit hook boundaries;
the SDK deep-copy bodies are separately exercised by the production host.
"""
import struct
import unicorn
from unicorn.x86_const import UC_X86_REG_ECX
from stock_ballistics_oracle import Original as Base, DATA


class Original(Base):
    def __init__(self,executable):
        super(Original,self).__init__(executable)
        self.wrapper=DATA+0x50000
        self.source=self.wrapper+0x100
        self.copy=self.source+0x100
        self.events=[]
        self.groups=0
        self.mask=0
        for address,function in ((0xfab2e0,self.quality),(0x9e3a70,self.clone),(0x9db720,self.construct)):
            self.hooks[address]=function
            self.u.hook_add(unicorn.UC_HOOK_CODE,lambda u,a,s,d:self.hooks[a](),begin=address,end=address)
        self.put(self.wrapper+0x24,'<I',self.source)
        self.put(self.source+8,'<I',0)

    def quality(self):
        self.events.append(('quality',self.arg(1)))
        self.ret(bool(self.groups&self.mask),0)

    def clone(self):
        self.events.append(('clone',self.u.reg_read(UC_X86_REG_ECX)))
        self.put(self.arg(1),'<I',self.copy)
        self.ret(self.arg(1),4)

    def construct(self):
        self.events.append(('construct',self.arg(1),self.arg(2)))
        self.ret(self.u.reg_read(UC_X86_REG_ECX),8)

    def run(self,groups,mask,disabled,args,keywords):
        self.events=[]
        self.groups,self.mask=groups,mask
        self.put(self.source+8,'<I',groups)
        self.put(0x1d45894,'<B',disabled)
        result=self.call(0x9dba40,0,[self.wrapper+4,args,keywords])
        return result,list(self.events)


def audit(executable):
    original=Original(executable)
    name=struct.unpack('<I',original.u.mem_read(0x1a1b99c,4))[0]
    assert bytes(original.u.mem_read(name,21))==b'PyMetaParticleSystem\0'
    table=struct.unpack('<4I',original.u.mem_read(0x1a1bb18,16))
    assert table[1:3]==(0x9dba40,3)
    assert bytes(original.u.mem_read(table[0],6))==b'clone\0'
    count=0
    for groups in (0,1,0x10000,0xffffffff):
        for mask in (0,0xffff):
            for disabled in (0,1):
                # Unmapped argument pointers prove neither tuple nor kwargs
                # is inspected, including the ordinary no-argument producer.
                for args,keywords in ((0,0),(0xbad00001,0xbad00002)):
                    result,events=original.run(groups,mask,disabled,args,keywords)
                    assert events[0]==('quality',groups)
                    if disabled or groups&mask:
                        assert result==0x1a5c798 and len(events)==1,(result,events)
                    else:
                        assert result not in (0,0x1a5c798)
                        assert events[1:]==[('clone',original.source),('construct',original.copy,0x1a1b990)],events
                    count+=1
    return count
