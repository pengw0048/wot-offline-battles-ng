"""Python 2.7: run the exact #1513 helpers/DecalMap module against the host BigWorld extension."""
import json, marshal, sys, types, zipfile

client, extension, payload = sys.argv[1], sys.argv[2], json.loads(sys.argv[3])
sys.path.insert(0, extension)
import BigWorld
import _render_pipeline_host as host

package = zipfile.ZipFile(client + '/res/packages/scripts.pkg')


class Section(object):
    """DataSection over a decoded packed-XML tree (readFloat/readString/[]/values/name)."""
    def __init__(self, name, tree): self.name, self.tree = name, tree
    def _child(self, name):
        for key, child in self.tree['children']:
            if key == name: return Section(key, child)
        return None
    def __getitem__(self, name): return self._child(name)
    def values(self): return [Section(key, child) for key, child in self.tree['children']]
    def readFloat(self, name, default=0.0):
        child = self._child(name)
        if child is None: return default
        kind, value = child.tree['value']
        return float(value) if kind in ('int', 'float') else default
    def readString(self, name, default=''):
        child = self._child(name)
        return child.tree['value'][1] if child is not None and child.tree['value'][0] == 'string' else default


errors = []
debug = types.ModuleType('debug_utils')
debug.LOG_ERROR = lambda *args: errors.append(['LOG_ERROR'] + [str(a) for a in args])
debug.LOG_CURRENT_EXCEPTION = lambda: errors.append(['LOG_CURRENT_EXCEPTION', repr(sys.exc_info()[1])])
resources = types.ModuleType('ResMgr')
resources.openSection = lambda path: Section('chassis_effects', payload['chassis']) if \
    path == 'scripts/item_defs/vehicles/common/chassis_effects.xml' else None
sys.modules.update(debug_utils=debug, ResMgr=resources)
module = types.ModuleType('helpers.DecalMap')
exec marshal.loads(package.read('scripts/client/helpers/DecalMap.pyc')[8:]) in module.__dict__


def battle(pipeline, repeats):
    """game.init builds g_instance once; each PlayerAvatar.__init__ calls initGroups(1.0)."""
    host.setPipeline(pipeline)
    del errors[:]
    decals = module.DecalMap(Section('decal', payload['decal']))
    loaded = host.state()
    for unused in range(repeats):
        decals.initGroups(1.0)
    textures = [child for key, child in payload['decal']['children'] if key == 'textures'][0]
    names = sorted(name for name, unused in textures['children'])
    return {'forward': BigWorld.isForwardPipeline(), 'isFalse': BigWorld.isForwardPipeline() is False,
            'type': type(BigWorld.isForwardPipeline()).__name__, 'afterInit': loaded, 'afterGroups': host.state(),
            'indices': dict((name, decals.getIndex(name)) for name in names), 'errors': list(errors)}


result = {'deferred': battle(0, 2), 'forward': battle(1, 1)}
calls = {}
for arguments in ((1,), (None,), (1, 2)):
    try:
        BigWorld.isForwardPipeline(*arguments)
        calls[repr(arguments)] = 'returned'
    except TypeError as error:
        calls[repr(arguments)] = 'TypeError: ' + str(error)
result['arguments'] = calls
print(json.dumps(result))
