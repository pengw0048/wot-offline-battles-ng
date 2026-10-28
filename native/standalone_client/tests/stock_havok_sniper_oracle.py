"""Run #1513 00653dd0 and its unmodified bool converter 00690570.

Only CPython tuple/int/string access, PyErr_Format, CRT stricmp and the common
argument-count error formatter are hooked. Every PE image write is recorded;
successful calls only increment Py_None's reference count. No Havok backend
exists on this original path, so there is no physics/presentation owner to add.
"""
import struct

import unicorn
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_EIP, UC_X86_REG_ESP
import stock_runtime_services_oracle as base

WRAPPER, BOOL_CONVERTER = 0x653dd0, 0x690570
INT_VALUE, STRING_VALUE, ERROR_FORMAT = 0xfe0b10, 0xfd64e0, 0xfc3720
STRICMP_IAT, STRICMP = 0x1492088, base.MARKS + 0x8000
TYPE, TEXT = base.MARKS + 0x8100, base.MARKS + 0x8200


class OriginalSniper(base.OriginalRuntimeServices):
    def __init__(self, executable):
        super(OriginalSniper, self).__init__(executable)
        self.wrapper = WRAPPER
        self.hooks[BOOL_CONVERTER] = lambda: None  # Execute the original converter.
        self.u.mem_write(STRICMP_IAT, struct.pack('<I', STRICMP))
        self.hooks.update({INT_VALUE: lambda: self._return(self.value),
                           STRING_VALUE: lambda: self._return(TEXT),
                           ERROR_FORMAT: self._type_error,
                           STRICMP: self._stricmp})
        for address in (INT_VALUE, STRING_VALUE, ERROR_FORMAT, STRICMP):
            self.u.hook_add(unicorn.UC_HOOK_CODE,
                            lambda u, address, size, data: self.hooks[address](),
                            begin=address, end=address)
        self.u.hook_add(unicorn.UC_HOOK_MEM_WRITE, self._write)

    def _type_error(self):
        self.calls.append(['TypeError', self._cstr(self._arg(2)), self._cstr(self._arg(3))])
        self._return(0)

    def _stricmp(self):
        left, right = self._cstr(self._arg(1)).lower(), self._cstr(self._arg(2)).lower()
        self._return(0 if left == right else 1)

    def _write(self, u, access, address, size, value, data):
        if 0x400000 <= address < base.STACK:
            self.writes.append((address, size))

    def run(self, argc=1, kind='int', value=0):
        self.argc, self.value = argc, value
        self.calls, self.writes = [], []
        flags = {'int': 0x800000, 'str': 0x8000000, 'other': 0}[kind]
        self.u.mem_write(base.ITEMS, struct.pack('<II', 1, TYPE))
        self.u.mem_write(TYPE + 0x54, struct.pack('<I', flags))
        if kind == 'str':
            self.u.mem_write(TEXT, value.encode('ascii') + b'\0')
        self.u.reg_write(UC_X86_REG_ESP, base.STACK + 0x80000)
        self.u.mem_write(base.STACK + 0x80000, struct.pack('<III', base.SENTINEL, 0, base.ARGS))
        self.u.emu_start(self.wrapper, base.SENTINEL + 1, count=10000)
        assert self.u.reg_read(UC_X86_REG_EIP) == base.SENTINEL
        return self.u.reg_read(UC_X86_REG_EAX), self.calls, self.writes


def audit(executable):
    original = OriginalSniper(executable)
    for kind, value in [('int', 0), ('int', 1), ('int', -3), ('str', 'True'),
                        ('str', 'FALSE'), ('str', 'tRuE')]:
        assert original.run(kind=kind, value=value) == (base.NONE, [], [(base.NONE, 4)])
    for kind, value in [('str', ''), ('str', 'yes'), ('str', '0'), ('other', 0)]:
        result, calls, writes = original.run(kind=kind, value=value)
        assert result == 0 and writes == []
        assert calls == [['TypeError', '%s must be set to a bool', '() argument 1']]
    for count in (0, 2, 3):
        assert original.run(argc=count) == (
            0, [['argCountError', 'wg_havokSetSniperMode', 1, 1, 'bool']], [])
