"""WGVehiclePhysics.configure transactions under the pinned CPython 2.7 API.

Optional arguments run the pinned physics_shared.pyc configurePhysics on real
client vehicle XML; the descriptor geometry it also needs is authored input.
"""
from __future__ import print_function
import copy
import sys
import stock_physics_probe as native
from vehicle_physics_configuration_inputs import make_configuration


def expect(exception, function, *arguments):
    try:
        function(*arguments)
    except exception:
        return
    raise AssertionError('%s was not raised' % exception.__name__)


# Success, unconfigured consumers and repeated in-place configure.
handle = native.create()
expect(ValueError, native.hull_com_z, handle)
expect(ValueError, native.set_frozen, handle, True)
assert native.summary(handle) == {'epoch': 0, 'frozen': False}
cfg = make_configuration()
assert native.configure(handle, cfg) is True
first = native.summary(handle)
assert first['epoch'] == 1 and first['slots'] == 2 and first['wheels'] == 10
assert first['components'] == 4 and first['boxes'] == 1 and first['polyhedra'] == (1, 1)
assert first['full_mass'] == 2.125 and first['grounds'] == 2 and first['gears'] == 4
assert native.hull_com_z(handle) == 3.0
assert native.configure(handle, cfg) is True
second = native.summary(handle)
assert second['epoch'] == 2 and second['components'] == 8 and second['polyhedra'] == (2, 2)
print('Configure success, unconfigured consumers and in-place reconfigure passed')

# A rejected Reader input or native plan leaves the live graph unchanged.
for mutate, exception in (
        (lambda value: value['modes']['normal'].pop('gravity'), ValueError),
        (lambda value: value.__setitem__('vehicleType', 1), ValueError),
        (lambda value: value['modes']['normal'].__setitem__('mode_index', -2), ValueError),
        (lambda value: value['modes']['normal']['fakegearbox']['bkwdgears'].__setitem__(
            'highRpm', [1.0]), ValueError)):
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
assert state['epoch'] == 3 and state['full_mass'] == 99.5
failed = make_configuration()
failed['modes']['normal'].pop('hullSize')
outer = make_configuration()
outer['modes']['normal']['gravity'] = Number(9.5, lambda: expect(ValueError, native.configure,
                                                                handle, failed))
assert native.configure(handle, outer) is True
assert native.summary(handle)['epoch'] == 4 and native.summary(handle)['full_mass'] == 2.125
# Destroying the owner from inside the Reader is outside this API; the stock
# method holds its Python self reference for the duration of the call.
print('Nested configure epoch and failed nested transaction cases passed')

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
        assert state['grounds'] == len(normal['chassis']['grounds'])
        assert physics.centerOfMass.z == state['hull_com_z']
        assert physics.isFrozen is False and physics.movementSignals == 0
        assert physics.simulationYBound == shared._SIMULATION_Y_BOUND
        assert physics.freezeVelEpsilon == shared.FREEZE_VEL_EPSILON
        native.destroy(owner)
        print('Pinned configurePhysics accepted %s (%s)' % (name, 'siege/complex' if siege else 'normal'))
print('Configure fixture passed; host math stand-ins prove ownership, not Win32 numerics')
