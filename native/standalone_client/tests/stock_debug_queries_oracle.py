"""Execute the exact #1513 statPing/getFPS wrappers and their owners in Unicorn.

statPing 006d8700 reads the statistics owner's +0x2618; its writer is the
per-frame tick 006da4c0, run here over a ConnectionControl, BWServerConnection,
ServerConnection and channel built in emulator memory (isConnected and
roundTripTimeInSeconds are two-instruction x86 stubs). getFPS 006188d0 runs
00631fa0 over DebugApp::instance (01a083b0); DebugApp::tick 006323a0 samples
frame times. PyTuple_Size, argCountError and the float/Vector4 getData helpers
are hooked through the runtime-services oracle.
"""
import struct

from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EIP, UC_X86_REG_ESP

import stock_runtime_services_oracle as base

STAT_PING, GET_FPS = 0x6d8700, 0x6188d0
STATS_TICK, DEBUG_TICK, GET_VECTOR4 = 0x6da4c0, 0x6323a0, 0x68ec60
DEBUG_APP, CONNECTION_CONTROL = 0x1a083b0, 0x1d20624
MEMORY_CRITICAL, TITLE_FPS = 0x1a085e4, 0x1d20c74
REGION = base.MARKS + 0x8000
CONTROL, BW_CONNECTION, SERVER, CHANNEL, VTABLE = (REGION + offset for offset in
                                                    (0x0, 0x400, 0x800, 0x2000, 0x2400))
CONNECTED, DISCONNECTED, ROUND_TRIP, LATENCY, EMPTY_MAP, DEBUG = (
    REGION + offset for offset in (0x2500, 0x2508, 0x2510, 0x2520, 0x2530, 0x3000))
VECTOR4_RESULT = base.MARKS + 0x6040


class OriginalDebugQueries(base.OriginalRuntimeServices):
    def __init__(self, executable):
        super(OriginalDebugQueries, self).__init__(executable)
        u = self.u
        self.hooks[GET_VECTOR4] = lambda: self._result(VECTOR4_RESULT, ('Vector4', list(
            struct.unpack('<4f', u.mem_read(self._arg(1), 16)))))
        u.hook_add(base.unicorn.UC_HOOK_CODE, lambda u, address, size, data: self.hooks[GET_VECTOR4](),
                   begin=GET_VECTOR4, end=GET_VECTOR4)
        # Unicorn keeps translated blocks, so the stubs are fixed and the vtable selects one.
        u.mem_write(CONNECTED, b'\xb0\x01\xc3')  # mov al, 1; ret
        u.mem_write(DISCONNECTED, b'\xb0\x00\xc3')
        u.mem_write(ROUND_TRIP, b'\xdd\x05' + struct.pack('<I', LATENCY) + b'\xc3')  # fld qword [LATENCY]; ret
        u.mem_write(VTABLE + 0x2c, struct.pack('<I', ROUND_TRIP))
        u.mem_write(CHANNEL, struct.pack('<I', VTABLE))
        u.mem_write(SERVER + 0x11c, struct.pack('<I', CHANNEL))
        u.mem_write(SERVER + 0x128c, struct.pack('<I', 0))
        u.mem_write(BW_CONNECTION + 0x58, struct.pack('<I', SERVER))
        u.mem_write(CONTROL + 0x14c, struct.pack('<I', 1))
        u.mem_write(CONNECTION_CONTROL, struct.pack('<I', CONTROL))
        u.mem_write(EMPTY_MAP, struct.pack('<I', EMPTY_MAP))
        u.mem_write(base.STATS, b'\0' * 0x2700)
        u.mem_write(base.STATS + 0x7c, struct.pack('<I', EMPTY_MAP))
        u.mem_write(MEMORY_CRITICAL, b'\0')
        u.mem_write(TITLE_FPS, b'\0')
        self.reset_debug_app()

    def call(self, address, this, words):
        u = self.u
        self.calls, self.backends, self.results = [], [], {}
        u.reg_write(base.UC_X86_REG_FPCW, 0x27f)
        u.reg_write(UC_X86_REG_ECX, this)
        u.reg_write(UC_X86_REG_ESP, base.STACK + 0x80000)
        u.mem_write(base.STACK + 0x80000, struct.pack('<I', base.SENTINEL) + b''.join(words))
        u.emu_start(address, base.SENTINEL + 1, count=200000)
        assert u.reg_read(UC_X86_REG_EIP) == base.SENTINEL, hex(u.reg_read(UC_X86_REG_EIP))
        return u.reg_read(UC_X86_REG_EAX)

    def wrapper(self, address, name, argc):
        self.argc = argc
        eax = self.call(address, 0, [struct.pack('<II', 0, base.ARGS)])
        return [name, argc, self.results.get(eax) or {0: 'NULL', base.NONE: 'None'}[eax], self.calls]

    def reset_debug_app(self):
        self.u.mem_write(DEBUG, b'\0' * 0x300)
        self.u.mem_write(DEBUG + 0x21c, struct.pack('<f', 1.0))

    def debug_tick(self, dt):
        self.call(DEBUG_TICK, DEBUG, [struct.pack('<ff', 0.0, dt)])
        fps, average, maximum, minimum = struct.unpack('<4f', self.u.mem_read(DEBUG + 0x78, 16))
        return [fps, average, minimum, maximum]

    def ping_tick(self, server, online, latency, loading):
        self.u.mem_write(CONTROL + 0x8c, struct.pack('<I', BW_CONNECTION if server else 0))
        self.u.mem_write(VTABLE + 0x14, struct.pack('<I', CONNECTED if online else DISCONNECTED))
        self.u.mem_write(LATENCY, struct.pack('<d', latency))
        self.u.mem_write(base.STATS + 0x2642, bytes([1 if loading else 0]))
        self.call(STATS_TICK, base.STATS, [struct.pack('<f', 1 / 60.0)])
        return struct.unpack('<f', self.u.mem_read(base.STATS + 0x2618, 4))[0]


FRAMES = [1 / 60.0, 1 / 30.0, 0.02, 0.05, 1 / 144.0, 0.0, 0.1, 0.25, 0.75, 1 / 120.0] * 7 + [0.1] * 60
# (server connection, online, latency seconds, loading timer); latencies are exact floats.
PING = [(0, 0, 0.0, 0), (1, 1, 0.25, 0), (1, 1, 0.0625, 0), (1, 1, 0.03125, 0),
        (1, 1, 0.0517578125, 0), (1, 1, 0.998046875, 0), (1, 1, 2.0, 0), (1, 0, 0.25, 0),
        (1, 1, 0.25, 0), (0, 0, 0.0, 1), (1, 1, 0.0625, 1), (0, 0, 0.0, 0)]


def run_wrappers(executable):
    original = OriginalDebugQueries(executable)
    report = []
    original.u.mem_write(base.STATS + 0x2618, struct.pack('<f', 123.5))
    original.u.mem_write(DEBUG_APP + 0x78, struct.pack('<4f', 1.5, 2.5, 3.5, 4.5))
    for name, address in (('statPing', STAT_PING), ('getFPS', GET_FPS)):
        for argc in range(3):
            report.append(original.wrapper(address, name, argc))
    return report


def expected_wrappers():
    """+0x78/+0x7c/+0x84/+0x80 are fps, average, minimum and maximum (006323a0)."""
    results = {'statPing': ('float', 123.5), 'getFPS': ('Vector4', [1.5, 2.5, 4.5, 3.5])}
    return [[name, argc, results[name] if argc == 0 else 'NULL',
             [] if argc == 0 else [['argCountError', name, 0, 0]]]
            for name in ('statPing', 'getFPS') for argc in range(3)]


def run_debug_app(executable):
    original = OriginalDebugQueries(executable)
    return [original.debug_tick(dt) for dt in FRAMES]


def run_ping(executable):
    original = OriginalDebugQueries(executable)
    return [original.ping_tick(*case) for case in PING]
