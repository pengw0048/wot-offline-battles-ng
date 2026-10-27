"""Descriptor-boundary fixtures for original f49860/f7e600/f81a60 readers.

Values are explicit test inputs, not vehicle dimensions or replacement tunings.
The optional pinned bytecode slice executes only its original default dictionary
producer and builders; it imports no fake BigWorld module.
"""
from __future__ import print_function
import copy
import gc
import math
import os
import weakref
import stock_physics_probe as native


from vehicle_physics_configuration_inputs import make_configuration
import vehicle_physics_reader_model as model


def by_name(result, name='normal'):
    return next(mode for mode in result['modes'] if mode['name'] == name)


def invalid(configuration, exception):
    previous = native.current()
    try:
        native.parse(configuration)
    except exception:
        pass
    else:
        raise AssertionError('Invalid configuration was accepted')
    assert native.current() == previous, 'Failed read changed previously published configuration'


native.reset_registry()
cfg = make_configuration()
parsed = native.parse(cfg)
normal = by_name(parsed)
assert parsed == model.read(cfg, 1)
assert parsed['vehicle_type'] == 0 and len(parsed['modes']) == 2
assert normal['body']['gravity'] == 1.125 and normal['body']['full_mass'] == 2.125
assert normal['body']['hull_size'] == (4.5, 5.5, 6.5)
assert normal['body']['gimlet_pushed_handbrake'] == 203.125
assert normal['shape']['terrain_aft_chamfer_fraction'] == 37.125
assert normal['chassis']['road_wheel_positions'] == [1, 2, 3, 4, 5]
assert normal['chassis']['slope_grip_side_dynamic_object'] == [14, 15, 16, 17]
assert normal['chassis']['yaw_precise_distance'] == 116.125
assert normal['engine']['losses'] == [151, 152]
assert normal['engine']['simple_min_rpm'] == 137.125, 'Reader must retain source RPM units'
assert parsed['anchor']['maximum_reaction_factor'] == 161.125
assert parsed['rail_factor_in_contact'] == 181.25 and parsed['gimlet_fields'] == 16
assert parsed['gimlet']['maximum_velocity_scale'] == 216.125
assert parsed['gears']['forward'][1] == {'switch_speed': 2, 'switch_hysteresis': 5, 'low_rpm': 8, 'high_rpm': 11}
# Stock registry: one group, so key 0 installs and key 3 is out of range.
assert len(normal['chassis']['grounds']) == 1 and normal['chassis']['grounds_complete'] is False
assert normal['chassis']['grounds'][0]['firm']['rolling_friction'] == 78.125
print('Complete source schema, source units and per-mode data passed')

# Original Reader accepts dict subclasses while bypassing overridden lookup.
class Dictionary(dict):
    def __getitem__(self, key):
        raise AssertionError('Reader called overridden dictionary lookup')

assert native.parse(Dictionary(cfg)) == parsed
invalid(object(), TypeError)
invalid(None, TypeError)
bad = make_configuration()
bad['vehicleType'] = 1
invalid(bad, ValueError)
bad['vehicleType'] = '0'
invalid(bad, TypeError)
bad['vehicleType'] = 1 << 40
invalid(bad, OverflowError)
bad = make_configuration()
bad['modes']['normal']['gravity'] = True
assert by_name(native.parse(bad))['body']['gravity'] == 1.0
bad['modes']['normal']['mode_index'] = -1.75
assert by_name(native.parse(bad))['mode_index'] == -1
bad = make_configuration()
bad['modes'][None] = bad['modes'].pop('siegeMode')
assert len(native.parse(bad)['modes']) == 2, 'Original dispatch does not restrict mode-key type'
bad = make_configuration()
bad['modes'] = []
invalid(bad, TypeError)  # empty iteration, then f84e80 needs modes.normal
print('Dictionary, bool, numeric-slot, Win32 integer and optional key-metadata guards passed')


def differential(configuration, count, label):
    try:
        expected = model.read(configuration, count)
    except model.Reject:
        try:
            invalid(configuration, (ValueError, TypeError))
        except AssertionError:
            raise AssertionError('Uncaught #1513 failure was accepted: %r' % (label,))
        return 'reject'
    actual = native.parse(configuration)
    if actual != expected:
        for key in sorted(expected):
            if actual.get(key) != expected[key]:
                raise AssertionError('Catch region mismatch %r at %r:\n%r\n%r' % (
                    label, key, actual.get(key), expected[key]))
    return 'accept'


# Every source path, missing and mistyped, against the native catch regions.
paths = []

def walk(value, prefix=()):
    for key, item in value.items():
        paths.append(prefix + (key,))
        if isinstance(item, dict):
            walk(item, prefix + (key,))

walk(make_configuration())
outcomes = {'accept': 0, 'reject': 0}
for count, groups in ((1, None), (4, [[0, 1], [2], [3], [9]])):
    native.reset_registry()
    if groups:
        native.setup_materials(groups)
    for path in paths:
        for replacement in (None, 'x', [1, 2], {}):
            bad = make_configuration()
            target = bad
            for key in path[:-1]:
                target = target[key]
            if replacement is None:
                del target[path[-1]]
            else:
                target[path[-1]] = copy.deepcopy(replacement)
            outcomes[differential(bad, count, (count, path, replacement))] += 1
native.reset_registry()
assert outcomes['reject'] and outcomes['accept'] > 10 * outcomes['reject'], outcomes
print('Catch-region differential passed: %(accept)u accepted and %(reject)u rejected mutations' % outcomes)

# Spot checks of the region structure the differential relies on.
bad = make_configuration()
del bad['modes']['normal']['isRotationAroundCenter']
mode = by_name(native.parse(bad))
assert mode['body']['overspeed_resistance_factor'] == 11.125 and mode['body']['rotation_around_center'] is False
assert mode['body']['center_rotation_forward_speed'] == 4.0, 'f846c0 keeps constructor words after the failure'
assert mode['shape']['use_complex_form'] is False and mode['siege_mode_available'] is True, 'shape skipped'
bad = make_configuration()
del bad['modes']['normal']['shape']
invalid(bad, ValueError)
bad['modes']['normal']['gravity'] = 'x'
assert by_name(native.parse(bad))['shape']['auxiliary_clearance'] == 0, 'f8fd60 skips shape after f846c0 fails'
bad = make_configuration()
del bad['modes']['normal']['engine']['smplMinRPM']
engine = by_name(native.parse(bad))['engine']
assert engine['losses'] == [151, 152] and engine['rotation_by_lock_choker'] == 136.125
assert engine['simple_min_rpm'] is None and engine['start_rpm'] is None
bad = make_configuration()
del bad['modes']['normal']['gimlet']['pushHB']
result = native.parse(bad)
assert result['gimlet_fields'] == 4 and result['gimlet']['push_in_contact'] == 204.125
assert by_name(result)['body']['gimlet_pushed_handbrake'] == 203.125
bad = make_configuration()
del bad['modes']['normal']['anchor']['anchorVelFactor']
result = native.parse(bad)
assert result['anchor'] is None and result['rail_factor_in_contact'] is None and result['gimlet_fields'] == 0
bad = make_configuration()
bad['modes']['normal']['fakegearbox']['bkwdgears']['lowRpm'] = [1, 2]
assert native.parse(bad)['gears'] is None, 'f84e80 installs both directions or neither'
bad = make_configuration()
del bad['modes']['normal']['fakegearbox']
del bad['modes']['siegeMode']['anchor']
assert native.parse(bad)['gears'] is None and native.parse(bad)['anchor'] is not None
bad = make_configuration()
del bad['modes']['normal']
invalid(bad, ValueError)
print('Body prefix, shape skip, engine prefix, global prefix and gearbox all-or-nothing passed')

# GroundSettings: registry, f83340 items, f968d0 bounds and f96860 lookups.
surface = make_configuration()['modes']['normal']['chassis']['grounds'][0]
calls = []
class Number(object):
    def __init__(self, value, callback=None):
        self.value = value
        self.callback = callback
    def __float__(self):
        calls.append(self.value)
        if self.callback:
            self.callback()
        return float(self.value)


def ground_case(grounds, groups=None):
    native.reset_registry()
    if groups is not None:
        native.setup_materials(groups)
    count, unused = native.registry()
    cfg = make_configuration()
    cfg['modes']['normal']['chassis']['grounds'] = grounds
    expected = model.read(cfg, count)
    actual = native.parse(cfg)
    assert actual == expected, (grounds, by_name(actual)['chassis']['grounds'], by_name(expected)['chassis']['grounds'])
    return by_name(actual)['chassis']


def tagged(value):
    result = copy.deepcopy(surface)
    for name in ('firm', 'medium', 'soft'):
        result[name]['rollingFriction'] = value + {'firm': 0.5, 'medium': 0.25, 'soft': 0.125}[name]
    return result

groups = [[0], [1], [2]]
chassis = ground_case({0: tagged(10), 1: tagged(20), 2: tagged(30)}, groups)
assert chassis['grounds_complete'] is True
assert [entry['medium']['rolling_friction'] for entry in chassis['grounds']] == [10.25, 20.25, 30.25]
chassis = ground_case({1: tagged(20)}, groups)
assert chassis['grounds_complete'] is False and chassis['grounds'][0]['firm']['rolling_friction'] == model.word(0x3d8f5c29)
for kind in (-2, 3, 0x7fffffff, -0x80000000):
    chassis = ground_case({kind: tagged(40), 0: tagged(10)}, groups)
    assert chassis['grounds_complete'] is False and chassis['grounds'][0]['soft']['rolling_friction'] == 10.125
del calls[:]
probe = copy.deepcopy(surface)
probe['firm']['dirtCumulationRate'] = Number(1)
chassis = ground_case({-1: probe, 0: tagged(10)}, [[0]])
assert calls == [] and chassis['grounds_complete'] is False, '-1 skips every surface conversion'
chassis = ground_case({-1: {}, 0: tagged(10)}, [[0]])
assert chassis['grounds_complete'] is False and chassis['grounds'][0]['firm']['rolling_friction'] == 10.5
for broken in ({'firm': 1}, {'medium': {}}, dict(tagged(1), soft='x'), 'x', 7):
    if isinstance(broken, dict) and 'medium' in broken and not broken['medium']:
        broken = dict(tagged(1), medium={})
    chassis = ground_case({1: broken, 0: tagged(10), 2: tagged(30)}, groups)
    assert chassis['grounds'][1] == model.ground_entry() and chassis['grounds'][2]['firm']['rolling_friction'] == 30.5
partial = tagged(5)
del partial['soft']['rotationFactor']
chassis = ground_case({0: partial}, [[0]])
assert chassis['grounds'][0] == model.ground_entry(), 'an item installs only after all three surfaces'
class Kind(int):
    def __hash__(self):
        return id(self)
    def __eq__(self, other):
        return self is other
chassis = ground_case({Kind(0): tagged(1), Kind(0): tagged(2)}, [[0]])
assert chassis['grounds_complete'] is False, 'duplicates exceed the table count'
chassis = ground_case([tagged(1)], [[0]])
assert chassis['grounds_complete'] is False and chassis['grounds'] == [model.ground_entry()]
chassis = ground_case({}, [])
assert chassis['grounds'] == [] and chassis['grounds_complete'] is True
# A key that is not integral escapes f83340: entries already installed stay.
native.reset_registry()
native.setup_materials(groups)
cfg = make_configuration()
cfg['modes']['normal']['chassis']['grounds'] = dict([(0, tagged(10)), ('k', tagged(1)), (2, tagged(30))])
chassis = by_name(native.parse(cfg))['chassis']
assert by_name(model.read(cfg, 3))['chassis'] == chassis
assert chassis['grounds_complete'] is False and chassis['yaw_precise_distance'] == 116.125
native.reset_registry()
print('Ground tables: complete, partial, -1 skip, bounds, surface failures, duplicates and key escape passed')

native.reset_registry()
assert native.registry() == (1, [0] * 256), 'static count 1 and zero map before any setup'
native.setup_materials([[0, 1], (2,), [5, 1]])
count, groups = native.registry()
assert count == 3 and groups[0] == 0 and groups[1] == 2 and groups[2] == 1 and groups[5] == 2
assert groups.count(-1) == 252
for bad, label in ((5, 'not a sequence'), ([[0], 7], 'group'), ([[256]], 'kind'), ([[-1]], 'negative'),
                   ([[1.0]], 'float'), ([[1 << 40]], 'long'), ([['a']], 'string kind')):
    try:
        native.setup_materials(bad)
    except TypeError:
        pass
    else:
        raise AssertionError('wg_setupGroundMaterialsMap accepted %s' % label)
    assert native.registry() == (1, [-1] * 256), 'a rejected map leaves count 1 and every kind -1'
native.setup_materials([[True], ''])
assert native.registry()[0] == 2 and native.registry()[1][1] == 0
native.reset_registry()
print('wg_setupGroundMaterialsMap registry contract passed')

del calls[:]
bad = make_configuration()
bad['modes']['normal']['hullSize'] = [Number(1), object(), Number(3)]
body = by_name(native.parse(bad))['body']
assert calls == [], 'Vector3 must validate all three objects before converting any'
assert body['hull_inertia_factors'] == (1.5, 2.5, 3.5) and body['hull_size'] == model.default(model.V3, (0x40200000, 0x3f8ccccd, 0x40c00000))
bad = make_configuration()
bad['modes']['normal']['chassis']['roadWheelPositions'] = [Number(1), object(), 3, 4, 5]
assert by_name(native.parse(bad))['chassis']['road_wheel_positions'] == [0] * 5
assert calls == [1], 'Fixed arrays use the native per-element conversion order'

class IntegerOnly(object):
    def __int__(self):
        return 1
bad = make_configuration()
bad['modes']['normal']['gravity'] = IntegerOnly()
assert by_name(native.parse(bad))['body'] == model.defaults(model.BODY)

bad = make_configuration()
source = bad['modes']['normal']
number = Number(7, lambda: source.pop('gravity'))
identity = weakref.ref(number)
source['gravity'] = number
del number
result = native.parse(bad)
assert by_name(result)['body']['gravity'] == 7
assert identity() is None, 'A retired source numeric object leaked beyond the read'

bad = make_configuration()
source = bad['modes']
number = Number(8, source.clear)
bad['modes']['normal']['gravity'] = number
invalid(bad, ValueError)  # f84e80 then finds no modes.normal

class Raising(object):
    def __float__(self):
        raise ZeroDivisionError('conversion callback')
bad = make_configuration()
bad['modes']['normal']['chassis']['clearance'] = Raising()
invalid(bad, ZeroDivisionError)  # surfaced, not caught: see the Reader comment
del number
gc.collect()
print('Original conversion order and reentrant dictionary/numeric lifetime cases passed')

client = os.environ.get('WOT_0922_CLIENT')
if client:
    import zipfile
    import marshal
    import opcode
    import struct
    import types
    import collections
    archive = zipfile.ZipFile(os.path.join(client, 'res/packages/scripts.pkg'))
    code = marshal.loads(archive.read('scripts/common/physics_shared.pyc')[8:])
    environment = {'math': math, 'pi': math.pi, 'copy': copy, 'collections': collections,
                   'True': True, 'False': False, 'None': None}
    offset = 0
    selected = ('VEHICLE_TYPE_TANK', 'ANCHOR_MAX_REACTION_FACTOR', 'ANCHOR_CONST_FRACTION', 'ANCHOR_VEL_FACTOR')
    while offset < len(code.co_code):
        operation = ord(code.co_code[offset])
        offset += 1
        if operation >= opcode.HAVE_ARGUMENT:
            argument = ord(code.co_code[offset]) + 256 * ord(code.co_code[offset + 1])
            offset += 2
            if operation == opcode.opmap['LOAD_CONST'] and ord(code.co_code[offset]) == opcode.opmap['STORE_NAME']:
                name = code.co_names[ord(code.co_code[offset + 1]) + 256 * ord(code.co_code[offset + 2])]
                if name in selected:
                    environment[name] = code.co_consts[argument]
    assert code.co_code[718:721] == chr(opcode.opmap['LOAD_CONST']) + struct.pack('<H', 67)
    assert code.co_code[2029:2032] == chr(opcode.opmap['STORE_NAME']) + struct.pack('<H', 107)
    body = code.co_code[718:2032] + chr(opcode.opmap['LOAD_CONST']) + struct.pack('<H', code.co_consts.index(None)) + chr(opcode.opmap['RETURN_VALUE'])
    exec types.CodeType(0, 0, code.co_stacksize, 0, body, code.co_consts, code.co_names, (),
                        code.co_filename, 'pinned-default-dictionary-slice', 153, '') in environment
    defaults = environment['g_defaultTankXPhysicsCfg']
    cfg = make_configuration()
    mode = cfg['modes']['normal']
    for key in ('gravity', 'hullCOMShiftY', 'hullInertiaFactors', 'smplFwMaxSpeed', 'smplBkMaxSpeed',
                'comFrictionYOffs', 'enableSabilization', 'shape', 'anchor', 'rail', 'gimlet'):
        if isinstance(defaults[key], dict):
            mode[key].update(copy.deepcopy(defaults[key]))
        else:
            mode[key] = copy.deepcopy(defaults[key])
    result = native.parse(cfg)
    rounded = lambda value: struct.unpack('<f', struct.pack('<f', value))[0]
    assert by_name(result)['body']['hull_inertia_factors'] == tuple(rounded(value) for value in defaults['hullInertiaFactors'])
    assert by_name(result)['shape']['use_complex_form'] is defaults['shape']['useComplexForm']
    assert result['anchor']['constant_fraction'] == rounded(defaults['anchor']['anchorConstFraction'])
    print('Pinned physics_shared.pyc default-dictionary producer passed; authored boundaries remain explicit test inputs')
print('Physics configuration source Reader passed; no configure success or physical motion was claimed')
