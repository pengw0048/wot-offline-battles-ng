"""Metadata-only seams for the pinned synchronous query consumer copy.

The fixed input and its gameplay algorithms are never edited. These tags
expose canonical closure captures to the native owner without guessing cells.
"""
from pathlib import Path

SENSOR_LAWS = (
    '_compiled_motion_skin_1513', '_owned_component_skin_1513',
    '_anonymous_original_surface_1513', '_point_in_world_box',
    '_segment_world_box_interval', '_projected_box_interval_1513',
    '_original_side_face_1513', '_instance_motion_envelope_1513',
    '_destructible_isolated_1513', 'is_excluded_1513',
    '_layout_repair_pending_1513', '_layout_generation_1513',
    '_get_destr_authority', '_broken_item_materials_1513',
    '_native_fragile_replacement_surface_1513', '_native_fragile_replaced_1513',
    '_destructible_bin_key', '_bin_keys_for_bounds',
    '_baked_bin_keys_for_bounds_1513', '_collision_region_revision_1513',
    '_spatial_revision_1513', '_spatial_cell_signature_1513',
    '_accepted_tree_collision_keys_1513', '_live_broken_collision_filter_1513',
    '_broken_collision_filter', 'ground_collision_filter',
    '_MotionSkinWitnesses1513', '_vector_dot', '_vector_cross', '_box_face_axes',
    'ground_filter_batch_1513', '_contact_members_for_bounds_1513',
)


def once(source, before, after):
    if source.count(after) == 1:
        return source
    if source.count(before) != 1:
        raise ValueError('Pinned native query metadata seam changed: ' + repr(before))
    return source.replace(before, after, 1)


def sensor(source):
    source = once(source,
        '\treturn reject_broken_skin\n\n\ndef ground_collision_filter',
        '\treject_broken_skin._native_query_filter = (1, broken, accepted_trees, None, None)\n'
        '\treturn reject_broken_skin\n\n\ndef ground_collision_filter')
    source = once(source,
        '\treturn reject_broken_skin\n\n\ndef prepare_horizontal_collision_filter',
        '\treject_broken_skin._native_query_filter = (0, member_ids, accepted_trees, authority, keep_native_surface)\n'
        '\treturn reject_broken_skin\n\n\ndef prepare_horizontal_collision_filter')
    source = once(source,
        '\treturn query\n\n\ndef _live_broken_collision_filter_1513',
        '\tquery._native_query_ground = (provider, cache)\n'
        '\treturn query\n\n\ndef _live_broken_collision_filter_1513')
    suffix = '\n# Canonical objects for the independent synchronous native query binding.\n_native_query_laws = tuple((globals(), name, globals()[name], getattr(globals()[name], \'func_code\', None)) for name in %r)\n' % (SENSOR_LAWS,)
    suffix += '''import types as _native_query_types
_native_query_filter_codes = tuple((mode, code) for mode, factory in
    ((0, _live_broken_collision_filter_1513), (1, _broken_collision_filter), (3, ground_filter_batch_1513))
    for code in factory.func_code.co_consts
    if isinstance(code, _native_query_types.CodeType) and code.co_name in ('reject_broken_skin', 'query'))
_native_query_keep_codes = tuple(code for code in _live_broken_collision_filter_1513.func_code.co_consts
    if isinstance(code, _native_query_types.CodeType) and code.co_name == 'keep_native_surface')
_native_query_witness_methods = tuple(getattr(getattr(_MotionSkinWitnesses1513, name), 'im_func',
    getattr(_MotionSkinWitnesses1513, name)) for name in ('__init__', 'prime', 'inside',
    'interval', 'footprint', 'projected', 'side'))
_native_query_geometry_methods = tuple(getattr(_native_geometry, name) for name in
    ('box_probe', 'motion_witnesses', 'box_envelope'))
_native_query_witness_codes = tuple(function.func_code for function in _native_query_witness_methods)
_native_query_geometry_codes = tuple(function.func_code for function in _native_query_geometry_methods)
'''

    if suffix not in source:
        source += suffix
    return source


def authority(source):
    suffix = '\n# Canonical ledger readers; native queries only read their plain data.\n_native_query_laws = tuple((globals(), name, globals()[name], globals()[name].func_code) for name in (\'is_destroyed\', \'destroyed_keys\'))\n'
    return source if suffix in source else source + suffix


def world(source):
    source = once(source, '    return observed_filter\n',
        '    observed_filter._native_query_filter = (2, collision_filter, candidates)\n'
        '    return observed_filter\n')
    suffix = '''\n_native_query_trace_codes = tuple((2, code) for code in
    _trace_collision_filter.func_code.co_consts if hasattr(code, 'co_name') and
    code.co_name == 'observed_filter')\n'''
    return source if suffix in source else source + suffix


ENGINE_SUFFIX = '''
# Metadata only. All collision/skin laws remain owned by the pinned module.
_NATIVE_QUERY_METHODS = tuple(getattr(getattr(EngineQuery, name), 'im_func',
    getattr(EngineQuery, name)) for name in ('_ray', '_skin', '_live', '_segment',
    '_scale', '_normalise', '_filter', '_alias', '_can_recast', '_skipped',
    '_invoke', '_recover_ground', '_remember'))
_NATIVE_QUERY_METHOD_CODES = tuple(function.func_code for function in _NATIVE_QUERY_METHODS)

def _native_query_bind(query):
    from . import destructibles_authority as authority
    if type(query) is not EngineQuery:
        return None
    laws = sensor._native_query_laws + authority._native_query_laws
    if any(mapping.get(name) is not function for mapping, name, function, code in laws):
        return None
    if tuple(getattr(getattr(EngineQuery, name), 'im_func',
        getattr(EngineQuery, name)) for name in ('_ray', '_skin', '_live', '_segment',
        '_scale', '_normalise', '_filter', '_alias', '_can_recast', '_skipped',
        '_invoke', '_recover_ground', '_remember')) != _NATIVE_QUERY_METHODS:
        return None
    from . import world_collision
    return (query.capabilities, sensor.__dict__, authority.__dict__, laws,
            globals(), _NATIVE_QUERY_METHODS, _native_query_flush,
            (query._vector, query._xyz, query._native_ray, query._skip_flags),
            sensor._native_query_filter_codes + world_collision._native_query_trace_codes)

def _native_query_begin(query, ground_batch, ground_owner=None):
    if query is None:
        return
    from . import worker_diagnostics
    query._native_query_ground = getattr(ground_batch, '_native_query_ground', None)
    query._native_query_ground_callable = ground_batch
    query._native_query_ground_owner = ground_owner
    query._native_query_ground_provider = getattr(ground_owner, 'ground_collision_filter', None)
    query._native_query_diagnostic = worker_diagnostics.current()

def _native_query_flush(query, skips, kept, traces, rays, counters):
    # Called only before a Python effect, or at the final synchronous result.
    if skips:
        sensor.g_offh_destr_ground_skips = getattr(sensor, 'g_offh_destr_ground_skips', 0) + skips
    for callback, surface in kept:
        callback(surface, (surface[3], surface[2]))
    for candidates, surface in traces:
        if len(candidates) < 16 and surface not in candidates:
            candidates.append(surface)
    diagnostic = getattr(query, '_native_query_diagnostic', None)
    if diagnostic is not None:
        for stage, space, start, end, elapsed in rays:
            diagnostic.geometry(stage, (space, start, end))
            # Native time was captured around the actual scene query.
            if diagnostic._stack:
                diagnostic._stack[-1][1] += elapsed
            diagnostic._add_stage(diagnostic._stages, stage, elapsed, elapsed)
            if diagnostic._actor_key in diagnostic._actors:
                diagnostic._add_stage(diagnostic._actors[diagnostic._actor_key]['stages'], stage, elapsed, elapsed)
        for name, value in counters:
            diagnostic.count(name, value)
'''


def engine(source):
    source = once(source, '\n    def _segment(self, start, end):',
        '\n        self._native_query_binding = _native_query_bind(self)\n'
        '\n    def _segment(self, start, end):')
    return source if ENGINE_SUFFIX in source else source + ENGINE_SUFFIX


def native_world(source):
    return once(source,
        '    def xyz(v):\n',
        '    if query is not None:\n'
        '        from .native_engine_query import _native_query_begin\n'
        '        _native_query_begin(query, ground_batch, module)\n'
        '    def xyz(v):\n')


def native_motion(source):
    return once(source,
        '        collision, crush = [world._UNPREPARED_COLLISION_FILTER], [False]\n',
        '        from .native_engine_query import _native_query_begin\n'
        '        _native_query_begin(query, ground_batch, world)\n'
        '        collision, crush = [world._UNPREPARED_COLLISION_FILTER], [False]\n')


ADAPTERS = {
    'native_engine_query.py': engine,
    'destructibles_sensor.py': sensor,
    'destructibles_authority.py': authority,
    'world_collision.py': world,
    'native_world.py': native_world,
    'native_motion_core.py': native_motion,
}
