"""Original GPU timing arithmetic and RDTSC CPU score; no synthetic renderer."""
import struct

import unicorn
from unicorn.x86_const import UC_X86_REG_EBP, UC_X86_REG_EDX

from stock_debug_queries_oracle import OriginalDebugQueries
import stock_runtime_services_oracle as base

SLEEP, SYSTEM_INFO = base.MARKS + 0x15010, base.MARKS + 0x15020


class OriginalPerformanceScores(OriginalDebugQueries):
    def __init__(self, executable):
        super().__init__(executable)
        for address, callback in ((0x934c10, self._ticks), (SLEEP, self._sleep),
                                   (SYSTEM_INFO, self._system_info),
                                   (0x92c5e0, lambda: self._return(self.vendor))):
            self.hooks[address] = callback
            self.u.hook_add(unicorn.UC_HOOK_CODE, lambda u, a, s, d: self.hooks[a](),
                            begin=address, end=address)
        self.u.mem_write(0x1491370, struct.pack('<I', SLEEP))
        self.u.mem_write(0x1491344, struct.pack('<I', SYSTEM_INFO))

    def _ticks(self):
        value = self.tick_values.pop(0)
        self._return(value & 0xffffffff)
        self.u.reg_write(UC_X86_REG_EDX, value >> 32)

    def _sleep(self):
        assert self._arg(1) == 500
        esp = self.u.reg_read(base.UC_X86_REG_ESP)
        self._return(0)
        self.u.reg_write(base.UC_X86_REG_ESP, esp + 8)

    def _system_info(self):
        pointer = self._arg(1)
        self.u.mem_write(pointer, b'\0' * 36)
        self.u.mem_write(pointer + 20, struct.pack('<I', self.processors))
        esp = self.u.reg_read(base.UC_X86_REG_ESP)
        self._return(0)
        self.u.reg_write(base.UC_X86_REG_ESP, esp + 8)

    def cpu(self, elapsed, processors, vendor):
        self.tick_values, self.processors, self.vendor = [0, elapsed], processors, vendor
        frame = base.STACK + 0x80000
        self.u.reg_write(UC_X86_REG_EBP, frame)
        self.u.reg_write(base.UC_X86_REG_ESP, frame - 0x1000)
        self.u.reg_write(base.UC_X86_REG_FPCW, 0x27f)
        self.u.mem_write(frame - 0x80, struct.pack('<I', base.STATS))
        self.u.emu_start(0x935570, 0x9355fb, count=10000)
        assert not self.tick_values
        return struct.unpack('<q', struct.pack('<II', self.u.reg_read(base.UC_X86_REG_EAX),
                                               self.u.reg_read(UC_X86_REG_EDX)))[0]

    def gpu(self, julia, random_texture):
        self.u.mem_write(0x1d38d60, struct.pack('<dd', julia, random_texture))
        self.u.emu_start(0x9c18a3, 0x9c197f, count=1000)
        return self.u.reg_read(base.UC_X86_REG_EAX)
