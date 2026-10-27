"""Execute the exact #1513 Math oscillator bindings and laws in Unicorn.

The four factories (008a0d60, 008a0900, 008a0ef0, 008a1140), the shared
reset/update/applyImpulse methods (008a1470, 008a14d0, 008a13c0) and the
attribute getters and setters run unchanged, together with every virtual law,
the std::function generators, mt19937, generate_canonical and __ftoui3.
PyTuple_Size/GetItem, the float/Vector3 Script::setData overloads,
Script::getData(Vector3), argCountError, operator new/delete and the
PyObjectPlus base constructor are hooked. The CRT __libm_sse2_* sinf, cosf,
acosf and expf thunks call the host libm so the CPython host build of the
production law, which calls the same libm, can be compared bit for bit.
"""
import ctypes
import ctypes.util
import struct

import pefile
import unicorn
from unicorn.x86_const import (UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EIP, UC_X86_REG_ESP,
                               UC_X86_REG_FPCW, UC_X86_REG_FS, UC_X86_REG_GDTR, UC_X86_REG_SS,
                               UC_X86_REG_XMM0)

TYPES = {'PyOscillator': (0x1a16950, 0x8a0d60), 'PyCompoundOscillator': (0x1a16a18, 0x8a0900),
         'PyRandomNoiseOscillatorFlat': (0x1a16ae0, 0x8a0ef0),
         'PyRandomNoiseOscillatorSpherical': (0x1a16ba8, 0x8a1140)}
METHODS = {'reset': 0x8a1470, 'update': 0x8a14d0, 'applyImpulse': 0x8a13c0}
ATTRIBUTES = {'externalForce': (0x8a1630, 0x8a17d0), 'velocity': (0x8a1660, 0x8a1840),
              'deviation': (0x8a1610, 0x8a1760), 'constraints': (0x8a15c0, 0x8a16f0)}
SET_FLOAT, SET_VECTOR3, GET_VECTOR3 = 0x68fba0, 0x690240, 0x68ebd0
TUPLE_SIZE, TUPLE_ITEM, ARG_COUNT_ERROR = 0xfb3610, 0xfb3390, 0x68e100
NEW, DELETE, PYOBJECT_INIT, BAD_CALL = 0x13d7fe9, 0x13d801e, 0x6a8520, 0x13d7914
LIBM = {0x13dad0d: 'sinf', 0x13dad25: 'cosf', 0x13dad3d: 'acosf', 0x13daddf: 'expf'}
LOG2F, CEIL, EXCEPT1 = 0x16059b8, 0x160585e, 0x13dafa1
NONE = 0x1a5c798
STACK, GDT, MARKS, HEAP = 0x6000000, 0x6300000, 0x6400000, 0x7000000
ARGS, ITEMS, CONSTANT, STUB_FLOAT, STUB_DOUBLE, SENTINEL = (MARKS + 0x100, MARKS + 0x200, MARKS + 0x800,
                                                          MARKS + 0x900, MARKS + 0x910, MARKS + 0xf00)
GOT = MARKS + 0x1000


def f32(value):
    return struct.unpack('<f', struct.pack('<f', value))[0]


def bits(value):
    return struct.unpack('<I', struct.pack('<f', value))[0]


def libm():
    library = ctypes.CDLL(ctypes.util.find_library('m') or None)
    functions = {}
    for name in ('sinf', 'cosf', 'acosf', 'expf'):
        function = getattr(library, name)
        function.restype, function.argtypes = ctypes.c_float, [ctypes.c_float]
        functions[name] = function
    return functions


class OriginalOscillators(object):
    def __init__(self, executable):
        pe = pefile.PE(str(executable))
        assert pe.FILE_HEADER.Machine == 0x14c
        image, base = pe.get_memory_mapped_image(), pe.OPTIONAL_HEADER.ImageBase
        self.u = u = unicorn.Uc(unicorn.UC_ARCH_X86, unicorn.UC_MODE_32)
        u.mem_map(base, (len(image) + 0xfff) & ~0xfff)
        u.mem_write(base, image)
        for region in (STACK, GDT, MARKS):
            u.mem_map(region, 0x100000)
        u.mem_map(HEAP, 0x4000000)
        teb = (0x1000 | (GDT + 0x1000 & 0xffffff) << 16 | 0x92 << 40 | 0x4 << 52 | ((GDT + 0x1000) >> 24 & 0xff) << 56)
        flat = 0xffff | 0x92 << 40 | 0xf << 48 | 0xc << 52
        u.mem_write(GDT, struct.pack('<QQQ', 0, teb, flat))
        u.reg_write(UC_X86_REG_GDTR, (0, GDT, 23, 0))
        u.reg_write(UC_X86_REG_FS, 8)
        u.reg_write(UC_X86_REG_SS, 16)
        # fld dword [CONSTANT]; ret and fld qword [CONSTANT]; ret
        u.mem_write(STUB_FLOAT, b'\xd9\x05' + struct.pack('<I', CONSTANT) + b'\xc3')
        u.mem_write(STUB_DOUBLE, b'\xdd\x05' + struct.pack('<I', CONSTANT) + b'\xc3')
        self.libm = libm()
        self.heap = HEAP
        self.hooks = {TUPLE_SIZE: lambda: self._return(len(self.args)),
                      TUPLE_ITEM: lambda: self._return(self._item(self._arg(2))),
                      SET_FLOAT: lambda: self._set_data('float'), SET_VECTOR3: lambda: self._set_data('Vector3'),
                      GET_VECTOR3: self._get_vector3, ARG_COUNT_ERROR: self._arg_count_error,
                      NEW: lambda: self._return(self._allocate(self._arg(1))), DELETE: lambda: self._return(0),
                      PYOBJECT_INIT: self._pyobject_init, BAD_CALL: self._unexpected,
                      LOG2F: self._log2f, CEIL: self._ceil, EXCEPT1: self._except1, SENTINEL: lambda: u.emu_stop()}
        for address, name in LIBM.items():
            self.hooks[address] = (lambda name: lambda: self._libm(name))(name)
        for address in self.hooks:
            u.hook_add(unicorn.UC_HOOK_CODE, lambda u, address, size, data: self.hooks[address](),
                       begin=address, end=address)
        self.objects = {}

    # -- hooked helpers ----------------------------------------------------
    def _arg(self, index):
        return struct.unpack('<I', self.u.mem_read(self.u.reg_read(UC_X86_REG_ESP) + 4 * index, 4))[0]

    def _cstr(self, address):
        return bytes(self.u.mem_read(address, 0x100)).split(b'\0')[0].decode('latin1')

    def _return(self, value, pop=0):
        esp = self.u.reg_read(UC_X86_REG_ESP)
        self.u.reg_write(UC_X86_REG_EAX, value & 0xffffffff)
        self.u.reg_write(UC_X86_REG_EIP, self._arg(0))
        self.u.reg_write(UC_X86_REG_ESP, esp + 4 + pop)

    def _allocate(self, size):
        address = (self.heap + 15) & ~15
        self.heap = address + size
        self.u.mem_write(address, b'\xcd' * size)
        return address

    def _pyobject_init(self):
        this = self.u.reg_read(UC_X86_REG_ECX)
        self.u.mem_write(this, struct.pack('<II', 1000, self._arg(1)))
        self._return(this, 8)

    def _item(self, index):
        value = self.args[index]
        if isinstance(value, dict):
            return self.objects[value['ref']]
        return ITEMS + 0x10 * index

    def _set_data(self, kind):
        index = (self._arg(1) - ITEMS) // 0x10 if self._arg(1) >= ITEMS and self._arg(1) < ITEMS + 0x100 else 0
        name = self._cstr(self._arg(3))
        self.calls.append(['setData', kind, name])
        if self.fail_at is not None and len([c for c in self.calls if c[0] == 'setData']) - 1 == self.fail_at:
            return self._return(0xffffffff)
        value = self.value if self.value is not None else self.args[index]
        if kind == 'float':
            self.u.mem_write(self._arg(2), struct.pack('<f', value))
        else:
            self.u.mem_write(self._arg(2), struct.pack('<fff', *value))
        self._return(0)

    def _get_vector3(self):
        self.vector = list(struct.unpack('<III', self.u.mem_read(self._arg(1), 12)))
        self._return(GOT)

    def _arg_count_error(self):
        total = self._arg(3)
        self.calls.append(['argCountError', self._cstr(self._arg(1)), self._arg(2), total] +
                          [self._cstr(self._arg(4 + n)) for n in range(total)])
        self._return(0)

    def _libm(self, name):
        value = struct.unpack('<f', struct.pack('<I', self.u.reg_read(UC_X86_REG_XMM0) & 0xffffffff))[0]
        self.u.reg_write(UC_X86_REG_XMM0, bits(self.libm[name](value)))
        self._return(0)

    def _log2f(self):
        value = struct.unpack('<f', self.u.mem_read(self.u.reg_read(UC_X86_REG_ESP) + 4, 4))[0]
        assert value == 4294967296.0
        self.u.mem_write(CONSTANT, struct.pack('<f', 32.0))
        self.u.reg_write(UC_X86_REG_EIP, STUB_FLOAT)

    def _ceil(self):
        import math
        value = struct.unpack('<d', self.u.mem_read(self.u.reg_read(UC_X86_REG_ESP) + 4, 8))[0]
        self.u.mem_write(CONSTANT, struct.pack('<d', math.ceil(value)))
        self.u.reg_write(UC_X86_REG_EIP, STUB_DOUBLE)

    def _except1(self):
        # _except1 with the default masked exceptions: restore the caller's
        # control word and return the default result in ST0.
        esp = self.u.reg_read(UC_X86_REG_ESP)
        self.u.reg_write(UC_X86_REG_FPCW, struct.unpack('<I', self.u.mem_read(esp + 0x1c, 4))[0] & 0xffff)
        self.u.mem_write(CONSTANT, bytes(self.u.mem_read(esp + 0x14, 8)))
        self.u.reg_write(UC_X86_REG_EIP, STUB_DOUBLE)

    def _unexpected(self):
        raise AssertionError('std::bad_function_call reached')

    # -- calls ---------------------------------------------------------------
    def _call(self, address, *stack, args=(), value=None, fail_at=None):
        u = self.u
        self.args, self.value, self.fail_at, self.calls, self.vector = list(args), value, fail_at, [], None
        u.reg_write(UC_X86_REG_FPCW, 0x27f)
        top = STACK + 0x80000
        u.reg_write(UC_X86_REG_ESP, top)
        u.mem_write(top, struct.pack('<%dI' % (len(stack) + 1), SENTINEL, *stack))
        u.emu_start(address, SENTINEL + 1, count=2000000)
        assert u.reg_read(UC_X86_REG_EIP) == SENTINEL, hex(u.reg_read(UC_X86_REG_EIP))
        return u.reg_read(UC_X86_REG_EAX)

    def new(self, ident, kind, args, fail_at=None):
        type_object, factory = TYPES[kind]
        result = self._call(factory, type_object, ARGS, args=args, fail_at=fail_at)
        if result:
            self.objects[ident] = result
        return result

    def method(self, ident, name, args=(), fail_at=None):
        return self._call(METHODS[name], self.objects[ident], ARGS, args=args, fail_at=fail_at)

    def get(self, ident, name):
        result = self._call(ATTRIBUTES[name][0], self.objects[ident], 0)
        assert result == GOT
        return self.vector

    def set(self, ident, name, vector, fail=False):
        return self._call(ATTRIBUTES[name][1], self.objects[ident], ITEMS, 0, value=vector,
                          fail_at=0 if fail else None)


def run_scenario(executable, operations):
    """Run the shared operation list; the CPython host consumer mirrors it."""
    original = OriginalOscillators(executable)
    report = []
    for operation in operations:
        verb = operation[0]
        if verb == 'new':
            _, ident, kind, args = operation
            report.append(['new', ident, bool(original.new(ident, kind, args))])
        elif verb == 'update':
            original.method(operation[1], 'update', [operation[2]])
        elif verb == 'impulse':
            original.method(operation[1], 'applyImpulse', [operation[2]])
        elif verb == 'reset':
            original.method(operation[1], 'reset')
        elif verb == 'set':
            original.set(operation[1], operation[2], operation[3])
        elif verb == 'get':
            report.append(['get', operation[1], operation[2], original.get(operation[1], operation[2])])
        else:
            raise ValueError(verb)
    return report


def contract_cases():
    """Argument-count and conversion-failure cases for every binding entry."""
    vector = [1.0, 2.0, 3.0]
    factories = {'PyOscillator': [1.0, vector, vector, vector],
                 'PyRandomNoiseOscillatorFlat': [1.0, 8600.0, 33.0, 0.01],
                 'PyRandomNoiseOscillatorSpherical': [0.7, 8600.0, 33.0, vector, 0.01]}
    cases = []
    for kind, full in sorted(factories.items()):
        for count in range(len(full) + 2):
            args = (full + [1.0, 1.0])[:count]
            cases.append(('new', kind, args, None))
            if count >= 3 and count <= len(full):
                cases.extend(('new', kind, args, index) for index in range(count))
    cases.append(('new', 'PyCompoundOscillator', [{'ref': 'a'}], None))
    cases.append(('new', 'PyCompoundOscillator', [{'ref': 'a'}, {'ref': 'b'}, {'ref': 'a'}], None))
    for name, full in (('reset', []), ('update', [0.1]), ('applyImpulse', [vector])):
        for count in range(3):
            cases.append(('method', name, (full + [0.5, 0.5])[:count], None))
        cases.extend(('method', name, full, index) for index in range(len(full)))
    return cases


def run_contract(executable):
    original = OriginalOscillators(executable)
    original.new('a', 'PyOscillator', [1.0, [1.0, 1.0, 1.0]] + [[1.0, 1.0, 1.0]] * 2)
    original.new('b', 'PyOscillator', [1.0, [1.0, 1.0, 1.0]] + [[1.0, 1.0, 1.0]] * 2)
    report = []
    for verb, name, args, fail_at in contract_cases():
        if verb == 'new':
            result = original.new('case', name, args, fail_at)
        else:
            result = original.method('a', name, args, fail_at)
        outcome = 'NULL' if result == 0 else ('None' if result == NONE else 'object')
        report.append([verb, name, len(args), fail_at, outcome, original.calls])
    for name in sorted(ATTRIBUTES):
        result = original.set('a', name, [1.0, 2.0, 3.0], fail=True)
        report.append(['setter', name, 1, 0, -1 if result == 0xffffffff else result, original.calls])
    return report
