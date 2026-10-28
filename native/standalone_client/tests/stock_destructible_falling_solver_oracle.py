"""Original 6b4950 wrapper/iteration; only external CRT sinf is bridged."""
import math
import ctypes
import os
import struct
import unicorn
from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_EIP,UC_X86_REG_ESP,UC_X86_REG_XMM0
import stock_runtime_services_oracle as base


class OriginalFallingSolver(base.OriginalRuntimeServices):
    def __init__(self,executable):
        super(OriginalFallingSolver,self).__init__(executable)
        self.u.hook_add(unicorn.UC_HOOK_CODE,lambda u,a,s,d:self.sine(),begin=0x13dad0d,end=0x13dad0d)
        self.sin_calls=0
        self.native_sine=ctypes.CDLL('ucrtbase.dll' if os.name=='nt' else None).sinf
        self.native_sine.argtypes=[ctypes.c_float];self.native_sine.restype=ctypes.c_float

    def sine(self):
        raw=self.u.reg_read(UC_X86_REG_XMM0)&0xffffffff
        value=struct.unpack('<f',struct.pack('<I',raw))[0]
        result=self.native_sine(value)
        bits=struct.unpack('<I',struct.pack('<f',result))[0]
        self.u.reg_write(UC_X86_REG_XMM0,bits);self.sin_calls+=1;self._return(0)

    def solve(self,values,argc=4,fail_at=None):
        u=self.u;self.argc=argc;self.values=list(values)+[0]*4;self.fail_at=fail_at
        self.calls=[];self.backends=[];self.results={};self.sin_calls=0
        self.environment_error=0
        stack=base.STACK+0x80000;u.reg_write(UC_X86_REG_ESP,stack)
        u.mem_write(stack,struct.pack('<3I',base.SENTINEL,0,base.ARGS))
        u.emu_start(0x6b4950,base.SENTINEL+1,count=100000)
        assert u.reg_read(UC_X86_REG_EIP)==base.SENTINEL
        eax=u.reg_read(UC_X86_REG_EAX)
        if eax==0:return None,self.calls,self.sin_calls
        assert eax==base.FLOAT_RESULT
        return struct.unpack('<I',struct.pack('<f',self.results[eax][1]))[0],self.calls,self.sin_calls
