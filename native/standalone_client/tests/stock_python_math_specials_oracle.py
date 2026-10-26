"""Execute the pinned #1513 math/cmath Python methods on bit-exact inputs.

Each case enters the original PyCFunction and runs its wrapper, helper and
special-value tables unchanged. Only Python object boundaries (argument
unpacking, float/complex construction, exceptions) and exact CRT
classification imports are supplied. A case that reaches any other CRT import
is reported as host-dependent instead of being given a substituted value;
cos/sin feeding the sign of an infinite or zero result are the only exception.
"""
import math
import re
import struct

INF, NINF = 0x7ff0000000000000, 0xfff0000000000000
# Reals covering each special_type class, integer poles, subnormal and huge.
REALS = [NINF, 0xc004000000000000, 0xbff0000000000000, 0xc008000000000000,
         0xfe37e43c8800759c, 0x8000000000000001, 0x8000000000000000, 0,
         1, 0x3fe0000000000000, 0x3ff0000000000000, 0x4000000000000000,
         0x4004000000000000, 0x7e37e43c8800759c, 0x7fefffffffffffff, INF,
         0x7ff8000000000000, 0xfff8000000000000, 0x7ff8000000000123]
PAIR = [NINF, 0xc004000000000000, 0x8000000000000000, 0, 0x3ff0000000000000,
        0x4004000000000000, INF, 0x7ff8000000000123, 0xfff8000000000000]
MATH1 = ['acos', 'acosh', 'asin', 'asinh', 'atan', 'atanh', 'ceil', 'cos', 'cosh',
         'degrees', 'erf', 'erfc', 'exp', 'expm1', 'fabs', 'floor', 'gamma',
         'lgamma', 'log', 'log10', 'log1p', 'radians', 'sin', 'sinh', 'sqrt',
         'tan', 'tanh']
MATH2 = ['atan2', 'copysign', 'fmod', 'hypot', 'pow']
CMATH = ['acos', 'acosh', 'asin', 'asinh', 'atan', 'atanh', 'cos', 'cosh', 'exp',
         'log', 'log10', 'phase', 'polar', 'sin', 'sinh', 'sqrt', 'tan', 'tanh']
VALUE_ERROR, OVERFLOW_ERROR = 0x1a5dafc, 0x1a5d9fc


def cases():
    """Return (id, module, function, kind, words) for every executed case."""
    out = []
    for name in MATH1:
        out += [('math', name, 'f1', (x,)) for x in REALS]
    for name in MATH2:
        out += [('math', name, 'f2', (y, x)) for y in PAIR for x in PAIR]
    for name in CMATH:
        out += [('cmath', name, 'c1', (r, i)) for r in PAIR for i in PAIR]
    out += [('cmath', 'rect', 'f2', (r, p)) for r in PAIR for p in PAIR]
    # Windows-only: integer exponents and the float(text) parser path.
    out += [('math', 'ldexp', 'fi', (x, n)) for x in (0x3ff8000000000000, 0xbff8000000000000,
            INF, 0x7ff8000000000123, 0) for n in (2000, -2000, 1 << 40, -(1 << 40))]
    out += [('math', name, 'fs', (word, text)) for name in ('lgamma', 'exp')
            for word, text in ((INF, 'inf'), (NINF, '-inf'), (0xfff8000000000000, 'nan'))]
    out += [('math', 'atan2', 'f2', (0xfff8000000000000, INF))]
    return [('%04d' % n,) + case for n, case in enumerate(out)]


def case_line(case):
    cid, module, name, kind, words = case
    tokens = ['%016x' % words[0]] + [str(w) if not isinstance(w, int) or kind == 'fi'
                                     else '%016x' % w for w in words[1:]]
    return ' '.join((cid, module, name, kind) + tuple(tokens))


def parse_outcomes(text):
    """Map case id to outcome from a Windows probe output file."""
    result = {}
    for line in text.splitlines():
        cid, value = line.split(' ', 1)
        result[cid] = value.replace('E:exceptions.', 'E:')
    return result


def bits(value):
    return struct.unpack('<Q', struct.pack('<d', value))[0]


def double(word):
    return struct.unpack('<d', struct.pack('<Q', word))[0]


def outcome_text(outcome):
    kind, words = outcome
    if kind == 'E':
        return 'E:' + words
    return kind + ':' + ','.join('%016x' % w for w in words)


class HostDependent(Exception):
    pass


class OriginalPythonMathOracle:
    SIGN_ONLY = {'cos', 'sin'}

    def __init__(self, executable, cmath_source=None):
        import pefile
        from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
        from unicorn import x86_const as reg
        p = pefile.PE(str(executable))
        if p.FILE_HEADER.Machine != 0x14c or p.FILE_HEADER.TimeDateStamp != 0x5a6edca4:
            raise ValueError('The math oracle requires the pinned Chinese HD #1513 PE')
        self.reg = reg
        self.u = u = Uc(UC_ARCH_X86, UC_MODE_32)
        base = p.OPTIONAL_HEADER.ImageBase
        image = p.get_memory_mapped_image()
        u.mem_map(base, (len(image)+4095) & ~4095)
        u.mem_write(base, image)
        for address in (0x10000000, 0x20000000, 0x30000000):
            u.mem_map(address, 65536)
        self.imports = {}
        for table in p.DIRECTORY_ENTRY_IMPORT:
            for entry in table.imports:
                stub = 0x30001000 + len(self.imports)*16
                self.imports[stub] = entry.name.decode() if entry.name else str(entry.ordinal)
                u.mem_write(entry.address, struct.pack('<I', stub))
                # A bare ret; a double result returns through double_return.
                u.mem_write(stub, b'\xc3')
        self.slot = 0x30000f00
        self.double_return = 0x30000e00
        u.mem_write(self.double_return, b'\xdd\x05' + struct.pack('<I', self.slot) + b'\xc3')
        self.errno = 0x20003000
        self.stop = 0x30000d00
        u.mem_write(self.stop, b'\x90')
        # fninit; fldcw [0x30000cf0] (Win32 default 53-bit x87); ret to target
        self.entry = 0x30000c00
        u.mem_write(0x30000cf0, struct.pack('<H', 0x27f))
        u.mem_write(self.entry, b'\xdb\xe3\xd9\x2d' + struct.pack('<I', 0x30000cf0) + b'\xc3')
        u.hook_add(UC_HOOK_CODE, self._import, begin=0x30001000,
                   end=0x30001000 + 16*len(self.imports))
        self.internal = {
            0xfdc0b0: self._as_double, 0xfc3cc0: lambda sp: 0,
            0xfdc460: self._float_result, 0xfc4320: self._set_string,
            0xfc3f60: self._set_errno, 0xfc48e0: self._unpack_tuple,
            0xfc4840: self._parse_tuple, 0xfe1920: self._build_value,
            0x101cb70: self._complex_result, 0xfe1940: lambda sp: 0x20005000,
            0xfe1810: lambda sp: 0,
        }
        for address in self.internal:
            u.hook_add(UC_HOOK_CODE, self._internal, begin=address, end=address)
        self.methods = {'math': self._table(0x1a9bc08), 'cmath': self._table(0x1a8e438)}
        self.sign_only, self.result, self.module = False, None, 'cmath'
        self._call(0x106fa20, [])  # Original initcmath populates its 7x7 tables.
        self.merged_zeros = []
        if cmath_source is not None:
            self._restore_source_zeros(cmath_source)

    def _restore_source_zeros(self, source):
        """Give retail tables the public source's zero signs, nothing else.

        The #1513 compiler shared one 16-byte constant for entries differing
        only in a zero sign. That artifact is separate from HUGE_VAL/Py_NAN.
        """
        env = {'P': math.pi, 'P14': 0.25*math.pi, 'P12': 0.5*math.pi,
               'P34': 0.75*math.pi, 'INF': math.inf, 'U': -9.5426319407711027e33}
        tables = re.findall(r'INIT_SPECIAL_VALUES\((\w+), \{(.*?)\}\)', source, re.S)
        entries = [(name, token.strip()) for name, body in tables
                   for pair in re.findall(r'C\(([^,]+),([^)]+)\)', body) for token in pair]
        if len(tables) != 11 or len(entries) != 11*49*2:
            raise ValueError('The pinned cmath special-value tables changed')
        for index, (name, token) in enumerate(entries):
            address = 0x214ecb0 + 8*index
            retail = self.qword(address)
            source = 0xfff8000000000000 if token == 'N' else bits(float(eval(token, {}, env)))
            if retail == source:
                continue
            if retail & ~(1 << 63) or source & ~(1 << 63):
                raise ValueError('Retail %s differs from source beyond a zero sign' % name)
            self.merged_zeros.append((name, index//2 % 49, index % 2))
            self.u.mem_write(address, struct.pack('<Q', source))

    def word(self, address):
        return struct.unpack('<I', self.u.mem_read(address, 4))[0]

    def qword(self, address):
        return struct.unpack('<Q', self.u.mem_read(address, 8))[0]

    def _cstr(self, address):
        data = bytes(self.u.mem_read(address, 64))
        return data[:data.index(b'\0')].decode('ascii')

    def _table(self, address):
        methods = {}
        while self.word(address):
            methods[self._cstr(self.word(address))] = (self.word(address+4),
                                                       self.word(address+8))
            address += 16
        return methods

    def _return(self, sp, eax):
        self.u.reg_write(self.reg.UC_X86_REG_EAX, eax)
        self.u.reg_write(self.reg.UC_X86_REG_EIP, self.word(sp))
        self.u.reg_write(self.reg.UC_X86_REG_ESP, sp+4)

    def _return_double(self, word):
        self.u.mem_write(self.slot, struct.pack('<Q', word))
        self.u.reg_write(self.reg.UC_X86_REG_EIP, self.double_return)

    def _internal(self, unused_uc, address, unused_size, unused_data):
        sp = self.u.reg_read(self.reg.UC_X86_REG_ESP)
        result = self.internal[address](sp)
        if result is not None:
            self._return(sp, result)

    def _import(self, unused_uc, address, unused_size, unused_data):
        sp = self.u.reg_read(self.reg.UC_X86_REG_ESP)
        name = self.imports[address]
        arg = lambda i: self.qword(sp+4+8*i)
        if name == '_errno':
            return self._return(sp, self.errno)
        if name in ('_finite', '_isnan'):
            exponent = arg(0) >> 52 & 0x7ff
            nan = exponent == 0x7ff and arg(0) & (1 << 52)-1
            result = exponent != 0x7ff if name == '_finite' else bool(nan)
            return self._return(sp, int(result))
        if name == '_copysign':
            return self._return_double(arg(0) & ~(1 << 63) | arg(1) & 1 << 63)
        if name == 'fabs':
            return self._return_double(arg(0) & ~(1 << 63))
        if name in ('floor', 'ceil'):
            x = double(arg(0))
            if not math.isfinite(x) or x == int(x):
                return self._return_double(arg(0))
            return self._return_double(bits(math.copysign(getattr(math, name)(x), x)))
        if name in self.SIGN_ONLY and self.module == 'cmath' and math.isfinite(double(arg(0))):
            self.sign_only = True
            return self._return_double(bits(getattr(math, name)(double(arg(0)))))
        sse = name[len('__libm_sse2_'):] if name.startswith('__libm_sse2_') else None
        if sse in self.SIGN_ONLY and self.module == 'cmath':
            # Register ABI: the argument and result are the low lane of xmm0.
            xmm = self.u.reg_read(self.reg.UC_X86_REG_XMM0)
            if math.isfinite(double(xmm & (1 << 64)-1)):
                self.sign_only = True
                value = bits(getattr(math, sse)(double(xmm & (1 << 64)-1)))
                self.u.reg_write(self.reg.UC_X86_REG_XMM0, xmm >> 64 << 64 | value)
                return self._return(sp, 0)
        raise HostDependent(name)

    def _as_double(self, sp):
        self._return_double(self.objects[self.word(sp+4)])

    def _float_result(self, sp):
        self.result = ('F', (self.qword(sp+4),))
        return 0x20006000

    def _complex_result(self, sp):
        self.result = ('C', (self.qword(sp+4), self.qword(sp+12)))
        return 0x20006000

    def _build_value(self, sp):
        if self._cstr(self.word(sp+4)) != 'dd':
            raise RuntimeError('Unexpected original Py_BuildValue format')
        self.result = ('T', (self.qword(sp+8), self.qword(sp+16)))
        return 0x20006000

    def _set_string(self, sp):
        kind = {VALUE_ERROR: 'ValueError', OVERFLOW_ERROR: 'OverflowError'}
        exc = kind[next(k for k in kind if self.word(k) == self.word(sp+4))]
        self.result = ('E', exc + ':' + self._cstr(self.word(sp+8)))
        return 0

    def _set_errno(self, sp):
        self.result = ('E', 'errno:%d' % self.word(self.errno))
        return 0

    def _unpack_tuple(self, sp):
        count = len(self.arguments)
        if not self.word(sp+12) <= count <= self.word(sp+16):
            raise RuntimeError('Unexpected original argument count')
        for i in range(self.word(sp+16)):
            pointer = self.word(sp+20+4*i)
            self.u.mem_write(pointer, struct.pack('<I', 0x20001000+16*i if i < count else 0))
        return 1

    def _parse_tuple(self, sp):
        fmt = self._cstr(self.word(sp+8))
        if fmt.split(':')[0] in ('D', 'D|D'):
            self.u.mem_write(self.word(sp+12), struct.pack('<QQ', *self.arguments))
        elif fmt == 'dd:rect':
            for i, word in enumerate(self.arguments):
                self.u.mem_write(self.word(sp+12+4*i), struct.pack('<Q', word))
        else:
            raise RuntimeError('Unexpected original PyArg_ParseTuple format ' + fmt)
        return 1

    def _call(self, function, arguments):
        reg, u = self.reg, self.u
        sp = 0x10008000
        u.mem_write(sp, struct.pack('<'+'I'*(len(arguments)+2), function, self.stop, *arguments))
        u.reg_write(reg.UC_X86_REG_ESP, sp)
        u.reg_write(reg.UC_X86_REG_EBP, 0)
        u.reg_write(reg.UC_X86_REG_MXCSR, 0x1f80)
        u.emu_start(self.entry, self.stop, count=200000)

    def run(self, module, name, kind, words):
        """Return (outcome, sign_only) or raise HostDependent."""
        if kind == 'fi':
            raise HostDependent('integer exponent object')
        if kind == 'fs':
            kind, words = 'f1', words[:1]
        self.sign_only, self.result, self.module = False, None, module
        self.u.mem_write(self.errno, bytes(4))
        self.arguments = words
        self.objects = {}
        for i, word in enumerate(words):
            # Fake objects keep a non-long type for loghelper's PyLong_Check.
            obj = 0x20001000+16*i
            self.u.mem_write(obj, struct.pack('<II', 1000, 0x20002000))
            self.objects[obj] = word
        self.u.mem_write(0x20002000, bytes(256))
        # The argument tuple header; cmath.log reads its size directly.
        size = 1 if kind == 'c1' else len(words)
        self.u.mem_write(0x20004000, struct.pack('<III', 1000, 0x20002000, size))
        function, flags = self.methods[module][name]
        args = 0x20001000 if flags == 8 else 0x20004000  # METH_O or METH_VARARGS
        self._call(function, [0, args])
        if self.result is None:
            raise RuntimeError('Original method returned without a result boundary')
        if self.sign_only and self.result[0] != 'E':
            # Host cos/sin may only choose the sign of an infinite or zero part.
            for word in self.result[1]:
                if word & ~(1 << 63) and math.isfinite(double(word)):
                    raise HostDependent('cos/sin magnitude')
        return self.result, self.sign_only
