"""Execute the exact #1513 FlashGUIComponent input-mode instructions in Unicorn and compare production.

wg_inputKeyMode getter 00aa86d0 (+ getData(uint) 0068ece0), setter 00aa8ba0 (+ setData(uint&)
0068fad0 and _ftol2 013d8a60), handleKeyEvent 00aa8700, handleMouseButtonEvent 00aa87d0 and
handleMouseEvent 00aa8890 run unchanged. The Python C API, SimpleGUIComponent's own handlers
(008c4b80/008c4d40/008c4f40) and PyMovieView's handlers (00aa6640/00aa67c0/00aa67d0) are hooked.
"""
import json
import struct
import subprocess

import pefile
import unicorn
from unicorn.x86_const import (UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EIP, UC_X86_REG_ESP, UC_X86_REG_FS,
                               UC_X86_REG_GDTR, UC_X86_REG_MXCSR, UC_X86_REG_SS)

GETTER, SETTER = 0xaa86d0, 0xaa8ba0
HANDLERS = {'key': (0xaa8700, 0x8c4b80, 0xaa6640), 'mouseButton': (0xaa87d0, 0x8c4d40, 0xaa67c0),
            'mouse': (0xaa8890, 0x8c4f40, 0xaa67d0)}
INT_FROM_LONG, LONG_FROM_ULONG = 0xfe1180, 0xffcb40
INT_AS_LONG, LONG_AS_ULONG, LONG_AS_LONG, FLOAT_AS_DOUBLE = 0xfe0b10, 0xffbf60, 0xffbb80, 0xfdc0b0
TYPE_IS_SUBTYPE, ERR_OCCURRED, ERR_CLEAR, ERR_FORMAT = 0xfbf700, 0xfc3cc0, 0xfc3650, 0xfc3720
FLOAT_TYPE, SSE2_AVAILABLE = 0x1a64370, 0x215e6dc
SCRIPT_SLOT, MOVIE_SLOT, MODE_SLOT = 0x84, 0x168, 0x170
STACK, GDT, MARKS = 0x6000000, 0x6300000, 0x6400000
OBJECT, SCRIPT, MOVIE, VALUE, TYPES, EVENT, FLD, SENTINEL = (
    MARKS, MARKS + 0x400, MARKS + 0x500, MARKS + 0x600, MARKS + 0x700, MARKS + 0x900, MARKS + 0xa00, MARKS + 0xb00)
INT_TYPE, LONG_TYPE, OTHER_TYPE = TYPES, TYPES + 0x80, TYPES + 0x100
KIND_TYPES = {'int': INT_TYPE, 'bool': INT_TYPE, 'long': LONG_TYPE, 'float': FLOAT_TYPE, 'str': OTHER_TYPE,
              'none': OTHER_TYPE}


def as_ulong(value):
    return (value, False) if 0 <= value < 1 << 32 else (0xffffffff, True)


def as_long(value):
    return (value & 0xffffffff, False) if -(1 << 31) <= value < 1 << 31 else (0xffffffff, True)


class OriginalFlashInputMode(object):
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
        # The CRT sets __sse2_available on every SSE2 CPU before any script runs.
        u.mem_write(SSE2_AVAILABLE, struct.pack('<I', 1))
        u.mem_write(INT_TYPE + 0x54, struct.pack('<I', 0x800000))
        u.mem_write(LONG_TYPE + 0x54, struct.pack('<I', 0x1000000))
        u.mem_write(OTHER_TYPE + 0x54, struct.pack('<I', 0))
        # PyFloat_AsDouble returns in st(0): its hook enters "fld qword [FLD+8]; ret".
        u.mem_write(FLD, b'\xdd\x05' + struct.pack('<I', FLD + 8) + b'\xc3')
        self.hooks = {
            INT_FROM_LONG: lambda: self._result('int', self._arg(1)),
            LONG_FROM_ULONG: lambda: self._result('long', self._arg(1)),
            INT_AS_LONG: lambda: self._return(self.value & 0xffffffff),
            LONG_AS_ULONG: lambda: self._convert(as_ulong),
            LONG_AS_LONG: lambda: self._convert(as_long),
            FLOAT_AS_DOUBLE: self._float,
            TYPE_IS_SUBTYPE: lambda: self._return(0),
            ERR_OCCURRED: lambda: self._return(1 if self.error else 0),
            ERR_CLEAR: self._clear,
            ERR_FORMAT: self._format,
            SENTINEL: lambda: u.emu_stop()}
        for name, (unused, base_handler, movie_handler) in HANDLERS.items():
            self.hooks[base_handler] = lambda: self._return(int(self.base), 4)
            self.hooks[movie_handler] = self._movie
        for address in self.hooks:
            u.hook_add(unicorn.UC_HOOK_CODE, self._hook, begin=address, end=address)

    def _hook(self, u, address, size, data):
        self.hooks[address]()

    def _arg(self, index):
        return struct.unpack('<I', self.u.mem_read(self.u.reg_read(UC_X86_REG_ESP) + 4 * index, 4))[0]

    def _cstr(self, address):
        return bytes(self.u.mem_read(address, 0x100)).split(b'\0')[0].decode('latin1')

    def _return(self, value, pop=0):
        esp = self.u.reg_read(UC_X86_REG_ESP)
        self.u.reg_write(UC_X86_REG_EAX, value & 0xffffffff)
        self.u.reg_write(UC_X86_REG_EIP, self._arg(0))
        self.u.reg_write(UC_X86_REG_ESP, esp + 4 + pop)

    def _result(self, kind, value):
        self.trace['result'] = [value, kind]
        self._return(VALUE)

    def _convert(self, function):
        value, failed = function(self.value)
        self.error = self.error or failed
        self._return(value)

    def _float(self):
        self.u.mem_write(FLD + 8, struct.pack('<d', self.value))
        self.u.reg_write(UC_X86_REG_EIP, FLD)

    def _clear(self):
        self.error = False
        self._return(0)

    def _format(self):
        self.trace['error'] = self._cstr(self._arg(2)) % self._cstr(self._arg(3))
        self.error = True
        self._return(0)

    def _movie(self):
        assert self.u.reg_read(UC_X86_REG_ECX) == MOVIE
        self.trace['movie'] = True
        self._return(int(self.movie), 4)

    def _call(self, address, this, *values):
        u = self.u
        self.trace, self.error = {'movie': False}, False
        u.reg_write(UC_X86_REG_ESP, STACK + 0x80000)
        u.reg_write(UC_X86_REG_MXCSR, 0x1f80)
        u.reg_write(UC_X86_REG_ECX, this)
        u.mem_write(STACK + 0x80000, struct.pack('<I' + 'I' * len(values), SENTINEL, *values))
        u.emu_start(address, SENTINEL + 1, count=100000)
        assert u.reg_read(UC_X86_REG_EIP) == SENTINEL, hex(u.reg_read(UC_X86_REG_EIP))
        return u.reg_read(UC_X86_REG_EAX)

    def _component(self, mode, script):
        self.u.mem_write(OBJECT, b'\0' * 0x200)
        self.u.mem_write(SCRIPT, struct.pack('<II', 100, OTHER_TYPE))
        self.u.mem_write(OBJECT + SCRIPT_SLOT, struct.pack('<I', SCRIPT if script else 0))
        self.u.mem_write(OBJECT + MOVIE_SLOT, struct.pack('<I', MOVIE))
        self.u.mem_write(OBJECT + MODE_SLOT, struct.pack('<I', mode))

    def set(self, initial, kind, value):
        self._component(initial, False)
        self.value = int(value) if kind in ('int', 'long', 'bool') else (value if kind == 'float' else 0)
        self.u.mem_write(VALUE, struct.pack('<II', 1, KIND_TYPES[kind]))
        status = self._call(SETTER, 0, OBJECT + 4, VALUE, 0)
        assert status in (0, 0xffffffff) and (status != 0) == ('error' in self.trace)
        error = self.trace.get('error')
        assert self.u.mem_read(SCRIPT, 4) == struct.pack('<I', 100)
        self._call(GETTER, 0, OBJECT + 4, 0)
        return [error] + self.trace['result']

    def handle(self, name, mode, base, script, movie):
        self._component(mode, script)
        self.base, self.movie = base, movie
        handler = HANDLERS[name][0]
        result = self._call(handler, OBJECT, EVENT) & 0xff
        assert struct.unpack('<I', self.u.mem_read(SCRIPT, 4))[0] == 100, 'script reference leaked'
        return [bool(result), self.trace['movie']]


VALUES = [('int', v) for v in (0, 1, 2, 3, 7, -1, (1 << 31) - 1, -(1 << 31))] + \
    [('bool', True), ('bool', False)] + [('float', v) for v in (2.7, -1.5, 0.0, 3.99)] + \
    [('long', v) for v in (0, 2, 3000000000, 4294967295, -1)] + [('str', 'x'), ('none', None)]
MODES = (0, 1, 2, 3, 4294967295)


def scenarios():
    steps = []
    for initial in (0, 2):
        for kind, value in VALUES:
            steps.append({'kind': 'set', 'initial': initial, 'value': [kind, value]})
    for mode in MODES:
        for base in (False, True):
            for script in (False, True):
                for movie in (False, True):
                    steps.append({'kind': 'key', 'mode': mode, 'base': base, 'script': script, 'movie': movie})
    return steps


def original(executable, steps):
    machine = OriginalFlashInputMode(executable)
    out = []
    for step in steps:
        if step['kind'] == 'set':
            out.append(machine.set(step['initial'], *step['value']))
        else:
            out.append(machine.handle('key', step['mode'], step['base'], step['script'], step['movie']))
    return out, machine


def mouse_is_mode_independent(machine):
    for name in ('mouseButton', 'mouse'):
        for base in (False, True):
            for script in (False, True):
                for movie in (False, True):
                    results = [machine.handle(name, mode, base, script, movie) for mode in MODES]
                    assert all(r == results[0] for r in results), (name, base, script, movie, results)
                    assert results[0] == [base or (not script and movie), not base and not script], results[0]


def comparable(step, value):
    # A 64-bit host returns PyInt for 2**31..2**32-1, where #1513 returns PyLong.
    if step['kind'] == 'set' and value[1] >= 1 << 31:
        return value[:2]
    return value


def compare(executable, host, trace, directory):
    steps = scenarios()
    expected, machine = original(executable, steps)
    mouse_is_mode_independent(machine)
    (directory / 'steps.json').write_text(json.dumps({'steps': steps}))
    output = subprocess.run([str(host), str(trace), str(directory / 'steps.json')], check=True,
                            capture_output=True, text=True).stdout
    actual = json.loads(output)
    assert len(actual) == len(expected)
    for step, want, got in zip(steps, expected, actual):
        if comparable(step, want) != comparable(step, got):
            raise AssertionError('original %r != production %r for %r' % (want, got, step))
    keys = [s for s in steps if s['kind'] == 'key']
    return '%d setter/getter and %d key-routing cases; mouse handlers ignore the mode' % (
        len(steps) - len(keys), len(keys))
