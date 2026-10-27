"""Execute the exact #1513 PyWatermarkersWrapper methods in Unicorn.

setEnabled 00fb1150, setSettings 00fb11d0 and setUserId 00fb12f0 run unchanged
over the image's EmptyWatermarker (01a5c240), including its vtable slot 0 and
the BW::string destructor. PyTuple_Size/GetItem, the four Script::setData
overloads and argCountError 0068e100 are hooked and recorded; the public
bodies of those helpers run in the CPython host test instead.
"""
from pathlib import Path
import re
import struct

import pefile
import unicorn
from unicorn.x86_const import (UC_X86_REG_EAX, UC_X86_REG_EIP, UC_X86_REG_ESP, UC_X86_REG_FS,
                               UC_X86_REG_GDTR, UC_X86_REG_SS)

METHODS = {'setEnabled': 0xfb1150, 'setSettings': 0xfb11d0, 'setUserId': 0xfb12f0}
SET_DATA = {0x690570: 'bool', 0x6903c0: 'int64', 0x68fe20: 'BW::string', 0x68fba0: 'float'}
TUPLE_SIZE, TUPLE_ITEM, ARG_COUNT_ERROR = 0xfb3610, 0xfb3390, 0x68e100
NONE, WATERMARKER, ENABLED = 0x1a5c798, 0x1a5c240, 0x1a5c250
STACK, GDT, MARKS = 0x6000000, 0x6300000, 0x6400000
WRAPPER, ARGS, ITEMS, SENTINEL = MARKS, MARKS + 0x100, MARKS + 0x200, MARKS + 0x400
PRODUCTION = Path(__file__).resolve().parents[1] / 'bigworld/stock_screener.cpp'
CONVERTERS = {'bool': 'bool', 'long long': 'int64', 'BW::string': 'BW::string', 'float': 'float'}


class OriginalScreener(object):
    def __init__(self, executable):
        pe = pefile.PE(str(executable))
        assert pe.FILE_HEADER.Machine == 0x14c
        image, base = pe.get_memory_mapped_image(), pe.OPTIONAL_HEADER.ImageBase
        self.u = u = unicorn.Uc(unicorn.UC_ARCH_X86, unicorn.UC_MODE_32)
        u.mem_map(base, (len(image) + 0xfff) & ~0xfff)
        u.mem_write(base, image)
        for region in (STACK, GDT, MARKS):
            u.mem_map(region, 0x100000)
        teb = (0x1000 | (GDT + 0x1000 & 0xffffff) << 16 | 0x92 << 40 | 0x4 << 52 | ((GDT + 0x1000) >> 24 & 0xff) << 56)
        flat = 0xffff | 0x92 << 40 | 0xf << 48 | 0xc << 52
        u.mem_write(GDT, struct.pack('<QQQ', 0, teb, flat))
        u.reg_write(UC_X86_REG_GDTR, (0, GDT, 23, 0))
        u.reg_write(UC_X86_REG_FS, 8)
        u.reg_write(UC_X86_REG_SS, 16)
        self.hooks = {TUPLE_SIZE: lambda: self._return(self.argc),
                      TUPLE_ITEM: lambda: self._return(ITEMS + self._arg(2)),
                      ARG_COUNT_ERROR: self._arg_count_error, SENTINEL: lambda: u.emu_stop()}
        for address, kind in SET_DATA.items():
            self.hooks[address] = (lambda kind: lambda: self._set_data(kind))(kind)
        for address in self.hooks:
            u.hook_add(unicorn.UC_HOOK_CODE, lambda u, address, size, data: self.hooks[address](),
                       begin=address, end=address)

    def _arg(self, index):
        return struct.unpack('<I', self.u.mem_read(self.u.reg_read(UC_X86_REG_ESP) + 4 * index, 4))[0]

    def _cstr(self, address):
        return bytes(self.u.mem_read(address, 0x100)).split(b'\0')[0].decode('latin1')

    def _return(self, value):
        esp = self.u.reg_read(UC_X86_REG_ESP)
        self.u.reg_write(UC_X86_REG_EAX, value & 0xffffffff)
        self.u.reg_write(UC_X86_REG_EIP, self._arg(0))
        self.u.reg_write(UC_X86_REG_ESP, esp + 4)

    def _set_data(self, kind):
        index = self._arg(1) - ITEMS
        self.calls.append(['setData', kind, index, self._cstr(self._arg(3))])
        if index == self.fail_at:
            return self._return(0xffffffff)
        if kind == 'bool':
            self.u.mem_write(self._arg(2), bytes([self.value]))
        self._return(0)

    def _arg_count_error(self):
        total = self._arg(3)
        self.calls.append(['argCountError', self._cstr(self._arg(1)), self._arg(2), total] +
                          [self._cstr(self._arg(4 + n)) for n in range(total)])
        self._return(0)

    def run(self, method, argc, fail_at=None, value=0, self_object=True):
        u = self.u
        self.argc, self.fail_at, self.value, self.calls = argc, fail_at, value, []
        u.mem_write(ENABLED, b'\x07')
        u.mem_write(WRAPPER, struct.pack('<III', 1, 0x1a5c138, WATERMARKER))
        u.reg_write(UC_X86_REG_ESP, STACK + 0x80000)
        u.mem_write(STACK + 0x80000, struct.pack('<III', SENTINEL, WRAPPER if self_object else 0, ARGS))
        u.emu_start(METHODS[method], SENTINEL + 1, count=100000)
        assert u.reg_read(UC_X86_REG_EIP) == SENTINEL, hex(u.reg_read(UC_X86_REG_EIP))
        result = {0: 'NULL', NONE: 'None'}[u.reg_read(UC_X86_REG_EAX)]
        return [method, argc, fail_at, value, self_object, result, bytes(u.mem_read(ENABLED, 1))[0], self.calls]


def signatures():
    """The PY_AUTO_METHOD_DECLARE argument types in the production binding."""
    text = re.sub(r'\s+', ' ', PRODUCTION.read_text())
    found = re.findall(r'PY_AUTO_METHOD_DECLARE\( RETVOID, (\w+), (.*?END) \)+', text)
    return {name: tuple(re.findall(r'ARG\( ([^,]+),', args)) for name, args in found}


def cases():
    result = []
    for method, kinds in sorted(signatures().items()):
        result.append((method, len(kinds), None, 1, False))
        for argc in range(4):
            result.append((method, argc, None, 1, True))
            if argc == len(kinds):
                result.extend((method, argc, index, 1, True) for index in range(argc))
                if method == 'setEnabled':
                    result.append((method, argc, None, 0, True))
    return result


def expected():
    """The same cases through the production PY_AUTO_METHOD_DECLARE expansion."""
    result, declared = [], signatures()
    assert sorted(declared) == sorted(METHODS), declared
    for method, argc, fail_at, value, self_object in cases():
        kinds = declared[method]
        if not self_object:
            calls, outcome = [], 'NULL'
        elif argc != len(kinds):
            calls, outcome = [['argCountError', method, len(kinds), len(kinds)] + list(kinds)], 'NULL'
        else:
            calls, outcome = [], 'None'
            for index, kind in enumerate(kinds):
                calls.append(['setData', CONVERTERS[kind], index, '() argument %d' % (index + 1)])
                if index == fail_at:
                    outcome = 'NULL'
                    break
        # Only the original writes the inert EmptyWatermarker flag; production drops it.
        flag = value if (method == 'setEnabled' and outcome == 'None') else 7
        result.append([method, argc, fail_at, value, self_object, outcome, flag, calls])
    return result


def run(executable):
    original = OriginalScreener(executable)
    return [original.run(*case) for case in cases()]
