"""Execute #1513 render setters, WorldApp sampling and authored draw gates.

Script argument conversion executes original instructions through the sniper
oracle's CPython/CRT boundaries. Gate slices run unchanged with their explicit
native frame/context inputs; stop addresses mark visible/hidden continuations.
No D3D drawing, map loading or native collision is claimed by these slices.
"""
import struct
import unicorn
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_EBP, UC_X86_REG_EDI, UC_X86_REG_EIP
from stock_havok_sniper_oracle import OriginalSniper
import stock_runtime_services_oracle as base

LOW_SETTER, FLORA_SETTER = 0x6368e0, 0xa3a470
LOW_MODE, FLORA_MODE = 0x1d21158, 0x1a1e488
CONTEXT, FRAME, WORLD = base.MARKS + 0x10000, base.MARKS + 0x11000, base.MARKS + 0x12000


class OriginalControlRendering(OriginalSniper):
    def __init__(self, executable):
        super(OriginalControlRendering, self).__init__(executable)
        self.endpoints = (0x636bb2, 0x6d1248, 0x6d1887, 0x6c57f5, 0x6c5917,
                          0xa3ab2b, 0xa3ad4b, 0xa3d2c1, 0xa3d30d)
        for endpoint in self.endpoints:
            self.u.hook_add(unicorn.UC_HOOK_CODE, lambda u, address, size, data: u.emu_stop(),
                            begin=endpoint, end=endpoint)

    def context(self, enabled):
        # abb1ed constructor initializes the low-detail flag to false. This
        # is also the independent shadow/reflection context's initial state.
        self.u.reg_write(UC_X86_REG_EDI, CONTEXT)
        self.u.emu_start(0xabb1ed, 0xabb1f1, count=10)
        assert bytes(self.u.mem_read(CONTEXT + 0x64, 1)) == b'\0'
        self.u.mem_write(LOW_MODE, bytes([int(enabled)]))
        self.u.reg_write(UC_X86_REG_EBP, FRAME)
        self.u.reg_write(UC_X86_REG_EAX, WORLD)
        self.u.emu_start(0x636b97, 0x636bb2, count=30)
        sampled = bytes(self.u.mem_read(FRAME - 0x64, 1))
        self.u.mem_write(CONTEXT + 0x64, sampled)
        return sampled != b'\0'

    def hidden(self, tree, flags):
        self.u.reg_write(UC_X86_REG_EAX, flags)
        self.u.reg_write(UC_X86_REG_EBP, FRAME)
        self.u.mem_write(FRAME - (0x68 if tree else 0xc4), struct.pack('<I', CONTEXT))
        self.u.emu_start(0x6c57d5 if tree else 0x6d1216, 0xffffffff, count=30)
        address = self.u.reg_read(UC_X86_REG_EIP)
        assert address in self.endpoints
        return address == (0x6c5917 if tree else 0x6d1887)

    def flora_allowed(self, update):
        self.u.emu_start(0xa3d2b8 if update else 0xa3ab1e, 0xffffffff, count=10)
        address = self.u.reg_read(UC_X86_REG_EIP)
        assert address in self.endpoints
        return address == (0xa3d2c1 if update else 0xa3ab2b)


def audit(executable):
    original = OriginalControlRendering(executable)
    for address, owner, name in ((LOW_SETTER, LOW_MODE, 'wg_setLowDetailedMode'),
                                 (FLORA_SETTER, FLORA_MODE, 'setFloraEnabled')):
        original.wrapper = address
        for kind, value, expected in [('int', 0, 0), ('int', 1, 1), ('int', -2, 1),
                                      ('str', 'False', 0), ('str', 'TRUE', 1)]:
            result, calls, writes = original.run(kind=kind, value=value)
            assert result == base.NONE and calls == []
            assert sorted(writes) == sorted([(base.NONE, 4), (owner, 1)])
            assert bytes(original.u.mem_read(owner, 1)) == bytes([expected])
            if owner == FLORA_MODE:
                assert original.flora_allowed(False) == bool(expected)
                assert original.flora_allowed(True) == bool(expected)
        for count in (0, 2):
            assert original.run(argc=count) == (
                0, [['argCountError', name, 1, 1, 'bool']], [])
        for kind, value in [('str', 'yes'), ('other', 0)]:
            result, calls, writes = original.run(kind=kind, value=value)
            assert result == 0 and writes == []
            assert calls == [['TypeError', '%s must be set to a bool', '() argument 1']]
    for enabled in (False, True, False):
        assert original.context(enabled) == enabled
        for flags in range(512):
            assert original.hidden(False, flags) == bool(enabled and flags & 0x80)
            assert original.hidden(True, flags) == bool(enabled and flags & 0x10)
