"""Original CPU measurement flow including original cached CPUID classifier.

RDTSC/Win32/CPUID are explicit primitive boundaries. The original935570,
92c5e0 and92c620 instructions, conversion helpers and weighting execute.
"""
import struct
import unicorn
from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_EBP,UC_X86_REG_EBX,UC_X86_REG_ECX,UC_X86_REG_EDX,UC_X86_REG_EIP
from stock_gpu_performancer_oracle import OriginalPerformanceScores
import stock_runtime_services_oracle as base


class OriginalCpuDetection(OriginalPerformanceScores):
    def __init__(self,executable):
        super(OriginalCpuDetection,self).__init__(executable)
        # Let the real92c5e0 and92c620 bodies execute, rather than supplying a vendor enum.
        self.hooks[0x92c5e0]=lambda:None
        self.hooks[0x92ab10]=lambda:self._return(0) # Temporary helper cleanup, no CPU decision.
        self.hooks[0x92c631]=self._cpuid
        for address in (0x92ab10,0x92c631):
            self.u.hook_add(unicorn.UC_HOOK_CODE,lambda u,a,s,d:self.hooks[a](),begin=address,end=address)
        self.events=[]

    def _ticks(self):
        self.events.append('R');super(OriginalCpuDetection,self)._ticks()

    def _sleep(self):
        self.events.append('S');super(OriginalCpuDetection,self)._sleep()

    def _system_info(self):
        self.events.append('G');super(OriginalCpuDetection,self)._system_info()

    def _cpuid(self):
        assert self.u.reg_read(UC_X86_REG_EAX)==0 and self.u.reg_read(UC_X86_REG_ECX)==0
        self.events.append('C')
        for register,value in zip((UC_X86_REG_EAX,UC_X86_REG_EBX,UC_X86_REG_ECX,UC_X86_REG_EDX),self.words):self.u.reg_write(register,value)
        self.u.reg_write(UC_X86_REG_EIP,0x92c633)

    def measure(self,start,end,processors,words):
        self.tick_values=[start,end];self.processors=processors;self.words=words;self.events=[]
        frame=base.STACK+0x80000
        self.u.reg_write(UC_X86_REG_EBP,frame);self.u.reg_write(base.UC_X86_REG_ESP,frame-0x1000)
        self.u.reg_write(base.UC_X86_REG_FPCW,0x27f)
        self.u.mem_write(frame-0x80,struct.pack('<I',base.STATS))
        self.u.emu_start(0x935570,0x9355fb,count=10000)
        assert not self.tick_values
        score=struct.unpack('<q',struct.pack('<II',self.u.reg_read(UC_X86_REG_EAX),self.u.reg_read(UC_X86_REG_EDX)))[0]
        return score,''.join(self.events)
