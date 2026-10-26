"""Execute exact #1513 NLS date/time/fractional helpers in Unicorn.

The helpers 006f49f0/006f4c90/006f4dd0 (dates), 006f4a90/006f4d30 (times),
their owners 006f3d50/006f42b0, CRT 013d83cf and the fractional formatter
006f3ed0 run unchanged. _localtime64_s, Get*FormatW, StringCchPrintfW
(006d5d90), PyUnicode_FromWideChar and allocation are hooked fixtures.
"""
import math
import random
import struct

import pefile
import unicorn
from unicorn.x86_const import (UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EIP, UC_X86_REG_ESP,
                               UC_X86_REG_FS, UC_X86_REG_GDTR, UC_X86_REG_MXCSR, UC_X86_REG_SS)

HELPERS = {'LongDate': (0x6f49f0, 2), 'ShortDate': (0x6f4c90, 1), 'YMDate': (0x6f4dd0, 8),
           'LongTime': (0x6f4a90, 0), 'ShortTime': (0x6f4d30, 2)}
FRACTIONAL, PRINTF, FROM_WIDE = 0x6f3ed0, 0x6d5d90, 0xff4910
OPERATOR_NEW, OPERATOR_DELETE, CRT_EXCEPTION, OWNER = 0x13d7fe9, 0x13d801e, 0x13d836d, 0x1d29ef0
STACK, HEAP, GDT, DATA = 0x6000000, 0x6100000, 0x6300000, 0x6400000
MAX_TIME64 = 0x793582aff  # UCRT max_time_t: quiet EINVAL beyond it, like negative times
NLS, RESULT, SENTINEL, SEPARATOR, EMPTY = DATA, DATA + 0x200, DATA + 0x400, DATA + 0x500, 0x14ace04


class Original(object):
    def __init__(self, executable):
        pe = pefile.PE(str(executable))
        assert pe.FILE_HEADER.Machine == 0x14c
        image, base = pe.get_memory_mapped_image(), pe.OPTIONAL_HEADER.ImageBase
        self.u = u = unicorn.Uc(unicorn.UC_ARCH_X86, unicorn.UC_MODE_32)
        u.mem_map(base, (len(image) + 0xfff) & ~0xfff)
        u.mem_write(base, image)
        for region in (STACK, HEAP, GDT, DATA):
            u.mem_map(region, 0x100000)
        teb = (0x1000 | (GDT + 0x1000 & 0xffffff) << 16 | 0x92 << 40 | 0x4 << 52 | ((GDT + 0x1000) >> 24 & 0xff) << 56)
        flat = 0xffff | 0x92 << 40 | 0xf << 48 | 0xc << 52
        u.mem_write(GDT, struct.pack('<QQQ', 0, teb, flat))
        u.reg_write(UC_X86_REG_GDTR, (0, GDT, 23, 0))
        u.reg_write(UC_X86_REG_FS, 8)
        u.reg_write(UC_X86_REG_SS, 16)
        u.mem_write(OWNER, struct.pack('<I', NLS))
        self.hooks = {OPERATOR_NEW: self._new, OPERATOR_DELETE: lambda: self._return(0),
                      CRT_EXCEPTION: lambda: self._return(None), PRINTF: self._printf,
                      FROM_WIDE: self._from_wide, SENTINEL: lambda: u.emu_stop()}
        pe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_IMPORT']])
        for entry in pe.DIRECTORY_ENTRY_IMPORT:
            for item in entry.imports:
                if item.name:
                    target = struct.unpack('<I', u.mem_read(item.address, 4))[0]
                    self.hooks[target] = (lambda name: lambda: self._import(name))(item.name.decode())
        for address in self.hooks:
            u.hook_add(unicorn.UC_HOOK_CODE, self._hook, begin=address, end=address)
        self.heap = HEAP

    def _hook(self, u, address, size, data):
        self.hooks[address]()

    def _arg(self, index):
        return struct.unpack('<I', self.u.mem_read(self.u.reg_read(UC_X86_REG_ESP) + 4 * index, 4))[0]

    def _wide(self, address, count=None):
        data = bytes(self.u.mem_read(address, 2 * (count if count is not None else 600)))
        text = data.decode('utf-16le')
        return text if count is not None else text.split('\0')[0]

    def _return(self, value, pop=0):
        esp = self.u.reg_read(UC_X86_REG_ESP)
        if value is not None:
            self.u.reg_write(UC_X86_REG_EAX, value & 0xffffffff)
        self.u.reg_write(UC_X86_REG_EIP, self._arg(0))
        self.u.reg_write(UC_X86_REG_ESP, esp + 4 + pop)

    def _new(self):
        address, self.heap = self.heap, self.heap + ((self._arg(1) + 15) & ~15)
        self._return(address)

    def _printf(self):
        # StringCchPrintfW(buffer, 317, L"%lf", double): the CRT text is a fixture.
        assert self._wide(self._arg(3)) == '%lf' and self._arg(2) == 0x13d
        value = struct.unpack('<d', self.u.mem_read(self.u.reg_read(UC_X86_REG_ESP) + 16, 8))[0]
        self.trace['input'] = '%f' % value
        self.u.mem_write(self._arg(1), self.trace['input'].encode('utf-16le') + b'\0\0')
        self._return(0)

    def _from_wide(self):
        self.trace['result'] = self._wide(self._arg(1), self._arg(2))
        self._return(RESULT)

    def _import(self, name):
        case = self.case
        if name == '_localtime64_s':
            time = struct.unpack('<q', self.u.mem_read(self._arg(2), 8))[0]
            self.trace['time'] = time
            if time < 0 or time > MAX_TIME64:
                self.u.mem_write(self._arg(1), b'\xff' * 36)
                return self._return(22)
            self.u.mem_write(self._arg(1), struct.pack('<9i', *case['tm']))
            self._return(0)
        elif name in ('GetDateFormatW', 'GetTimeFormatW', 'GetNumberFormatW'):
            self.trace['locale'] = self._arg(1)
            self.trace['flags'] = self._arg(2)
            if name == 'GetNumberFormatW':
                fmt = self._arg(4)
                self.trace['format'] = None if not fmt else list(struct.unpack('<IIIIII', self.u.mem_read(fmt, 24)))
                if fmt:
                    self.trace['format'][3] = self._wide(self.trace['format'][3])
                    self.trace['format'][4] = self._wide(self.trace['format'][4])
            else:
                self.trace['system'] = list(struct.unpack('<8H', self.u.mem_read(self._arg(3), 16)))
                assert self._arg(4) == 0
            output, size = self._arg(5), self._arg(6)
            self.trace['size'] = size
            system = self.trace.get('system')
            valid = system is None or (1 <= system[1] <= 12 and 1 <= system[3] <= 31 and
                                       system[4] < 24 and system[5] < 60 and system[6] < 60)
            if case['fail'] or not valid:
                return self._return(0, 24)
            self.u.mem_write(output, case['output'].encode('utf-16le') + b'\0\0')
            self._return(len(case['output']) + 1, 24)
        elif name in ('memcpy', 'memmove'):
            self.u.mem_write(self._arg(1), bytes(self.u.mem_read(self._arg(2), self._arg(3))))
            self._return(self._arg(1))
        elif name == 'free':
            self._return(0)
        else:
            raise RuntimeError('unexpected import ' + name)

    def _start(self, address, stack):
        self.u.reg_write(UC_X86_REG_ESP, STACK + 0x80000)
        self.u.reg_write(UC_X86_REG_MXCSR, 0x1f80)
        self.u.mem_write(STACK + 0x80000, stack)
        self.u.emu_start(address, SENTINEL + 1, count=2000000)
        assert self.u.reg_read(UC_X86_REG_EIP) == SENTINEL

    def time(self, helper, case):
        self.case, self.trace = case, {'result': None}
        self._start(HELPERS[helper][0], struct.pack('<IId', SENTINEL, RESULT + 0x10, case['seconds']))
        return self.trace

    def fractional(self, case):
        self.case, self.trace = case, {'result': None}
        u = self.u
        u.mem_write(SEPARATOR, case['separator'].encode('utf-16le') + b'\0\0')
        u.mem_write(NLS + 0x54, case['separator'].encode('utf-16le') + b'\0\0')
        u.mem_write(NLS + 0x64, bytes([case['valid']]))
        u.mem_write(NLS + 0x68, struct.pack('<IIIIII', case['digits'], 1, 3, NLS + 0x54, SEPARATOR, 1))
        wide = HEAP + 0x80000
        u.mem_write(wide, b'\0' * 16 + struct.pack('<II', 0, 7))
        u.reg_write(UC_X86_REG_ECX, NLS)
        self._start(FRACTIONAL, struct.pack('<IdiI', SENTINEL, case['value'], case['precision'], wide))
        size, capacity = struct.unpack('<II', u.mem_read(wide + 16, 8))
        pointer = struct.unpack('<I', u.mem_read(wide, 4))[0] if capacity >= 8 else wide
        self.trace['result'] = self._wide(pointer, size)
        after = struct.unpack('<IIIIII', u.mem_read(NLS + 0x68, 24))
        self.trace['restored'] = after[0] == case['digits'] and after[3] == NLS + 0x54
        return self.trace


def time_cases(seed=1513, count=600):
    generator = random.Random(seed)
    cases = []
    for unused in range(count):
        seconds = generator.choice((0.0, 1.9, -0.5, -1.0, float('nan'), float('inf'), -float('inf'),
                                    1709687229.75, 2.0 ** 31, 4102444800.0, 1e19, -1e19,
                                    32536799999.0, 32536800000.0,
                                    generator.uniform(-1e3, 4e9), generator.uniform(0, 32537e6)))
        tm = [generator.randrange(0, 61), generator.randrange(0, 60), generator.randrange(0, 24),
              generator.randrange(1, 32), generator.randrange(0, 12), generator.randrange(70, 1100),
              generator.randrange(0, 7), generator.randrange(0, 366), generator.randrange(-1, 2)]
        cases.append({'seconds': seconds, 'tm': tm, 'fail': generator.random() < .1,
                      'output': generator.choice(('Tuesday, March 5, 2024', u'2024年3月', '20:07'))})
    return cases


def fractional_cases(seed=1513, count=600):
    generator = random.Random(seed)
    cases = []
    for unused in range(count):
        text = generator.choice(('1,200', '0.000', '12.50', '1.00-', '(1.00)', '7', '100', '3,000,000',
                                 u'1 234,560', '0,0', ''))
        cases.append({'value': generator.choice((1.2, 0.0, -2.5, 1e20, 1.0 / 3.0, 12345.678)),
                      'precision': generator.choice((-1, 0, 1, 2, 3, 6)), 'digits': generator.randrange(0, 5),
                      'valid': generator.random() < .85, 'fail': generator.random() < .1, 'output': text,
                      'separator': generator.choice((u',', u'.', u'٫'))})
    return cases
