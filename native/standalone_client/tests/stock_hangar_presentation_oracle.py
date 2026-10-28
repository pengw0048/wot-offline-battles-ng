"""Execute the exact #1513 hangar presentation wrappers in Unicorn.

SetDrawInflux 00aeb660, wg_enableGUIBackground 00633330, wg_setGUIBackground
006333e0, wg_setPyModelTexture 006b3b30, GUI.syncMousePosition 00aa8290 and
wgSetEdgeDetectColors/wgAddEdgeDetectEntity/wgDelEdgeDetectEntity (00652ee0,
00652ab0, 00652cf0) run unchanged, including the WGBackgroundImage store
006f9160, the BW::string copies and destructors and the embodiment walk.
PyTuple_Size/GetItem, the Script::setData converters, argCountError,
PyErr_Format/PyErr_PrintEx and the native owners past each wrapper (Influx
f99d60, texture load 006f9090, model walk 006b45a0, SimpleGUI vtable +0x10,
EdgeDrawer colours af4d20, add af3c50 and remove af4480) are recorded.
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
             'syncMousePosition': 0xaa8290, 'wgSetEdgeDetectColors': 0x652ee0,
             'wgAddEdgeDetectEntity': 0x652ab0, 'wgDelEdgeDetectEntity': 0x652cf0,
             'getGammaCorrection': 0x6503c0, 'setGammaCorrection': 0x6522b0,
             'getColorGradingStrength': 0x650140, 'setColorGradingStrength': 0x652020,
             'wg_updateColorGrading': 0x654bc0}
GRADING_UPDATE = 0xa20670
SET_DATA = {0x690570: 'bool', 0x68fe20: 'BW::string', 0x68fba0: 'float', 0x8f46f0: 'BW::PyModelPtr',
            0x664990: 'PyEntityPtr', 0x68fad0: 'uint', 0x64e160: 'BW::vector<Vector4>'}
ERR_FORMAT, PRINT_EX, FREE, WARNING_SLOT = 0xfc3720, 0xfc7680, 0x13d8019, 0x1a5db0c
EDGE_SLOT, EDGE_COLORS, EDGE_ADD, EDGE_DEL = 0x1d20c70, 0xaf4d20, 0xaf3c50, 0xaf4480
# RenderSystem 01d380d4 +0x310 is the primary SwapChain (gamma +0x1b0, dirty
# +0x1b4); Renderer 01d20620 +0x10 is the pipeline, vtable +0x94 its ColorGrading.
GET_FLOAT, RENDER_SYSTEM_SLOT, RENDERER_SLOT = 0x68ed00, 0x1d380d4, 0x1d20620
TUPLE_SIZE, TUPLE_ITEM, ARG_COUNT_ERROR, GET_BOOL = 0xfb3610, 0xfb3390, 0x68e100, 0x68ee00
INFLUX, LOAD_BACKGROUND, MODEL_TEXTURE = 0xf99d60, 0x6f9090, 0x6b45a0
INFLUX_FLAG, BACKGROUND_SLOT, GUI_SLOT = 0x1a1bfd0, 0x1a08620, 0x1d1fee0
INCREMENT_IAT, DECREMENT_IAT = 0x149139c, 0x14913a0
NONE, TRUE, FLOAT = 0x1a5c798, 0x1a5c7a0, 0x1a5c7a8
STACK, GDT, MARKS = 0x6000000, 0x6300000, 0x6400000
ARGS, ITEMS, BACKGROUND, GUI, VTABLE = MARKS + 0x100, MARKS + 0x200, MARKS + 0x400, MARKS + 0x500, MARKS + 0x600
MODEL, SUPER_MODEL, TOP_MODEL = MARKS + 0x700, MARKS + 0x780, MARKS + 0x800
DISPATCH, INCREMENT, DECREMENT, SENTINEL = MARKS + 0x900, MARKS + 0x904, MARKS + 0x908, MARKS + 0x90c
DRAWER, PY_ENTITY, ENTITY, EMBODIMENTS = MARKS + 0xa00, MARKS + 0xa40, MARKS + 0xb00, MARKS + 0xc00
AUXILIARY, VECTOR = MARKS + 0xd00, MARKS + 0xe00
RENDER_SYSTEM, RENDERER, PIPELINE, SWAP_CHAIN = MARKS + 0xf00, MARKS + 0xf40, MARKS + 0xf80, MARKS + 0x1000
PIPELINE_VTABLE, GRADING, GRADING_LOOKUP = MARKS + 0x1200, MARKS + 0x1300, MARKS + 0x914
WARNINGS = {'zero': 'wgSetEdgeDetectColors - colors size is zero. ' + '\t' * 9 + 'Using defaults colors.\n',
            'great': 'wgSetEdgeDetectColors - colors size is great then zero. ' + '\t' * 9 +
            'Using the first three items.\n',
            'mode': 'Incorrect draw mode (0, 1, 2 are only valid).\n',
            'colour': 'Incorrect color index (0-3 are only valid).\n'}
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
                      ERR_FORMAT: self._format, PRINT_EX: self._print_ex, FREE: lambda: self._return(0),
                      EDGE_COLORS: self._edge_colors, EDGE_ADD: self._edge_add, EDGE_DEL: self._edge_del,
                      GET_FLOAT: self._get_float, GRADING_LOOKUP: self._grading,
                      GRADING_UPDATE: self._grading_update,
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
        elif kind == 'uint':
            self.u.mem_write(target, struct.pack('<I', self.values[index]))
        elif kind == 'PyEntityPtr':
            self.u.mem_write(target, struct.pack('<I', PY_ENTITY if self.values[index] else 0))
        elif kind == 'BW::vector<Vector4>':
            count = self.values[index]
            self.u.mem_write(VECTOR, struct.pack('<4f', 0.5, 0.25, 0.125, 1.0) * count)
            self.u.mem_write(target, struct.pack('<III', VECTOR, VECTOR + 16 * count, VECTOR + 16 * count))
        else:
            self.u.mem_write(target, struct.pack('<I', MODEL if self.values[index] else 0))
        self._return(0)

    def _format(self):
        kind = 'Warning' if self._arg(1) == self._word(WARNING_SLOT) else hex(self._arg(1))
        message = self._cstr(self._arg(2))
        self.calls.append(['PyErr_Format', kind] + [name for name, text in WARNINGS.items() if text == message])
        self._return(0)

    def _get_float(self):
        self.calls.append(['getData', 'float', struct.unpack('<f', struct.pack('<I', self._arg(1)))[0]])
        self._return(FLOAT)

    def _grading(self):
        self.calls.append(['pipeline.colorGrading', self.u.reg_read(UC_X86_REG_ECX) == PIPELINE])
        self._return(GRADING if self.grading else 0)

    def _grading_update(self):
        # Observe entry without replacing the actual native owner method.
        # Its random-LUT switch is off here; no host renderer is fabricated.
        self.calls.append(['colorGrading.update', self.u.reg_read(UC_X86_REG_ECX) == GRADING])

    def _print_ex(self):
        self.calls.append(['PyErr_PrintEx', self._arg(1)])
        self._return(0)

    def _edge_colors(self):
        vector = self._arg(1)
        first, last = struct.unpack('<II', self.u.mem_read(vector, 8))
        self.calls.append(['edge.colors', self.u.reg_read(UC_X86_REG_ECX) == DRAWER, (last - first) // 16])
        self._return(0, 4)

    def _edge_add(self):
        key = self._word(self._arg(1))
        self.calls.append(['edge.add', self.u.reg_read(UC_X86_REG_ECX) == DRAWER, key, self._arg(2),
                           self._arg(3), self._arg(4) & 0xff])
        self._return(0, 16)

    def _edge_del(self):
        self.calls.append(['edge.del', self.u.reg_read(UC_X86_REG_ECX) == DRAWER, self._word(self._arg(1))])
        self._return(0, 4)

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

    def run(self, function, argc, fail_at=None, values=(1, 1, 1), drawer=True, grading=False):
        u = self.u
        self.argc, self.fail_at, self.values, self.calls = argc, fail_at, values, []
        self.grading = grading
        u.mem_write(RENDER_SYSTEM_SLOT, struct.pack('<I', RENDER_SYSTEM))
        u.mem_write(RENDER_SYSTEM + 0x310, struct.pack('<I', SWAP_CHAIN))
        u.mem_write(SWAP_CHAIN + 0x1b0, struct.pack('<fB', 1.25, 0))
        u.mem_write(RENDERER_SLOT, struct.pack('<I', RENDERER))
        u.mem_write(RENDERER + 0x10, struct.pack('<I', PIPELINE))
        u.mem_write(PIPELINE, struct.pack('<I', PIPELINE_VTABLE))
        u.mem_write(PIPELINE_VTABLE + 0x94, struct.pack('<I', GRADING_LOOKUP))
        u.mem_write(GRADING + 0x1c, struct.pack('<f', 0.75))
        u.mem_write(GRADING + 0x21, b'\x00')
        # PyEntity +8 -> Entity: primary +0xe8, auxiliary vector +0xec/+0xf0; each
        # embodiment's key is the dword at +0x10 (00b4e280).
        u.mem_write(EDGE_SLOT, struct.pack('<I', DRAWER if drawer else 0))
        u.mem_write(PY_ENTITY, struct.pack('<III', 5, 0, ENTITY))
        u.mem_write(ENTITY + 0xe8, struct.pack('<III', EMBODIMENTS, AUXILIARY, AUXILIARY + 8))
        u.mem_write(AUXILIARY, struct.pack('<II', EMBODIMENTS + 0x20, EMBODIMENTS + 0x40))
        for index in range(3):
            u.mem_write(EMBODIMENTS + 0x20 * index + 0x10, struct.pack('<I', 101 + index))
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
            outcome = {0: 'NULL', NONE: 'None', TRUE: 'True', FLOAT: 'float'}[u.reg_read(UC_X86_REG_EAX)]
        state = [bytes(u.mem_read(INFLUX_FLAG, 1))[0], bytes(u.mem_read(BACKGROUND + 12, 1))[0],
                 self._word(TOP_MODEL + 4), struct.unpack('<f', u.mem_read(SWAP_CHAIN + 0x1b0, 4))[0],
                 bytes(u.mem_read(SWAP_CHAIN + 0x1b4, 1))[0], struct.unpack('<f', u.mem_read(GRADING + 0x1c, 4))[0]]
        return [function, argc, fail_at, list(values), drawer, outcome, state, self.calls]


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
            result.append((function, argc, None, (1, 1, 1, 1), True))
        result.extend((function, len(kinds), index, (1, 1, 1, 1), True) for index in range(len(kinds)))
        if kinds and kinds[0] in ('bool', 'BW::PyModelPtr'):
            result.append((function, len(kinds), None, (0, 1, 1, 1), True))
        if function == 'syncMousePosition':
            result.append((function, 2, None, (0.25, -0.5, 0, 0), True))
        if function in ('setGammaCorrection', 'setColorGradingStrength'):
            result.extend((function, 1, None, (value, 0, 0, 0), True) for value in (0.5, 1.37, 2.0, 0.25))
    for drawer in (True, False):
        for count in (0, 1, 4, 5):
            result.append(('wgSetEdgeDetectColors', 1, None, (count,), drawer))
        for values in ((1, 0, 0, 0), (1, 3, 2, 1), (1, 4, 0, 0), (1, 1, 3, 0), (1, 9, 9, 1), (0, 1, 0, 0)):
            for argc in (2, 3, 4):
                result.append(('wgAddEdgeDetectEntity', argc, None, values, drawer))
        for values in ((1,), (0,)):
            result.append(('wgDelEdgeDetectEntity', 1, None, values, drawer))
    return result


def expected():
    """The same cases through the production PY_AUTO expansion and native owners."""
    result, declared = [], signatures()
    assert set(FUNCTIONS) <= set(declared), declared
    for function, argc, fail_at, values, drawer in cases():
        ret, kinds = declared[function]
        state, calls = [7, 7, 2, 1.25, 0, 0.75], []
        required = len(kinds) - (2 if function == 'wgAddEdgeDetectEntity' else 0)
        if not required <= argc <= len(kinds):
            calls, outcome = [['argCountError', function, required, len(kinds)] + list(kinds)], 'NULL'
            result.append([function, argc, fail_at, list(values), drawer, outcome, state, calls])
            continue
        outcome = 'None'
        for index, kind in enumerate(kinds[:argc]):
            calls.append(['setData', kind, index, '() argument %d%s' % (index + 1,
                                                                         ' optionally' if index >= required else '')])
            if index == fail_at:
                outcome = 'NULL'
                break
        if outcome == 'NULL':
            pass
        elif function == 'getGammaCorrection':
            calls.append(['getData', 'float', 1.25])
            outcome = 'float'
        elif function == 'setGammaCorrection':
            # The swap-chain setter stores the converted float and marks it dirty.
            state[3], state[4] = struct.unpack('<f', struct.pack('<f', values[0]))[0], 1
        elif function == 'getColorGradingStrength':
            # This client has no ColorGrading owner: the original absent branch.
            calls += [['pipeline.colorGrading', True], ['getData', 'float', 0.0]]
            outcome = 'float'
        elif function == 'wg_updateColorGrading':
            calls.append(['pipeline.colorGrading', True])
        elif function == 'setColorGradingStrength':
            calls.append(['pipeline.colorGrading', True])
        elif function == 'wgSetEdgeDetectColors':
            if values[0] == 0:
                calls.append(['PyErr_Format', 'Warning', 'zero'])
                outcome = 'NULL'
            else:
                if values[0] > 4:
                    calls += [['PyErr_Format', 'Warning', 'great'], ['PyErr_PrintEx', 0]]
                if drawer:
                    calls.append(['edge.colors', True, values[0]])
        elif function in ('wgAddEdgeDetectEntity', 'wgDelEdgeDetectEntity'):
            colour, mode, flag = (list(values[1:argc]) + [0, 0, 0])[:3] if function == 'wgAddEdgeDetectEntity' \
                else (0, 0, 0)
            if drawer and function == 'wgAddEdgeDetectEntity':
                if mode > 2 or colour > 3:
                    calls += [['PyErr_Format', 'Warning', 'mode' if mode > 2 else 'colour'], ['PyErr_PrintEx', 0]]
            if drawer and not values[0]:
                # Production raises TypeError here instead of dereferencing None.
                calls.append(['crash'])
                outcome = 'crash'
            elif drawer:
                for key in (101, 102, 103):
                    calls.append(['edge.add', True, key, colour, mode, flag] if function == 'wgAddEdgeDetectEntity'
                                 else ['edge.del', True, key])
        elif outcome == 'None':
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
        result.append([function, argc, fail_at, list(values), drawer, outcome, state, calls])
    return result


def run(executable):
    original = OriginalWrappers(executable)
    return [original.run(*case) for case in cases()]


def grading_owner(executable):
    """What an existing ColorGrading owner would receive: the live +0x1c value."""
    original = OriginalWrappers(executable)
    return [original.run('getColorGradingStrength', 0, grading=True)[-3:],
            original.run('setColorGradingStrength', 1, values=(0.37, 0, 0, 0), grading=True)[-3:]]


def grading_update_owner(executable):
    """Retail's existing-owner dispatch, not a restored client rendering owner."""
    original = OriginalWrappers(executable)
    return original.run('wg_updateColorGrading', 0, grading=True)
