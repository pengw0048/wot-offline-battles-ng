# CPython 2.7 executes the original #1513 methods against production builtins.
import marshal
import sys
import types
import zipfile
import BigWorld
import _flags


class Vector4(tuple):
    def __new__(cls, *args):
        return tuple.__new__(cls, args)
    def __div__(self, value):
        return tuple(component / float(value) for component in self)


class Bag(object):
    def __init__(self, **values):
        self.__dict__.update(values)


archive = zipfile.ZipFile(sys.argv[1])
code = marshal.loads(archive.read('scripts/client/gui/Scaleform/managers/ColorSchemeManager.pyc')[8:])
code = next(c for c in code.co_consts if isinstance(c, types.CodeType) and c.co_name == 'BattleColorSchemeManager')
namespace = {'BigWorld': BigWorld, 'Math': Bag(Vector4=Vector4)}


def method(name):
    body = next(c for c in code.co_consts if isinstance(c, types.CodeType) and c.co_name == name)
    return types.FunctionType(body, namespace)


emblems, colours = method('__set3DFlagsEmblem'), method('__set3DFlagsColors')
dp = Bag(getTeamsOnArena=lambda: [1, 2], isAllyTeam=lambda team: team == 1)
owner = Bag(sessionProvider=Bag(getArenaDP=lambda: dp), getColorGroup=lambda: 'default',
            colors=Bag(getSubScheme=lambda name, group: {
                'rgba': Vector4(0, 255, 0, 255) if name == 'flag_team_green' else Vector4(255, 0, 0, 255)}))
for name, function in [('wg_setFlagEmblem', emblems), ('wg_setFlagColor', colours)]:
    native = getattr(BigWorld, name)
    assert isinstance(native, types.BuiltinFunctionType)
    delattr(BigWorld, name)
    try:
        function(owner)
    except AttributeError as error:
        assert name in str(error)
    else:
        raise AssertionError('Original PYC negative control did not fail: ' + name)
    setattr(BigWorld, name, native)
emblems(owner)
colours(owner)
dp.getTeamsOnArena = lambda: []
emblems(owner)  # Neutral team is required even before the team list is ready.
for function, args in [(BigWorld.wg_setFlagColor, ()), (BigWorld.wg_setFlagColor, (0,)),
                       (BigWorld.wg_setFlagColor, ('team', (1, 1, 1, 1))),
                       (BigWorld.wg_setFlagColor, (0, [1, 1, 1, 1])),
                       (BigWorld.wg_setFlagEmblem, (0, 'name')),
                       (BigWorld.wg_setFlagEmblem, (0, None, (0, 0, 1, 1))),
                       (BigWorld.wg_setFlagEmblem, (0, 'name', (0, 0, 1)))]:
    try:
        function(*args)
    except TypeError:
        pass
    else:
        raise AssertionError('Invalid argument accepted: %r' % (args,))
_flags.checkOwner()
print 'Original WTCP colour/emblem PYC and production scene owner passed'
