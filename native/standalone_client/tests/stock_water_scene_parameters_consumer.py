"""Python 2.7: run the exact #1513 ArenaType reader and Avatar init step with recording stand-ins."""
import json, marshal, sys, types, zipfile

client, payload = sys.argv[1], json.loads(sys.argv[2])
package = zipfile.ZipFile(client + '/res/packages/scripts.pkg')


def function(member, name):
    def walk(code):
        yield code
        for value in code.co_consts:
            if hasattr(value, 'co_code'):
                for nested in walk(value): yield nested
    found = [code for code in walk(marshal.loads(package.read(member)[8:])) if code.co_name == name]
    assert len(found) == 1, (member, name, len(found))
    return found[0]


class Any(object):
    """Permissive stand-in for modules and objects the measured lines do not use."""
    def __init__(self, name='any'): self.__dict__['_name'] = name
    def __getattr__(self, name): return Any(self._name + '.' + name)
    def __call__(self, *args, **kwargs): return Any(self._name + '()')
    def __iter__(self): return iter(())
    def __nonzero__(self): return False


class Section(object):
    """BigWorld DataSection.readFloat over a decoded packed-XML tree."""
    def __init__(self, tree): self.tree = tree
    def readFloat(self, path, default=0.0):
        node = self.tree
        for part in path.split('/'):
            node = node.get('children', {}).get(part) if node else None
        if node is None: return default
        kind, value = node['value']
        if kind == 'int': return float(value)  # PackedSection::asFloat for TYPE_INT
        if kind == 'float': return float(value)
        raise AssertionError((path, kind))
    def __getattr__(self, name): return Any('section.' + name)


def read_geometry(tree):
    code = function('scripts/common/ArenaType.pyc', '__readGeometryCfg')
    helpers = dict((name, lambda *args: Any(name)) for name in code.co_names if name.startswith('__read'))
    environment = dict(helpers, IS_CLIENT=True, IS_WEB=False, IS_CELLAPP=False, IS_BASEAPP=False,
                       i18n=Any('i18n'), __builtins__=__builtins__)
    environment.update(dict((name, Any(name)) for name in code.co_names if name not in environment
                            and not hasattr(__builtins__, name)))
    reader = types.FunctionType(code, environment, '__readGeometryCfg')
    cfg = reader(1, 'probe', Section(tree), Section({}))
    return dict((key, cfg[key]) for key in ('waterTexScale', 'waterFreqX', 'waterFreqZ'))


calls = []


class BigWorldRecorder(object):
    def __getattr__(self, name):
        def call(*args):
            calls.append([name] + [repr(value) for value in args])
            return Any('BigWorld.' + name)
        if name == 'entities':
            return {}
        return call


class Steps(object):
    ALL_STEPS_PASSED = 15
    INIT_COMPLETED = 16


class Arena(object):
    def __init__(self, arenaType): self.arenaType = arenaType; self.period = 0; self.onPeriodChange = []
    def __getattr__(self, name): return Any('arena.' + name)


class Mover(object):
    def setSpaceID(self, spaceID): calls.append(['ProjectileMover.setSpaceID', repr(spaceID)])


class Avatar(object):
    def __init__(self, values):
        self.__dict__['_PlayerAvatar__initProgress'] = Steps.ALL_STEPS_PASSED
        self.__dict__['_PlayerAvatar__projectileMover'] = Mover()
        self.__dict__['arena'] = Arena(type('ArenaType', (object,), values)())
        self.__dict__['spaceID'] = 7
    def isObserver(self): return False
    def __getattr__(self, name): return Any('self.' + name)


def enter(values):
    code = function('scripts/client/Avatar.pyc', '__onInitStepCompleted')
    environment = dict((name, Any(name)) for name in code.co_names)
    environment.update(BigWorld=BigWorldRecorder(), _INIT_STEPS=Steps, isinstance=isinstance,
                       __builtins__=dict(vars(__builtins__), __import__=lambda *args, **kwargs: Any(args[0])))
    for name in ('True', 'False', 'None', 'isinstance', 'len', 'getattr'):
        environment.pop(name, None)
    step = types.FunctionType(code, environment, '__onInitStepCompleted')
    avatar = Avatar(values)
    start = len(calls)
    step(avatar)
    first = calls[start:]
    step(avatar)  # INIT_COMPLETED already set: the guard returns before any native call
    assert len(calls) == start + len(first), calls[start + len(first):]
    return first


results = []
for case in payload:
    values = read_geometry(case['tree'])
    sequence = enter(values)
    names = [call[0] for call in sequence]
    water = [call for call in sequence if call[0].startswith('wg_setWater')]
    results.append({'name': case['name'], 'values': values, 'water': water,
                    'after_world_draw': names.index('wg_setWaterTexScale') > names.index('worldDrawEnabled'),
                    'before_projectiles': names.index('wg_setWaterFreqZ') < names.index('ProjectileMover.setSpaceID')})
print(json.dumps(results))
