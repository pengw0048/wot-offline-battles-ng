"""Execute the exact #1513 hangar presentation wrappers in Unicorn.

SetDrawInflux 00aeb660, wg_enableGUIBackground 00633330, wg_setGUIBackground
006333e0, wg_setPyModelTexture 006b3b30 and GUI.syncMousePosition 00aa8290 run
unchanged, including the WGBackgroundImage store 006f9160 and the BW::string
copies and destructors. PyTuple_Size/GetItem, the Script::setData converters,
argCountError and the native owners past each wrapper (Influx f99d60, texture
load 006f9090, model walk 006b45a0, SimpleGUI vtable +0x10) are recorded.
"""
from pathlib import Path
import re
import struct

import pefile
import unicorn
from unicorn.x86_const import (UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EIP, UC_X86_REG_ESP,
                               UC_X86_REG_FS, UC_X86_REG_GDTR, UC_X86_REG_SS)

FUNCTIONS = {'SetDrawInflux': 0xaeb660, 'wg_enableGUIBackground': 0x633330,
             'wg_setGUIBackground': 0x6333e0, 'wg_setPyModelTexture': 0x6b3b30,
             'syncMousePosition': 0xaa8290}
SET_DATA = {0x690570: 'bool', 0x68fe20: 'BW::string', 0x68fba0: 'float', 0x8f46f0: 'BW::PyModelPtr'}
TUPLE_SIZE, TUPLE_ITEM, ARG_COUNT_ERROR, GET_BOOL = 0xfb3610, 0xfb3390, 0x68e100, 0x68ee00
INFLUX, LOAD_BACKGROUND, MODEL_TEXTURE = 0xf99d60, 0x6f9090, 0x6b45a0
INFLUX_FLAG, BACKGROUND_SLOT, GUI_SLOT = 0x1a1bfd0, 0x1a08620, 0x1d1fee0
INCREMENT_IAT, DECREMENT_IAT = 0x149139c, 0x14913a0
NONE, TRUE = 0x1a5c798, 0x1a5c7a0
STACK, GDT, MARKS = 0x6000000, 0x6300000, 0x6400000
ARGS, ITEMS, BACKGROUND, GUI, VTABLE = MARKS + 0x100, MARKS + 0x200, MARKS + 0x400, MARKS + 0x500, MARKS + 0x600
MODEL, SUPER_MODEL, TOP_MODEL = MARKS + 0x700, MARKS + 0x780, MARKS + 0x800
DISPATCH, INCREMENT, DECREMENT, SENTINEL = MARKS + 0x900, MARKS + 0x904, MARKS + 0x908, MARKS + 0x90c
PRODUCTION = Path(__file__).resolve().parents[1] / 'bigworld/stock_hangar_presentation.cpp'
STRINGS = {0: 'background.dds', 1: 'diffuseMap', 2: 'shadow.dds'}


class OriginalWrappers(object):
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
        u.mem_write(INCREMENT_IAT, struct.pack('<II', INCREMENT, DECREMENT))
        self.hooks = {TUPLE_SIZE: lambda: self._return(self.argc),
                      TUPLE_ITEM: lambda: self._return(ITEMS + self._arg(2)),
                      ARG_COUNT_ERROR: self._arg_count_error, GET_BOOL: self._get_bool,
                      INFLUX: self._influx, LOAD_BACKGROUND: self._load_background,
                      MODEL_TEXTURE: self._model_texture, DISPATCH: self._dispatch,
                      INCREMENT: lambda: self._counter(1), DECREMENT: lambda: self._counter(-1),
                      SENTINEL: lambda: u.emu_stop()}
        for address, kind in SET_DATA.items():
            self.hooks[address] = (lambda kind: lambda: self._set_data(kind))(kind)
        for address in self.hooks:
            u.hook_add(unicorn.UC_HOOK_CODE, lambda u, address, size, data: self.hooks[address](),
                       begin=address, end=address)
        u.hook_add(unicorn.UC_HOOK_MEM_UNMAPPED, lambda *args: self._crash())

    def _word(self, address):
        return struct.unpack('<I', self.u.mem_read(address, 4))[0]

    def _arg(self, index):
        return self._word(self.u.reg_read(UC_X86_REG_ESP) + 4 * index)

    def _cstr(self, address):
        return bytes(self.u.mem_read(address, 0x100)).split(b'\0')[0].decode('latin1')

    def _return(self, value, pop=0):
        esp = self.u.reg_read(UC_X86_REG_ESP)
        self.u.reg_write(UC_X86_REG_EAX, value & 0xffffffff)
        self.u.reg_write(UC_X86_REG_EIP, self._arg(0))
        self.u.reg_write(UC_X86_REG_ESP, esp + 4 + pop)

    def _crash(self):
        self.calls.append(['crash'])
        self.u.emu_stop()
        return False

    def _set_data(self, kind):
        index, target = self._arg(1) - ITEMS, self._arg(2)
        self.calls.append(['setData', kind, index, self._cstr(self._arg(3))])
        if index == self.fail_at:
            return self._return(0xffffffff)
        if kind == 'bool':
            self.u.mem_write(target, bytes([self.values[index]]))
        elif kind == 'float':
            self.u.mem_write(target, struct.pack('<f', self.values[index]))
        elif kind == 'BW::string':
            text = STRINGS[index].encode('latin1')
            self.u.mem_write(target, text.ljust(16, b'\0') + struct.pack('<II', len(text), 15))
        else:
            self.u.mem_write(target, struct.pack('<I', MODEL if self.values[index] else 0))
        self._return(0)

    def _arg_count_error(self):
        total = self._arg(3)
        self.calls.append(['argCountError', self._cstr(self._arg(1)), self._arg(2), total] +
                          [self._cstr(self._arg(4 + n)) for n in range(total)])
        self._return(0)

    def _get_bool(self):
        self.calls.append(['getData', self._arg(1)])
        self._return(TRUE)

    def _influx(self):
        self.calls.append(['influx', self._arg(1) & 0xff, self._arg(2) & 0xff])
        self._return(0)

    def _load_background(self):
        string = self._arg(1)
        size, capacity = struct.unpack('<II', self.u.mem_read(string + 16, 8))
        data = string if capacity < 16 else self._word(string)
        self.calls.append(['loadBackground', self.u.reg_read(UC_X86_REG_ECX) == BACKGROUND,
                           bytes(self.u.mem_read(data, size)).decode('latin1')])
        self._return(0, 4)

    def _model_texture(self):
        refs = [struct.unpack('<II', self.u.mem_read(self._arg(n), 8)) for n in (2, 3)]
        self.calls.append(['modelTexture', self._arg(1) == TOP_MODEL] +
                          [bytes(self.u.mem_read(data, size)).decode('latin1') for data, size in refs])
        self._return(0)

    def _dispatch(self):
        event = struct.unpack('<iiiff', self.u.mem_read(self._arg(1), 20))
        self.calls.append(['SimpleGUI.handleMouseEvent', self.u.reg_read(UC_X86_REG_ECX) == GUI] + list(event))
        self._return(0, 4)

    def _counter(self, delta):
        address = self._arg(1)
        value = (self._word(address) + delta) & 0xffffffff
        self.u.mem_write(address, struct.pack('<I', value))
        self._return(value, 4)

    def run(self, function, argc, fail_at=None, values=(1, 1, 1)):
        u = self.u
        self.argc, self.fail_at, self.values, self.calls = argc, fail_at, values, []
        u.mem_write(INFLUX_FLAG, b'\x07')
        u.mem_write(BACKGROUND, struct.pack('<IIII', 0x14b062c, 0, 0, 0x07))
        u.mem_write(BACKGROUND_SLOT, struct.pack('<I', BACKGROUND))
        u.mem_write(GUI, struct.pack('<I', VTABLE))
        u.mem_write(VTABLE + 0x10, struct.pack('<I', DISPATCH))
        u.mem_write(GUI_SLOT, struct.pack('<I', GUI))
        # PyModel (refcount 2, +0x20 SuperModel); SuperModel +8 is the shared top model.
        u.mem_write(MODEL, struct.pack('<I', 2) + b'\0' * 0x1c + struct.pack('<I', SUPER_MODEL))
        u.mem_write(SUPER_MODEL, struct.pack('<III', 0, 0, TOP_MODEL))
        u.mem_write(TOP_MODEL, struct.pack('<II', 0, 2))
        u.reg_write(UC_X86_REG_ESP, STACK + 0x80000)
        u.mem_write(STACK + 0x80000, struct.pack('<III', SENTINEL, 0, ARGS))
        try:
            u.emu_start(FUNCTIONS[function], SENTINEL + 1, count=200000)
        except unicorn.UcError:
            assert self.calls[-1:] == [['crash']], self.calls
        if self.calls[-1:] == [['crash']]:
            outcome = 'crash'
        else:
            assert u.reg_read(UC_X86_REG_EIP) == SENTINEL, hex(u.reg_read(UC_X86_REG_EIP))
            outcome = {0: 'NULL', NONE: 'None', TRUE: 'True'}[u.reg_read(UC_X86_REG_EAX)]
        state = [bytes(u.mem_read(INFLUX_FLAG, 1))[0], bytes(u.mem_read(BACKGROUND + 12, 1))[0],
                 self._word(TOP_MODEL + 4)]
        return [function, argc, fail_at, list(values), outcome, state, self.calls]


def signatures():
    """The PY_AUTO_MODULE_FUNCTION argument types in the production binding."""
    text = re.sub(r'\s+', ' ', PRODUCTION.read_text())
    result = {}
    for match in re.finditer(r'PY_AUTO_MODULE_FUNCTION\( ', text):
        depth, end = 1, match.end()
        while depth:
            depth += {'(': 1, ')': -1}.get(text[end], 0)
            end += 1
        ret, name, args = text[match.end():end - 1].split(', ', 2)
        result[name] = (ret, tuple(re.findall(r'(?:OPT)?ARG\( ([^,]+),', args)))
    return result


def cases():
    result = []
    for function, (unused, kinds) in sorted(signatures().items()):
        if function not in FUNCTIONS:
            continue
        for argc in range(len(kinds) + 2):
            result.append((function, argc, None, (1, 1, 1)))
        result.extend((function, len(kinds), index, (1, 1, 1)) for index in range(len(kinds)))
        if kinds[0] in ('bool', 'BW::PyModelPtr'):
            result.append((function, len(kinds), None, (0, 1, 1)))
        if function == 'syncMousePosition':
            result.append((function, 2, None, (0.25, -0.5, 0)))
    return result


def expected():
    """The same cases through the production PY_AUTO expansion and native owners."""
    result, declared = [], signatures()
    assert set(FUNCTIONS) <= set(declared), declared
    for function, argc, fail_at, values in cases():
        ret, kinds = declared[function]
        state, calls = [7, 7, 2], []
        if argc != len(kinds):
            calls, outcome = [['argCountError', function, len(kinds), len(kinds)] + list(kinds)], 'NULL'
            result.append([function, argc, fail_at, list(values), outcome, state, calls])
            continue
        outcome = 'None'
        for index, kind in enumerate(kinds):
            calls.append(['setData', kind, index, '() argument %d' % (index + 1)])
            if index == fail_at:
                outcome = 'NULL'
                break
        if outcome == 'None':
            if function == 'SetDrawInflux':
                state[0] = values[0]
                calls.append(['influx', values[0], 1])
            elif function == 'wg_enableGUIBackground':
                state[1] = values[0]
            elif function == 'wg_setGUIBackground':
                calls.append(['loadBackground', True, STRINGS[0]])
            elif function == 'syncMousePosition':
                calls.append(['SimpleGUI.handleMouseEvent', True, 0, 0, 0, float(values[0]), float(values[1])])
            elif not values[0]:
                # Production raises TypeError here instead of dereferencing None.
                calls.append(['crash'])
                outcome = 'crash'
            else:
                calls += [['modelTexture', True, STRINGS[1], STRINGS[2]], ['getData', 1]]
                outcome = 'True'
        result.append([function, argc, fail_at, list(values), outcome, state, calls])
    return result


def run(executable):
    original = OriginalWrappers(executable)
    return [original.run(*case) for case in cases()]
