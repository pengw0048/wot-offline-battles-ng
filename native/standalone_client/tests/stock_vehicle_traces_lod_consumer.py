"""Run the unchanged #1513 trace assembler against the production LOD owner."""
import collections
import gc
import marshal
import math
import opcode
import os
import sys
import types
import zipfile
import Vehicular
from trace_probe import tick, owners

archive = zipfile.ZipFile(os.path.join(sys.argv[1], 'res', 'packages', 'scripts.pkg'))
module = marshal.loads(archive.read('scripts/client/vehicle_systems/model_assembler.pyc')[8:])
assembler = next(c for c in module.co_consts if isinstance(c, types.CodeType) and c.co_name == 'assembleVehicleTraces')

# Read the module's actual LOAD_CONST / STORE_NAME assignment. The default
# must stay None; substituting a convenient integer would hide the live bug.
def constant_assignment(code, name):
    previous = None
    offset = 0
    while offset < len(code.co_code):
        op = ord(code.co_code[offset]); offset += 1
        argument = None
        if op >= opcode.HAVE_ARGUMENT:
            argument = ord(code.co_code[offset]) | (ord(code.co_code[offset + 1]) << 8)
            offset += 2
        if op == opcode.opmap['STORE_NAME'] and code.co_names[argument] == name:
            assert previous[0] == opcode.opmap['LOAD_CONST']
            return code.co_consts[previous[1]]
        previous = (op, argument)
    raise AssertionError('Original constant assignment missing')

default_priority = constant_assignment(module, 'DEFAULT_MAX_LOD_PRIORITY')
assert default_priority is None
shared = marshal.loads(archive.read('scripts/common/items/components/shared_components.pyc')[8:])
fields = next(c for c in shared.co_consts if c == ('maxLodDistance', 'maxPriority'))
LodSettings = collections.namedtuple('LodSettings', fields)

class Box(object):
    def __init__(self, **values): self.__dict__.update(values)

environment = {'__builtins__': __builtins__, 'Vehicular': Vehicular,
               'shared_components': Box(LodSettings=LodSettings),
               'DEFAULT_MAX_LOD_PRIORITY': default_priority,
               'DataLinks': Box(createBoolLink=lambda owner, name: (owner, name)),
               'DecalMap': Box(g_instance=Box(getTextureSet=lambda name: {'earth': (1, 2, 3), 'bump': 4})),
               'material_kinds': Box(EFFECT_MATERIAL_IDS_BY_NAMES={'earth': (3, 4)})}
assemble = types.FunctionType(assembler, environment)
limit = 0xffffffff

if not os.environ.get('WOT_TRACE_LOD_NEGATIVE'):
    value = Vehicular.VehicleTraces()
    for config, expected in [(LodSettings(100., None), (100., limit)), ((25.,), (25., limit)),
                             ((-5., 0), (-5., 0)), ((12, 15), (12., 15)),
                             ((5., 3.75), (5., 3)), ((5., -1.75), (5., limit)),
                             ((5., 0xffffffffL), (5., limit)), ((True, True), (1., 1))]:
        assert value.setLodSettings(config) is None
        assert value.snapshot()[:2] == expected
    value.setLodSettings((float('nan'), None)); assert math.isnan(value.snapshot()[0])
    value.setLodSettings((float('inf'), None)); assert math.isinf(value.snapshot()[0])
    value.setLodSettings((7., 2))
    before = value.snapshot()
    for args in [(), ((1., None), 3), ((),), ([],), ([1., None],), (None,),
                 ((1., None, 3),), ((None, None),), (('bad', None),),
                 ((1., object()),), ((1., -1),), ((1., -1L),), ((1., 0x100000000L),)]:
        try: value.setLodSettings(*args)
        except TypeError: pass
        else: raise AssertionError('Invalid LOD arguments accepted: %r' % (args,))
        assert value.snapshot() == before
    value.destroy()
    try: value.setLodSettings((10., None))
    except ValueError: pass
    else: raise AssertionError('Destroyed owner accepted settings')
    value.activate(); assert owners() == 0
    del value; gc.collect()

# Different trace configurations use exactly the same producer and owner.
# activate/tick/deactivate/destroy are the production TraceState bodies; only
# projection/rendering after its LOD gate is replaced by a submission receipt.
for round_id, distance in ((1, 80.), (2, 200.)):
    config = Box(textureSet='earth', bufferPrefs='tracks', centerOffset=.25,
                 size=(.5, 1.), lodDist=distance)
    appearance = Box(typeDescriptor=Box(chassis=Box(traces=config)),
                     compoundModel=object(), flyingInfoProvider=object())
    assemble(appearance, Box(movementInfo=object()), (100, distance))
    trace = appearance.vehicleTraces
    assert trace.snapshot()[:2] == (distance, limit)
    assert owners() == 0
    trace.activate(); trace.activate(); assert owners() == 1 and tick() == 1
    trace.setLodLink((100, distance + 1)); assert tick() == 0
    trace.setLodLink((100, distance)); trace.setLodSettings((distance, 99)); assert tick() == 0
    trace.setLodSettings((distance, None)); assert tick() == 1
    trace.setLodLink(None); assert tick() == 1
    trace.deactivate(); trace.deactivate(); assert owners() == 0 and tick() == 0
    trace.activate(); trace.destroy(); trace.destroy(); assert owners() == 0 and tick() == 0
    del appearance, trace; gc.collect(); assert owners() == 0

print('Trace LOD owner and exact #1513 assembleVehicleTraces passed')
