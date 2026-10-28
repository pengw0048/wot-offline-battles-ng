# Python 2.7: production cached queries and exact first-lobby consumer.
import json
import marshal
import sys
import types
import zipfile

import BigWorld as B
import _graphics_detection_host as host

profiles = json.loads(sys.argv[1])
host.configure(profiles)
assert host.state() == (0, 0)
assert [B.getAutoDetectGraphicsSettingsScore(index) for index in range(-1, 9)] == [0] * 10
assert host.state() == (0, 0)  # Cache-only getter neither initializes nor measures.
for index, name in enumerate(('max', 'HIGH', 'MeDiuM', 'low', u'MIN')):
    value = B.getSystemPerformancePresetIdFromName(name)
    assert value == index and type(value) is int
assert B.getSystemPerformancePresetIdFromName(' max ') == 5
assert host.state() == (1, 0)
saved = {'gpuMemory': 2 ** 40, 'gpuScore': 95, 'ram': 8192,
         'virtualMemory': 4096, 'cpuFrequency': 20000}
host.importScores(saved)
assert host.exportScores() == saved
assert B.getAutoDetectGraphicsSettingsScore(0) == 2 ** 40
assert type(B.getAutoDetectGraphicsSettingsScore(0)) is (long if sys.maxint < 2 ** 40 else int)
assert B.detectGraphicsPresetFromSystemSettings() == 0
assert host.exportScores() == saved and host.state() == (1, 0)
host.importScores(None)
assert host.exportScores() == saved
host.importScores({'virtualMemory': 2000})
assert host.exportScores() == {'gpuMemory': 0, 'gpuScore': 0, 'ram': 0,
                              'virtualMemory': 2000, 'cpuFrequency': 0}
assert B.getAutoDetectGraphicsSettingsScore(-1) == B.getAutoDetectGraphicsSettingsScore(7) == 0
try:
    B.getAutoDetectGraphicsSettingsScore('1')
except TypeError:
    pass
else:
    raise AssertionError('Score query accepted a string index')
for name, args in (('getAutoDetectGraphicsSettingsScore', ()),
                   ('getAutoDetectGraphicsSettingsScore', (0, 1)),
                   ('getSystemPerformancePresetIdFromName', ()),
                   ('getSystemPerformancePresetIdFromName', (1,)),
                   ('detectGraphicsPresetFromSystemSettings', (1,))):
    try:
        getattr(B, name)(*args)
    except TypeError:
        pass
    else:
        raise AssertionError('Accepted wrong query shape: ' + name)

with zipfile.ZipFile(sys.argv[2]) as archive:
    epic = marshal.loads(archive.read('scripts/client/gui/game_control/epic_mode_controller.pyc')[8:])
    statistics = marshal.loads(archive.read('scripts/client/helpers/statistics.pyc')[8:])
cls = next(code for code in epic.co_consts if isinstance(code, types.CodeType) and code.co_name == 'EpicModeController')
score_class = next(code for code in statistics.co_consts
                   if isinstance(code, types.CodeType) and code.co_name == 'HARDWARE_SCORE_PARAMS')
score_fields = types.FunctionType(score_class, {'__name__': 'original_statistics'})()
params = type('ScoreParams', (), score_fields)
game = type('Game', (), {'GAMEPLAY_EPIC_STANDARD': 'standard',
                         'GAMEPLAY_EPIC_DOMINATION': 'epic_domination', 'GAMEPLAY_DOMINATION': 'domination'})
namespace = {'__builtins__': __builtins__, 'BigWorld': B, 'HARDWARE_SCORE_PARAMS': params,
             'GAME': game, '_RESTRICED_PRESET_NAMES': ['MIN'], '_VIRTUAL_MEMORY_LIMIT': 2048,
             'LOG_DEBUG': lambda *args: None}
methods = dict((code.co_name, types.FunctionType(code, namespace)) for code in cls.co_consts
               if isinstance(code, types.CodeType) and code.co_name in
               ('onLobbyInited', '__checkForEpicRandomValidation', '__validateEpicRandomUsage'))
class Settings(object):
    def __init__(self):
        self.values = {'standard': True, 'epic_domination': True, 'domination': True}
        self.applied = []
    def getSetting(self, name):
        return self.values[name]
    def applySetting(self, name, value):
        self.values[name] = value
        self.applied.append((name, value))
    def applyStorages(self, save):
        return []
    def confirmChanges(self, changes):
        pass
    def clearStorages(self):
        pass
owner = type('Owner', (), {})()
owner.settingsCore = Settings()
filters = {'isEpicRandomCheckboxClicked': False}
owner._EpicModeController__getFilters = lambda: filters
owner._EpicModeController__checkForEpicRandomValidation = lambda: methods['__checkForEpicRandomValidation'](owner)
owner._EpicModeController__validateEpicRandomUsage = lambda: methods['__validateEpicRandomUsage'](owner)
methods['onLobbyInited'](owner, None)
assert owner.settingsCore.applied == [('standard', False), ('epic_domination', False)]
assert host.exportScores()['virtualMemory'] == 2000 and host.state() == (1, 0)
getter = B.detectGraphicsPresetFromSystemSettings
del B.detectGraphicsPresetFromSystemSettings
filters['isEpicRandomCheckboxClicked'] = True
methods['onLobbyInited'](owner, None)  # Saved checkbox guard skips the query.
filters['isEpicRandomCheckboxClicked'] = False
try:
    methods['onLobbyInited'](owner, None)
except AttributeError:
    pass
else:
    raise AssertionError('Original first-lobby validation did not need the query')
B.detectGraphicsPresetFromSystemSettings = getter
host.device(0, 0x300, 0x300)
assert B.getSystemPerformancePresetIdFromName('MAX') == 0
try:
    B.detectGraphicsPresetFromSystemSettings()
except RuntimeError:
    pass
else:
    raise AssertionError('Queried shader capabilities without an active device')
# Device absence preserves the previous cache and pending work; it never uses
# the fake producer or applies an early result. A valid explicit preset is not
# auto-detected. The deferred full path applies exactly once when ready.
host.schedule(1, 2)
host.finish()
assert host.state()[1] == 0 and host.applied() == (0, -1)
host.schedule(1, -1)
host.schedule(0, -1)
host.finish()
assert host.state()[1] == 0 and host.exportScores()['virtualMemory'] == 2000
host.device(1, 0x300, 0x300)
host.finish()
assert host.state()[1] == 1 and host.applied()[0] == 1
assert host.exportScores() == dict((key, 1000) for key in saved)
host.finish()
assert host.state()[1] == 1 and host.applied()[0] == 1
host.schedule(0, -1)
host.finish()
assert host.state()[1] == 2 and host.applied()[0] == 1
host.measure()
assert host.state()[1] == 3
host.missingResource()
try:
    B.getSystemPerformancePresetIdFromName('MIN')
except RuntimeError:
    pass
else:
    raise AssertionError('Accepted missing authored profiles')
assert host.exportScores()['virtualMemory'] == 1000
