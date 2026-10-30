"""The native lifecycle ports match the pinned Python methods on identical fakes."""
import ast
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
RUNTIME = 'src/res/scripts/client/gui/mods/offline_lan_0922/battle_runtime.py'
METHODS = ('_pending_combat_for_record', '_apply_vehicle_statistics', '_death_attacker_engine_id',
           '_should_suppress_postmortem_killer', '_sync_fire_effect', '_stop_remote_visual',
           '_event_entity_key', '_remote_visual_components', '_store_remote_visual_components',
           '_apply_critical_state', '_apply_stun_state', '_apply_siege_state', '_present_critical',
           '_sync_remote_visual_components', '_set_record_spot_visibility', '_apply_spot_presentation',
           '_apply_turret_detachment', '_finalize_dead_remote_wreck', '_apply_health', '_apply_record_pose',
           '_flush_tombstone', '_destroy_entity', '_update_entity', '_apply_sync_event', '_queue_bot_create',
           '_flush_pending_bot_create', '_flush_pending_entities', '_create_remote', '_materialize_record',
           'on_events', '_prepare_ordered_event', '_event_is_ready', '_apply_ordered_event', '_drain_event_journal',
           '_known_event_state', '_merge_shot_event_state', '_merge_combat_event_state', '_merge_stun_event_state',
           '_combat_event_state', '_combat_attack_reason', '_validate_combat_event_contract',
           '_missing_projectile_attacker_allowed', '_pending_event_references', '_stun_entity_key',
           '_record_is_event_ready', '_combat_event_source', '_apply_combat_event', '_frame')
GLOBALS = ('_number', '_COMBAT_EVENT_KINDS', '_SHOT_EVENT_KINDS', '_SIMPLE_EVENT_KINDS')

FIXTURE = r'''
METHOD_NAMES = %r
import copy, math, sys
sys.path.insert(0, sys.argv[1])
import offline_client_lifecycle_native as native
NATIVE_QUERY = None
set_draw_visibility = None
ENGINE_MODE_IDLE = 0
exec(open(sys.argv[2]).read())

def rep(value):
    if callable(value) and not isinstance(value, Log) and hasattr(value, '__name__'):
        return '<callable %s>' % value.__name__
    if isinstance(value, tuple) and any(callable(item) for item in value):
        return '(' + ', '.join(rep(item) for item in value) + ')'
    return repr(value)

class Log(object):
    """Fake engine object: records every call and answers from a script."""
    def __init__(self, log, name, answers=None, missing=(), **attrs):
        self._log, self._name, self._answers, self._missing = log, name, answers or {}, missing
        self.__dict__.update(attrs)
    def __repr__(self):
        return '<%s>' % self._name
    def __getattr__(self, attr):
        if attr.startswith('__') or attr in self._missing:
            raise AttributeError(attr)
        def call(*args, **kwargs):
            self._log.append((self._name, attr) + tuple(rep(a) for a in args) +
                             tuple(sorted((k, rep(v)) for k, v in kwargs.items())))
            answer = self._answers.get(attr)
            return answer(*args) if callable(answer) else answer
        call.__name__ = '%s.%s' % (self._name, attr)
        return call

def battle(log, **extra):
    b = Pinned()
    b._worker_mode = extra.get('worker', False)
    b._binding = Log(log, 'binding')
    b._records = extra.get('records', {})
    b._event_journal = extra.get('journal', [])
    b._outlined_engine_id = extra.get('outlined')
    vehicle = Log(log, 'vehicle')
    b._remote_factory = Log(log, 'factory', {'get': lambda engine: vehicle})
    b._clear_target_outline = lambda: log.append(('battle', 'clear_outline'))
    return b

def run(name, make, args):
    """Run pinned and native on deep-copied inputs; compare result, mutations and calls."""
    out = []
    for impl in ('pinned', 'native'):
        log = []
        b, values = make(log)
        if impl == 'pinned':
            # The method under test, even where the fixture stubs it for its callers.
            fn = Pinned.__dict__[name].__get__(b)
            try:
                result = ('ok', fn(*values))
            except Exception as error:
                result = ('error', type(error).__name__, str(error))
        else:
            try:
                result = ('ok', NATIVE[name](b, *values))
            except Exception as error:
                result = ('error', type(error).__name__, str(error))
        battle_after = tuple(repr(b.__dict__.get(name, '-')) for name in (
            '_local_siege_pending', '_targeting_signature', '_local_physics', '_local_suspension_params',
            '_local_suspension_probe_trace', '_local_suspension_disabled', '_local_suspension_pitch_velocity',
            '_last_health', '_local_speed', '_event_journal', '_accepted_event_ids', '_applied_event_ids',
            '_projectile_lineage', '_pending_bot_creates', '_local_last_attacker', '_last_frame_time',
            '_worker_frame_callbacks', '_worker_probe_simulation_caps', '_soft_static_recast_budget', '_offframe_seconds',
            '_worker_probe_authority_callbacks', '_worker_probe_bot_generated', '_worker_probe_bot_enqueued',
            '_worker_probe_bot_send_failed', '_worker_probe_bot_count', '_local_frame_stages'))
        log.append(('after',) + battle_after)
        probe = getattr(b, '_entity_probe', None)
        entity_after = None if probe is None else tuple(repr(probe.__dict__.get(name)) for name in (
            'stunInfo', '_offlineStunFactors', '_spot_visible', '_offlineNativeDrawVisible', 'targetCaps',
            '_offlineNativeMarkerVisible', 'health', 'isCrewActive', 'last_killer_id'))
        identities = [str(id(v)) for v in values if isinstance(v, dict)]
        def scrub(value):
            text = repr(value)
            for identity in identities:
                text = text.replace(identity, 'ID')
            return text
        out.append((scrub(result), scrub(log), scrub([v for v in values if isinstance(v, dict)]),
                    scrub(b._records), entity_after))
    (pr, pl, pv, prec, pm), (nr, nl, nv, nrec, nm) = out
    TALLY.setdefault(name, [0, 0, 0])[0 if pr.startswith("('error'") else 1 if pl == '[]' else 2] += 1
    if pr.startswith("('error'"):
        # Pinned validation errors keep their type and message; other failures only need to fail.
        assert nr.startswith("('error'") and (not pr.startswith("('error', 'RuntimeError'") or pr == nr), (name, args, pr, nr)
    else:
        if (pr, pl, pv, prec, pm) != (nr, nl, nv, nrec, nm):
            for field, (x, y) in enumerate(zip((pr, pl, pv, prec, pm), (nr, nl, nv, nrec, nm))):
                if x != y:
                    x, y = str(x), str(y)
                    k = next((i for i in range(min(len(x), len(y))) if x[i] != y[i]), min(len(x), len(y)))
                    raise AssertionError('%s %r field %d\npinned: ...%s\nnative: ...%s' % (
                        name, args, field, x[max(0, k - 300):k + 300], y[max(0, k - 300):k + 300]))

KINDS = _COMBAT_EVENT_KINDS
TALLY = {}
NATIVE = {
    '_pending_combat_for_record': lambda b, r: native.pending_combat(b, r, KINDS),
    '_apply_vehicle_statistics': native.vehicle_statistics,
    '_death_attacker_engine_id': native.death_attacker_engine_id,
    '_should_suppress_postmortem_killer': native.suppress_postmortem_killer,
    '_sync_fire_effect': lambda b, e, burning=None: native.sync_fire_effect(b, e, burning),
    '_stop_remote_visual': native.stop_remote_visual,
    '_apply_critical_state': lambda b, r, payload, authority=None: native.critical_state(b, r, payload, authority, critical_damage),
    '_apply_stun_state': native.stun_state,
    '_apply_siege_state': lambda b, r, st: native.siege_state(b, r, st, vehicle_physics),
    '_present_critical': native.present_critical,
    '_sync_remote_visual_components': native.remote_visual_components,
    '_set_record_spot_visibility': lambda b, r, v, m=None: native.spot_visibility(b, r, v, m, NATIVE_QUERY, set_draw_visibility, ENGINE_MODE_IDLE),
    '_apply_spot_presentation': native.spot_presentation,
    '_apply_turret_detachment': lambda b, r, e, st=None: native.turret_detachment(b, r, e, st),
    '_finalize_dead_remote_wreck': native.finalize_dead_wreck,
    '_apply_health': lambda b, r, st, attacker_id=0, reason_id=None, force_cause=False, attack_reason_id=None,
                            suppress_combat_presentation=False, suppress_postmortem_killer=False: native.apply_health(
        b, r, st, attacker_id, reason_id, force_cause, attack_reason_id, suppress_combat_presentation,
        suppress_postmortem_killer, critical_damage, native_client_environment, native_client_drive),
    '_apply_record_pose': lambda b, r, pose: native.record_pose(b, r, pose, native_client_replica, _engine_rotation, BOT_MOVING_SPEED),
    '_flush_tombstone': native.flush_tombstone,
    '_destroy_entity': native.destroy_entity,
    '_update_entity': native.update_entity,
    '_apply_sync_event': native.sync_event,
    '_queue_bot_create': native.queue_bot_create,
    '_flush_pending_bot_create': native.flush_bot_create,
    '_flush_pending_entities': lambda b, now: native.flush_pending_entities(b, now, native_client_replica),
    '_create_remote': lambda b, e: native.create_remote(b, e, _marks_on_gun, _engine_rotation, bot_planner),
    '_materialize_record': lambda b, r: native.materialize_record(b, r, set_draw_visibility),
    'on_events': native.on_events,
    '_prepare_ordered_event': lambda b, e: native.prepare_ordered_event(b, e, globals()),
    '_event_is_ready': lambda b, e: native.event_is_ready(b, e, globals()),
    '_apply_ordered_event': lambda b, e: native.apply_ordered_event(b, e, globals()),
    '_drain_event_journal': lambda b: native.drain_event_journal(b, globals()),
    '_known_event_state': native.known_event_state,
    '_merge_shot_event_state': lambda b, e: native.merge_shot_event_state(b, e, globals()),
    '_merge_combat_event_state': native.merge_combat_event_state,
    '_merge_stun_event_state': native.merge_stun_event_state,
    '_combat_event_state': native.combat_event_state,
    '_combat_attack_reason': native.combat_attack_reason,
    '_validate_combat_event_contract': lambda b, e: native.validate_combat_event_contract(b, e, globals()),
    '_missing_projectile_attacker_allowed': native.missing_projectile_attacker_allowed,
    '_pending_event_references': native.pending_event_references,
    '_apply_combat_event': lambda b, e, update_state=True: native.apply_combat_event(b, e, update_state, globals()),
    '_frame': lambda b: native.frame(b, globals()),
}
PROBE_KINDS = ('visibility', 'lane', 'cover', 'ground', 'motion')
BOT_SOFT_RECAST_BUDGET = 8
FRAME_SECONDS = 0.0
def _xyz(value):
    return tuple(float(v) for v in value)
def _distance_2d(a, b):
    return math.hypot(a[0] - b[0], a[2] - b[2])
native_client_projectile = None
native_bot_runtime = None
_PROFILE_CLOCK = lambda: 5.0
spotting = Log([], 'spotting', SHOT_CAMOUFLAGE_SECONDS=0.75)
lan_protocol = Log([], 'lan_protocol', MAX_PROJECTILE_DAMAGE_STICKER=1000)
import types
def fake_module(name, value):
    sys.modules['gui.mods.offline_lan_0922.' + name] = value
    globals()[name] = value
for name in ('gui', 'gui.mods', 'gui.mods.offline_lan_0922'):
    sys.modules.setdefault(name, types.ModuleType(name))
bot_planner = None
def _marks_on_gun(value):
    return ('marks', value)
native_client_environment = native_client_drive = native_client_replica = None
BOT_MOVING_SPEED = 0.05
def _engine_rotation(yaw, pitch=0.0, roll=0.0):
    return ('rotation', yaw, pitch, roll)
vehicle_physics = None
critical_damage = None

def stub(b, log, name, answer=None):
    def call(*args, **kwargs):
        log.append(('battle', name) + tuple(rep(a) for a in args) + tuple(sorted((k, rep(v)) for k, v in kwargs.items())))
        return answer(*args) if callable(answer) else answer
    setattr(b, name, call)
cases = 0
frag_values = [0, 3, 3.7, '5', 'x', None, True]
for worker in (False, True):
    for frags in frag_values:
        for presented in (None, 0, 3, 5):
            for killer in (False, True, None):
                for presented_killer in (None, True):
                    def make(log, worker=worker, frags=frags, presented=presented, killer=killer, pk=presented_killer):
                        record = {'engine_id': 41}
                        if presented is not None: record['presented_frags'] = presented
                        if pk is not None: record['presented_team_killer'] = pk
                        state = {} if frags is None else {'frags': frags}
                        if killer is not None: state['team_killer'] = killer
                        return battle(log, worker=worker), (record, state)
                    run('_apply_vehicle_statistics', make, (worker, frags, presented, killer, presented_killer)); cases += 1
records = {'bot:7': {'engine_id': 70, 'state': {'team': 2}, 'spot_visible': False},
           'player:3': {'engine_id': 30, 'state': {'team': 1}},
           'bot:8': {'engine_id': 80, 'state': {'team': 1}, 'spot_visible': True}}
for kind in ('bot', 'player', None, u'bot'):
    for ident in (7, 3, '7', 'x', None, 99):
        def make(log, kind=kind, ident=ident):
            state = {'death_attacker_kind': kind}
            if ident is not None: state['death_attacker_id'] = ident
            return battle(log, records=copy.deepcopy(records)), (state,)
        run('_death_attacker_engine_id', make, (kind, ident)); cases += 1
for local in (True, False):
    for health in (0, 5, None, 'x'):
        for alive in (True, False, None):
            for attacker in (70, 30, 80, 0, None, 'q', 10**30):
                for team in (1, 2, None, 'z'):
                    def make(log, local=local, health=health, alive=alive, attacker=attacker, team=team):
                        victim = {'local': local}
                        state = {}
                        if health is not None: state['health'] = health
                        if alive is not None: state['alive'] = alive
                        if team is not None: state['team'] = team
                        return battle(log, records=copy.deepcopy(records)), (victim, state, attacker)
                    run('_should_suppress_postmortem_killer', make, (local, health, alive, attacker, team)); cases += 1
journal_rows = [{'kind': 'hit', 'target_bot': 7}, {'kind': 'hit', 'target': 3, 'cosmetic_only': True},
                {'kind': 'shot', 'target_bot': 7}, {'kind': 'health', 'target': 3}, {'kind': 'bot_hit'}]
for subset in range(1 << len(journal_rows)):
    for target in ('bot:7', 'player:3', 'bot:8'):
        def make(log, subset=subset, target=target):
            recs = copy.deepcopy(records)
            journal = [dict(row) for i, row in enumerate(journal_rows) if subset >> i & 1]
            return battle(log, records=recs, journal=journal), (recs[target],)
        run('_pending_combat_for_record', make, (subset, target)); cases += 1
for has_fire in (True, False):
    for running in (True, False):
        for burning in (None, True, False, 0, 1):
            for on_fire in (True, False, None):
                for compound in (True, False):
                    def make(log, has_fire=has_fire, running=running, burning=burning, on_fire=on_fire, compound=compound):
                        extra = Log(log, 'fire', {'isRunningFor': lambda e: running})
                        extras = {'fire': extra} if has_fire else {}
                        attrs = {'typeDescriptor': Log(log, 'descriptor', extrasDict=extras),
                                 'appearance': Log(log, 'appearance', compoundModel=object() if compound else None)}
                        if on_fire is not None: attrs['is_on_fire'] = on_fire
                        entity = Log(log, 'entity', **attrs)
                        args = (entity,) if burning is None else (entity, burning)
                        return battle(log), args
                    run('_sync_fire_effect', make, (has_fire, running, burning, on_fire, compound)); cases += 1
for outlined in (41, 5, None):
    for components in ({}, {'visual_started': True}, {'world_marker_started': True},
                       {'minimap_started': True}, {'world_marker_started': True, 'minimap_started': True},
                       {'world_marker_started': False, 'visual_started': True}):
        for native_remote in (True, False):
            def make(log, outlined=outlined, components=components, native_remote=native_remote):
                record = dict(components, engine_id=41, native_remote=native_remote)
                return battle(log, outlined=outlined), (record,)
            run('_stop_remote_visual', make, (outlined, components, native_remote)); cases += 1
for canonical in (None, {'fire': True}):
    for authority in (None, {'critical_revision': 2}):
        for reconciled in (True, False):
            for local in (True, False):
                for existing in (None, {'fire': True}, {'fire': False}):
                    for entity_ok in (True, False):
                        for gun in (None, 'gun'):
                            for worker in (False, True):
                                def make(log, canonical=canonical, authority=authority, reconciled=reconciled, local=local,
                                         existing=existing, entity_ok=entity_ok, gun=gun, worker=worker):
                                    global critical_damage
                                    b = battle(log, worker=worker)
                                    critical_damage = Log(log, 'critical_damage', {'apply_payload': lambda e, c: ['event']})
                                    globals()['critical_damage'] = critical_damage
                                    stub(b, log, '_critical_state', lambda payload: None if canonical is None else dict(canonical))
                                    stub(b, log, '_reconcile_critical_authority', reconciled)
                                    stub(b, log, '_server_entity', Log(log, 'entity') if entity_ok else None)
                                    stub(b, log, '_advance_local_gun_edge', (0, 'edge'))
                                    stub(b, log, '_advance_local_gun_to')
                                    stub(b, log, '_present_critical')
                                    b._gun_state = gun
                                    record = {'engine_id': 41, 'local': local, 'state': {'health': 5}}
                                    if existing is not None: record['critical_state'] = existing
                                    args = (record, {'raw': 1}) if authority is None else (record, {'raw': 1}, authority)
                                    return b, args
                                run('_apply_critical_state', make, (canonical, authority, reconciled, local, existing, entity_ok, gun, worker)); cases += 1
for end in (0, 5000, -1, True, 2.5):
    for kind in ('', 'bot', u'player', 'x'):
        for attacker in (0, 7, True):
            for factors in (None, {'reload': 1.2, 'dispersion': 1.5}):
                for presented in ('none', 'same', 'other'):
                    for estimated in (None, 1000.0):
                        for local in (True, False):
                            for variant in range(4):
                                def make(log, end=end, kind=kind, attacker=attacker, factors=factors, presented=presented,
                                         estimated=estimated, local=local, variant=variant):
                                    worker, entity_ok, gun, native_cb = variant == 3, variant != 2, variant == 1, variant == 0
                                    b = battle(log, worker=worker)
                                    stub(b, log, '_clock', 50.0)
                                    stub(b, log, '_projectile_estimated_server_time', estimated)
                                    stub(b, log, '_server_clock', 200.0)
                                    entity = Log(log, 'entity', missing=() if native_cb else ('set_stunInfo',), stunInfo=1.5)
                                    stub(b, log, '_server_entity', entity if entity_ok else None)
                                    stub(b, log, '_advance_local_gun_edge'); stub(b, log, '_apply_current_reload_factor', True)
                                    stub(b, log, '_publish_reload_event'); stub(b, log, '_publish_targeting_info')
                                    b._gun_state = Log(log, 'gun', reload_time=1.0, reload_duration=4.0) if gun else None
                                    view = Log(log, 'view', STUN=12)
                                    b._runtime = Log(log, 'runtime', vehicle_view_state=view)
                                    feedback = Log(log, 'feedback')
                                    provider = Log(log, 'provider', shared=Log(log, 'shared', feedback=feedback))
                                    b._avatar = Log(log, 'avatar', guiSessionProvider=provider)
                                    state = {'stun_end_server_time_ms': end, 'stun_attacker_kind': kind, 'stun_attacker_id': attacker}
                                    if factors is not None: state['stun_factors'] = dict(factors)
                                    record = {'engine_id': 41, 'local': local}
                                    signature = (int(end), str(kind), int(attacker), tuple(sorted((factors or {}).items())))
                                    if presented == 'same': record['presented_stun_state'] = signature
                                    if presented == 'other': record['presented_stun_state'] = (1, 'bot', 2, ())
                                    return b, (record, state)
                                def make_checked(log, make=make):
                                    b, args = make(log)
                                    entity = b._server_entity(41)
                                    del log[:]
                                    original = b._server_entity
                                    b._server_entity = lambda engine: (log.append(('battle', '_server_entity', repr(engine))), entity)[1]
                                    b._entity_probe = entity
                                    return b, args
                                run('_apply_stun_state', make_checked, (end, kind, attacker, factors, presented, estimated, local, variant)); cases += 1
def spot_battle(log, worker=False, outlined=None, alive=True):
    global NATIVE_QUERY, set_draw_visibility
    NATIVE_QUERY = Log(log, 'query')
    set_draw_visibility = lambda vehicle, draw: log.append(('set_draw_visibility', repr(vehicle), repr(draw)))
    globals()['NATIVE_QUERY'], globals()['set_draw_visibility'] = NATIVE_QUERY, set_draw_visibility
    b = battle(log, worker=worker, outlined=outlined)
    stub(b, log, '_record_alive', alive); stub(b, log, '_present_vehicle_dead')
    stub(b, log, '_run_optional_feature', True); stub(b, log, '_sync_fire_effect')
    stub(b, log, '_spot_presentation_visibility', lambda entity, remembered, was_model_visible=False: (bool(remembered), not was_model_visible))
    provider = Log(log, 'provider')
    b._avatar = Log(log, 'avatar', guiSessionProvider=provider)
    return b
COMPONENTS = ({}, {'visual_started': True}, {'world_marker_started': True}, {'minimap_started': True},
              {'world_marker_started': True, 'minimap_started': True})
for components in COMPONENTS:
    for team in (True, False, 0):
        for world in (True, False):
            for native_remote in (True, False):
                for outlined in (41, None):
                    for alive in (True, False):
                        for failing in (None, 'start_vehicle_minimap', 'stop_vehicle_marker'):
                            def make(log, components=components, team=team, world=world, native_remote=native_remote, outlined=outlined, alive=alive, failing=failing):
                                b = spot_battle(log, outlined=outlined, alive=alive)
                                def boom(*a):
                                    raise RuntimeError('signal failed')
                                b._binding = Log(log, 'binding', dict(((failing, boom),)) if failing else {})
                                b._sync_remote_visual_components = Pinned._sync_remote_visual_components.__get__(b)
                                record = dict(components, engine_id=41, native_remote=native_remote)
                                vehicle = Log(log, 'vehicle')
                                b._entity_probe = vehicle
                                return b, (record, vehicle, team, world)
                            run('_sync_remote_visual_components', make, (components, team, world, native_remote, outlined, alive, failing)); cases += 1
for visible in (True, False, 1):
    for marker in (None, True, False):
        for transition in (None, True, False):
            for spot in (None, True, False):
                for presentation in ('ready', 'not_ready', 'none'):
                    for native_remote in (True, False):
                        for variant in range(5):
                            def make(log, visible=visible, marker=marker, transition=transition, spot=spot, presentation=presentation, native_remote=native_remote, variant=variant):
                                b = spot_battle(log, alive=variant != 1)
                                stub(b, log, '_sync_remote_visual_components')
                                attrs = {'model': None if variant == 4 else 'model', 'engineMode': (3, 1)}
                                vehicle = Log(log, 'vehicle', missing=('update_tracks',) if variant == 2 else (), **attrs)
                                b._remote_factory = Log(log, 'factory', {'get': lambda engine: vehicle})
                                record = {'engine_id': 41, 'native_remote': native_remote,
                                          'presentation': presentation != 'none', 'ready': presentation == 'ready',
                                          'state': {'health': 300, 'display_health': 280}}
                                if spot is not None: record['spot_visible'] = spot
                                if transition is not None: record['_spot_marker_transition'] = transition
                                if variant == 3: record['_spot_presentation_signature'] = (bool(visible), bool(marker), True, True)
                                if variant in (0, 3): record['deferred_health_presentation'] = True
                                b._entity_probe = vehicle
                                args = (record, visible) if marker is None and variant % 2 else (record, visible, marker)
                                return b, args
                            run('_set_record_spot_visibility', make, (visible, marker, transition, spot, presentation, native_remote, variant)); cases += 1
for alive in (True, False):
    for dead_known in (True, False, None):
        for remembered in (True, False):
            for spot in (True, False, None):
                for failing in (False, True):
                    def make(log, alive=alive, dead_known=dead_known, remembered=remembered, spot=spot, failing=failing):
                        b = spot_battle(log, alive=alive)
                        if failing:
                            def boom(record, visible):
                                raise RuntimeError('spot failed')
                            b._set_record_spot_visibility = boom
                        else:
                            stub(b, log, '_set_record_spot_visibility')
                        record = {'engine_id': 41}
                        if dead_known is not None: record['dead_marker_known'] = dead_known
                        if spot is not None: record['spot_visible'] = spot
                        return b, (record, Log(log, 'entity'), remembered)
                    run('_apply_spot_presentation', make, (alive, dead_known, remembered, spot, failing)); cases += 1
for worker in (False, True):
    for detached in (True, False, None):
        def make(log, worker=worker, detached=detached):
            b = battle(log, worker=worker)
            stub(b, log, '_mark_turret_detached'); stub(b, log, '_queue_detached_turret')
            entity = Log(log, 'entity', missing=() if detached is not None else ('isTurretDetached',), isTurretDetached=detached)
            return b, ({'engine_id': 41}, entity, {'health': 0})
        run('_apply_turret_detachment', make, (worker, detached)); cases += 1
for factory_kind in ('none', 'no_get', 'returns_none', 'vehicle'):
    for model in (True, False):
        for native_remote in (True, False):
            for spot in (None, True, False):
                for marker in (None, True):
                    for failing in (False, True):
                        def make(log, factory_kind=factory_kind, model=model, native_remote=native_remote, spot=spot, marker=marker, failing=failing):
                            b = battle(log)
                            vehicle = Log(log, 'vehicle', model='m' if model else None)
                            if factory_kind == 'none': b._remote_factory = None
                            elif factory_kind == 'no_get': b._remote_factory = Log(log, 'factory', missing=('get',))
                            else: b._remote_factory = Log(log, 'factory', {'get': lambda engine: None if factory_kind == 'returns_none' else vehicle})
                            if failing:
                                def boom(*a): raise ValueError('spot failed')
                                b._set_record_spot_visibility = boom
                            else:
                                stub(b, log, '_set_record_spot_visibility', 'spotted')
                            stub(b, log, '_warn_optional_failure')
                            record = {'engine_id': 41, 'native_remote': native_remote, 'wreck_known': 1, 'deferred_health_presentation': True,
                                      '_spot_presentation_signature': (1,)}
                            if spot is not None: record['spot_visible'] = spot
                            if marker is not None: record['spot_marker_visible'] = marker
                            entity = Log(log, 'entity', model='m' if model else None)
                            b._entity_probe = vehicle
                            return b, (record, entity)
                        run('_finalize_dead_remote_wreck', make, (factory_kind, model, native_remote, spot, marker, failing)); cases += 1
POSES = [{'x': 1.0, 'y': 2.0, 'z': 3.0, 'yaw': 0.5}, {'x': 1.0, 'y': 2.0, 'z': 3.0, 'yaw': 0.5, 'aim_yaw': 0.7, 'gun_pitch': -0.1,
         'pitch': 0.02, 'roll': 0.01, 'siege_state': 2}, {}, {'yaw': 0.5, 'siege_state': None}]
for pose_index in range(len(POSES)):
    for owned in (False, True):
        for local in (False, True):
            for history in ('none', 'same', 'stale_lifecycle', 'aim_only'):
                for state_index in range(4):
                    for variant in range(4):
                        def make(log, pose_index=pose_index, owned=owned, local=local, history=history, state_index=state_index, variant=variant):
                            global native_client_replica
                            b = battle(log)
                            native_client_replica = Log(log, 'replica', {'owns': lambda battle, record: owned})
                            globals()['native_client_replica'] = native_client_replica
                            b._generation = 3
                            stub(b, log, '_clock', 12.5); stub(b, log, '_vector', lambda xyz: ('vector',) + tuple(xyz))
                            stub(b, log, '_projectile_plain_pose', lambda xyz, pose: {'xyz': list(xyz)})
                            stub(b, log, '_remember_remote_track_turn', 0.25)
                            stub(b, log, '_update_bot_tracks')
                            stub(b, log, '_run_optional_feature', lambda name, fn, args: variant != 2)
                            vehicle = Log(log, 'vehicle', missing=('settle_motion',) if variant == 3 else ())
                            b._remote_factory = Log(log, 'factory', {'get': lambda engine: vehicle})
                            states = [{'speed': 0.0, 'alive': True, 'health': 100}, {'speed': 3.0, 'movement_dir': 1, 'siege_state': 1},
                                      {'alive': False, 'health': 0, 'rotation_dir': -1}, {}]
                            record = {'engine_id': 41, 'local': local, 'kind': ('bot', 'player', 'other', 'bot')[variant],
                                      'native_remote': variant != 1, 'state': dict(states[state_index]), 'presented_siege_state': 2}
                            pose = dict(POSES[pose_index])
                            p = pose
                            sig = (p.get('x', 0.0), p.get('y', 0.0), p.get('z', 0.0), p.get('yaw', 0.0), p.get('pitch', 0.0),
                                   p.get('roll', 0.0), p.get('aim_yaw', p.get('yaw', 0.0)), p.get('gun_pitch', 0.0))
                            if history != 'none':
                                record['_remote_pose_signature'] = sig
                                record['_remote_pose_lifecycle'] = (3, 41, 'IDENTITY')
                                record['_remote_track_state_signature'] = (0.0, True, 0.25)
                                if history == 'aim_only': record['_remote_aim_signature'] = 'old'
                            return b, (record, pose)
                        def identity(log, make=make):
                            b, (record, pose) = make(log)
                            if record.get('_remote_pose_lifecycle') == (3, 41, 'IDENTITY'):
                                record['_remote_pose_lifecycle'] = (3, 41, id(record)) if history != 'stale_lifecycle' else (2, 41, id(record))
                                if 'old' != record.get('_remote_aim_signature'):
                                    p = pose
                                    record['_remote_aim_signature'] = (record['_remote_pose_lifecycle'], p.get('yaw', 0.0), p.get('pitch', 0.0),
                                                                       p.get('aim_yaw', p.get('yaw', 0.0)), p.get('gun_pitch', 0.0), 2)
                            return b, (record, pose)
                        history_value = history
                        def bound(log, identity=identity, history=history):
                            return identity(log)
                        run('_apply_record_pose', bound, (pose_index, owned, local, history, state_index, variant)); cases += 1
ORCH_STUBS = ('_materialize_record', '_create_remote', '_queue_bot_create', '_update_entity', '_destroy_entity',
              '_flush_tombstone', '_clear_target_outline', '_stop_remote_visual', '_fallback_postmortem_viewpoint',
              '_apply_record_pose', '_report_memory', '_start_team_chat_when_roster_ready', '_present_vehicle_dead',
              '_set_record_spot_visibility', '_apply_siege_state', '_apply_vehicle_statistics', '_drain_event_journal',
              '_apply_stun_state', '_apply_critical_state', '_reconcile_critical_authority', '_apply_health')
def orch_battle(log, variant, records=None, worker=False):
    global native_client_replica, set_draw_visibility, bot_planner
    b = battle(log, worker=worker, records=records or {}, outlined=41 if variant % 3 == 1 else None)
    for name in ORCH_STUBS:
        stub(b, log, name)
    stub(b, log, '_pending_event_references', variant % 2 == 1)
    stub(b, log, '_server_entity', None if variant % 4 == 2 else Log(log, 'entity'))
    if variant % 5 == 3:
        def ref(engine): raise ReferenceError('gone')
        b._server_entity = ref
    b._records_revision = 4
    b._pending_bot_creates = {}
    b._pending_bot_create_order = []
    b.state = 'running'
    b._server = Log(log, 'server', missing=('forgetVehicleEnter', 'vehicleEnterStatus') if variant % 2 else (),
                    answers={'vehicleEnterStatus': lambda engine: (('completed', None), ('pending', None), ('failed', 'bad'))[variant % 3]})
    b._binding = Log(log, 'binding', {'is_vehicle_ready': lambda engine: variant % 4 != 1,
                                      'properties_from_compact_descr': lambda cd, team, name: {'publicInfo': {}, 'cd': cd}})
    vehicle = Log(log, 'vehicle', model=None if variant == 7 else 'model', missing=('_offlineNativeMarkerVisible',) if variant % 2 else (),
                  _offlineNativeMarkerVisible=variant % 3 == 0)
    b._remote_factory = Log(log, 'factory', {'get': lambda engine: vehicle, 'error': lambda engine: 'boom' if variant == 9 else None,
                                             'is_ready': lambda engine: variant % 3 != 2, 'create': lambda *a: None if variant == 8 else 77},
                            missing=('set_entity_interpolate_motion',) if variant == 5 else (), native_entities=variant % 2 == 0)
    b._bots = Log(log, 'bots', {'is_authority': lambda: variant % 2 == 0}) if variant != 6 else None
    native_client_replica = Log(log, 'replica', {'flush_pending': lambda battle, now: 'native' if variant == 4 else None})
    set_draw_visibility = lambda v, d: log.append(('set_draw_visibility', rep(v), rep(d)))
    bot_planner = Log(log, 'planner', {'bot_initially_visible': lambda team, own, flag: team == own})
    globals().update(native_client_replica=native_client_replica, set_draw_visibility=set_draw_visibility, bot_planner=bot_planner)
    b.client = Log(log, 'client', team=1)
    b._config = {'vehicle': 'ussr:T-34', 'startupTimeoutSeconds': 20.0}
    b._start_message = {'bot_authority_id': -1} if variant % 2 else None
    stub(b, log, '_clock', 100.0); stub(b, log, '_vector', lambda xyz: ('vector', tuple(xyz)))
    stub(b, log, '_resolve_player_descriptor', Log(log, 'pdesc', {'makeCompactDescr': lambda: 'PCD'}, maxHealth=900))
    stub(b, log, '_resolve_descriptor', Log(log, 'desc', {'makeCompactDescr': lambda: 'CD'}, maxHealth=700))
    stub(b, log, '_remote_outfit', 'outfit'); stub(b, log, '_lan_prebattle_id', 5)
    stub(b, log, '_state_world_pose', ((1.0, 2.0, 3.0), 0.5)); stub(b, log, '_bot_authority_is_local', variant % 3 == 0)
    stub(b, log, '_projectile_boundary_pose', lambda pos, seed: {'pos': pos})
    stub(b, log, '_record_alive', variant % 2 == 0); stub(b, log, '_pending_combat_for_record', variant == 3)
    stub(b, log, '_death_attacker_engine_id', 12); stub(b, log, '_should_suppress_postmortem_killer', variant % 2 == 1)
    stub(b, log, '_ammo_bay_special_health', -7)
    b._worker_mode = worker
    b._bots_ready_reported = variant == 2
    b._bot_create_started = None if variant % 2 else 1.0
    b._last_bot_create_team = None if variant < 3 else 2
    return b
RECORD_SHAPES = [
    {'engine_id': 41, 'presentation': True, 'native_remote': True, 'arena_added': True, 'ready': True},
    {'engine_id': 41, 'presentation': True, 'native_remote': False, 'ready': False, 'state': {'team_killer': True}},
    {'engine_id': 41, 'presentation': False, 'ready': True, 'state': {'health': 3}},
    {'engine_id': 41, 'presentation': False, 'ready': False, 'pending_pose': {'x': 1}},
    {'engine_id': 41, 'local': True, 'state': {'health': 5}},
    {'engine_id': 41, 'tombstone': True, 'presentation': True, 'visible_destroy_requested': False},
    {'engine_id': 41, 'kind': 'player', 'presentation': True, 'ready': True, 'arena_added': False,
     'state': {'critical_revision': 1, 'critical_base_revision': 1, 'critical_ack_seq': 1, 'death_reason': 2}},
    {'engine_id': 41, 'presentation': True, 'ready': True, 'state': {'critical': {'fire': True}, 'death_reason': None}},
]
EVENTS = [
    {'type': 'destroy', 'entity': 'bot:4', 'kind': 'bot', 'keep_corpse': True, 'state': {'frags': 1}},
    {'type': 'destroy', 'entity': 'bot:4', 'kind': 'bot'},
    {'type': 'update', 'entity': 'bot:4', 'kind': 'bot', 'state': {'health': 7}, 'pose': {'x': 1.0}, 'presentation_time_us': 55},
    {'type': 'update', 'entity': 'bot:4', 'kind': 'player', 'interpolated': True, 'pose': {'x': 2.0}, 'presentation_bracket': [1, 2]},
    {'type': 'update', 'entity': 'bot:4', 'kind': 'bot', 'interpolated': True},
    {'type': 'create', 'entity': 'bot:4', 'kind': 'bot', 'id': 4, 'state': {'team': 2, 'slot': 1, 'x': 1.0, 'z': 2.0}, 'pose': {'yaw': 0.3, 'q': 9}},
    {'type': 'create', 'entity': 'player:9', 'kind': 'player', 'id': 9,
     'state': {'team': 1, 'slot': 3, 'vehicle_compact_descr': 'abc', 'marks_on_gun': 2, 'shot_penalty_until': 4.5}, 'initial_state': {'health': 1200}},
    {'type': 'create', 'entity': 'bot:5', 'kind': 'bot', 'id': 5, 'state': {'team': 1}},
    {'type': 'other', 'entity': 'bot:4'},
]
for event_index in range(len(EVENTS)):
    for shape in range(len(RECORD_SHAPES) + 2):
        for variant in range(10):
            for worker in (False, True):
                def make(log, event_index=event_index, shape=shape, variant=variant, worker=worker):
                    records = {}
                    if shape < len(RECORD_SHAPES):
                        records['bot:4'] = copy.deepcopy(RECORD_SHAPES[shape])
                    b = orch_battle(log, variant, records, worker)
                    if shape == len(RECORD_SHAPES):
                        b._pending_bot_creates['bot:4'] = {'state': {'team': 2}}
                        b._pending_bot_create_order[:] = ['bot:9', 'bot:4']
                    if variant == 6: b.state = 'leaving'
                    return b, (copy.deepcopy(EVENTS[event_index]),)
                for name in ('_destroy_entity', '_update_entity', '_apply_sync_event', '_queue_bot_create', '_create_remote'):
                    run(name, make, (event_index, shape, variant, worker)); cases += 1
for shape in range(len(RECORD_SHAPES)):
    for variant in range(10):
        for worker in (False, True):
            def make(log, shape=shape, variant=variant, worker=worker):
                record = copy.deepcopy(RECORD_SHAPES[shape])
                b = orch_battle(log, variant, {'bot:4': record}, worker)
                return b, (record,)
            run('_flush_tombstone', make, (shape, variant, worker)); cases += 1
            run('_materialize_record', make, (shape, variant, worker)); cases += 1
PENDING = [{}, {'bot:1': {'state': {'team': 1}}, 'bot:2': {'state': {'team': 2}}}, {'bot:1': {'state': {}}}, {'bot:1': None}]
for pending_index in range(len(PENDING)):
    for order in ([], ['bot:1'], ['bot:1', 'bot:2'], ['bot:2', 'bot:1', 'bot:3']):
        for variant in range(10):
            def make(log, pending_index=pending_index, order=order, variant=variant):
                records = {'bot:2': {'engine_id': 2}} if variant == 9 else {}
                b = orch_battle(log, variant, records)
                b._pending_bot_creates = copy.deepcopy(PENDING[pending_index])
                b._pending_bot_create_order = list(order)
                def create(event):
                    log.append(('battle', '_create_remote', rep(event)))
                    if variant % 2 == 0 and event and event.get('state') is not None:
                        b._records[event.get('entity', 'bot:1') if 'entity' in event else order[0]] = {'engine_id': 1}
                b._create_remote = create
                return b, (50.0,)
            run('_flush_pending_bot_create', make, (pending_index, order, variant)); cases += 1
for variant in range(10):
    for now in (10.0, 200.0):
        def make(log, variant=variant, now=now):
            records = dict(('bot:%d' % i, dict(copy.deepcopy(RECORD_SHAPES[i]), ready_deadline=100.0 if i % 2 else None))
                           for i in range(len(RECORD_SHAPES)))
            b = orch_battle(log, variant, records)
            stub(b, log, '_materialize_record', variant % 3 == 0)
            return b, (now,)
        run('_flush_pending_entities', make, (variant, now)); cases += 1
def event_battle(log, variant=0, worker=False, records=None, journal=None):
    b = battle(log, worker=worker, records=records if records is not None else {
        'bot:7': {'engine_id': 70, 'ready': True, 'presentation': True, 'arena_added': True, 'state': {'health': 300, 'alive': True}},
        'player:3': {'engine_id': 30, 'ready': True, 'state': {'health': 500}, 'presentation_time_us': 900},
        'bot:8': {'engine_id': 80, 'ready': False, 'state': {'health': 0, 'alive': False}}},
        journal=journal if journal is not None else [])
    b._pending_bot_creates = {'bot:9': {'state': {'team': 1}}}
    b._projectile_lineage = set(['p1'])
    b._accepted_event_ids = set(['seen'])
    b._applied_event_ids = set()
    b._projectile_meta = {'p2': {'bot_presentation_time_us': 100, 'bot_visual_launch_time': 1.0 if variant % 2 else None}}
    b._replay_mode = variant == 3
    b.state = 'leaving' if variant == 9 else 'running'
    b._start_message = {'round_id': 4} if variant % 2 else None
    b._last_snapshot = {'tick': 1} if variant % 3 else None
    b._bots = Log(log, 'bots') if variant % 4 else None
    for name in ('_ignore_live_payload', '_observe_projectile_message', '_show_shot', '_accept_projectile_event',
                 '_apply_combat_event', '_apply_vehicle_statistics_event', '_apply_environment_critical_feedback',
                 '_apply_assist_event', '_apply_detection_event', '_apply_stun_state', '_apply_projectile_ricochet_event',
                 '_apply_projectile_terminal_event', '_apply_battle_result', '_set_projectile_epoch'):
        stub(b, log, name)
    def observe(message):
        log.append(('battle', '_observe_destructibles_disabled', rep(message)))
        if variant == 7: raise ValueError('observe failed')
    b._observe_destructibles_disabled = observe
    stub(b, log, '_apply_destructible_event', None if variant == 5 else True)
    stub(b, log, '_reconcile_bot_authority', variant % 2 == 0)
    stub(b, log, '_clock', 3.0)
    stub(b, log, '_critical_state', lambda c: dict(c, canonical=True))
    stub(b, log, '_attack_reason', lambda name, default: default)
    stub(b, log, '_projectile_wire_meta', lambda e: {'projectile_id': 'p9'} if e.get('projectile_id') else None)
    fake_module('native_client_environment', Log(log, 'environment'))
    fake_module('native_bot_runtime', Log(log, 'bot_runtime'))
    return b
COMBAT_EVENTS = [
    {'kind': 'hit', 'source': 'shot', 'attack_reason': 0, 'attacker_bot': 7, 'target': 3, 'health': 250, 'death_reason': 0, 'damage': 50,
     'shot_result': 1, 'critical': {'fire': True}, 'critical_revision': 4, 'combat_revision': 2},
    {'kind': 'bot_hit', 'source': 'shot', 'attack_reason': 0, 'attacker': 3, 'target_bot': 7, 'health': 0, 'dead': True,
     'death_reason': 0, 'critical': {'ammo_rack_death': True}, 'display_health': 20, 'blocked_damage': 30, 'shot_result': 1},
    {'kind': 'health', 'source': 'fire', 'attack_reason': 1, 'target_bot': 7, 'health': 10, 'death_reason': 0},
    {'kind': 'health', 'source': 'player_left', 'attack_reason': None, 'target': 3, 'health': 0, 'death_reason': 0},
    {'kind': 'hit', 'source': 'environment', 'attack_reason': 3, 'target': 3, 'health': 0, 'dead': True, 'death_reason': 3,
     'attacker_bot': 7, 'push_attributed': True},
    {'kind': 'health', 'source': 'environment', 'attack_reason': 5, 'target': 3, 'health': 0, 'dead': True, 'death_reason': 5},
    {'kind': 'hit', 'source': 'shot', 'attack_reason': 0, 'attacker_bot': 99, 'target': 3, 'health': 100, 'death_reason': 0,
     'projectile_id': 'p1', 'cosmetic_only': True, 'damage': 0, 'shot_result': 0},
    {'kind': 'hit', 'source': 'ram', 'attack_reason': 2, 'attacker': 3, 'target_bot': 7, 'health': 'bad', 'death_reason': 0},
    {'kind': 'hit', 'source': 'shot', 'attack_reason': 'x', 'attacker': 3, 'target_bot': 7, 'health': 100, 'death_reason': 0},
    {'kind': 'hit', 'source': 'shot', 'attack_reason': 0, 'attacker': 3, 'target_bot': 7, 'health': 100, 'death_reason': 2},
    {'kind': 'bot_hit', 'source': 'shot', 'attack_reason': 0, 'attacker': 3, 'target_bot': 7, 'health': 50, 'death_reason': 0,
     'damage_sticker': 77, 'cosmetic_only': 1},
    {'kind': 'hit', 'source': 'client_simulation', 'attack_reason': 9, 'target': 3, 'health': 40, 'death_reason': 0, 'damage_sticker': 5},
    {'kind': 'health', 'source': 'fire', 'attack_reason': 1, 'attacker_bot': 7, 'target': 3, 'health': 0, 'dead': True, 'death_reason': 1},
    {'kind': 'hit', 'source': 'shot', 'attack_reason': 0, 'target': 3, 'health': 0, 'death_reason': 0, 'dead': True},
    {'kind': 'hit', 'source': 'lava', 'attack_reason': 0, 'target': 3, 'health': 0, 'death_reason': 0},
    {'kind': 'hit', 'target': 3, 'health': 0},
    {'kind': 'stun', 'target_kind': 'bot', 'target_id': 7, 'active': True, 'stun_end_server_time_ms': 5000,
     'stun_factors': {'reload': 1.2}, 'attacker_kind': 'player', 'attacker_id': 3},
    {'kind': 'stun', 'target_kind': 'player', 'target_id': 3, 'active': False, 'stun_end_server_time_ms': 0},
    {'kind': 'stun', 'target_kind': 'bot', 'target_id': 9, 'active': True, 'stun_end_server_time_ms': 0},
    {'kind': 'stun', 'target_kind': 'tank', 'target_id': 7, 'active': True, 'stun_end_server_time_ms': 10},
    {'kind': 'stun', 'target_kind': 'bot', 'target_id': 7, 'active': True, 'stun_end_server_time_ms': 10, 'attacker_kind': 'bot', 'attacker_id': True},
    {'kind': 'shot', 'attacker_bot': 7, 'projectile_id': 'p2', 'bot_presentation_time_us': 1000},
    {'kind': 'bot_shot', 'attacker': 3, 'bot_presentation_time_us': 800},
    {'kind': 'shot', 'attacker_bot': 8, 'bot_presentation_time_us': 10, '_pose_wait_started': 2.0},
    {'kind': 'shot', 'attacker_bot': 55},
    {'kind': 'shot'},
    {'kind': 'projectile_impact', 'projectile_id': 'p2', 'resolved_time_ms': 1500},
    {'kind': 'projectile_ricochet', 'projectile_id': 'p2', 'segment_start_time_ms': 3000},
    {'kind': 'environment_critical', 'target': 3, 'attacker_bot': 8},
    {'kind': 'environment_critical', 'target_bot': 9},
    {'kind': 'authority', 'authority_epoch': 3, 'player_id': 3},
    {'kind': 'destructible', 'destructible_kind': 'tree'},
    {'kind': 'vehicle_statistics'}, {'kind': 'assist'}, {'kind': 'detection'}, {'kind': 'battle_result'}, {'kind': 'bot_manifest'},
    {'kind': 'mystery'},
    {'kind': 'hit', 'source': 'shot', 'attack_reason': 0, 'attacker': 3, 'target_bot': 7, 'health': 50, 'dead': True,
     'death_reason': 0, 'shot_result': 1},
]
EVENT_FUNCTIONS = ('_combat_event_state', '_merge_combat_event_state', '_validate_combat_event_contract', '_combat_attack_reason',
                   '_missing_projectile_attacker_allowed', '_merge_stun_event_state', '_merge_shot_event_state',
                   '_prepare_ordered_event', '_event_is_ready', '_apply_ordered_event')
for event_index in range(len(COMBAT_EVENTS)):
    for variant in range(10):
        for worker in (False, True):
            def make(log, event_index=event_index, variant=variant, worker=worker):
                b = event_battle(log, variant, worker)
                return b, (copy.deepcopy(COMBAT_EVENTS[event_index]),)
            for name in EVENT_FUNCTIONS:
                if name == '_combat_event_state':
                    def make_state(log, make=make, variant=variant):
                        b, (event,) = make(log)
                        return b, (event, {'health': 300, 'display_health': 300} if variant % 2 else None,
                                   'bot:7' if variant % 3 else 'player:3')
                    run(name, make_state, (event_index, variant, worker)); cases += 1
                else:
                    run(name, make, (event_index, variant, worker)); cases += 1
for key in ('bot:7', 'bot:9', 'bot:1', 'player:3'):
    for variant in range(3):
        def make(log, key=key, variant=variant):
            journal = [copy.deepcopy(e) for e in COMBAT_EVENTS[:6]] if variant else []
            return event_battle(log, variant, journal=journal), (key,)
        run('_known_event_state', make, (key, variant)); cases += 1
        run('_pending_event_references', make, (key, variant)); cases += 1
MESSAGES = [None, {}, {'events': None}, {'events': [COMBAT_EVENTS[0], {'event_id': 'seen', 'kind': 'hit'}, 'junk',
                                                     {'kind': 'hit'}, dict(COMBAT_EVENTS[37], event_id=7),
                                                     dict(COMBAT_EVENTS[2], event_id='e2'), dict(COMBAT_EVENTS[16], event_id='e3')]}]
for message_index in range(len(MESSAGES)):
    for variant in range(10):
        def make(log, message_index=message_index, variant=variant):
            b = event_battle(log, variant)
            stub(b, log, '_prepare_ordered_event', lambda e: (_ for _ in ()).throw(RuntimeError('bad event')) if e.get('kind') == 'mystery' else None)
            stub(b, log, '_drain_event_journal')
            message = copy.deepcopy(MESSAGES[message_index])
            if message and message.get('events') and variant % 2: message['events'][0] = dict(message['events'][0], event_id='e1')
            return b, (message,)
        run('on_events', make, (message_index, variant)); cases += 1
JOURNALS = [
    [dict(COMBAT_EVENTS[i], event_id='j%d' % i) for i in (0, 2, 30, 31, 16, 21, 26)],
    [dict(COMBAT_EVENTS[i], event_id='j%d' % i) for i in (21, 0, 2, 30, 6)],
    [dict(COMBAT_EVENTS[i], event_id='j%d' % i) for i in (31, 31, 32, 33)],
    [],
]
for journal_index in range(len(JOURNALS)):
    for variant in range(10):
        def make(log, journal_index=journal_index, variant=variant):
            b = event_battle(log, variant, journal=copy.deepcopy(JOURNALS[journal_index]))
            def ready(event):
                log.append(('battle', '_event_is_ready', rep(event.get('event_id'))))
                k = event.get('kind')
                if k == 'shot' and variant % 3 == 0:
                    event['_presentation_wait'] = True
                    return False
                if k == 'stun' and variant == 4:
                    raise RuntimeError('lost entity')
                if k == 'authority' and variant == 6:
                    return False
                return True
            b._event_is_ready = ready
            def apply(event):
                log.append(('battle', '_apply_ordered_event', rep(event.get('event_id'))))
                if event.get('kind') == 'destructible' and variant % 2:
                    return False
                if event.get('kind') == 'health' and variant == 8:
                    raise ValueError('apply failed')
                return True
            b._apply_ordered_event = apply
            return b, ()
        run('_drain_event_journal', make, (journal_index, variant)); cases += 1
for event_index in list(range(16)) + [38]:
    for variant in range(12):
        for local in (False, True):
            for update in (True, False):
                def make(log, event_index=event_index, variant=variant, local=local, update=update):
                    global critical_damage
                    b = event_battle(log, variant % 10)
                    b._records['player:3']['local'] = local
                    b._records['player:3']['critical_base_revision'] = 2
                    b._records['bot:7']['critical_state'] = {'fire': True, 'canonical': True} if variant == 2 else None
                    b._gun_state = Log(log, 'gun') if variant % 3 == 0 else None
                    b._local_critical_owned = variant % 2 == 0
                    b._local_critical_base_revision = 2
                    critical_damage = Log(log, 'critical_damage', {'apply_payload': lambda e, pres: [
                        {'kind': 'ammo_rack', 'state': 'destroyed', 'cause': 'fire'}, {'kind': 'device', 'state': 'critical'}]})
                    globals()['critical_damage'] = critical_damage
                    entity = Log(log, 'entity')
                    stub(b, log, '_server_entity', lambda engine: None if (variant == 4 and engine == 30) or (variant == 5 and engine == 70) else entity)
                    for name in ('_present_damage_sticker', '_present_combat_hit', '_present_combat_feedback', '_present_environment_feedback',
                                 '_advance_local_gun_to', '_present_critical', '_apply_health'):
                        stub(b, log, name)
                    stub(b, log, '_advance_local_gun_edge', (0, 'edge'))
                    stub(b, log, '_is_blind_local_attack', variant % 2 == 1)
                    stub(b, log, '_should_suppress_postmortem_killer', variant == 7)
                    stub(b, log, '_reconcile_critical_authority', variant != 8)
                    event = copy.deepcopy(COMBAT_EVENTS[event_index])
                    if variant == 10: event['critical'] = {'devices': ['a'], 'destroyed': [], 'events': [{'name': 'leftTrackHealth'}, {'name': 'gun'}]}
                    if variant == 11 and 'display_health' not in event: event['display_health'] = 15
                    b._entity_probe = entity
                    args = (event,) if update else (event, False)
                    return b, args
                run('_apply_combat_event', make, (event_index, variant, local, update)); cases += 1
class Scope(object):
    def __init__(self, log, suppress):
        self.log, self.suppress = log, suppress
    def __enter__(self):
        self.log.append(('scope', 'enter'))
    def __exit__(self, kind, value, trace):
        self.log.append(('scope', 'exit', rep(kind), rep(value)))
        return self.suppress
class FrameBattle(Pinned):
    """Unknown runtime methods log their call and return a scripted answer."""
    def __getattr__(self, name):
        if name.startswith('__'):
            raise AttributeError(name)
        answers = self.__dict__.setdefault('_answers', {})
        log = self.__dict__['_log']
        def call(*args, **kwargs):
            log.append(('battle', name) + tuple(rep(a) for a in args) + tuple(sorted((k, rep(v)) for k, v in kwargs.items())))
            answer = answers.get(name)
            return answer(*args, **kwargs) if callable(answer) else answer
        call.__name__ = 'battle.' + name
        return call
def frame_battle(log, variant, worker, live, profiling):
    global native_client_projectile
    b = FrameBattle()
    b._log = log
    b._records = {}
    for name in METHOD_NAMES:
        if name != '_frame' and not isinstance(Pinned.__dict__.get(name), staticmethod):
            b.__dict__[name] = FrameBattle.__getattr__(b, name)
    b._answers = {
        '_clock': 10.0, '_run_optional_feature': lambda name, fn, args=(), disable=None, kwargs=None: (3, 1) if 'pose' in name else True,
        '_projectile_is_authority': variant % 3 == 0, '_prebattle_transition_ready': variant % 4 == 1,
        '_authority_players': ['p'], '_present_authority_bot_poses': (5, 2), '_drain_worker_transport': variant != 6,
        '_enqueue_bot_message': lambda m: m.get('type') != 'bot_state' or variant % 2 == 0,
        '_projectile_record_poses': 'poses', '_collection_counts': {'n': 1}, '_worker_control_snapshot': {'c': 1},
        '_worker_diagnostic_totals': {'d': 1}}
    if variant == 7:
        def boom(now): raise RuntimeError('flush failed')
        b._answers['_flush_pending_entities'] = boom
    b.state = 'running' if variant != 9 else 'stopped'
    b._worker_mode = worker
    b._worker_frame_callbacks = 0
    b._worker_probe = 'probe' if variant == 2 else None
    b._worker_probe_simulation_caps = b._worker_probe_authority_callbacks = 0
    b._worker_probe_bot_generated = b._worker_probe_bot_enqueued = b._worker_probe_bot_send_failed = 0
    b._last_frame_time = None if variant == 0 else (9.5 if variant % 2 else 9.99)
    b._soft_static_recast_budget = [0]
    b._offframe_seconds = 0.25
    b._battle_live = live
    b._replay_mode = variant == 8
    b._prebattle_deadline = None if variant == 3 else 9.8
    b._destructibles = 'destructibles' if variant % 2 else None
    b._projectiles = Log(log, 'projectiles', {'__len__': None}) if variant % 3 == 0 else None
    b._projectile_visual_meta = {'m': 1} if variant == 5 else {}
    b._projectile_perf = {'advance_ms': 0.1}
    b._local_position = [1.0, 2.0, 3.0]
    b._local_camera_velocity = (0.5, 0.0, 0.5)
    b._local_speed = 2.5
    b._local_airborne = False
    b._local_grind = 0
    b._generation = 2
    b._last_snapshot = {'tick': 4} if variant % 2 else None
    b._start_message = {'round_id': 3} if variant % 2 else None
    b._config = {'map': '33_fjord'}
    b._sync = Log(log, 'sync') if variant != 4 else None
    b._authority_pose_writes = b._authority_pose_skips = b._authority_aim_writes = b._authority_aim_skips = 1
    b.client = Log(log, 'client', missing=('transport_performance_snapshot',) if variant % 2 else ())
    b._runtime = Log(log, 'runtime', compatibility=Log(log, 'compat'))
    b._avatar = Log(log, 'avatar')
    bot_answers = {'update': lambda dt, now, players=None: [{'type': 'bot_state'}, {'type': 'other'}],
                   'probe_totals': (1, 2, 3, 4, 5), 'probe_duration_totals': (0.1, 0.2, 0.3, 0.4, 0.5),
                   'is_authority': variant % 2 == 0, 'probe_timing_state': 'on', 'load_report': {'l': 1}}
    b._bots = Log(log, 'bots', bot_answers, _shot_lane_pending_pairs=variant % 2, _sample_time_us=7) if variant != 5 else None
    diagnostics = Log(log, 'diagnostics', {'begin': 17, 'emit_due': variant % 2 == 0}, enabled=profiling)
    b._frame_diagnostics = diagnostics if variant != 0 else None
    b._combat_diagnostics = Log(log, 'combat', {'drain_completed': ['c'], 'checkpoint': 'k', 'finish_frame': 'f'}) if variant % 2 else None
    native_client_projectile = Log(log, 'native_projectile', {'enabled': variant % 2 == 0})
    fake_module('native_client_projectile', native_client_projectile)
    fake_module('offline_replay', Log(log, 'offline_replay'))
    setattr(sys.modules['gui.mods.offline_lan_0922'], 'offline_replay', sys.modules['gui.mods.offline_lan_0922.offline_replay'])
    sensor = types.ModuleType('destructibles_sensor')
    sensor.collision_preparation_scope_1513 = lambda: Scope(log, variant == 8)
    sys.modules['gui.mods.offline_lan_0922.destructibles_sensor'] = sensor
    return b
for variant in range(10):
    for worker in (False, True):
        for live in (False, True):
            for profiling in (False, True):
                def make(log, variant=variant, worker=worker, live=live, profiling=profiling):
                    b = frame_battle(log, variant, worker, live, profiling)
                    b._battle_probe = b
                    return b, ()
                run('_frame', make, (variant, worker, live, profiling)); cases += 1
HEALTH_STATES = [
    {'health': 300, 'alive': True}, {'health': 0, 'alive': False, 'death_reason': 1},
    {'health': 0, 'display_health': 25, 'death_reason': 0, 'critical': {'ammo_rack_death': True}},
    {'health': 120, 'alive': False}, {'health': 0, 'display_health': None, 'death_reason': 5},
    {'health': 50, 'display_health': 45}, {}, {'health': 'x'}, {'health': 0, 'death_reason': 2, 'critical': {}},
    {'health': 120, 'alive': False, 'display_health': 0}, {'health': 0, 'alive': True, 'display_health': 40, 'death_reason': 7},
]
for state_index in range(len(HEALTH_STATES)):
    for previous in (None, 'alive', 'dead'):
        for local in (True, False):
            for args_index in range(4):
                for variant in range(8):
                    def make(log, state_index=state_index, previous=previous, local=local, args_index=args_index, variant=variant):
                        global critical_damage, native_client_environment, native_client_drive
                        b = battle(log, worker=variant == 7, outlined=41 if variant in (1, 5) else None)
                        critical_damage = Log(log, 'critical_damage', {'apply_death': lambda e, cause: None if variant == 2 else {'cause': cause}})
                        native_client_environment = Log(log, 'environment'); native_client_drive = Log(log, 'drive')
                        globals().update(critical_damage=critical_damage, native_client_environment=native_client_environment,
                                         native_client_drive=native_client_drive)
                        constants = Log(log, 'constants', SPECIAL_VEHICLE_HEALTH=Log(log, 'special', TURRET_DETACHED=-5))
                        compatibility = Log(log, 'compat', missing=('clear_postmortem_killer',) if variant == 6 else ())
                        b._runtime = Log(log, 'runtime', constants=constants, compatibility=compatibility)
                        b._avatar = Log(log, 'avatar', guiSessionProvider=Log(log, 'provider'))
                        b._binding = Log(log, 'binding', missing=('arena_vehicle_killed',) if variant == 3 else ())
                        def boom(engine): raise RuntimeError('wreck failed')
                        b._remote_factory = Log(log, 'factory', {'request_wreck': boom} if variant == 4 else {})
                        b._sender = Log(log, 'sender', forward=1.0, turn=1.0) if variant != 5 else None
                        b._local_speed = 7.0
                        b._last_health = {}
                        if previous == 'alive': b._last_health[41] = (300, 300, True, 0)
                        if previous == 'dead': b._last_health[41] = (0, 0, False, 1)
                        attrs = {'health': 300, 'isCrewActive': True}
                        missing = ()
                        if variant == 1: attrs['isAlive'] = False
                        elif variant == 5: missing = ('onHealthChanged', 'isAlive')
                        else: attrs['isAlive'] = Log(log, 'alive_fn', {'__call__': None}) if False else (lambda: variant % 2 == 0)
                        entity = Log(log, 'entity', missing=missing, **attrs)
                        stub(b, log, '_server_entity', None if variant == 6 and state_index == 0 else entity)
                        for name in ('_detach_late_ammo_turret', '_combat_target_is_spotted', '_clear_target_outline', '_sync_fire_effect',
                                     '_queue_local_damage_report', '_release_target_lock', '_present_vehicle_dead',
                                     '_report_local_kill_contract', '_fallback_postmortem_viewpoint', '_finalize_dead_remote_wreck',
                                     '_apply_turret_detachment', '_warn_optional_failure'):
                            stub(b, log, name, True if name == '_combat_target_is_spotted' else None)
                        stub(b, log, '_ammo_bay_special_health', -7)
                        stub(b, log, '_attack_reason', lambda name, default: default)
                        stub(b, log, '_critical_state', lambda payload: dict(payload, canonical=True))
                        record = {'engine_id': 41, 'local': local, 'presentation': variant != 0, 'native_remote': variant % 2 == 1}
                        if state_index == 2 and variant in (1, 3): record['ammo_death_cause'] = 'fire' if variant == 1 else 'world_collision'
                        state = copy.deepcopy(HEALTH_STATES[state_index])
                        b._entity_probe = entity
                        args = [(record, state), (record, state, 9), (record, state, 9, 2, True, 3, True, True),
                                (record, state, 0, None, False, None, True, False)][args_index]
                        return b, args
                    def checked(log, make=make):
                        b, args = make(log)
                        return b, args
                    run('_apply_health', checked, (state_index, previous, local, args_index, variant)); cases += 1
EVENT_SETS = [
    [], [{'kind': 'ammo_rack', 'state': 'destroyed'}],
    [{'kind': 'device', 'name': 'engine', 'state': 'destroyed', 'cause': 'fire'},
     {'kind': 'device', 'name': 'gun', 'state': 'normal', 'old_state': 'critical'},
     {'kind': 'device', 'name': 'radio', 'state': 'critical', 'old_state': 'destroyed'},
     {'kind': 'device', 'name': 'track', 'state': 'critical', 'cause': 'repair'}],
    [{'kind': 'crew', 'name': 'driver', 'state': 'normal'}, {'kind': 'crew', 'name': 'gunner', 'state': 'destroyed', 'cause': 'drowning'},
     {'kind': 'crew', 'name': 'loader', 'state': 'destroyed', 'cause': 'fire'}, {'kind': 'crew', 'name': 'commander', 'state': 'destroyed'}],
    [{'kind': 'fire', 'state': True, 'cause': 'ramming'}, {'kind': 'fire', 'state': True}, {'kind': 'fire', 'state': False},
     {'kind': 'other'}, {'kind': 'ammo_rack', 'state': 'damaged'}],
    [{'kind': 'device', 'name': 'bad', 'state': 'critical'}],
    [{'kind': 'device', 'name': 'engine', 'state': 'critical', 'cause': 'world_collision'}],
]
CODES = ['DEVICE_REPAIRED', 'DEVICE_REPAIRED_TO_CRITICAL', 'DEVICE_DESTROYED_AT_FIRE', 'DEVICE_CRITICAL_AT_SHOT',
         'TANKMAN_RESTORED', 'TANKMAN_HIT_AT_DROWNING', 'TANKMAN_HIT', 'TANKMAN_HIT_AT_SHOT',
         'DEVICE_STARTED_FIRE_AT_RAMMING', 'DEVICE_STARTED_FIRE_AT_SHOT', 'FIRE_STOPPED', 'DEVICE_CRITICAL_AT_WORLD_COLLISION']
for events_index in range(len(EVENT_SETS)):
    for local in (True, False):
        for spot in (True, False, None):
            for attacker in (0, 9, None):
                for variant in range(6):
                    def make(log, events_index=events_index, local=local, spot=spot, attacker=attacker, variant=variant):
                        b = battle(log, worker=variant == 5)
                        indices = dict((code, i + 1) for i, code in enumerate(CODES)) if variant != 3 else {'FIRE_STOPPED': 2}
                        modes = Log(log, 'modes', HE_DETONATION=4) if variant != 4 else Log(log, 'modes', missing=('HE_DETONATION',))
                        constants = Log(log, 'constants', AMMOBAY_DESTRUCTION_MODE=modes, DAMAGE_INFO_INDICES=indices)
                        b._runtime = Log(log, 'runtime', constants=constants)
                        b._avatar = None if variant == 2 else Log(log, 'avatar')
                        entity = Log(log, 'entity', typeDescriptor=Log(log, 'td'))
                        stub(b, log, '_server_entity', entity if variant != 1 else None)
                        stub(b, log, '_sync_fire_effect'); stub(b, log, '_damage_info_is_serviceable', True)
                        stub(b, log, '_critical_extra_index', lambda td, name: -1 if name == 'bad' else len(name or ''))
                        stub(b, log, '_show_damage_info', lambda *a: a[1] % 2 == 0); stub(b, log, '_report_critical_presentation')
                        record = {'engine_id': 41, 'local': local}
                        if spot is not None: record['spot_visible'] = spot
                        return b, (record, [dict(e) for e in EVENT_SETS[events_index]], attacker)
                    run('_present_critical', make, (events_index, local, spot, attacker, variant)); cases += 1
SIEGE = Log([], 'siege', DISABLED=0, SWITCHING_ON=1, ENABLED=2, SWITCHING_OFF=3)
for siege in (0, 1, 2, 3, 7, True, None):
    for left in (0, 1500, -5, 2.0):
        for presented in (None, 0, 2):
            for pending in (None, (True, None), (True, 9), (False, 30)):
                for seq in (None, 10, 40):
                    for local in (True, False):
                        for variant in range(5):
                            def make(log, siege=siege, left=left, presented=presented, pending=pending, seq=seq, local=local, variant=variant):
                                global vehicle_physics
                                b = battle(log, worker=variant == 4)
                                vehicle_physics = Log(log, 'vehicle_physics', {'derive_params': lambda d, f: 'physics'})
                                b._runtime = Log(log, 'runtime', constants=Log(log, 'constants', VEHICLE_SIEGE_STATE=SIEGE))
                                b._local_siege_pending = pending
                                has_siege = variant != 1
                                descriptor = Log(log, 'descriptor', hasSiegeMode=has_siege) if variant != 2 else None
                                entity = Log(log, 'entity', typeDescriptor=descriptor) if descriptor is not None else Log(log, 'entity', missing=('typeDescriptor',))
                                stub(b, log, '_server_entity', entity)
                                stub(b, log, '_report_local_siege_edge'); stub(b, log, '_advance_local_gun_edge', (0, 'edge'))
                                stub(b, log, '_advance_local_gun_to'); stub(b, log, '_select_local_siege_pose')
                                stub(b, log, '_local_factors', 'factors'); stub(b, log, '_update_local_hull_aiming')
                                b._gun_state = Log(log, 'gun', {'adopt_descriptor': lambda d: True}) if variant in (0, 3) else None
                                b._sender = Log(log, 'sender') if variant == 0 else None
                                b._local_matrix = 'matrix' if variant in (0, 3) else None
                                b._local_physics = 'old' if variant in (0, 3) else None
                                b._remote_factory = Log(log, 'factory')
                                record = {'engine_id': 41, 'local': local, 'native_remote': variant == 4}
                                if presented is not None: record['presented_siege_state'] = presented
                                state = {}
                                if siege is not None: state['siege_state'] = siege
                                state['siege_time_left_ms'] = left
                                if seq is not None: state['input_seq'] = seq
                                b._entity_probe = None
                                return b, (record, state)
                            def checked(log, make=make):
                                b, args = make(log)
                                b._battle_probe = b
                                return b, args
                            run('_apply_siege_state', checked, (siege, left, presented, pending, seq, local, variant)); cases += 1
print('lifecycle differential ok %d' % cases)
for name, (errors, quiet, effects) in sorted(TALLY.items()):
    print('%s errors=%d no_calls=%d with_calls=%d' % (name, errors, quiet, effects))
    assert quiet + effects, name
'''


def pinned_source(fork):
    """The runtime as staged by every other adapter: what the lifecycle ports replace."""
    import importlib.util
    import sys
    sys.path.insert(0, str(ROOT / 'bigworld'))
    spec = importlib.util.spec_from_file_location('prepare_offline_fork', ROOT / 'bigworld/prepare_offline_fork.py')
    prepare = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(prepare)
    prepare.client_lifecycle_bindings.battle = lambda source: source
    source = prepare.ADAPTERS[Path(RUNTIME)]((Path(fork) / RUNTIME).read_text())
    tree = ast.parse(source)
    lines = source.splitlines()
    chunks = ['import math', '_INTEGER_TYPES = (int, long)', 'class Pinned(object):',
              "    def __repr__(self):\n        return '<battle>'"]
    for node in tree.body:
        if isinstance(node, ast.Assign) and any(getattr(t, 'id', None) in GLOBALS for t in node.targets):
            chunks.insert(1, '\n'.join(lines[node.lineno - 1:node.end_lineno]))
        if isinstance(node, ast.FunctionDef) and node.name in GLOBALS:
            chunks.insert(1, '\n'.join(lines[node.lineno - 1:node.end_lineno]))
        if isinstance(node, ast.ClassDef) and node.name == 'BattleRuntime':
            for item in node.body:
                if isinstance(item, ast.FunctionDef) and item.name in METHODS:
                    start = min([d.lineno for d in item.decorator_list] + [item.lineno])
                    chunks.append('\n'.join(lines[start - 1:item.end_lineno]))
    text = '\n\n'.join(chunks) + '\n'
    # Package-relative imports resolve to the fixture's fakes.
    text = text.replace('from .destructibles_sensor import collision_preparation_scope_1513',
                        "collision_preparation_scope_1513 = sys.modules['gui.mods.offline_lan_0922.destructibles_sensor']"
                        ".collision_preparation_scope_1513")
    for module in ('native_client_environment', 'native_client_drive', 'native_client_replica', 'native_bot_runtime',
                   'native_client_projectile'):
        text = text.replace('from . import %s' % module, '%s = globals()[%r]' % (module, module))
    return text.replace('from . import native_client_query', 'native_client_query = NATIVE_QUERY')


class LifecycleDifferential(unittest.TestCase):
    def test_native_ports_match_pinned_methods(self):
        fork = os.environ.get('WOT_FORK_0101_SOURCE')
        if not fork:
            self.skipTest('Pinned gameplay source is required')
        py27 = subprocess.check_output(['pyenv', 'which', 'python2.7'], text=True,
                                       env=dict(os.environ, PYENV_VERSION='2.7.18')).strip()
        prefix = subprocess.check_output([py27, '-c', 'import sys;print(sys.prefix)'], text=True).strip()
        with tempfile.TemporaryDirectory(prefix='lifecycle-') as folder:
            folder = Path(folder)
            subprocess.run(['c++', '-std=c++17', '-O1', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter',
                            '-Wno-register', '-bundle', '-undefined', 'dynamic_lookup',
                            '-isystem', str(Path(prefix) / 'include/python2.7'), '-I' + str(ROOT / 'bigworld'),
                            str(ROOT / 'tests/stock_native_client_lifecycle_host.cpp'),
                            '-o', str(folder / 'offline_client_lifecycle_native.so')], check=True)
            (folder / 'pinned.py').write_text(pinned_source(fork))
            (folder / 'fixture.py').write_text(FIXTURE.replace('%r', repr(METHODS), 1))
            run = subprocess.run([py27, '-B', str(folder / 'fixture.py'), str(folder), str(folder / 'pinned.py')],
                                 capture_output=True, text=True, timeout=120)
            self.assertEqual(run.returncode, 0, run.stdout[-3000:] + run.stderr[-6000:])
            self.assertIn('lifecycle differential ok', run.stdout)
            print(run.stdout)


if __name__ == '__main__':
    unittest.main()
