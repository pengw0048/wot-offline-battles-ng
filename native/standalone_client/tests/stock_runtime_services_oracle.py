"""Execute the exact #1513 runtime-service wrappers and App frame-pacing methods in Unicorn.

The eleven BigWorld wrappers run unchanged, including timeExact's App clock
0061a860 over a timeGetTime import hook, wg_getClientStatistics' disabled
collector branch, and wg_setSpecialFPSMode/wg_disableSpecialFPSMode into App
0061e950/0061e880 with their CRT conversions and the saved-rate global
01d1ff80. PyTuple_Size/GetItem, Script::setData, argCountError, the
float/uint getData helpers, PyErr_SetString, Moo::rc(), the URL cache purge
and the retail telemetry owner are hooked and recorded.
"""
from pathlib import Path
import re
import struct

import pefile
import unicorn
from unicorn.x86_const import (UC_X86_REG_CR0, UC_X86_REG_CR4, UC_X86_REG_EAX, UC_X86_REG_ECX,
                               UC_X86_REG_EIP, UC_X86_REG_ESP, UC_X86_REG_FPCW, UC_X86_REG_FS,
                               UC_X86_REG_GDTR, UC_X86_REG_MXCSR, UC_X86_REG_SS)

WRAPPERS = {
    'PFbeginFrame': 0x618500, 'PFendFrame': 0x6185c0,
    'timeExact': 0x6529f0, 'wg_getFrameTimestamp': 0x653850, 'addUPLMessage': 0x6d8280,
    'enableBattleStatisticCollector': 0x6d8340, 'enableLoadingTimer': 0x6d8450,
    'wg_getClientStatistics': 0x6d8750, 'wg_setSpecialFPSMode': 0x6548d0,
    'wg_disableSpecialFPSMode': 0x6536a0, 'wg_setReducedFpsMode': 0x619890,
    'wg_clearTextureReuseList': 0x6196a0, 'purgeUrlRequestCache': 0x67d490,
}
SET_DATA = {0x690570: 'bool', 0x68fe20: 'BW::string', 0x68fba0: 'float'}
TUPLE_SIZE, TUPLE_ITEM, ARG_COUNT_ERROR = 0xfb3610, 0xfb3390, 0x68e100
GET_FLOAT, GET_UINT, SET_STRING = 0x68ed00, 0x68ece0, 0xfc4320
RENDER_CONTEXT, CLEAR_REUSE_LIST, PURGE = 0x9288d0, 0x923550, 0x84c4f0
STATISTICS_OWNER, TELEMETRY_CALLS = 0x6da060, (0x6da280, 0x615190)
APP_POINTER, TIME_GET_TIME_IAT, SAVED_RATE = 0x1d1ff60, 0x1491b48, 0x1d1ff80
URL_MANAGER, ENVIRONMENT_ERROR, COLLECTOR_ENABLED = 0x1d3482c, 0x1a5d234, 0x1a0d761
NONE = 0x1a5c798
STACK, GDT, MARKS = 0x6000000, 0x6300000, 0x6400000
APP, RC, STATS, ARGS, ITEMS = MARKS, MARKS + 0x1000, MARKS + 0x2000, MARKS + 0x5000, MARKS + 0x5100
TIME_GET_TIME, SENTINEL, FLOAT_RESULT, UINT_RESULT = MARKS + 0x6000, MARKS + 0x6010, MARKS + 0x6020, MARKS + 0x6030
PRODUCTION = Path(__file__).resolve().parents[1] / 'bigworld/stock_runtime_services.cpp'
CONVERTERS = {'bool': 'bool', 'BW::string': 'BW::string', 'float': 'float'}
PRODUCTION_RESULTS = {'timeExact': 'float', 'wg_getFrameTimestamp': 'uint'}


def float32(value):
    return struct.unpack('<f', struct.pack('<f', value))[0]


class OriginalRuntimeServices(object):
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
        u.reg_write(UC_X86_REG_CR0, (u.reg_read(UC_X86_REG_CR0) & ~0x4) | 0x2)
        u.reg_write(UC_X86_REG_CR4, u.reg_read(UC_X86_REG_CR4) | 0x600)
        # The Win32 main-thread defaults: x87 53-bit precision and masked exceptions.
        u.reg_write(UC_X86_REG_FPCW, 0x27f)
        u.reg_write(UC_X86_REG_MXCSR, 0x1f80)
        u.mem_write(APP_POINTER, struct.pack('<I', APP))
        u.mem_write(TIME_GET_TIME_IAT, struct.pack('<I', TIME_GET_TIME))
        self.hooks = {
            TUPLE_SIZE: lambda: self._return(self.argc),
            TUPLE_ITEM: lambda: self._return(ITEMS + self._arg(2)),
            ARG_COUNT_ERROR: self._arg_count_error,
            TIME_GET_TIME: lambda: self._return(self.ticks),
            GET_FLOAT: lambda: self._result(FLOAT_RESULT, ('float', self._float(1))),
            GET_UINT: lambda: self._result(UINT_RESULT, ('uint', self._arg(1))),
            SET_STRING: self._set_string,
            RENDER_CONTEXT: lambda: self._return(RC),
            CLEAR_REUSE_LIST: lambda: self._backend('clearTextureReuseList'),
            PURGE: lambda: self._backend('purgeUrlRequestCache'),
            STATISTICS_OWNER: lambda: self._backend('statisticsOwner', STATS),
            SENTINEL: lambda: u.emu_stop()}
        for address in TELEMETRY_CALLS:
            self.hooks[address] = (lambda address: lambda: self._backend('telemetry %06x' % address))(address)
        for address, kind in SET_DATA.items():
            self.hooks[address] = (lambda kind: lambda: self._set_data(kind))(kind)
        for address in self.hooks:
            u.hook_add(unicorn.UC_HOOK_CODE, lambda u, address, size, data: self.hooks[address](),
                       begin=address, end=address)

    def _arg(self, index):
        return struct.unpack('<I', self.u.mem_read(self.u.reg_read(UC_X86_REG_ESP) + 4 * index, 4))[0]

    def _float(self, index):
        return struct.unpack('<f', self.u.mem_read(self.u.reg_read(UC_X86_REG_ESP) + 4 * index, 4))[0]

    def _cstr(self, address):
        return bytes(self.u.mem_read(address, 0x100)).split(b'\0')[0].decode('latin1')

    def _return(self, value):
        esp = self.u.reg_read(UC_X86_REG_ESP)
        self.u.reg_write(UC_X86_REG_EAX, value & 0xffffffff)
        self.u.reg_write(UC_X86_REG_EIP, self._arg(0))
        self.u.reg_write(UC_X86_REG_ESP, esp + 4)

    def _result(self, marker, value):
        self.results[marker] = value
        self._return(marker)

    def _backend(self, name, value=0):
        self.backends.append(name)
        self._return(value)

    def _set_string(self):
        self.calls.append(['PyErr_SetString', self._arg(1) == self.environment_error, self._cstr(self._arg(2))])
        self._return(0)

    def _set_data(self, kind):
        index = self._arg(1) - ITEMS
        self.calls.append(['setData', kind, index, self._cstr(self._arg(3))])
        if index == self.fail_at:
            return self._return(0xffffffff)
        if kind == 'bool':
            self.u.mem_write(self._arg(2), bytes([1 if self.values[index] else 0]))
        elif kind == 'float':
            self.u.mem_write(self._arg(2), struct.pack('<f', self.values[index]))
        self._return(0)

    def _arg_count_error(self):
        total = self._arg(3)
        self.calls.append(['argCountError', self._cstr(self._arg(1)), self._arg(2), total] +
                          [self._cstr(self._arg(4 + n)) for n in range(total)])
        self._return(0)

    def configure(self, normal, special, start=0):
        self.u.mem_write(APP + 0xa, b'\0')  # timeGetTime branch: 1000.0 ticks per second
        self.u.mem_write(APP + 0x568, struct.pack('<Q', start))
        self.u.mem_write(APP + 0x590, struct.pack('<QQ', normal, special))
        self.u.mem_write(APP + 0x614, b'\0')
        self.u.mem_write(SAVED_RATE, struct.pack('<f', 0.0))

    def app(self):
        normal, = struct.unpack('<Q', self.u.mem_read(APP + 0x590, 8))
        return [normal, bytes(self.u.mem_read(APP + 0x614, 1))[0]]

    def run(self, name, values=(), argc=None, fail_at=None, ticks=0, frame=0, url=True, collector=False):
        u = self.u
        self.argc = len(values) if argc is None else argc
        self.values, self.fail_at, self.ticks = list(values) + [0] * 4, fail_at, ticks
        self.calls, self.backends, self.results = [], [], {}
        self.environment_error = struct.unpack('<I', u.mem_read(ENVIRONMENT_ERROR, 4))[0]
        u.mem_write(RC + 0x598, struct.pack('<I', frame))
        u.mem_write(URL_MANAGER, struct.pack('<I', MARKS + 0x7000 if url else 0))
        u.mem_write(COLLECTOR_ENABLED, bytes([1 if collector else 0]))
        u.mem_write(STATS, b'\0' * 0x2700)
        u.reg_write(UC_X86_REG_ECX, 0)
        u.reg_write(UC_X86_REG_ESP, STACK + 0x80000)
        u.mem_write(STACK + 0x80000, struct.pack('<III', SENTINEL, 0, ARGS))
        u.emu_start(WRAPPERS[name], SENTINEL + 1, count=100000)
        assert u.reg_read(UC_X86_REG_EIP) == SENTINEL, hex(u.reg_read(UC_X86_REG_EIP))
        eax = u.reg_read(UC_X86_REG_EAX)
        result = self.results.get(eax) or {0: 'NULL', NONE: 'None'}[eax]
        return [name, self.argc, fail_at, result, self.calls]


def declarations():
    """(return kind, argument kinds and defaults) from the production PY_AUTO declarations."""
    text = re.sub(r'\s+', ' ', PRODUCTION.read_text())
    result = {}
    for ret, name, args in re.findall(r'PY_AUTO_MODULE_FUNCTION\( (RET\w+), (\w+), (.*?), BigWorld \)', text):
        result[name] = (ret, [(bool(optional), kind)
                              for optional, kind in re.findall(r'(OPT)?ARG\( ([^,]+),', args)])
    for name in re.findall(r'PY_MODULE_FUNCTION_WITH_KEYWORDS\( (\w+), BigWorld \)', text):
        result[name] = ('KEYWORDS', [])
    return result


def wrapper_cases():
    cases = []
    for name in sorted(WRAPPERS):
        for argc in range(3):
            cases.append((name, argc, None))
        ret, args = declarations()[name]
        for index in range(len(args)):
            cases.append((name, len(args), index))
    return cases


VALUES = {'PFbeginFrame': ['battle.frame'], 'addUPLMessage': ['HANGAR LOADING STATE: CONNECTED'], 'enableBattleStatisticCollector': [True],
          'enableLoadingTimer': [True], 'wg_setReducedFpsMode': [False], 'wg_setSpecialFPSMode': [30.0]}


def run_wrappers(executable):
    original = OriginalRuntimeServices(executable)
    report = []
    for name, argc, fail_at in wrapper_cases():
        original.configure(8, 16, start=250)
        values = (VALUES.get(name, []) + ['extra', 'extra'])[:argc]
        report.append(original.run(name, values, argc, fail_at, ticks=3583, frame=0x80000007))
    original.configure(8, 16)
    report.append(original.run('purgeUrlRequestCache', url=False))
    return report


def expected_wrappers():
    """The same cases through the production declarations and backends."""
    result, declared = [], declarations()
    assert sorted(declared) == sorted(WRAPPERS), declared
    for name, argc, fail_at in wrapper_cases():
        ret, args = declared[name]
        required = sum(1 for optional, unused in args if not optional)
        if ret == 'KEYWORDS':
            result.append([name, argc, fail_at, 'None', []])
            continue
        if not required <= argc <= len(args):
            calls = [['argCountError', name, required, len(args)] + [kind for unused, kind in args]]
            result.append([name, argc, fail_at, 'NULL', calls])
            continue
        calls, outcome = [], {'float': ('float', float32((3583 - 250) / 1000.0)),
                              'uint': ('uint', 0x80000007)}.get(PRODUCTION_RESULTS.get(name), 'None')
        for index in range(argc):
            optional, kind = args[index]
            calls.append(['setData', CONVERTERS[kind], index,
                          '() argument %d%s' % (index + 1, ' optionally' if optional else '')])
            if index == fail_at:
                outcome = 'NULL'
                break
        result.append([name, argc, fail_at, outcome, calls])
    result.append(['purgeUrlRequestCache', 0, None, 'NULL',
                   [['PyErr_SetString', True, 'PyURLRequest has not been initialised']]])
    return result


# Hangar create/destroy and arena load transitions; values in timeGetTime milliseconds.
PACING = (
    ('retail rates', 8, 16, [('disable',), ('set',), ('set',), ('disable',), ('disable',),
                             ('set', 30.0), ('set', 0.0), ('set', -1.0), ('set', float('nan')),
                             ('set', float('inf')), ('set', 7.0), ('disable',),
                             ('reduced', True), ('reduced', True), ('reduced', False)]),
    ('no special rate', 8, 0, [('set',), ('disable',)]),
    ('unlimited normal', 0, 16, [('set',), ('disable',)]),
    ('unlimited normal nested', 0, 16, [('set',), ('set',), ('disable',)]),
)


def run_frame_pacing(executable):
    original = OriginalRuntimeServices(executable)
    report = []
    for label, normal, special, operations in PACING:
        original.configure(normal, special)
        for operation in operations:
            name = {'set': 'wg_setSpecialFPSMode', 'disable': 'wg_disableSpecialFPSMode',
                    'reduced': 'wg_setReducedFpsMode'}[operation[0]]
            assert original.run(name, operation[1:])[3] == 'None'
            report.append([label, [operation[0]] + [repr(value) for value in operation[1:]], original.app()])
    return report

