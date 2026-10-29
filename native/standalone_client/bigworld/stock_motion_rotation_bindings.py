"""Optional prepared-copy seam for the synchronous rotation experiment."""
import importlib.util
from pathlib import Path

_spec = importlib.util.spec_from_file_location(
    'stock_offline_query_bindings', Path(__file__).with_name('stock_offline_query_bindings.py'))
_query_bindings = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_query_bindings)
once = _query_bindings.once


def runtime(source):
    anchor = '        active_reader = getattr(\n            self._destructibles, \'native_replacement_bsp_active\', None)\n'
    insertion = '''        from . import native_rotation_world
        if native_rotation_world._NATIVE_ROTATION_ENABLED:
            result = native_rotation_world.run(self, globals(), position,
                start_yaw, end_yaw, descriptor, pitch, roll, record_local,
                pivot_offset, contact_trace, include_static, translation)
            if result is not None:
                return result
'''
    source = once(source, anchor, insertion + anchor)
    suffix = '''
# Prepared-copy identities for the independent rotation entry.
_native_rotation_laws = tuple((globals(), name, globals()[name], globals()[name].func_code)
    for name in ('_angle_delta', '_destructible_rotation_interval_bbox',
        '_reference_rotation_interval_bbox', '_rotation_departing_contact',
        '_reference_rotation_departing_contact', '_destructible_world_sweep_descriptor'))
_native_rotation_laws += tuple((vehicle_physics.__dict__, name, getattr(vehicle_physics, name),
    getattr(vehicle_physics, name).func_code) for name in ('track_pivot_position',))
_native_rotation_laws += tuple((world_collision.__dict__, name, getattr(world_collision, name),
    getattr(world_collision, name).func_code) for name in ('_hull_pose_y', '_drivable_surface'))
from . import destructibles_sensor as _native_rotation_sensor
_native_rotation_producers = tuple((_native_rotation_sensor.__dict__, name,
    getattr(_native_rotation_sensor, name), getattr(getattr(_native_rotation_sensor, name), 'func_code', None))
    for name in ('_vehicle_body_bbox', '_vehicle_hull_bbox', 'native_replacement_bsp_active'))
_native_rotation_vector = getattr(BattleRuntime.__dict__['_vector'], 'im_func', BattleRuntime.__dict__['_vector'])
'''
    return source if suffix in source else source + suffix
