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


def numbers(names, start=1):
    return dict((name, index + start + 0.125) for index, name in enumerate(names.split()))


def make_mode(index):
    body = numbers('gravity fullMass hullCOMShiftY hullBoxOffsetZ turretTopWidth smplFwMaxSpeed smplBkMaxSpeed '
                   'smplRotSpeed angVelocityFactor allowedRPMExcess overspeedResistFactor centerRotationFwdSpeed '
                   'movementRevertSpeed comFrictionYOffs comSideFriction')
    body.update(mode_index=index, hullInertiaFactors=(1.5, 2.5, 3.5), hullSize=(4.5, 5.5, 6.5),
                turretTopPos=(7.5, 8.5, 9.5), isRotationAroundCenter=True, enableSabilization=False,
                siegeModeAvailable=True)
    body['shape'] = numbers('auxClearance tankBoardAngle tankAftChamferFraction tankFrontChamferFraction '
                            'terrBoardAngle terrFrontChamferFraction terrAftChamferFraction', 31)
    body['shape']['useComplexForm'] = True
    chassis = numbers('chassisMass chassisMassFraction clearance trackGaugeFactor trackToBeLockedDelay '
                      'wheelRadius wheelInertiaFactor wheelFwdInertiaFactor stiffness0 stiffness1 damping '
                      'wheelRestitution wheelPenetration brake rotationBrake wheelSinkageResistFactor '
                      'wheelDryFrictionFactor sideFrictionConstantRatio', 51)
    chassis.update(wheelDetachOnRoll=False, wheelUsePseudoContacts=True, flatSideFriction=False,
                   roadWheelPositions=[1, 2, 3, 4, 5], stiffnessFactors=(6, 7, 8, 9, 10))
    for name in ('slopeResistTerrain', 'slopeResistStaticObject', 'slopeResistDynamicObject'):
        chassis[name] = (11, 12, 13)
    for name in ('slopeGripLngTerrain', 'slopeGripSdwTerrain', 'slopeGripLngStaticObject',
                 'slopeGripSdwStaticObject', 'slopeGripLngDynamicObject', 'slopeGripSdwDynamicObject'):
        chassis[name] = [14, 15, 16, 17]
    surface = numbers('dirtCumulationRate dirtReleaseRate dirtSideVelocity maxDirt hbComSideFriction '
                      'sideFriction fwdFriction rollingFriction hbSideFrictionAddition rotationFactor', 71)
    chassis['grounds'] = {0: dict((name, copy.deepcopy(surface)) for name in ('firm', 'medium', 'soft')),
                          3: dict((name, copy.deepcopy(surface)) for name in ('firm', 'medium', 'soft'))}
    chassis['hullAiming'] = {
        'pitch': numbers('correctionCenterZ correctionSpeed pitchMin pitchMax correctionStiffness '
                         'correctionDamping correctionScale', 91),
        'yaw': numbers('gimletForce stiffness damping preciseRestitution dampingYawDist preciseYawDist', 111)}
    body['chassis'] = chassis
    engine = numbers('smplEnginePower engineInertia smplEngJoinRatio idleChoker rotationChoker '
                     'rotationByLockChoker smplMinRPM idleRPM startRPM engVelMax engVelBkMax engVelRot', 131)
    engine['engineLoses'] = (151, 152)
    body['engine'] = engine
    body['anchor'] = numbers('anchorMaxReactionFactor anchorConstFraction anchorVelFactor', 161)
    body['rail'] = {'railFactorInContact': 181.25}
    body['gimlet'] = numbers('wPushedDiag wPushedRot wPushedHB pushInContact pushHB pushStop '
                             'pushRotOnSpotFixedPeriod pushRotOnSpotGrowPeriod gimletPushOnSpotInit '
                             'gimletPushOnSpotFinal pushRotOnMoveFixedPeriod pushRotOnMoveGrowPeriod '
                             'gimletPushOnMoveInit gimletPushOnMoveFinal gimletVelScaleMin gimletVelScaleMax', 201)
    return body


def make_configuration():
    gears = {'switchSpeed': [1, 2, 3], 'switchHysteresis': (4, 5, 6),
             'lowRpm': [7, 8, 9], 'highRpm': (10, 11, 12)}
    return {'vehicleType': 0, 'modes': {'normal': make_mode(0), 'siegeMode': make_mode(1)},
            'fakegearbox': {'fwdgears': copy.deepcopy(gears), 'bkwdgears': copy.deepcopy(gears)}}


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


cfg = make_configuration()
parsed = native.parse(cfg)
normal = by_name(parsed)
assert parsed['vehicle_type'] == 0 and len(parsed['modes']) == 2
assert normal['body']['gravity'] == 1.125 and normal['body']['full_mass'] == 2.125
assert normal['body']['hull_size'] == (4.5, 5.5, 6.5)
assert normal['body']['hull_inertia_factors'] == (1.5, 2.5, 3.5)
assert normal['body']['turret_top_position'] == (7.5, 8.5, 9.5)
assert normal['body']['gimlet_pushed_rotation'] == 202.125
assert normal['body']['gimlet_pushed_handbrake'] == 203.125
assert normal['shape']['terrain_aft_chamfer_fraction'] == 37.125
assert normal['shape']['use_complex_form'] is True
assert normal['chassis']['wheel_use_pseudo_contacts'] is True
assert normal['chassis']['wheel_sinkage_resistance_factor'] == 66.125
assert normal['chassis']['road_wheel_positions'] == [1, 2, 3, 4, 5]
assert normal['chassis']['stiffness_factors'] == [6, 7, 8, 9, 10]
assert normal['chassis']['slope_grip_side_dynamic_object'] == [14, 15, 16, 17]
assert normal['chassis']['pitch_correction_center_z'] == 91.125
assert normal['chassis']['yaw_precise_distance'] == 116.125
assert set(ground['material_kind'] for ground in normal['chassis']['grounds']) == set([0, 3])
assert normal['chassis']['grounds'][0]['firm']['rolling_friction'] == 78.125
assert normal['engine']['losses'] == [151, 152]
assert normal['engine']['simple_min_rpm'] == 137.125, 'Reader must retain source RPM units'
assert normal['engine']['maximum_backward_angular_velocity'] == 141.125
assert parsed['anchor_maximum_reaction_factor'] == 161.125
assert parsed['rail_factor_in_contact'] == 181.25
assert parsed['gimlet']['maximum_velocity_scale'] == 216.125
assert parsed['forward_gears'][1] == {'switch_speed': 2, 'switch_hysteresis': 5, 'low_rpm': 8, 'high_rpm': 11}
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
for field in ('isRotationAroundCenter', 'enableSabilization', 'siegeModeAvailable'):
    bad = make_configuration()
    bad['modes']['normal'][field] = 1
    invalid(bad, TypeError)
bad = make_configuration()
bad['modes']['normal']['gravity'] = '9.81'
invalid(bad, TypeError)
bad['modes']['normal']['gravity'] = True
assert by_name(native.parse(bad))['body']['gravity'] == 1.0
bad['modes']['normal']['mode_index'] = -1.75
assert by_name(native.parse(bad))['mode_index'] == -1
bad = make_configuration()
bad['modes'][None] = bad['modes'].pop('siegeMode')
assert len(native.parse(bad)['modes']) == 2, 'Original dispatch does not restrict mode-key type'
print('Dictionary, bool, numeric-slot, Win32 integer and optional key-metadata guards passed')

for field in ('hullInertiaFactors', 'hullSize', 'turretTopPos'):
    bad = make_configuration()
    bad['modes']['normal'][field] = (1, 2)
    invalid(bad, ValueError)
for field in ('roadWheelPositions', 'stiffnessFactors'):
    bad = make_configuration()
    bad['modes']['normal']['chassis'][field] = [1, 2, 3, 4]
    invalid(bad, ValueError)
bad = make_configuration()
bad['fakegearbox']['fwdgears']['lowRpm'] = [1, 2]
invalid(bad, ValueError)
bad = make_configuration()
bad['modes']['normal']['engine']['engineLoses'] = {'0': 1, '1': 2}
invalid(bad, TypeError)
# Missing each required numeric/bool/dict/sequence key must fail atomically.
paths = []

def required_paths(value, prefix=()):
    for key, item in value.items():
        paths.append(prefix + (key,))
        if isinstance(item, dict):
            required_paths(item, prefix + (key,))

required_paths(make_configuration())
for path in paths:
    # A non-normal mode is optional, but every supplied mode is complete.
    if path == ('modes', 'siegeMode'):
        continue
    if len(path) == 5 and path[:1] == ('modes',) and path[2:4] == ('chassis', 'grounds'):
        continue
    if len(path) >= 3 and path[:2] == ('modes', 'siegeMode'):
        if path[2] in ('anchor', 'rail'):
            continue
        if len(path) >= 4 and path[2] == 'gimlet' and path[3] not in ('wPushedRot', 'wPushedHB'):
            continue
    bad = make_configuration()
    target = bad
    for key in path[:-1]:
        target = target[key]
    del target[path[-1]]
    try:
        invalid(bad, (ValueError, TypeError))
    except AssertionError:
        raise AssertionError('Required field did not fail: %r' % (path,))
print('Required-field, sequence-width, equal-gear-length and atomic failure cases passed')

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

bad = make_configuration()
bad['modes']['normal']['hullSize'] = [Number(1), object(), Number(3)]
invalid(bad, TypeError)
assert calls == [], 'Vector3 must validate all three objects before converting any'
bad = make_configuration()
bad['modes']['normal']['chassis']['roadWheelPositions'] = [Number(1), object(), 3, 4, 5]
invalid(bad, TypeError)
assert calls == [1], 'Fixed arrays use the native per-element conversion order'

class IntegerOnly(object):
    def __int__(self):
        return 1
bad = make_configuration()
bad['modes']['normal']['gravity'] = IntegerOnly()
invalid(bad, TypeError)

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
invalid(bad, ValueError)
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
    assert result['anchor_constant_fraction'] == rounded(defaults['anchor']['anchorConstFraction'])
    print('Pinned physics_shared.pyc default-dictionary producer passed; authored boundaries remain explicit test inputs')
print('Physics configuration source Reader passed; no configure success or physical motion was claimed')
