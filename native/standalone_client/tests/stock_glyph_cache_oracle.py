"""Execute the exact #1513 GUI.wg_setGlyphCacheParams instructions in Unicorn and compare production.

The setter 00a9faf0, setData(uint&) 0068fad0 with PyFloat_AsDouble 00fdc0b0
and _ftol2 013d8a60, and argCountError 0068e100 run unchanged. The remaining
Python C API, allocation and CRT imports are hooked; the manager singleton at
01d1fee8 points at a filled block whose bytes 0xa8..0xe2 are compared.
"""
import json
import struct
import subprocess

import pefile
import unicorn
from unicorn.x86_const import (UC_X86_REG_EAX, UC_X86_REG_EIP, UC_X86_REG_ESP, UC_X86_REG_FS,
                               UC_X86_REG_GDTR, UC_X86_REG_MXCSR, UC_X86_REG_SS)

SETTER, MANAGER_POINTER, NONE, TYPE_ERROR, FLOAT_TYPE = 0xa9faf0, 0x1d1fee8, 0x1a5c798, 0x1a5db10, 0x1a64370
SSE2_FLAG = 0x215e6dc
TUPLE_SIZE, TUPLE_ITEM, INT_AS_LONG, IS_SUBTYPE = 0xfb3610, 0xfb3390, 0xfe0b10, 0xfbf700
LONG_AS_ULONG, LONG_AS_LONG, ERR_OCCURRED, ERR_CLEAR, ERR_FORMAT = 0xffbf60, 0xffbb80, 0xfc3cc0, 0xfc3650, 0xfc3720
OPERATOR_NEW, OPERATOR_DELETE, OPERATOR_DELETE_SIZED = 0x13d7fe9, 0x13d801e, 0x13d8019
STACK, HEAP, GDT, MARKS = 0x6000000, 0x6100000, 0x6300000, 0x6400000
ARGS, ITEMS, TYPES, MANAGER, SENTINEL = MARKS, MARKS + 0x100, MARKS + 0x400, MARKS + 0x800, MARKS + 0x900
INT_FLAG, LONG_FLAG = 0x800000, 0x1000000
# Python value kinds as the 32-bit runtime represents them.
KINDS = {'int': 0, 'long': 1, 'other': 2}


class OriginalGlyphCache(object):
    def __init__(self, executable):
        pe = pefile.PE(str(executable))
        assert pe.FILE_HEADER.Machine == 0x14c
        image, base = pe.get_memory_mapped_image(), pe.OPTIONAL_HEADER.ImageBase
        self.u = u = unicorn.Uc(unicorn.UC_ARCH_X86, unicorn.UC_MODE_32)
        u.mem_map(base, (len(image) + 0xfff) & ~0xfff)
        u.mem_write(base, image)
        for region in (STACK, HEAP, GDT, MARKS):
            u.mem_map(region, 0x100000)
        teb = (0x1000 | (GDT + 0x1000 & 0xffffff) << 16 | 0x92 << 40 | 0x4 << 52 | ((GDT + 0x1000) >> 24 & 0xff) << 56)
        flat = 0xffff | 0x92 << 40 | 0xf << 48 | 0xc << 52
        u.mem_write(GDT, struct.pack('<QQQ', 0, teb, flat))
        u.reg_write(UC_X86_REG_GDTR, (0, GDT, 23, 0))
        u.reg_write(UC_X86_REG_FS, 8)
        u.reg_write(UC_X86_REG_SS, 16)
        # The CRT selects the SSE2 conversion on every x86 CPU the client supports.
        u.mem_write(SSE2_FLAG, struct.pack('<I', 1))
        u.mem_write(MANAGER_POINTER, struct.pack('<I', MANAGER))
        self.hooks = {
            TUPLE_SIZE: lambda: self._return(len(self.args)),
            TUPLE_ITEM: lambda: self._return(ITEMS + 0x10 * self._arg(2)),
            INT_AS_LONG: lambda: self._return(self._value()),
            IS_SUBTYPE: lambda: self._return(0),
            LONG_AS_ULONG: self._as_ulong, LONG_AS_LONG: self._as_long,
            ERR_OCCURRED: lambda: self._return(1 if self.pending else 0),
            ERR_CLEAR: self._clear, ERR_FORMAT: self._format,
            OPERATOR_NEW: self._new,
            OPERATOR_DELETE: lambda: self._return(0), OPERATOR_DELETE_SIZED: lambda: self._return(0),
            SENTINEL: lambda: u.emu_stop()}
        pe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_IMPORT']])
        for entry in pe.DIRECTORY_ENTRY_IMPORT:
            for item in entry.imports:
                if item.name:
                    target = struct.unpack('<I', u.mem_read(item.address, 4))[0]
                    self.hooks[target] = (lambda name: lambda: self._import(name))(item.name.decode())
        for address in self.hooks:
            u.hook_add(unicorn.UC_HOOK_CODE, self._hook, begin=address, end=address)
        for kind, flags in ((0, INT_FLAG), (1, LONG_FLAG), (2, 0)):
            u.mem_write(TYPES + 0x60 * kind + 0x54, struct.pack('<I', flags))

    def _hook(self, u, address, size, data):
        self.hooks[address]()

    def _arg(self, index):
        return struct.unpack('<I', self.u.mem_read(self.u.reg_read(UC_X86_REG_ESP) + 4 * index, 4))[0]

    def _cstr(self, address):
        return bytes(self.u.mem_read(address, 0x200)).split(b'\0')[0].decode('latin1')

    def _return(self, value, pop=0):
        esp = self.u.reg_read(UC_X86_REG_ESP)
        if value is not None:
            self.u.reg_write(UC_X86_REG_EAX, value & 0xffffffff)
        self.u.reg_write(UC_X86_REG_EIP, self._arg(0))
        self.u.reg_write(UC_X86_REG_ESP, esp + 4 + pop)

    def _value(self):
        return self.args[(self._arg(1) - ITEMS) // 0x10][1]

    def _as_ulong(self):
        value = self._value()
        if not 0 <= value < 2 ** 32:
            self.pending = 'OverflowError'
            return self._return(0xffffffff)
        self._return(value)

    def _as_long(self):
        value = self._value()
        if not -2 ** 31 <= value < 2 ** 31:
            self.pending = 'OverflowError'
            return self._return(0xffffffff)
        self._return(value)

    def _clear(self):
        self.pending = None
        self._return(0)

    def _new(self):
        size, address = self._arg(1), self.heap
        self.heap += (max(size, 1) + 15) & ~15
        self._return(address)

    def _format(self):
        assert self._arg(1) == TYPE_ERROR
        text = self._cstr(self._arg(2))
        values = [self._arg(3 + n) for n in range(8)]
        parts, index = [], 0
        for piece in text.split('%')[1:]:
            parts.append(self._cstr(values[index]) if piece[0] == 's' else values[index])
            index += 1
        self.pending = 'TypeError: ' + text % tuple(parts)
        self._return(0)

    def _import(self, name):
        if name in ('memcpy', 'memmove'):
            self.u.mem_write(self._arg(1), bytes(self.u.mem_read(self._arg(2), self._arg(3))))
            return self._return(self._arg(1))
        if name == 'memset':
            self.u.mem_write(self._arg(1), bytes([self._arg(2) & 0xff]) * self._arg(3))
            return self._return(self._arg(1))
        if name == 'strlen':
            return self._return(len(self._cstr(self._arg(1))))
        if name == 'strncmp':
            count = self._arg(3)
            first, second = self._cstr(self._arg(1))[:count], self._cstr(self._arg(2))[:count]
            return self._return(0 if first == second else (1 if first > second else 0xffffffff))
        raise RuntimeError('unexpected import ' + name)

    def call(self, args):
        """args: (kind, value) with kind int/long/other/float; returns [result, error, flag, bytes]."""
        u = self.u
        self.args, self.pending, self.heap = [], None, HEAP
        for index, (kind, value) in enumerate(args):
            item = ITEMS + 0x10 * index
            if kind == 'float':
                u.mem_write(item, struct.pack('<IId', 1, FLOAT_TYPE, value))
            else:
                u.mem_write(item, struct.pack('<II', 1, TYPES + 0x60 * KINDS[kind]))
            self.args.append((kind, value))
        u.mem_write(MANAGER, b'\xcc' * 0x100)
        u.reg_write(UC_X86_REG_ESP, STACK + 0x80000)
        u.reg_write(UC_X86_REG_MXCSR, 0x1f80)
        u.mem_write(STACK + 0x80000, struct.pack('<III', SENTINEL, 0, ARGS))
        u.emu_start(SETTER, SENTINEL + 1, count=1000000)
        assert u.reg_read(UC_X86_REG_EIP) == SENTINEL, hex(u.reg_read(UC_X86_REG_EIP))
        result = u.reg_read(UC_X86_REG_EAX)
        block = bytes(u.mem_read(MANAGER, 0x100))
        # Only the flag 0xa8 and the 0x37 parameter bytes may change.
        assert block[:0xa8] == b'\xcc' * 0xa8 and block[0xa9:0xac] == b'\xcc' * 3, block.hex()
        assert block[0xe4:] == b'\xcc' * 0x1c, block.hex()
        written = block[0xa8] != 0xcc
        return [{0: 'NULL', NONE: 'None'}[result], self.pending, block[0xa8] if written else None,
                block[0xac:0xe3].hex() if written else None]


def scenarios():
    big = 2 ** 31
    # Out-of-range floats are excluded: C++ leaves (int) of them undefined.
    return [
        [], [('int', 1)], [('int', 0)], [('int', 1), ('float', 1024.0), ('float', 1024.0)],
        [('int', 1), ('float', 2048.0), ('float', 2048.0)], [('int', 1), ('float', 4096.0), ('float', 4096.0)],
        [('int', 2), ('int', 512), ('int', 256), ('int', 64)], [('int', 3), ('float', 2.9), ('float', -2.5)],
        [('int', 0), ('int', 0), ('int', 0), ('int', 0)], [('int', 1), ('int', big - 1)],
        [('int', 1), ('long', big)], [('int', 1), ('long', 2 ** 32 - 1)], [('long', 5)],
        [('int', -1)], [('int', 1), ('int', -7)], [('int', 1), ('long', 2 ** 32)], [('int', 1), ('long', -1)],
        [('int', 1), ('other', 'x')], [('other', None)], [('int', 1), ('int', 2), ('other', 'y')],
        [('int', 1), ('int', 2), ('int', 3), ('other', None)],
        [('int', 1), ('int', 2), ('int', 3), ('int', 4), ('int', 5)],
        [('float', 1.0), ('float', 0.5), ('float', 1e9), ('float', -0.0)],
    ]


def original(executable, steps):
    machine = OriginalGlyphCache(executable)
    return [machine.call([tuple(a) for a in step]) for step in steps]


def compare(executable, host, trace, directory):
    steps = scenarios()
    expected = original(executable, steps)
    (directory / 'steps.json').write_text(json.dumps(steps))
    output = subprocess.run([str(host), str(trace), str(directory / 'steps.json')], check=True,
                            capture_output=True, text=True).stdout
    actual = json.loads(output)
    assert len(actual) == len(expected)
    for step, want, got in zip(steps, expected, actual):
        if want != got:
            raise AssertionError('original %r != production %r for %r' % (want, got, step))
    return '%d calls (%d stored, %d rejected)' % (len(steps), sum(1 for e in expected if e[2] is not None),
                                                  sum(1 for e in expected if e[1]))
