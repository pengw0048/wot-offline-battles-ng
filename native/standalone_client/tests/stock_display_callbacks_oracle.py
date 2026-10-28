"""Exact #1513 inactive-mode setter -> event -> Python thunk execution.

The real b0c4a0 comparison/update/dispatch and658cc0 reference admission run.
Only each display-mode object's ordinal getter/setter and the final Python
call/tuple allocation are fixtures. No D3D device switch is simulated.
"""
import struct

import unicorn

from stock_debug_queries_oracle import OriginalDebugQueries
import stock_runtime_services_oracle as base

OWNER, VTABLE, VALUE, SETTER, GETTER, EVENTS, TUPLE, RECEIVER = (
    base.MARKS + offset for offset in (0xc000, 0xc400, 0xc500, 0xc600,
                                       0xc610, 0xc700, 0xc800, 0xc900))


class OriginalDisplayCallbacks(OriginalDebugQueries):
    def __init__(self, executable):
        super().__init__(executable)
        self.events = []
        u = self.u
        for index, offset in enumerate((0x18, 0x50, 0x34)):
            u.mem_write(OWNER + offset, struct.pack('<I', VTABLE))
            u.mem_write(VALUE + 4 * index, struct.pack('<I', 0))
        u.mem_write(VTABLE + 0x30, struct.pack('<I', GETTER))
        u.mem_write(VTABLE + 0x28, struct.pack('<I', SETTER))
        u.mem_write(OWNER + 0x120, struct.pack('<III', EVENTS, EVENTS + 8, EVENTS + 8))
        u.mem_write(EVENTS, struct.pack('<II', 0, 0x658cc0))
        u.mem_write(0x1d21a40, struct.pack('<I', RECEIVER))
        u.mem_write(RECEIVER, struct.pack('<I', 1))
        for address, callback in ((GETTER, self._get), (SETTER, self._set),
                                   (0xfb3420, self._tuple), (0x68e560, self._invoke)):
            self.hooks[address] = callback
            u.hook_add(unicorn.UC_HOOK_CODE, lambda u, a, s, d: self.hooks[a](),
                       begin=address, end=address)

    def _index(self):
        offset = self.u.reg_read(base.UC_X86_REG_ECX) - OWNER
        return (0x18, 0x50, 0x34).index(offset)

    def _get(self):
        self._return(VALUE + 4 * self._index())

    def _set(self):
        esp = self.u.reg_read(base.UC_X86_REG_ESP)
        assert self._arg(1) == OWNER
        self.u.mem_write(VALUE + 4 * self._index(), struct.pack('<I', self._arg(2)))
        self._return(0)
        self.u.reg_write(base.UC_X86_REG_ESP, esp + 12)

    def _tuple(self):
        assert self._arg(1) == 0
        self._return(TUPLE)

    def _invoke(self):
        assert self._arg(1) == RECEIVER and self._arg(2) == TUPLE
        self.events.append(list(struct.unpack('<III', self.u.mem_read(VALUE, 12))))
        references = struct.unpack('<I', self.u.mem_read(RECEIVER, 4))[0]
        assert references == 2  # The real thunk retains before Python reentry.
        self.u.mem_write(RECEIVER, struct.pack('<I', references - 1))
        self._return(1)

    def select(self, mode, ordinal):
        self.call(0xb0c4a0, OWNER, [struct.pack('<II', mode, ordinal)])
        assert struct.unpack('<I', self.u.mem_read(RECEIVER, 4))[0] == 1
        return list(self.events)
