"""Independent prepared-rotation adapter; ordinary world queries stay unchanged."""
from __future__ import print_function

_NATIVE_ROTATION_ENABLED = True
_calls = {'admitted': 0, 'fallback': 0, 'failed_worlds': 0}


def snapshot():
    return dict(_calls)


def _fallback():
    _calls['fallback'] += 1
    return None


def run(owner, laws, position, start_yaw, end_yaw, descriptor, pitch=None,
        roll=None, record_local=True, pivot_offset=0.0, contact_trace=None,
        include_static=False, translation=(0.0, 0.0)):
    if not _NATIVE_ROTATION_ENABLED:
        return None
    pinned = laws.get('_native_rotation_laws', ())
    if not pinned or any(mapping.get(name) is not function or
            getattr(function, 'func_code', None) is not code
            for mapping, name, function, code in pinned):
        return _fallback()
    if owner._destructibles is not laws.get('_native_rotation_sensor'):
        return _fallback()
    producers = laws.get('_native_rotation_producers', ())
    if not producers or any(mapping.get(name) is not function or
            getattr(function, 'func_code', None) is not code
            for mapping, name, function, code in producers):
        return _fallback()
    vector = getattr(owner._vector, 'im_func', owner._vector)
    if vector is not laws.get('_native_rotation_vector'):
        return _fallback()
    try:
        import offline_rotation_world_native as backend
    except ImportError:
        return _fallback()
    import math
    active_reader = getattr(owner._destructibles, 'native_replacement_bsp_active', None)
    replacement_active = False
    if (not include_static and not pivot_offset and math.hypot(*translation) <= 1.e-9 and
            callable(active_reader)):
        replacement_active = bool(active_reader())
        if not replacement_active:
            return True
    if pitch is None:
        pitch = owner._local_pitch
    if roll is None:
        roll = owner._local_roll
    reader = getattr(owner._destructibles, '_vehicle_body_bbox', None)
    if not callable(reader):
        reader = getattr(owner._destructibles, '_vehicle_hull_bbox', None)
    if not callable(reader):
        return True
    bbox = reader(descriptor)
    if not isinstance(bbox, (tuple, list)) or len(bbox) < 2:
        return True
    if abs(laws['_angle_delta'](float(start_yaw), float(end_yaw))) <= 1.e-8:
        return True
    from . import native_math, world_collision as world, destructibles_sensor as sensor
    from .native_engine_query import EngineQuery, _native_query_begin
    query = getattr(owner, '_native_world_query', None)
    if query is not None:
        try:
            query._live()
        except RuntimeError:
            query = None
    if query is None:
        query = EngineQuery(owner, native_math._load())
        owner._native_world_query = query
    if (query._runtime.bigworld is not owner._runtime.bigworld or
            query._runtime.math is not owner._runtime.math or query._space != owner._avatar.spaceID):
        raise RuntimeError('World query owner does not match its live engine')
    V = owner._runtime.math.Vector3
    ground_provider = world.ground_collision_filter
    initial_ground = sensor.ground_filter_batch_1513(ground_provider)
    _native_query_begin(query, initial_ground, world)
    values = tuple(float(position[i]) for i in range(3)) + tuple(
        float(corner[i]) for corner in bbox[:2] for i in range(3)) + (
        float(start_yaw), float(end_yaw), float(pitch), float(roll), float(pivot_offset),
        float(translation[0]), float(translation[1]),
        float(laws['DESTRUCTIBLE_POSE_MAX_ANGLE_STEP']), float(world._drivable_surface.func_defaults[0]),
        laws['DESTRUCTIBLE_POSE_MAX_SWEEP_STEPS'], bool(include_static), callable(active_reader), replacement_active)
    tuning = (world._WORLD_SOFT_RECAST_BUDGET, world._GROUND_HIT_EPSILON, world._MAX_DRIVABLE_GRADIENT)
    space = owner._avatar.spaceID
    make_descriptor = laws['_destructible_world_sweep_descriptor']

    def make_world(row, body):
        query._live()
        x, y, z, yaw, speed, dt, pitch, roll, motion_yaw = row[:9]
        left, right, back, front = row[9:13]
        motion_yaw = motion_yaw if row[15] else None
        trace = dict(position=(x, y, z), yaw=yaw, speed=speed, dt=dt,
            motion_yaw=motion_yaw, pitch=pitch, roll=roll, airborne=bool(row[16]),
            extents=(max(abs(left), abs(right)), back, front), lateral_bounds=(left, right))
        state = [world._UNPREPARED_COLLISION_FILTER]
        crush = [False]
        current_descriptor = []
        ground_batch = sensor.ground_filter_batch_1513(ground_provider)
        _native_query_begin(query, ground_batch, world)

        def dispatch(op, rows):
            query._live()
            if op == 1:
                a, b = rows[0]
                state[0] = world._trace_collision_filter(
                    world.prepare_horizontal_collision_filter(V(*a), V(*b)), trace)
                return state[0]
            if op == 7:
                a, unused_end, unused_flags = rows[0]
                provider = (ground_batch if ground_batch is not None and
                    world.ground_collision_filter is ground_provider else world.ground_collision_filter)
                return provider(a[0], a[2])
            if op == 5:
                if not current_descriptor:
                    current_descriptor.append(make_descriptor(descriptor, body))
                result = []
                for a, b, hit in rows:
                    cleared = world._destroy_and_recast(space, V(*a), V(*b), hit,
                        yaw, speed, current_descriptor[0], crush, False, None, False, state[0])
                    query._live()
                    result.append(2 if cleared == 'kinetic' else 1 if cleared is True else 0)
                return tuple(result)
            if op == 6:
                for a, b, hit, reason, ground_ahead, profile in rows:
                    world._record_hard_contact(trace,
                        ('', 'raised_wall', 'ground_profile', 'solid_lane', 'upper_lane')[reason],
                        V(*a), V(*b), hit, ground_ahead, profile)
                return None
            raise RuntimeError('Unexpected rotation world operation: %s' % op)
        return dispatch, trace

    result = backend.run(values, query.capabilities, tuning, make_world)
    if result is None:
        return _fallback()
    _calls['admitted'] += 1
    clear, failures, blocking_failure, trace = result
    _calls['failed_worlds'] += failures
    if not clear:
        if contact_trace is not None:
            contact_trace.update(trace or {})
            contact_trace.update(pivot_offset=pivot_offset, start_yaw=start_yaw, end_yaw=end_yaw)
        if record_local:
            owner._local_world_collision_trace = trace or {}
            owner._local_motion_kinds = 'world'
            owner._local_motion_status = 'hard'
    return bool(clear)
