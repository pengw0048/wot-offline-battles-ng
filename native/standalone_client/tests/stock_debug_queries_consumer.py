# Python 2.7, run inside stock_debug_queries_host: argv = [this, scripts.pkg, mode]
# mode 'absent' runs the original DebugController without statPing/getFPS;
# 'port' runs it over the production bindings, statistics task and DebugApp.
import imp
import marshal
import struct
import sys
import zipfile

import _debug_host as host

PACKAGE, MODE = sys.argv[1], sys.argv[2]
ARCHIVE = zipfile.ZipFile(PACKAGE)
PANEL = []
RECORDED = []
INTERVALS = []


def module(name, **attributes):
    result = sys.modules.get(name) or imp.new_module(name)
    result.__dict__.update(attributes)
    sys.modules[name] = result
    if '.' in name:
        parent, child = name.rsplit('.', 1)
        setattr(module(parent), child, result)
    return result


def load(member, name):
    result = module(name)
    exec marshal.loads(ARCHIVE.read(member)[8:]) in result.__dict__
    return result


def expect_error(kind, text, call, *args):
    try:
        call(*args)
    except kind as error:
        assert str(error) == text, (str(error), text)
        return
    raise AssertionError('%r%r did not raise %s' % (call, args, kind.__name__))


def float32(value):
    return struct.unpack('<f', struct.pack('<f', value))[0]


class TimeInterval(object):
    """gui.shared.utils.TimeInterval: the test drives each interval tick."""
    def __init__(self, interval, owner, name):
        self.interval, self.owner, self.name, self.running = interval, owner, name, False
        INTERVALS.append(self)

    def start(self):
        self.running = True

    def stop(self):
        self.running = False

    def fire(self):
        assert self.running
        getattr(self.owner, self.name)()


class Replay(object):
    isPlaying = isBattleSimulation = isRecording = False
    fps, ping, isLaggingNow = 0, 0, False

    def setFpsPingLag(self, fps, ping, lag):
        RECORDED.append((fps, ping, lag))


class StatsCollector(object):
    updates = 0

    def update(self):
        StatsCollector.updates += 1


REPLAY = Replay()
module('BigWorld', statLagDetected=lambda: False)
module('BattleReplay', g_replayCtrl=REPLAY)
module('gui.battle_control.battle_constants', BATTLE_CTRL_ID=type('BATTLE_CTRL_ID', (), {'DEBUG': 17}))
module('gui.battle_control.view_components', IViewComponentsController=object)
module('gui.shared.utils.TimeInterval', TimeInterval=TimeInterval)
module('helpers.dependency', descriptor=lambda interface: StatsCollector())
module('skeletons.helpers.statistics', IStatisticsCollector=type('IStatisticsCollector', (object,), {}))
debug_ctrl = load('scripts/client/gui/battle_control/controllers/debug_ctrl.pyc',
                  'gui.battle_control.controllers.debug_ctrl')
BigWorld = sys.modules['BigWorld']


class Panel(debug_ctrl.IDebugPanel):
    def updateDebugInfo(self, ping, fps, isLaggingNow, fpsReplay=-1):
        PANEL.append((ping, fps, isLaggingNow, fpsReplay))


controller = debug_ctrl.DebugController()
controller.setViewComponents(Panel())
controller.startControl()
interval = INTERVALS[-1]
assert (interval.interval, interval.name) == (0.2, '_update')

if MODE == 'absent':
    expect_error(AttributeError, "'module' object has no attribute 'statPing'", interval.fire)
    assert PANEL == [] and StatsCollector.updates == 0
    print('absent: every DebugController interval raises AttributeError at statPing')
    sys.exit(0)

assert host.initScripts() == 4
assert host.tasks() == [('StockClientStatistics', '>Finale')], host.tasks()
expect_error(TypeError, 'statPing() takes no arguments.', BigWorld.statPing, 1)
expect_error(TypeError, 'getFPS() takes no arguments.', BigWorld.getFPS, None)

# Before the first statistics tick both owners hold their constructed zeros.
assert BigWorld.statPing() == 0.0 and BigWorld.getFPS() == (0.0, 0.0, 0.0, 0.0)
interval.fire()
assert PANEL[-1] == (0, 0, False, -1), PANEL

# Offline entry: no ConnectionControl server connection -> 999 ms after a frame.
host.setConnection(1, 0, 0, 0.0)
host.tickStatistics()
assert BigWorld.statPing() == 999.0 and type(BigWorld.statPing()) is float

# DebugApp samples real frame times; [1] is the warm-up-aware 50-frame average.
frames = [1 / 60.0, 1 / 30.0, 0.02, 0.05, 1 / 144.0] * 30
for index, dt in enumerate(frames):
    sample = host.tickDebugApp(dt)
    assert BigWorld.getFPS() == sample, (index, BigWorld.getFPS(), sample)
fps, average, minimum, maximum = BigWorld.getFPS()
window = [float32(value) for value in frames[-50:]]
assert minimum == min(float32(1 / value) for value in window)
assert maximum == max(float32(1 / value) for value in window)
assert 25.0 < average < 60.0 and fps > 0
interval.fire()
assert PANEL[-1] == (999, int(average), False, -1), PANEL
print('port: offline ping 999 ms; getFPS %r -> panel fps %d' % (BigWorld.getFPS(), PANEL[-1][1]))

# Nonconstant frame time moves the average after the next 0.5 s update.
before = BigWorld.getFPS()[1]
for unused in range(60):
    host.tickDebugApp(0.1)
assert BigWorld.getFPS()[1] != before and abs(BigWorld.getFPS()[1] - 10.0) < 1e-4

# Retail connection lifecycle: online latency, offline channel, disconnect.
for online, latency, expected in ((1, 0.25, 200.0), (1, 0.0625, 12.5), (1, 0.03125, 1.0),
                                  (1, 2.0, 999.0), (0, 0.25, 999.0)):
    host.setConnection(1, 1, online, latency)
    host.tickStatistics()
    assert BigWorld.statPing() == expected, (online, latency, BigWorld.statPing())
host.setConnection(1, 1, 1, 0.25)
host.tickStatistics()
host.setLoadingTimer(1)  # enableLoadingTimer(True): the tick keeps the last ping
host.setConnection(1, 0, 0, 0.0)
host.tickStatistics()
assert BigWorld.statPing() == 200.0
host.setLoadingTimer(0)
host.tickStatistics()
assert BigWorld.statPing() == 999.0
host.resetPing()  # enableBattleStatisticCollector(True) clears it until the next frame
assert BigWorld.statPing() == 0.0
host.tickStatistics()
host.setConnection(0, 0, 0, 0.0)  # no ConnectionControl during teardown
host.tickStatistics()
assert BigWorld.statPing() == 999.0
print('port: retail-connection ping 200/12.5/1/999 ms, loading timer freeze and reset')

# The recording guard forwards the native values; the replay guard reads getFPS only.
REPLAY.isRecording = True
interval.fire()
assert RECORDED[-1] == (BigWorld.getFPS()[1], 999.0, False), RECORDED
REPLAY.isRecording, REPLAY.isPlaying, REPLAY.fps, REPLAY.ping = False, True, 24.5, 87
interval.fire()
assert PANEL[-1] == (87, int(BigWorld.getFPS()[1]), False, 24), PANEL
updates = StatsCollector.updates
REPLAY.isPlaying = False
controller.stopControl()
assert not interval.running and controller._debugPanelUI is None and StatsCollector.updates == updates
print('port: recording and replay guards; stopControl ends the interval')
