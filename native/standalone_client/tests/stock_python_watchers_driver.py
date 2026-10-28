"""Embedded CPython 2.7: BigWorld watcher calls and the exact #1513 OfflineMapCreator over them."""
from __future__ import print_function

import marshal
import os
import sys
import types
import zipfile

import BigWorld

NAMES = ('setWatcher', 'getWatcher', 'getWatcherDir', 'addWatcher', 'delWatcher')


def report(name, value):
    print('%s %r' % (name, value))
    sys.stdout.flush()


# Every positional shape returns None; #1513 never reads the arguments.
SHAPES = ((), ('Visibility/GUI',), ('Visibility/GUI', False), ('Visibility/GUI', True),
          ('Client Settings/Script tick', False), ('Render/Shadows/qualityPreset', 7),
          (None,), (1, 2, 3), ('a', 'b', 'c', 'd'), (object(),), (u'Visibility/GUI', [1]))
for name in NAMES:
    function = getattr(BigWorld, name)
    report(name + '-positional', all(function(*shape) is None for shape in SHAPES))
    try:
        function(path='Visibility/GUI')
        report(name + '-keyword', 'accepted')
    except TypeError as error:
        report(name + '-keyword', str(error))
report('module-functions', sorted(name for name in dir(BigWorld) if not name.startswith('_')))

package = zipfile.ZipFile(os.environ['WOT_SCRIPTS_PACKAGE'])
module = marshal.loads(package.read('scripts/client/OfflineMapCreator.pyc')[8:])
owner = [code for code in module.co_consts if hasattr(code, 'co_code') and code.co_name == 'OfflineMapCreator'][0]
methods = dict((code.co_name, code) for code in owner.co_consts if hasattr(code, 'co_code'))

failures = []
watcher_calls = []
native_set_watcher = BigWorld.setWatcher


def record_set_watcher(*args):
    result = native_set_watcher(*args)
    watcher_calls.append((args, result))
    return result


class Recorder(object):
    def __init__(self, name):
        self.name = name

    def __call__(self, *args, **kwargs):
        return None

    def __getattr__(self, name):
        return Recorder(name)


class Avatar(object):
    pass


entities = {}


def create_entity(*args):
    entities[7] = Avatar()
    return 7


for name, value in (('worldDrawEnabled', Recorder('worldDrawEnabled')), ('createSpace', lambda: 42),
                    ('addSpaceGeometryMapping', lambda *args: 9), ('createEntity', create_entity),
                    ('entities', entities), ('player', Recorder('player')),
                    ('cameraSpaceID', Recorder('cameraSpaceID')), ('camera', Recorder('camera')),
                    ('clearEntitiesAndSpaces', Recorder('clear')), ('isClientSpace', lambda space: True),
                    ('delSpaceGeometryMapping', Recorder('delMapping')), ('clearSpace', Recorder('clearSpace')),
                    ('releaseSpace', Recorder('releaseSpace'))):
    setattr(BigWorld, name, value)


def log_current_exception():
    failures.append(repr(sys.exc_info()[1]))


namespace = {'__builtins__': __builtins__, 'BigWorld': BigWorld, 'LOG_DEBUG': lambda *args: None,
             'LOG_CURRENT_EXCEPTION': log_current_exception, 'g_appLoader': Recorder('g_appLoader'),
             'constants': Recorder('constants'), 'MapActivities': Recorder('MapActivities'),
             '_V_START_POS': (0.0, 0.0, 0.0), '_V_START_ANGLES': (0.0, 0.0, 0.0)}
Creator = type('OfflineMapCreator', (object,), dict(
    (name, types.FunctionType(methods[name], namespace, name)) for name in ('__init__', 'create', 'destroy', 'cancel')))
Creator._OfflineMapCreator__loadCfg = lambda self, kind, map_name: None
Creator._OfflineMapCreator__getArenaTypeId = lambda self, map_name: 145
Creator._OfflineMapCreator__setupCamera = lambda self: None


def replay(label, set_watcher):
    del failures[:]
    del watcher_calls[:]
    BigWorld.setWatcher = set_watcher
    try:
        creator = Creator()
        creator.create('45_north_america')
        created = creator._OfflineMapCreator__isActive
        creator.destroy()
        creator.create('45_north_america')
        creator.cancel()
        report(label + '-escaped', None)
    except Exception as error:
        report(label + '-escaped', repr(error))
        created = None
    report(label + '-created', created)
    report(label + '-logged', list(failures))
    report(label + '-watcher-calls', list(watcher_calls))


replay('native', record_set_watcher)


def failing_set_watcher(path, value):
    # Harness sensitivity: the Hybrid lookup's error shape from the room log.
    raise TypeError('Failed to set %s to %s.' % (path, value))


replay('hybrid-error-shape', failing_set_watcher)
BigWorld.setWatcher = native_set_watcher
