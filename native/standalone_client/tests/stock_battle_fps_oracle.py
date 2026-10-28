"""Execute the #1513 enable/reset/tick/getter over its original FPS owner.

Connection and Python conversions inherit the debug-query fixture. Unrelated
CSV storage/writing remains stubbed; the real reset, sample arithmetic, buffer
limit and getter execute. In particular this oracle never forces the collector
.data flag off, as the older runtime-services disabled-dictionary test does.
"""
import struct

import pefile
import unicorn

from stock_debug_queries_oracle import OriginalDebugQueries, STATS_TICK
import stock_runtime_services_oracle as base

GET_BATTLE_FPS, GET_VECTOR3 = 0x6d84e0, 0x68ebd0
RESULT = base.MARKS + 0x6050


class OriginalBattleFps(OriginalDebugQueries):
    def __init__(self, executable):
        super().__init__(executable)
        pe = pefile.PE(str(executable))
        assert pe.get_data(0x1a0d761 - pe.OPTIONAL_HEADER.ImageBase, 1) == b'\1'
        # The parent oracle stubs this reset for wrapper-only tests. Preserve
        # instruction execution here, including restart after retained samples.
        self.hooks[0x6da280] = lambda: None
        self._hook(0x6da7c0, self._csv)
        self._hook(GET_VECTOR3, lambda: self._result(RESULT, ('Vector3', list(
            struct.unpack('<3f', self.u.mem_read(self._arg(1), 12))))))

    def _hook(self, address, callback):
        self.hooks[address] = callback
        self.u.hook_add(unicorn.UC_HOOK_CODE, lambda u, a, s, d: self.hooks[a](),
                        begin=address, end=address)

    def _csv(self):
        esp = self.u.reg_read(base.UC_X86_REG_ESP)
        self._return(0)
        self.u.reg_write(base.UC_X86_REG_ESP, esp + 12)  # thiscall ret 8

    def enable(self, value):
        self.argc, self.values, self.fail_at = 1, [value], None
        result = self.call(base.WRAPPERS['enableBattleStatisticCollector'], 0,
                           [struct.pack('<II', 0, base.ARGS)])
        assert result == base.NONE

    def snapshot(self):
        self.argc = 0
        result = self.call(GET_BATTLE_FPS, 0, [struct.pack('<II', 0, base.ARGS)])
        return self.results[result][1]

    def actions(self, actions):
        output = []
        for action in actions:
            if action[0] == 'enable':
                self.enable(action[1])
            elif action[0] == 'loading':
                self.u.mem_write(base.STATS + 0x2642, bytes([int(action[1])]))
            elif action[0] == 'frame':
                self.call(STATS_TICK, base.STATS, [struct.pack('<f', action[1])])
            elif action[0] == 'get':
                output.append(self.snapshot())
            else:
                raise AssertionError(action)
        return output


def frame_actions():
    actions = [['get'], ['frame', .2], ['get'], ['enable', True], ['get']]
    for dt in [0., .01, .1, .2, .19, .5, 1., .03, .47] * 5:
        actions.extend([['frame', dt], ['get']])
    actions.extend([['loading', True], ['frame', .5], ['get'], ['loading', False],
                    ['enable', False], ['frame', 8.], ['get'], ['enable', True], ['get']])
    # Complete 2400 half-second windows, then prove the next full window stops
    # collection before aggregate counters consume that frame.
    actions.extend([['frame', .5]] * 2400)
    actions.extend([['get'], ['frame', 1.], ['get'], ['frame', .1], ['get'],
                    ['enable', True], ['get'], ['frame', -.1], ['get'], ['frame', .2], ['get']])
    return actions
