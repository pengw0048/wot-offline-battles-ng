"""Execute exact #1513 PyMutex instructions in Unicorn and compare production.

tp_new 006ae2a0, create 006ae480, close 006ae430 (+ 006ae7b0), acquire
006ae350 (+ CRT 013d83b1), release 006ae620, dealloc 006ae740 and the
argument formatter 0068e100 run unchanged. Python C API, Script converters,
allocation and KERNEL32 imports are hooked and recorded.
"""
import json
import math
import random
import struct
import subprocess

import pefile
import unicorn
from unicorn.x86_const import (UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EIP, UC_X86_REG_ESP,
                               UC_X86_REG_FS, UC_X86_REG_GDTR, UC_X86_REG_MXCSR, UC_X86_REG_SS)

METHODS = {'new': 0x6ae2a0, 'create': 0x6ae480, 'close': 0x6ae430, 'acquire': 0x6ae350,
           'release': 0x6ae620, 'dealloc': 0x6ae740}
TUPLE_SIZE, TUPLE_ITEM, SET_STRING, SET_FLOAT, GET_BOOL = 0xfb3610, 0xfb3390, 0x68fe20, 0x68fba0, 0x68ee00
ERR_SET_STRING, ERR_FORMAT, OBJECT_INIT, OPERATOR_NEW, OPERATOR_DELETE = 0xfc4320, 0xfc3720, 0x6a8520, 0x13d7fe9, 0x13d801e
CRT_EXCEPTION, NONE, PYMUTEX = 0x13d836d, 0x1a5c798, 0x1a0cb40
EXCEPTIONS = {0x1a5e648: 'SystemError', 0x1a5db10: 'TypeError'}  # *01a5dadc, *01a5dae4
STACK, HEAP, GDT, MARKS = 0x6000000, 0x6100000, 0x6300000, 0x6400000
OBJECT, ARGS, ITEM, TRUE, FALSE, SENTINEL = MARKS, MARKS + 0x100, MARKS + 0x200, MARKS + 0x300, MARKS + 0x310, MARKS + 0x400
KERNEL = ('CreateMutexA', 'WaitForSingleObject', 'ReleaseMutex', 'CloseHandle', 'GetLastError', 'FormatMessageA')


class OriginalPyMutex(object):
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
        self.hooks = {TUPLE_SIZE: lambda: self._return(len(self.step['args'])),
                      TUPLE_ITEM: lambda: self._return(ITEM), SET_STRING: self._set_string,
                      SET_FLOAT: self._set_float, GET_BOOL: self._get_bool,
                      ERR_SET_STRING: lambda: self._error(self._arg(2), None),
                      ERR_FORMAT: lambda: self._error(self._arg(2), [self._arg(3 + n) for n in range(8)]),
                      OBJECT_INIT: self._object_init, OPERATOR_NEW: self._new, OPERATOR_DELETE: lambda: self._return(0),
                      CRT_EXCEPTION: lambda: self._return(None), SENTINEL: lambda: u.emu_stop()}
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

    def _cstr(self, address):
        return bytes(self.u.mem_read(address, 0x500)).split(b'\0')[0].decode('latin1')

    def _return(self, value, pop=0):
        esp = self.u.reg_read(UC_X86_REG_ESP)
        if value is not None:
            self.u.reg_write(UC_X86_REG_EAX, value & 0xffffffff)
        self.u.reg_write(UC_X86_REG_EIP, self._arg(0))
        self.u.reg_write(UC_X86_REG_ESP, esp + 4 + pop)

    def _new(self):
        size, address = self._arg(1), self.heap
        self.heap += (size + 15) & ~15
        self._return(address)

    def _object_init(self):
        # PyObjectPlus(type, false) constructor: initialise the PyObject header.
        this = self.u.reg_read(UC_X86_REG_ECX)
        self.u.mem_write(this, struct.pack('<II', 1, self._arg(1)))
        self.trace['created'] = this
        self._return(this, 8)

    def _set_string(self):
        target, value = self._arg(2), self.step['args'][0]
        if not isinstance(value, str):
            return self._return(0xffffffff)
        data = value.encode('latin1')
        if len(data) < 16:
            self.u.mem_write(target, data + b'\0' * (16 - len(data)) + struct.pack('<II', len(data), 15))
        else:
            buffer, self.heap = self.heap, self.heap + ((len(data) + 16) & ~15)
            self.u.mem_write(buffer, data + b'\0')
            self.u.mem_write(target, struct.pack('<I', buffer) + b'\0' * 12 + struct.pack('<II', len(data), len(data)))
        self._return(0)

    def _set_float(self):
        value = self.step['args'][0]
        if not isinstance(value, float):
            return self._return(0xffffffff)
        self.u.mem_write(self._arg(2), struct.pack('<f', value))
        self._return(0)

    def _get_bool(self):
        self._return(TRUE if self._arg(1) & 0xff else FALSE)

    def _error(self, text_address, values):
        text, parts, index = self._cstr(text_address), [], 0
        if values is None:
            self.trace['errors'].append([EXCEPTIONS[self._arg(1)], text])
            return self._return(0)
        for piece in text.split('%')[1:]:
            parts.append(self._cstr(values[index]) if piece[0] == 's' else values[index])
            index += 1
        self.trace['errors'].append([EXCEPTIONS[self._arg(1)], text % tuple(parts)])
        self._return(0)

    def _import(self, name):
        kernel = self.step['kernel']
        if name == 'CreateMutexA':
            self.trace['calls'].append([name, self._arg(1), self._arg(2), self._cstr(self._arg(3))])
            self._return(kernel['created'], 12)
        elif name == 'WaitForSingleObject':
            self.trace['calls'].append([name, self._arg(1), self._arg(2)])
            self._return(kernel['wait'], 8)
        elif name in ('ReleaseMutex', 'CloseHandle'):
            self.trace['calls'].append([name, self._arg(1)])
            self._return(kernel['released' if name == 'ReleaseMutex' else 'closed'], 4)
        elif name == 'GetLastError':
            self.trace['calls'].append([name])
            self._return(kernel['lastError'])
        elif name == 'FormatMessageA':
            self.trace['calls'].append([name, self._arg(1), self._arg(3), self._arg(4), self._arg(6)])
            if kernel['formatted']:
                text = ('system message %u\r\n' % self._arg(3)).encode()
                self.u.mem_write(self._arg(5), text + b'\0')
                return self._return(len(text), 28)
            self._return(0, 28)
        elif name in ('strncmp', 'memcpy', 'memmove', 'strlen', 'free'):
            if name == 'strncmp':
                count = self._arg(3)
                first = self._cstr(self._arg(1))[:count]
                second = self._cstr(self._arg(2))[:count]
                self._return(0 if first == second else (1 if first > second else 0xffffffff))
            elif name in ('memcpy', 'memmove'):
                self.u.mem_write(self._arg(1), bytes(self.u.mem_read(self._arg(2), self._arg(3))))
                self._return(self._arg(1))
            elif name == 'strlen':
                self._return(len(self._cstr(self._arg(1))))
            else:
                self._return(0)
        else:
            raise RuntimeError('unexpected import ' + name)

    def run(self, step, handle):
        u, self.step = self.u, step
        self.trace = {'calls': [], 'errors': []}
        u.mem_write(OBJECT, struct.pack('<III', 1, PYMUTEX, handle))
        u.reg_write(UC_X86_REG_ESP, STACK + 0x80000)
        u.reg_write(UC_X86_REG_MXCSR, 0x1f80)
        if step['method'] == 'new':
            stack = struct.pack('<IIII', SENTINEL, PYMUTEX, ARGS, 0)
        elif step['method'] == 'dealloc':
            stack = struct.pack('<II', SENTINEL, OBJECT)
        else:
            stack = struct.pack('<IIII', SENTINEL, OBJECT, ARGS, 0)
        u.mem_write(STACK + 0x80000, stack)
        u.emu_start(METHODS[step['method']], SENTINEL + 1, count=1000000)
        assert u.reg_read(UC_X86_REG_EIP) == SENTINEL, hex(u.reg_read(UC_X86_REG_EIP))
        result = u.reg_read(UC_X86_REG_EAX)
        target = self.trace.get('created', OBJECT)
        self.trace['handle'] = struct.unpack('<I', u.mem_read(target + 8, 4))[0]
        if step['method'] == 'dealloc':
            self.trace['result'] = 'void'
        elif step['method'] == 'new':
            self.trace['result'] = 'NULL' if result == 0 else 'object'
            if result == 0:
                self.trace['handle'] = 0
        else:
            self.trace['result'] = {0: 'NULL', NONE: 'None', TRUE: 'True', FALSE: 'False'}[result]
        return self.trace


def scenarios(seed=1513, count=1500):
    generator = random.Random(seed)
    names = ('wot_ipc', '', 'Global\\wot_ipc', 'a' * 40, 'Local\\x')
    floats = (0.0, 0.25, 1.0, -1.0, 4294967.5, 1e30, -1e-4, float('inf'), float('nan'), 2.5e-4, 3.0e9, -0.0)
    result = []
    for unused in range(count):
        method = generator.choice(('new', 'create', 'create', 'close', 'acquire', 'acquire', 'release', 'dealloc'))
        expected = {'new': 0, 'create': 1, 'close': 0, 'acquire': 1, 'release': 0, 'dealloc': 0}[method]
        size = expected if generator.random() < .85 else generator.choice((0, 1, 2))
        if method == 'dealloc':
            size = 0
        if method == 'create':
            args = [generator.choice(names) if generator.random() < .9 else 1] + [0] * (size - 1)
        elif method == 'acquire':
            value = generator.choice(floats) if generator.random() < .5 else generator.uniform(-5e6, 5e6)
            args = [struct.unpack('<f', struct.pack('<f', value))[0] if generator.random() < .92 else 'x'] + [0] * (size - 1)
        else:
            args = [0] * size
        args = args[:size]
        kernel = {'created': generator.choice((0, 0x1000, 0x2004)), 'lastError': generator.choice((0, 2, 5, 6, 183, 288)),
                  'wait': generator.choice((0, 0x80, 0x102, 0xffffffff, 0xc0)), 'released': generator.choice((0, 1, 1)),
                  'closed': generator.choice((0, 1, 1, 1)), 'formatted': generator.random() < .8}
        result.append({'method': method, 'args': args, 'handle': generator.choice((0, 0x30, 0x2004)), 'kernel': kernel})
    return result


def compare(executable, host, trace_script, directory):
    steps = scenarios()
    (directory / 'scenarios.json').write_text(json.dumps(steps))
    environment = {'IPC_SCENARIOS': str(directory / 'scenarios.json'), 'IPC_TRACES': str(directory / 'traces.json')}
    subprocess.run([str(host), str(trace_script)], env=environment, check=True)
    production = json.loads((directory / 'traces.json').read_text())
    original = OriginalPyMutex(executable)
    counts = {}
    for index, (step, actual) in enumerate(zip(steps, production)):
        expected = original.run(step, step['handle'])
        where = (index, step)
        kernel = [call for call in expected['calls'] if call[0] in KERNEL]
        produced = actual['calls']
        if step['method'] == 'dealloc':
            # 006ae740 formats a pending SystemError after a failed CloseHandle;
            # production closes quietly (the GetLastError read is not used).
            assert [c for c in produced if c[0] == 'CloseHandle'] == [c for c in kernel if c[0] == 'CloseHandle'], where
            assert actual['error'] is None, where
            continue
        assert produced == kernel, (where, produced, kernel)
        assert actual['handle'] == expected['handle'], (where, actual, expected)
        typed = [error for error in expected['errors']]
        if expected['result'] == 'NULL':
            if typed:
                assert actual['error'] == typed[-1], (where, actual, expected)
            else:
                # The hooked Script converter failed; production uses the real one.
                assert actual['error'] is not None and actual['error'][0] in ('TypeError', 'ValueError'), where
            counts['raised'] = counts.get('raised', 0) + 1
        elif typed:
            # The original returns its value and leaves the error pending.
            assert typed[-1][0] == 'SystemError' and actual['error'] == typed[-1], (where, actual, expected)
            counts['pending-raised'] = counts.get('pending-raised', 0) + 1
        else:
            assert actual['error'] is None and actual['result'] == expected['result'], (where, actual, expected)
            counts[expected['result']] = counts.get(expected['result'], 0) + 1
        if step['method'] == 'acquire' and kernel:
            counts['waits'] = counts.get('waits', 0) + 1
    return counts
