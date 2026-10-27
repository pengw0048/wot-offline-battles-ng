"""Reference model of the #1513 configure Reader catch regions.

Key order, catch regions and constructor words come from the WorldOfTanks.exe
#1513 instructions (f846c0, f8fd60, f8fe40, f822a0, f83340, f830d0, f88700,
f7ecd0, f85c70, f97d30, f84e80 and the f900a0/f814d0/f81150 constructors),
written independently of the production Reader so the two can be diffed.
Fail is WGPhysics::PyProxyException; Reject is a failure no region catches.
"""
import struct


class Fail(Exception):
    pass


class Reject(Exception):
    pass


def f32(value):
    return struct.unpack('<f', struct.pack('<f', value))[0]


def word(bits):
    return struct.unpack('<f', struct.pack('<I', bits))[0]


NUMBERS = (bool, int, long, float)


def floating(value):
    if isinstance(value, NUMBERS):
        return f32(float(value))
    raise Fail()


def boolean(value):
    if type(value) is bool:
        return value
    raise Fail()


def integral(value):
    if isinstance(value, NUMBERS):
        return int(value)
    raise Fail()


def numbers(value, count=None):
    if not isinstance(value, (list, tuple)) or (count is not None and len(value) != count):
        raise Fail()
    return [floating(item) for item in value]


def child(value, key):
    if not isinstance(value, dict) or not dict.__contains__(value, key):
        raise Fail()
    return dict.__getitem__(value, key)


def items(value):
    return dict.items(value) if isinstance(value, dict) else []


F, B, V3 = 'f', 'b', 'v3'
# (source path, production field, conversion, f900a0 constructor word)
BODY = [
    ('gravity', 'gravity', F, 0x411cf5c3), ('fullMass', 'full_mass', F, 0x42100000),
    ('hullInertiaFactors', 'hull_inertia_factors', V3, (0x3f800000, 0, 0)),
    ('hullSize', 'hull_size', V3, (0x40200000, 0x3f8ccccd, 0x40c00000)),
    ('hullCOMShiftY', 'hull_com_shift_y', F, 0xbe99999a), ('hullBoxOffsetZ', 'hull_box_offset_z', F, 0),
    ('turretTopPos', 'turret_top_position', V3, (0, 0x3f8ccccd, 0xbecccccd)),
    ('turretTopWidth', 'turret_top_width', F, 0x3fc00000),
    ('smplFwMaxSpeed', 'simple_forward_max_speed', F, 0x41200000),
    ('smplBkMaxSpeed', 'simple_backward_max_speed', F, 0x40a00000),
    ('smplRotSpeed', 'simple_rotation_speed', F, 0x3f800000),
    ('angVelocityFactor', 'angular_velocity_factor', F, 0x3e99999a),
    ('allowedRPMExcess', 'allowed_rpm_excess', F, 0x3fa66666),
    ('overspeedResistFactor', 'overspeed_resistance_factor', F, 0x3f800000),
    ('isRotationAroundCenter', 'rotation_around_center', B, False),
    ('centerRotationFwdSpeed', 'center_rotation_forward_speed', F, 0x40800000),
    ('movementRevertSpeed', 'movement_revert_speed', F, 0x3f000000),
    ('comFrictionYOffs', 'com_friction_y_offset', F, 0x3f333333),
    ('comSideFriction', 'com_side_friction', F, 0x3f000000),
    ('enableSabilization', 'stabilization_enabled', B, True),
    (('gimlet', 'wPushedRot'), 'gimlet_pushed_rotation', F, 0x3f800000),
    (('gimlet', 'wPushedHB'), 'gimlet_pushed_handbrake', F, 0x3f800000)]
SHAPE = [('useComplexForm', 'use_complex_form', B, False)] + [
    (key, field, F, 0) for key, field in (
        ('auxClearance', 'auxiliary_clearance'), ('tankBoardAngle', 'tank_board_angle'),
        ('tankAftChamferFraction', 'tank_aft_chamfer_fraction'),
        ('tankFrontChamferFraction', 'tank_front_chamfer_fraction'),
        ('terrBoardAngle', 'terrain_board_angle'),
        ('terrFrontChamferFraction', 'terrain_front_chamfer_fraction'),
        ('terrAftChamferFraction', 'terrain_aft_chamfer_fraction'))]
CHASSIS = [
    ('chassisMass', 'chassis_mass', F, 0), ('chassisMassFraction', 'chassis_mass_fraction', F, 0x3e19999a),
    ('clearance', 'clearance', F, 0x3f4ccccd), ('trackGaugeFactor', 'track_gauge_factor', F, 0x3f800000),
    ('wheelDetachOnRoll', 'wheel_detach_on_roll', B, False),
    ('trackToBeLockedDelay', 'track_to_be_locked_delay', F, 0x3e99999a),
    ('wheelRadius', 'wheel_radius', F, 0x3ecccccd), ('wheelInertiaFactor', 'wheel_inertia_factor', F, 0x3f800000),
    ('wheelFwdInertiaFactor', 'wheel_forward_inertia_factor', F, 0x3f800000),
    ('stiffness0', 'stiffness_zero', F, 0x4384d333), ('stiffness1', 'stiffness_one', F, 0x3f800000),
    ('damping', 'damping', F, 0x401903af), ('wheelRestitution', 'wheel_restitution', F, 0x3e4ccccd),
    ('wheelPenetration', 'wheel_penetration', F, 0x3ca3d70a),
    ('wheelUsePseudoContacts', 'wheel_use_pseudo_contacts', B, False),
    ('brake', 'brake', F, 0x41200000), ('rotationBrake', 'rotation_brake', F, 0x41200000),
    ('wheelSinkageResistFactor', 'wheel_sinkage_resistance_factor', F, 0),
    ('wheelDryFrictionFactor', 'wheel_dry_friction_factor', F, 0x3f800000),
    ('sideFrictionConstantRatio', 'side_friction_constant_ratio', F, 0),
    ('roadWheelPositions', 'road_wheel_positions', 5, [0] * 5),
    ('stiffnessFactors', 'stiffness_factors', 5, [0x3f800000] * 5)] + [
    (key, field, width, None) for key, field, width in (
        ('slopeResistTerrain', 'slope_resistance_terrain', 3),
        ('slopeResistStaticObject', 'slope_resistance_static_object', 3),
        ('slopeResistDynamicObject', 'slope_resistance_dynamic_object', 3),
        ('slopeGripLngTerrain', 'slope_grip_forward_terrain', 4),
        ('slopeGripSdwTerrain', 'slope_grip_side_terrain', 4),
        ('slopeGripLngStaticObject', 'slope_grip_forward_static_object', 4),
        ('slopeGripSdwStaticObject', 'slope_grip_side_static_object', 4),
        ('slopeGripLngDynamicObject', 'slope_grip_forward_dynamic_object', 4),
        ('slopeGripSdwDynamicObject', 'slope_grip_side_dynamic_object', 4))] + [
    ('flatSideFriction', 'flat_side_friction', B, True)]
AIMING = [(('hullAiming', 'pitch', key), field, F, bits) for key, field, bits in (
    ('correctionCenterZ', 'pitch_correction_center_z', 0), ('correctionSpeed', 'pitch_correction_speed', 0x3f000000),
    ('pitchMin', 'pitch_min', 0xbe32b8c3), ('pitchMax', 'pitch_max', 0x3e32b8c3),
    ('correctionStiffness', 'pitch_correction_stiffness', 0x42480000),
    ('correctionDamping', 'pitch_correction_damping', 0x3e4ccccd),
    ('correctionScale', 'pitch_correction_scale', 0x3f000000))] + [
    (('hullAiming', 'yaw', key), field, F, bits) for key, field, bits in (
        ('gimletForce', 'yaw_gimlet_force', 0x40800000), ('stiffness', 'yaw_stiffness', 0x461c4000),
        ('damping', 'yaw_damping', 0x44610000), ('preciseRestitution', 'yaw_precise_restitution', 0x3e99999a),
        ('dampingYawDist', 'yaw_damping_distance', 0x3cf5c28f),
        ('preciseYawDist', 'yaw_precise_distance', 0x3bc49ba6))]
ENGINE = [('engineLoses', 'losses', 2, None)] + [
    (key, field, F, bits) for key, field, bits in (
        ('smplEnginePower', 'simple_engine_power', 0x447a0000), ('engineInertia', 'engine_inertia', 0x3bc49ba6),
        ('smplEngJoinRatio', 'simple_engine_join_ratio', 0), ('idleChoker', 'idle_choker', 0x3dcccccd),
        ('rotationChoker', 'rotation_choker', 0x3f800000),
        ('rotationByLockChoker', 'rotation_by_lock_choker', 0x3f800000),
        ('smplMinRPM', 'simple_min_rpm', None), ('idleRPM', 'idle_rpm', None), ('startRPM', 'start_rpm', None),
        ('engVelMax', 'maximum_forward_angular_velocity', 0x43517084),
        ('engVelBkMax', 'maximum_backward_angular_velocity', 0x43374274),
        ('engVelRot', 'rotation_angular_velocity', 0x43374274))]
# f81150 table entry and f83340 temporaries share these words.
SURFACE = [(key, field, F, bits) for key, field, bits in (
    ('dirtCumulationRate', 'dirt_cumulation_rate', 0x3f800000), ('dirtReleaseRate', 'dirt_release_rate', 0x3f000000),
    ('dirtSideVelocity', 'dirt_side_velocity', 0x40200000), ('maxDirt', 'max_dirt', 0x40000000),
    ('hbComSideFriction', 'handbrake_com_side_friction', 0), ('sideFriction', 'side_friction', 0x3f800000),
    ('fwdFriction', 'forward_friction', 0x3f800000), ('rollingFriction', 'rolling_friction', 0x3d8f5c29),
    ('hbSideFrictionAddition', 'handbrake_side_friction_addition', 0),
    ('rotationFactor', 'rotation_factor', 0x3f800000))]
GIMLET = [(key, field) for key, field in (
    ('wPushedDiag', 'pushed_diagonal_weight'), ('wPushedRot', 'pushed_rotation_weight'),
    ('wPushedHB', 'pushed_handbrake_weight'), ('pushInContact', 'push_in_contact'), ('pushHB', 'push_handbrake'),
    ('pushStop', 'push_stop'), ('pushRotOnSpotFixedPeriod', 'on_spot_fixed_period'),
    ('pushRotOnSpotGrowPeriod', 'on_spot_grow_period'), ('gimletPushOnSpotInit', 'on_spot_initial_push'),
    ('gimletPushOnSpotFinal', 'on_spot_final_push'), ('pushRotOnMoveFixedPeriod', 'on_move_fixed_period'),
    ('pushRotOnMoveGrowPeriod', 'on_move_grow_period'), ('gimletPushOnMoveInit', 'on_move_initial_push'),
    ('gimletPushOnMoveFinal', 'on_move_final_push'), ('gimletVelScaleMin', 'minimum_velocity_scale'),
    ('gimletVelScaleMax', 'maximum_velocity_scale'))]


def default(kind, bits):
    if bits is None:
        return None
    if kind == B:
        return bits
    if kind == V3:
        return tuple(word(item) for item in bits)
    if isinstance(kind, int):
        return [word(item) for item in bits]
    return word(bits)


def defaults(specs):
    return dict((field, default(kind, bits)) for unused, field, kind, bits in specs)


def convert(value, kind):
    if kind == F:
        return floating(value)
    if kind == B:
        return boolean(value)
    if kind == V3:
        return tuple(numbers(value, 3))
    return numbers(value, kind)


def region(source, specs, out):
    """Assign each field in native order; a Fail keeps the parsed prefix."""
    for path, field, kind, unused in specs:
        node = source
        for key in (path if isinstance(path, tuple) else (path,)):
            node = child(node, key)
        out[field] = convert(node, kind)


def ground_entry():
    surface = defaults(SURFACE)
    return {'soft': dict(surface), 'medium': dict(surface), 'firm': dict(surface)}


def read_grounds(value, table):
    """f83340: per-item region; the key conversion escapes to f822a0."""
    complete, installed = True, 0
    for key, item in items(value):
        kind = integral(key)
        surfaces, ok = {}, kind != -1
        try:
            for name in ('firm', 'medium', 'soft'):
                if ok:
                    surfaces[name] = defaults(SURFACE)
                    try:  # f830d0
                        region(child(item, name), SURFACE, surfaces[name])
                    except Fail:
                        ok = False
        except Fail:
            complete = False
            continue
        if ok and 0 <= kind < len(table):  # f968d0 unsigned bound
            table[kind] = {'soft': surfaces['soft'], 'medium': surfaces['medium'], 'firm': surfaces['firm']}
            installed += 1
        else:
            complete = False
    return complete and installed == len(table)


def read_chassis(value, chassis):
    try:  # f822a0
        region(value, CHASSIS, chassis)
        chassis['grounds_complete'] = read_grounds(child(value, 'grounds'), chassis['grounds'])
    except Fail:
        pass
    try:  # f88700
        region(value, AIMING, chassis)
    except Fail:
        pass


def read_mode(key, value, count):
    mode = {'name': key if isinstance(key, str) else '', 'body': defaults(BODY), 'shape': defaults(SHAPE),
            'siege_mode_available': False, 'engine': defaults(ENGINE)}
    try:
        mode['mode_index'] = integral(child(value, 'mode_index'))
    except Fail:
        raise Reject()
    try:  # f846c0
        region(value, BODY, mode['body'])
        body = True
    except Fail:
        body = False
    if body:
        try:
            shape = child(value, 'shape')
        except Fail:
            raise Reject()
        try:  # f8fe40
            region(shape, SHAPE, mode['shape'])
        except Fail:
            pass
    try:  # f8fd60
        mode['siege_mode_available'] = boolean(child(value, 'siegeModeAvailable'))
    except Fail:
        pass
    chassis = defaults(CHASSIS + AIMING)
    chassis['grounds'] = [ground_entry() for unused in range(count)]
    chassis['grounds_complete'] = False
    try:  # f900a0 states 22-23
        read_chassis(child(value, 'chassis'), chassis)
    except Fail:
        pass
    mode['chassis'] = chassis
    try:  # f900a0 states 29-30, f7ecd0 has no region
        region(child(value, 'engine'), ENGINE, mode['engine'])
    except Fail:
        pass
    return mode


def read_gears(value):
    columns = [numbers(child(value, key)) for key in ('switchSpeed', 'switchHysteresis', 'lowRpm', 'highRpm')]
    if len(set(len(column) for column in columns)) != 1:
        raise Fail()
    return [{'switch_speed': a, 'switch_hysteresis': b, 'low_rpm': c, 'high_rpm': d}
            for a, b, c, d in zip(*columns)]


def read(root, count):
    """Expected parse of root with a registry of count groups, or Reject."""
    if not isinstance(root, dict):
        raise Reject()
    try:
        vehicle_type = integral(child(root, 'vehicleType'))
        modes = child(root, 'modes')
    except Fail:
        raise Reject()
    if vehicle_type != 0:
        raise Reject()
    result = {'vehicle_type': 0, 'modes': [read_mode(key, value, count) for key, value in items(modes)],
              'anchor': None, 'rail_factor_in_contact': None,
              'gimlet': dict((field, 0.0) for unused, field in GIMLET), 'gimlet_fields': 0, 'gears': None}
    try:  # f85c70 inside f900a0 state 32
        normal = child(child(root, 'modes'), 'normal')
        anchor = child(normal, 'anchor')
        values = [floating(child(anchor, key)) for key in
                  ('anchorMaxReactionFactor', 'anchorConstFraction', 'anchorVelFactor')]
        result['anchor'] = dict(zip(('maximum_reaction_factor', 'constant_fraction', 'velocity_factor'), values))
        result['rail_factor_in_contact'] = floating(child(child(normal, 'rail'), 'railFactorInContact'))
        gimlet = child(normal, 'gimlet')
        for index, (key, field) in enumerate(GIMLET):
            result['gimlet'][field] = floating(child(gimlet, key))
            result['gimlet_fields'] = index + 1
    except Fail:
        pass
    try:  # f84e80 looks up modes.normal before its region
        normal = child(child(root, 'modes'), 'normal')
    except Fail:
        raise Reject()
    try:
        box = child(normal, 'fakegearbox')
        result['gears'] = {'forward': read_gears(child(box, 'fwdgears')),
                           'backward': read_gears(child(box, 'bkwdgears'))}
    except Fail:
        pass
    return result


def lookup(groups, count, kind, strength):
    """f96860/f600d0: (table entry, surface) with soft 0, medium 1, firm 2."""
    group = groups[kind] if 0 <= kind <= 0xff else -1
    if group < 0 or group >= count:
        group = 0
    return group, {2: 1, 3: 0}.get(strength, 2)
