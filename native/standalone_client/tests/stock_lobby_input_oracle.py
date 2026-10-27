"""Execute exact #1513 lobby input instructions in Unicorn and compare production.

wg_setRedefineKeysMode 00aaed30 (+ setData(bool&) 00690570), the key translator
00aaf350 that reads its byte, the lock getters 00aaec70/00aaecb0/00aaecf0 and
wg_getLangCode 006f2e50 (+ 006f4820, bw_wtoutf8 00687780, argCountError 0068e100)
run unchanged. The Python C API, allocation and Win32 imports are hooked.
"""
import json
import struct
import subprocess

import pefile
import unicorn
from unicorn.x86_const import (UC_X86_REG_EAX, UC_X86_REG_EIP, UC_X86_REG_ESP, UC_X86_REG_FS,
                               UC_X86_REG_GDTR, UC_X86_REG_MXCSR, UC_X86_REG_SS)

SET_MODE, TRANSLATE, LANG = 0xaaed30, 0xaaf350, 0x6f2e50
LOCKS = {'caps': 0xaaec70, 'num': 0xaaecb0, 'scroll': 0xaaecf0}
MODE_BYTE, NONE, TYPE_ERROR = 0x1d5947c, 0x1a5c798, 0x1a5db10
TUPLE_SIZE, TUPLE_ITEM, INT_AS_LONG, STRING_AS_STRING = 0xfb3610, 0xfb3390, 0xfe0b10, 0xfd64e0
ERR_SET_STRING, ERR_FORMAT, BOOL_FROM_LONG, STRING_FROM_SIZE = 0xfc4320, 0xfc3720, 0xfc35f0, 0xfd8720
OPERATOR_NEW, OPERATOR_DELETE, OPERATOR_DELETE_SIZED = 0x13d7fe9, 0x13d801e, 0x13d8019
ARRAY_NEW, ARRAY_DELETE = 0x13dbe40, 0x13d8030
STACK, HEAP, GDT, MARKS = 0x6000000, 0x6100000, 0x6300000, 0x6400000
ARGS, ITEMS, TYPES, TEXT, OUT, EVENT, TRUE, FALSE, SENTINEL = (
    MARKS, MARKS + 0x100, MARKS + 0x200, MARKS + 0x400, MARKS + 0x600, MARKS + 0x700,
    MARKS + 0x800, MARKS + 0x810, MARKS + 0x900)
INT_FLAG, STRING_FLAG = 0x800000, 0x8000000


class OriginalLobbyInput(object):
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
        self.hooks = {
            TUPLE_SIZE: lambda: self._return(len(self.args)),
            TUPLE_ITEM: lambda: self._return(ITEMS + 0x10 * self._arg(2)),
            INT_AS_LONG: lambda: self._return(self.args[(self._arg(1) - ITEMS) // 0x10][1]),
            STRING_AS_STRING: self._string,
            ERR_SET_STRING: lambda: self._error(self._cstr(self._arg(2))),
            ERR_FORMAT: self._format,
            BOOL_FROM_LONG: lambda: self._return(TRUE if self._arg(1) else FALSE),
            STRING_FROM_SIZE: self._result_string,
            OPERATOR_NEW: self._new, ARRAY_NEW: self._new,
            OPERATOR_DELETE: lambda: self._return(0), OPERATOR_DELETE_SIZED: lambda: self._return(0),
            ARRAY_DELETE: lambda: self._return(0),
            SENTINEL: lambda: u.emu_stop()}
        pe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_IMPORT']])
        for entry in pe.DIRECTORY_ENTRY_IMPORT:
            for item in entry.imports:
                if item.name:
                    target = struct.unpack('<I', u.mem_read(item.address, 4))[0]
                    self.hooks[target] = (lambda name: lambda: self._import(name))(item.name.decode())
        for address in self.hooks:
            u.hook_add(unicorn.UC_HOOK_CODE, self._hook, begin=address, end=address)
        for index, flags in enumerate((INT_FLAG, STRING_FLAG, 0)):
            u.mem_write(TYPES + 0x60 * index + 0x54, struct.pack('<I', flags))

    def _hook(self, u, address, size, data):
        self.hooks[address]()

    def _arg(self, index):
        return struct.unpack('<I', self.u.mem_read(self.u.reg_read(UC_X86_REG_ESP) + 4 * index, 4))[0]

    def _cstr(self, address):
        return bytes(self.u.mem_read(address, 0x200)).split(b'\0')[0].decode('latin1')

    def _wstr(self, address):
        data = bytes(self.u.mem_read(address, 0x200))
        units = [struct.unpack('<H', data[i:i + 2])[0] for i in range(0, len(data), 2)]
        return ''.join(chr(c) for c in units[:units.index(0)])

    def _return(self, value, pop=0):
        esp = self.u.reg_read(UC_X86_REG_ESP)
        if value is not None:
            self.u.reg_write(UC_X86_REG_EAX, value & 0xffffffff)
        self.u.reg_write(UC_X86_REG_EIP, self._arg(0))
        self.u.reg_write(UC_X86_REG_ESP, esp + 4 + pop)

    def _new(self):
        size, address = self._arg(1), self.heap
        self.heap += (max(size, 1) + 15) & ~15
        self._return(address)

    def _string(self):
        index = (self._arg(1) - ITEMS) // 0x10
        address = TEXT + 0x40 * index
        self.u.mem_write(address, self.args[index][1].encode('latin1') + b'\0')
        self._return(address)

    def _error(self, text):
        self.trace['error'] = text
        self._return(0)

    def _format(self):
        assert self._arg(1) == TYPE_ERROR
        text = self._cstr(self._arg(2))
        values = [self._arg(3 + n) for n in range(8)]
        parts, index = [], 0
        for piece in text.split('%')[1:]:
            parts.append(self._cstr(values[index]) if piece[0] == 's' else values[index])
            index += 1
        self._error(text % tuple(parts))

    def _result_string(self):
        self.trace['result'] = list(bytes(self.u.mem_read(self._arg(1), self._arg(2))))
        self._return(TEXT + 0x300)

    def _import(self, name):
        if name == 'GetKeyState':
            key = self._arg(1)
            self.trace['calls'].append('GetKeyState:%d' % key)
            return self._return(self.keys.get(key, 0) & 0xffff | (0xffff0000 if self.keys.get(key, 0) < 0 else 0), 4)
        if name == 'GetKeyboardLayout':
            self.trace['calls'].append('GetKeyboardLayout:%d' % self._arg(1))
            return self._return(self.layout, 4)
        if name == 'GetLocaleInfoW':
            locale, kind, buffer, size = (self._arg(n) for n in range(1, 5))
            self.trace['calls'].append('GetLocaleInfoW:%d:%d:%d' % (locale, kind, size))
            text = self.locales.get(locale)
            if text is None or kind != 3:
                return self._return(0, 16)
            if size:
                self.u.mem_write(buffer, text.encode('utf-16-le') + b'\0\0')
            return self._return(len(text) + 1, 16)
        if name == 'WideCharToMultiByte':
            page, flags, source, count, output, size = (self._arg(n) for n in range(1, 7))
            assert page == 65001 and flags == 0 and count == 0xffffffff
            data = self._wstr(source).encode('utf-8') + b'\0'
            if size:
                self.u.mem_write(output, data)
            return self._return(len(data), 32)
        if name == '_stricmp':
            first, second = self._cstr(self._arg(1)).lower(), self._cstr(self._arg(2)).lower()
            return self._return(0 if first == second else (1 if first > second else 0xffffffff))
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

    def _call(self, address, *values):
        u = self.u
        self.trace, self.heap = {'calls': []}, HEAP
        u.reg_write(UC_X86_REG_ESP, STACK + 0x80000)
        u.reg_write(UC_X86_REG_MXCSR, 0x1f80)
        u.mem_write(STACK + 0x80000, struct.pack('<I' + 'I' * len(values), SENTINEL, *values))
        u.emu_start(address, SENTINEL + 1, count=1000000)
        assert u.reg_read(UC_X86_REG_EIP) == SENTINEL, hex(u.reg_read(UC_X86_REG_EIP))
        return u.reg_read(UC_X86_REG_EAX)

    def _python(self, address, args):
        self.args = args
        for index, (kind, value) in enumerate(args):
            self.u.mem_write(ITEMS + 0x10 * index, struct.pack('<II', 1, TYPES + 0x60 * kind))
        result = self._call(address, 0, ARGS)
        return {0: 'NULL', NONE: 'None', TRUE: True, FALSE: False}.get(result, 'string'), self.trace

    def set_mode(self, args):
        result, trace = self._python(SET_MODE, args)
        return [result, trace.get('error'), self.u.mem_read(MODE_BYTE, 1)[0]]

    def lock(self, name, keys):
        self.keys = keys
        result, trace = self._python(LOCKS[name], [])
        return [result, trace['calls']]

    def lang(self, layout, locales, args=()):
        self.layout, self.locales = layout, locales
        result, trace = self._python(LANG, list(args))
        return [trace.get('result') if result == 'string' else result, trace.get('error'), trace['calls']]

    def translate(self, key, state, modifiers, keys):
        self.keys = keys
        self.u.mem_write(EVENT, struct.pack('<IiI', key, state, modifiers) + b'\0' * 8)
        self.u.mem_write(OUT, b'\xcc' * 0x18)
        assert self._call(TRANSLATE, OUT, EVENT) == OUT
        kind, states, code, ascii, wchar, index = struct.unpack('<IB3xIB3xIB3x', bytes(self.u.mem_read(OUT, 0x18)))
        return [kind, states, code, ascii, wchar, index, self.trace['calls']]


# Python inputs: (kind, value) where kind 0 is an int subclass, 1 a str subclass, 2 anything else.
MODE_INPUTS = [[], [(0, 1), (0, 0)], [(0, 1)], [(0, 0)], [(0, 2)], [(0, 0xffffffff)], [(1, 'TRUE')],
               [(1, 'false')], [(1, 'True')], [(1, 'yes')], [(2, None)], [(1, 'FALSE')]]
KEY_STATES = [{}, {0x14: 1}, {0x90: 1, 0x14: -127, 0x91: -128}, {0x90: -127, 0x91: 1}, {0x14: 2}]
# A failed GetLocaleInfoW is excluded: the original then reads an unfilled buffer.
LAYOUTS = [0x04090409, 0x04190419, 0xf0080804, 0x040c0c0c, 0x0001, 0x0002, 0x0003]
LOCALES = {0x0409: 'ENU', 0x0419: 'RUS', 0x0804: 'CHS', 0x0c0c: 'FRC', 0x0001: 'A', 0x0002: 'éé',
           0x0003: 'ABCDEFGHIJKLMNOPQRSTUVWXYZ'}


def scenarios():
    result = []
    for args in MODE_INPUTS:
        result.append({'kind': 'mode', 'args': args})
        for key in range(0x140):
            result.append({'kind': 'key', 'key': key, 'state': 0, 'modifiers': 0, 'keys': {}})
    for mode in ([(0, 1)], [(0, 0)]):
        result.append({'kind': 'mode', 'args': mode})
        for key in (0x01, 0x1C, 0x1E, 0x30, 0x2A, 0x36, 0x1D, 0x9D, 0x38, 0xB8, 0x3A, 0x45, 0x46, 0x9C):
            for state in (0, 3, -1):
                for modifiers in range(8):
                    result.append({'kind': 'key', 'key': key, 'state': state, 'modifiers': modifiers, 'keys': {}})
            for keys in KEY_STATES:
                result.append({'kind': 'key', 'key': key, 'state': 0, 'modifiers': 0, 'keys': keys})
    for name in ('caps', 'num', 'scroll'):
        for keys in KEY_STATES:
            result.append({'kind': 'lock', 'name': name, 'keys': keys, 'args': []})
        result.append({'kind': 'lock', 'name': name, 'keys': {}, 'args': [(0, 1)]})
    for layout in LAYOUTS:
        result.append({'kind': 'lang', 'layout': layout, 'args': []})
    result.append({'kind': 'lang', 'layout': LAYOUTS[0], 'args': [(0, 1)]})
    return result


def original(executable, steps):
    machine = OriginalLobbyInput(executable)
    locales = {int(k): v for k, v in LOCALES.items()}
    out = []
    for step in steps:
        keys = {int(k): v for k, v in step.get('keys', {}).items()}
        if step['kind'] == 'mode':
            out.append(machine.set_mode([tuple(a) for a in step['args']]))
        elif step['kind'] == 'key':
            out.append(machine.translate(step['key'], step['state'], step['modifiers'], keys))
        elif step['kind'] == 'lock':
            machine.keys = keys
            result, trace = machine._python(LOCKS[step['name']], [tuple(a) for a in step['args']])
            out.append([result, trace.get('error'), trace['calls']])
        else:
            out.append(machine.lang(step['layout'], locales, [tuple(a) for a in step['args']]))
    return out


def compare(executable, host, trace, directory):
    steps = scenarios()
    expected = original(executable, steps)
    (directory / 'steps.json').write_text(json.dumps({'steps': steps, 'locales': LOCALES}))
    output = subprocess.run([str(host), str(trace), str(directory / 'steps.json')], check=True,
                            capture_output=True, text=True).stdout
    actual = json.loads(output)
    assert len(actual) == len(expected)
    for step, want, got in zip(steps, expected, actual):
        if want != got:
            raise AssertionError('original %r != production %r for %r' % (want, got, step))
    keys = [s for s in steps if s['kind'] == 'key']
    return '%d steps (%d translated key events)' % (len(steps), len(keys))
