"""WGVehiclePhysics.configure transactions under the pinned CPython 2.7 API.

Optional arguments run the pinned physics_shared.pyc configurePhysics on real
client vehicle XML; the descriptor geometry it also needs is authored input.
"""
from __future__ import print_function
import copy
import sys
import stock_physics_probe as native
from vehicle_physics_configuration_inputs import make_configuration
import vehicle_physics_reader_model as model


def by_name(result, name='normal'):
    return next(mode for mode in result['modes'] if mode['name'] == name)


class Raising(object):
    def __float__(self):
        raise ZeroDivisionError('conversion callback')


def expect(exception, function, *arguments):
    try:
        function(*arguments)
    except exception:
        return
    raise AssertionError('%s was not raised' % exception.__name__)


# Success, unconfigured consumers and repeated in-place configure.
native.reset_registry()
handle = native.create()
expect(ValueError, native.hull_com_z, handle)
expect(ValueError, native.set_frozen, handle, True)
assert native.summary(handle) == {'epoch': 0, 'frozen': False}
cfg = make_configuration()
assert native.configure(handle, cfg) is True
first = native.summary(handle)
assert first['epoch'] == 1 and first['slots'] == 2 and first['wheels'] == 10
assert first['components'] == 4 and first['boxes'] == 1 and first['polyhedra'] == (1, 1)
assert first['full_mass'] == 2.125 and first['gears'] == 4 and first['curves'] == (3, 4, 4)
assert len(first['grounds']) == 1 and first['grounds_complete'] is False
assert first['chassis'] == by_name(model.read(cfg, 1))['chassis']
assert native.hull_com_z(handle) == 3.0
assert native.configure(handle, cfg) is True
second = native.summary(handle)
assert second['epoch'] == 2 and second['components'] == 8 and second['polyhedra'] == (2, 2)
print('Configure success, unconfigured consumers and in-place reconfigure passed')

# A rejected Reader input or native plan leaves the live graph unchanged.
for mutate, exception in (
        (lambda value: value['modes']['normal'].pop('mode_index'), ValueError),
        (lambda value: value.__setitem__('vehicleType', 1), ValueError),
        (lambda value: value['modes']['normal'].__setitem__('mode_index', -2), ValueError),
        (lambda value: value['modes'].pop('normal'), ValueError),
        (lambda value: value['modes']['normal'].pop('shape'), ValueError),
        (lambda value: value['modes']['normal']['chassis'].__setitem__('clearance', Raising()),
         ZeroDivisionError)):
    bad = make_configuration()
    mutate(bad)
    expect(exception, native.configure, handle, bad)
    assert native.summary(handle) == second
fresh = native.create()
sparse = make_configuration()
sparse['modes'] = {'siegeMode': sparse['modes']['siegeMode']}
expect(ValueError, native.configure, fresh, sparse)
assert native.summary(fresh)['epoch'] == 0
print('Rejected Reader and slot plans preserve live state passed')

# Caught regions commit coherently: constructor words for per-mode settings,
# the Tank's previous anchor/rail/gimlet suffix/gearbox for the globals.
partial = make_configuration()
normal = partial['modes']['normal']
del normal['anchor']['anchorConstFraction']
del normal['fakegearbox']
del normal['comSideFriction']
del normal['engine']['idleChoker']
normal['chassis']['wheelRadius'] = 'x'
normal['fullMass'] = 7.5
assert native.configure(handle, partial) is True
state = native.summary(handle)
expected = by_name(model.read(partial, 1))
assert state['epoch'] == 3 and state['components'] == 12, 'reconfigure keeps the original accumulation'
assert state['body'] == expected['body'] and state['body']['com_side_friction'] == 0.5
assert state['engine'] == expected['engine'] and state['engine']['idle_choker'] == model.word(0x3dcccccd)
assert state['chassis'] == expected['chassis'] and state['chassis']['wheel_radius'] == model.word(0x3ecccccd)
assert state['full_mass'] == 7.5
for key in ('anchor', 'rail', 'gimlet', 'curves', 'curve_points'):
    assert state[key] == second[key], key
gimlet = make_configuration()
del gimlet['modes']['normal']['gimlet']['pushRotOnSpotFixedPeriod']
gimlet['modes']['normal']['gimlet']['wPushedDiag'] = 1.5
gimlet['modes']['normal']['rail']['railFactorInContact'] = 2.5
assert native.configure(handle, gimlet) is True
state = native.summary(handle)
assert state['rail'] == 2.5 and state['gimlet']['pushed_diagonal_weight'] == 1.5
assert state['gimlet']['push_stop'] == 206.125 and state['gimlet']['on_spot_fixed_period'] == second['gimlet']['on_spot_fixed_period']
assert state['curves'] == second['curves']
# A fresh Tank whose gearbox and anchor regions failed keeps constructor values.
blank = native.create()
assert native.configure(blank, partial) is True
state = native.summary(blank)
assert state['curves'] == (0, 0, 0) and state['gears'] == -1 and state['curve_points'] == []
assert state['anchor'] == (0.5, 0.0, 0.0) and state['rail'] == 0.0
assert state['gimlet']['on_spot_fixed_period'] == model.f32(0.2)
native.destroy(blank)
# Every region caught: the smallest input #1513 accepts builds a Tank from
# constructor words alone.
minimal = {'vehicleType': 0, 'modes': {'normal': {'mode_index': 0}}}
blank = native.create()
assert native.configure(blank, minimal) is True
state = native.summary(blank)
expected = by_name(model.read(minimal, 1))
assert state['body'] == expected['body'] == model.defaults(model.BODY)
assert state['shape'] == model.defaults(model.SHAPE) and state['engine'] == model.defaults(model.ENGINE)
assert state['chassis'] == expected['chassis'] and state['curves'] == (0, 0, 0) and state['wheels'] == 10
native.destroy(blank)
print('Caught-region defaults, global prefix retention and previous gearbox passed')

# f84e80 reads the root as a discarded Vehicle::Settings only when the Tank
# holds no mode settings; modes.normal is then missing and the call fails.
reads = []
class Counted(object):
    def __float__(self):
        reads.append(1)
        return 1.0
empty = {'vehicleType': 0, 'modes': {}, 'gravity': Counted()}
blank = native.create()
expect(ValueError, native.configure, blank, empty)
assert reads == [1] and native.summary(blank) == {'epoch': 0, 'frozen': False}
before = native.summary(handle)
expect(ValueError, native.configure, handle, empty)
assert reads == [1] and native.summary(handle) == before
native.destroy(blank)
print('Empty-settings root read and rejection passed')


# Reentrancy: a nested configure during conversion supersedes the outer one.
class Number(object):
    def __init__(self, value, callback):
        self.value = value
        self.callback = callback

    def __float__(self):
        callback, self.callback = self.callback, None
        if callback:
            callback()
        return float(self.value)


nested = make_configuration()
nested['modes']['normal']['fullMass'] = 99.5
outer = make_configuration()
outer['modes']['normal']['gravity'] = Number(9.5, lambda: native.configure(handle, nested))
expect(RuntimeError, native.configure, handle, outer)
state = native.summary(handle)
assert state['epoch'] == 5 and state['full_mass'] == 99.5
failed = make_configuration()
failed['modes']['normal'].pop('mode_index')
outer = make_configuration()
outer['modes']['normal']['gravity'] = Number(9.5, lambda: expect(ValueError, native.configure,
                                                                handle, failed))
assert native.configure(handle, outer) is True
assert native.summary(handle)['epoch'] == 6 and native.summary(handle)['full_mass'] == 2.125
# Destroying the owner from inside the Reader is outside this API; the stock
# method holds its Python self reference for the duration of the call.
print('Nested configure epoch and failed nested transaction cases passed')

# Registry -> f814d0 table -> f96860 consumer, including a later remap.
native.setup_materials([[0, 7], [3], [200], [9, 1]])
owner = native.create()
assert native.configure(owner, make_configuration()) is True
count, groups = native.registry()
table = native.summary(owner)['grounds']
assert len(table) == 4 and table[0]['soft']['rolling_friction'] == 78.125
assert table[3]['soft']['rolling_friction'] == 78.125 and table[1] == model.ground_entry()
kinds = [-0x80000000, -2, -1, 0, 1, 3, 7, 8, 9, 200, 255, 256, 0x7fffffff]
for kind in kinds:
    for strength in range(-1, 6):
        assert native.lookup(owner, kind, strength) == model.lookup(groups, count, kind, strength)
native.setup_materials([[1], [2], [3], [4], [5], [6], [7]])
count, groups = native.registry()
assert len(native.summary(owner)['grounds']) == 4, 'a table keeps its construction size'
for kind in kinds:
    for strength in range(0, 5):
        assert native.lookup(owner, kind, strength) == model.lookup(groups, 4, kind, strength)
native.setup_materials([])
assert native.configure(owner, make_configuration()) is True
expect(IndexError, native.lookup, owner, 0, 1)  # f96860 would read past an empty table
native.destroy(owner)
native.reset_registry()
print('Ground registry, installed table and consumer lookup grid passed')

# Root's b9cebcbd boundary inputs, verbatim, under the stock registry.
for key, empty in ((0, False), (2147483647, False), (-2, False), (-1, False), (-1, True)):
    value = make_configuration()
    for mode in value['modes'].values():
        surface = copy.deepcopy(mode['chassis']['grounds'][0])
        mode['chassis']['grounds'] = {key: {} if empty else surface}
    owner = native.create()
    assert native.configure(owner, value) is True
    state = native.summary(owner)
    installed = sum(entry != model.ground_entry() for entry in state['grounds'])
    assert (installed, state['grounds_complete']) == ((1, True) if key == 0 else (0, False))
    assert state['chassis'] == by_name(model.read(value, 1))['chassis']
    print('material_kind=%d empty=%r accepted=True table=%d installed=%d complete=%r' % (
        key, empty, len(state['grounds']), installed, state['grounds_complete']))
    native.destroy(owner)

native.set_frozen(handle, True)
assert native.summary(handle)['frozen'] is True
native.set_frozen(handle, False)
for index in range(20):
    native.destroy(native.create())
    owner = native.create()
    native.configure(owner, make_configuration())
    native.configure(owner, make_configuration())
    native.destroy(owner)
native.destroy(handle)
native.destroy(fresh)
print('Freeze consumer and repeated teardown passed')

if len(sys.argv) > 2:
    import vehicle_physics_producer as producer
    shared, scope = producer.load(sys.argv[1])
    nodes = producer.nodes(sys.argv[2])
    for path in sorted(nodes):
        base = producer.server_base(producer.read_xphysics(scope, nodes[path]))
        name = path.split('/')[-2] + ':' + path.split('/')[-1][:-4]
        siege = name == 'sweden:S11_Strv_103B'
        descriptor = producer.descriptor(name, {'detailed': base},
                                         siege=producer.descriptor(name, {'detailed': base}) if siege else None)
        owner = native.create()

        class Physics(object):
            """The configurePhysics consumers, routed to the native owner."""
            def configure(self, value):
                self.configuration = copy.deepcopy(value)
                return native.configure(owner, value)

            @property
            def hullCOMZ(self):
                return native.hull_com_z(owner)

            def __setattr__(self, key, value):
                if key == 'isFrozen':
                    native.set_frozen(owner, value)
                object.__setattr__(self, key, value)

        physics = Physics()
        result = shared.configurePhysics(physics, base, descriptor, base['gravityFactor'])
        state = native.summary(owner)
        normal = result['modes']['normal']
        assert state['epoch'] == 1 and state['slots'] == 2 and state['wheels'] == 10
        assert state['complex'] is siege and state['boxes'] == (1 if siege else 0)
        assert abs(state['full_mass'] - normal['fullMass']) <= abs(normal['fullMass']) * 1e-6
        kinds = sorted(normal['chassis']['grounds'])
        assert len(state['grounds']) == 1 and state['grounds_complete'] is (kinds == [0])
        native.setup_materials([[kind] for kind in range(len(kinds))])
        assert kinds == list(range(len(kinds))), kinds
        regrouped = native.create()
        assert native.configure(regrouped, physics.configuration) is True
        table = native.summary(regrouped)
        assert len(table['grounds']) == len(kinds) and table['grounds_complete'] is True
        assert table['chassis'] == by_name(model.read(physics.configuration, len(kinds)))['chassis']
        native.destroy(regrouped)
        native.reset_registry()
        assert physics.centerOfMass.z == state['hull_com_z']
        assert physics.isFrozen is False and physics.movementSignals == 0
        assert physics.simulationYBound == shared._SIMULATION_Y_BOUND
        assert physics.freezeVelEpsilon == shared.FREEZE_VEL_EPSILON
        native.destroy(owner)
        print('Pinned configurePhysics accepted %s (%s)' % (name, 'siege/complex' if siege else 'normal'))
print('Configure fixture passed; host math stand-ins prove ownership, not Win32 numerics')
