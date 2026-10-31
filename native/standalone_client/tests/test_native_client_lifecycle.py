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
           '_record_is_event_ready', '_combat_event_source', '_apply_combat_event', '_frame',
           '_optional_feature_enabled', '_run_optional_feature', '_schedule', '_maintain_standard_space_visibility',
           '_start_team_chat_when_roster_ready', '_prewarm_critical_layout', '_maybe_send_battle_ready', '_retry_bot_manifest',
           '_attack_reason', '_is_blind_local_attack', '_apply_vehicle_statistics_event', '_present_direct_spot',
           '_apply_detection_event', '_apply_assist_event', '_apply_environment_critical_feedback',
           '_apply_destructible_event', '_feedback_entity_key', '_combat_record_team', '_combat_target_is_spotted',
           '_present_damage_sticker', '_apply_battle_result', '_present_combat_feedback', '_present_combat_hit', '_show_shot',
           '_projectile_is_authority', '_owns_projectile', '_projectile_estimated_server_time', '_new_projectile_meta',
           '_projectile_source_descriptor', '_projectile_attacker_engine_id', '_projectile_source_entity',
           '_remember_fire_timeline', '_projectile_shot', '_descriptor_shot', '_admit_projectile_visual',
           '_show_local_shot_without_extra', '_shot_muzzle_drawn', '_show_replay_local_muzzle',
           '_reset_projectile_visuals', '_report_player_shot_timing', '_projectile_wire_meta',
           '_projectile_plain_pose', '_projectile_boundary_pose', '_projectile_visual_age', '_projectile_record_positions',
           '_projectile_vehicle_matrices', '_projectile_descriptor_at_pose', '_report_projectile_terminal_failure',
           '_confirm_bot_projectile_launch', '_projectile_visual_start', '_set_projectile_epoch', '_projectile_record_pose',
           '_sample_projectile_positions', '_projectile_frozen_target', '_projectile_live_critical_target',
           '_discard_provisional_projectile', '_observe_projectile_message', '_install_projectile_meta', '_projectile_record_poses',
           '_projectile_explosion', '_present_projectile_wreck_hit', '_stop_projectile_visual',
           '_ensure_projectile_visual_native_presentation', '_ensure_projectile_visual',
           '_flush_pending_projectile_resolutions', '_prune_projectile_position_history', '_advance_projectiles',
           '_accept_projectile_event', '_apply_projectile_ricochet_event', '_apply_projectile_terminal_event',
           '_projectile_launch_mismatches', 'on_fire_intent_result', '_defer_local_projectile_presentation',
           '_reconcile_projectile_snapshot', '_clock', '_tick_critical_states', '_advance_artillery_arcs',
           '_advance_authority_worker_probe', '_hit_damage_factor', '_impact_sound_identity', '_surface_effect_material',
           '_release_target_lock', '_report_effect', '_report_hit_impulse', '_decoded_hit_impulse_direction',
           '_present_hit_impulse', '_event_shell', '_shell_is_gold', '_report_feedback_shell_failure',
           '_feedback_shell_fields', '_fallback_postmortem_viewpoint', '_report_local_kill_contract',
           '_projectile_cosmetic_allowed', '_diagnostic_int', '_diagnostic_vehicle_info', '_diagnostic_value',
           'on_snapshot', '_apply_rules', '_ammo_tick', '_sync_local_server_marker', '_observe_local_vehicle')
CLASS_ATTRIBUTES = ('_ASSIST_EVENT_TYPES', '_EFFECT_REPORT_LIMIT', '_HIT_IMPULSE_REPORT_LIMIT')
GLOBALS = ('_xyz', '_format_xyz', '_number', '_COMBAT_EVENT_KINDS', '_SHOT_EVENT_KINDS', '_SIMPLE_EVENT_KINDS', '_field', '_PROJECTILE_IMMUTABLE_FIELDS',
           '_MutedShootingExtra', 'PROJECTILE_MAX_TIME_MS', '_angle_delta',
           'PROJECTILE_SUSTAIN_SECONDS', 'PROJECTILE_CHORDS_PER_FRAME', 'PROJECTILE_MAX_CHORDS_PER_FRAME', 'PROJECTILE_PROGRESS_SECONDS')

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
        # A fixture may run work the method scheduled (a BigWorld callback) before the comparison.
        drive = b.__dict__.get('_drive_deferred')
        if drive is not None:
            try:
                drive()
            except Exception as error:
                log.append(('deferred error', type(error).__name__, str(error)))
        battle_after = tuple(repr(b.__dict__.get(name, '-')) for name in (
            '_local_siege_pending', '_targeting_signature', '_local_physics', '_local_suspension_params',
            '_local_suspension_probe_trace', '_local_suspension_disabled', '_local_suspension_pitch_velocity',
            '_last_health', '_local_speed', '_event_journal', '_accepted_event_ids', '_applied_event_ids',
            '_projectile_lineage', '_pending_bot_creates', '_local_last_attacker', '_last_frame_time',
            '_worker_frame_callbacks', '_worker_probe_simulation_caps', '_soft_static_recast_budget', '_offframe_seconds',
            '_worker_probe_authority_callbacks', '_worker_probe_bot_generated', '_worker_probe_bot_enqueued',
            '_worker_probe_bot_send_failed', '_worker_probe_bot_count', '_local_frame_stages',
            '_standard_space_visibility', '_next_space_visibility_check', '_team_chat_initialized', '_ready_sent',
            '_next_bot_manifest_retry', '_bot_manifest_retry_deadline', '_bot_manifest_retry_identity',
            '_battle_result', '_round_finished_notified', '_battle_live', '_fire_timeline',
            '_projectile_visual_meta', '_replay_muzzle_count', '_bot_fire_seen', '_bot_fire_confirmations',
            '_bot_launch_payloads', '_projectile_epoch', '_projectile_visual_terminals', '_projectile_resolution_acks',
            '_local_projectile_presented', '_local_projectile_presentations', '_projectile_terminal_data',
            '_projectile_scene_stop_reasons', '_next_projectile_progress_time', '_projectile_position_history',
            '_projectile_meta', '_projectile_server_time_ms', '_projectile_server_local_time', '_pose_motion_time_us',
            '_pose_motion_local_time', '_projectile_perf', '_projectile_scan_count', '_projectile_candidate_count',
            '_projectile_target_positions', '_projectile_previous_positions', '_projectile_current_positions',
            '_projectile_frame_start', '_projectile_frame_end', '_projectile_advance_active', '_projectile_historic_pose_cache',
            '_projectile_revision', '_next_critical_report_time', '_worker_probe_attempted', '_worker_probe', '_last_snapshot',
            '_rules_state'))
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
    '_optional_feature_enabled': native.optional_feature_enabled,
    '_run_optional_feature': lambda b, f, cb, args=(), on_error=None, disable=True, kwargs=None: native.run_optional_feature(
        b, f, cb, args, on_error, disable, kwargs),
    '_schedule': lambda b, d, fn, ammo=False: native.schedule(b, d, fn, ammo, globals()),
    '_maintain_standard_space_visibility': lambda b, now: native.maintain_space_visibility(b, now, globals()),
    '_start_team_chat_when_roster_ready': native.start_team_chat,
    '_prewarm_critical_layout': lambda b: native.prewarm_critical_layout(b, globals()),
    '_maybe_send_battle_ready': native.maybe_send_battle_ready,
    '_retry_bot_manifest': native.retry_bot_manifest,
    '_attack_reason': native.attack_reason,
    '_is_blind_local_attack': native.blind_local_attack,
    '_apply_vehicle_statistics_event': native.vehicle_statistics_event,
    '_present_direct_spot': native.present_direct_spot,
    '_apply_detection_event': native.detection_event,
    '_apply_assist_event': native.assist_event,
    '_apply_environment_critical_feedback': lambda b, e: native.environment_critical_feedback(b, e, globals()),
    '_apply_destructible_event': lambda b, e: native.destructible_event(b, e, native_client_drive),
    '_present_damage_sticker': native.present_damage_sticker,
    '_apply_battle_result': native.battle_result,
    '_present_combat_feedback': lambda b, e, t, a, reason_id=None: native.present_combat_feedback(b, e, t, a, reason_id),
    '_present_combat_hit': lambda b, e, t, a, attacker_id: native.present_combat_hit(b, e, t, a, attacker_id, globals()),
    '_show_shot': lambda b, e, update_state=True: native.show_shot(b, e, update_state, globals()),
    '_projectile_is_authority': lambda b: native.projectile_is_authority(b, globals()),
    '_owns_projectile': lambda b, m: native.owns_projectile(b, m, globals()),
    '_projectile_estimated_server_time': native.projectile_estimated_server_time,
    '_new_projectile_meta': native.new_projectile_meta,
    '_projectile_source_descriptor': native.projectile_source_descriptor,
    '_projectile_attacker_engine_id': native.projectile_attacker_engine_id,
    '_projectile_source_entity': native.projectile_source_entity,
    '_remember_fire_timeline': native.remember_fire_timeline,
    '_projectile_shot': lambda b, m: native.projectile_shot(b, m, globals()),
    '_admit_projectile_visual': native.admit_projectile_visual,
    '_show_local_shot_without_extra': lambda b, e, c: native.show_local_shot_without_extra(b, e, c, globals()),
    '_shot_muzzle_drawn': native.shot_muzzle_drawn,
    '_show_replay_local_muzzle': native.show_replay_local_muzzle,
    '_reset_projectile_visuals': native.reset_projectile_visuals,
    '_report_player_shot_timing': native.report_player_shot_timing,
    '_projectile_wire_meta': lambda b, raw: native.projectile_wire_meta(raw, globals()),
    '_projectile_plain_pose': lambda b, position, state=None: native.projectile_plain_pose(position, state, globals()),
    '_projectile_boundary_pose': lambda b, position, state=None: native.projectile_boundary_pose(position, state, globals()),
    '_projectile_visual_age': lambda b, raw: native.projectile_visual_age(raw, globals()),
    '_projectile_record_positions': lambda b: native.projectile_record_positions(b, globals()),
    '_projectile_vehicle_matrices': lambda b, r, v, ground_matrix=None: native.projectile_vehicle_matrices(b, r, v, ground_matrix),
    '_projectile_descriptor_at_pose': native.projectile_descriptor_at_pose,
    '_report_projectile_terminal_failure': lambda b, m, st, stage, reason, error=None:
        native.report_projectile_terminal_failure(b, m, st, stage, reason, error, globals()),
    '_confirm_bot_projectile_launch': lambda b, n: native.confirm_bot_projectile_launch(b, n, globals()),
    '_projectile_visual_start': lambda b, e, n: native.projectile_visual_start(b, e, n, globals()),
    '_set_projectile_epoch': lambda b, value, now: native.set_projectile_epoch(b, value, now, globals()),
    '_projectile_record_pose': lambda b, k, r, pos: native.projectile_record_pose(b, k, r, pos, globals()),
    '_sample_projectile_positions': lambda b, now, poses: native.sample_projectile_positions(b, now, poses, globals()),
    '_projectile_frozen_target': lambda b, t, pose: native.projectile_frozen_target(b, t, pose, globals()),
    '_projectile_live_critical_target': lambda b, r, t: native.projectile_live_critical_target(b, r, t, globals()),
    '_discard_provisional_projectile': lambda b, pid: native.discard_provisional_projectile(b, pid, native_client_projectile, globals()),
    '_observe_projectile_message': lambda b, m: native.observe_projectile_message(b, m, native_client_projectile, native_bot_runtime,
                                                                                 globals()),
    '_install_projectile_meta': lambda b, n: native.install_projectile_meta(b, n, globals()),
    '_projectile_record_poses': lambda b: native.projectile_record_poses(b, globals()),
    '_projectile_explosion': lambda b, pid, impact, outcome='impact': native.projectile_explosion(b, pid, impact, outcome, globals()),
    '_present_projectile_wreck_hit': lambda b, pid, e: native.present_projectile_wreck_hit(b, pid, e, globals()),
    '_stop_projectile_visual': lambda b, pid, e: native.stop_projectile_visual(b, pid, e, globals()),
    '_ensure_projectile_visual_native_presentation': lambda b, n, now, visual_start=None:
        native.ensure_projectile_visual_native_presentation(b, n, now, visual_start, globals()),
    '_ensure_projectile_visual': lambda b, n, now, visual_start=None:
        native.ensure_projectile_visual(b, n, now, visual_start, native_client_projectile, globals()),
    '_flush_pending_projectile_resolutions': lambda b: native.flush_pending_projectile_resolutions(b, globals()),
    '_prune_projectile_position_history': lambda b, states=None: native.prune_projectile_position_history(b, states, globals()),
    '_advance_projectiles': lambda b, now, force_progress=False: native.advance_projectiles(
        b, now, force_progress, native_client_projectile, native_bot_runtime, globals()),
    '_accept_projectile_event': lambda b, e: native.accept_projectile_event(b, e, native_client_projectile, native_bot_runtime, globals()),
    '_apply_projectile_ricochet_event': lambda b, e: native.apply_projectile_ricochet_event(b, e, native_bot_runtime, globals()),
    '_apply_projectile_terminal_event': lambda b, e: native.apply_projectile_terminal_event(
        b, e, native_client_projectile, native_bot_runtime, globals()),
    'on_fire_intent_result': lambda b, m: native.on_fire_intent_result(b, m, globals()),
    '_defer_local_projectile_presentation': lambda b, e: native.defer_local_projectile_presentation(b, e, globals()),
    '_reconcile_projectile_snapshot': lambda b, m: native.reconcile_projectile_snapshot(
        b, m, native_client_projectile, native_bot_runtime, globals()),
    '_clock': lambda b: native.clock(b, globals()),
    '_tick_critical_states': lambda b, dt: native.tick_critical_states(b, dt, native_client_critical, globals()),
    '_advance_artillery_arcs': lambda b, now: native.advance_artillery_arcs(b, now, native_bot_runtime, globals()),
    '_advance_authority_worker_probe': lambda b: native.advance_authority_worker_probe(b, globals()),
    '_hit_damage_factor': lambda b, e, r: native.hit_damage_factor(b, e, r, globals()),
    '_impact_sound_identity': lambda b, r, a, d, f: native.impact_sound_identity(b, r, a, d, f, globals()),
    '_surface_effect_material': lambda b, i: native.surface_effect_material(b, i, globals()),
    '_release_target_lock': lambda b, e: native.release_target_lock(b, e, globals()),
    '_report_effect': lambda b, k, m, i, w, d, event=None: native.report_effect(b, k, m, i, w, d, event, globals()),
    '_report_hit_impulse': lambda b, r, d, i, p: native.report_hit_impulse(b, r, d, i, p, globals()),
    '_decoded_hit_impulse_direction': lambda b, e, r: native.decoded_hit_impulse_direction(b, e, r, globals()),
    '_present_hit_impulse': lambda b, e, r, fx, sd=None: native.present_hit_impulse(b, e, r, fx, sd, globals()),
    '_event_shell': lambda b, a, e: native.event_shell(b, a, e, globals()),
    '_shell_is_gold': lambda b, sh: native.shell_is_gold(b, sh, globals()),
    '_report_feedback_shell_failure': lambda b, r: native.report_feedback_shell_failure(b, r, globals()),
    '_feedback_shell_fields': lambda b, e, a: native.feedback_shell_fields(b, e, a, globals()),
    '_fallback_postmortem_viewpoint': lambda b, x: native.fallback_postmortem_viewpoint(b, x, globals()),
    '_report_local_kill_contract': lambda b, r, a, reason: native.report_local_kill_contract(b, r, a, reason, globals()),
    '_projectile_cosmetic_allowed': lambda b, e, v: native.projectile_cosmetic_allowed(e, v, globals()),
    'on_snapshot': lambda b, m: native.on_snapshot(b, m, globals()),
    '_apply_rules': lambda b, r: native.apply_rules(b, r, globals()),
    '_ammo_tick': lambda b: native.ammo_tick(b, globals()),
    '_sync_local_server_marker': lambda b: native.sync_local_server_marker(b, globals()),
    '_observe_local_vehicle': lambda b, m, n: native.observe_local_vehicle(b, m, n, globals()),
}
SPACE_VISIBILITY_CHECK_SECONDS = 0.5
class _LiveSpaceVisibilityPending(Exception):
    pass
def _underlying_function(function):
    return getattr(function, '__func__', function)
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
native_client_environment = native_client_drive = native_client_replica = native_client_critical = None
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
for disabled in (None, set(), set(['spotting'])):
    for name in ('spotting', 'outline', 7):
        def make(log, disabled=disabled, name=name):
            b = battle(log)
            if disabled is not None: b._disabled_optional_features = set(disabled)
            return b, (name,)
        run('_optional_feature_enabled', make, (disabled, name)); cases += 1
for disabled in (False, True):
    for behaviour in ('ok', 'raise', 'raise_cleanup_ok', 'raise_cleanup_fail', 'kwargs', 'keyboard'):
        for disable in (True, False):
            def make(log, disabled=disabled, behaviour=behaviour, disable=disable):
                b = battle(log)
                if disabled: b._disabled_optional_features = set(['feature'])
                stub(b, log, '_warn_optional_failure')
                def callback(*args, **kwargs):
                    log.append(('callback', rep(args), rep(sorted(kwargs.items()))))
                    if behaviour.startswith('raise'): raise ValueError('boom')
                    if behaviour == 'keyboard': raise KeyError('k')
                    return 'result'
                def cleanup():
                    log.append(('cleanup',))
                    if behaviour == 'raise_cleanup_fail': raise IOError('cleanup broke')
                on_error = cleanup if behaviour.startswith('raise_cleanup') else None
                kwargs = {'hud_only': True} if behaviour == 'kwargs' else None
                return b, ('feature', callback, (1, 2), on_error, disable, kwargs)
            run('_run_optional_feature', make, (disabled, behaviour, disable)); cases += 1
for ammo in (False, True):
    for existing in (None, 41):
        for variant in range(6):
            def make(log, ammo=ammo, existing=existing, variant=variant):
                b = battle(log)
                b._generation = 3
                b._ammo_callback_id = existing
                b._ammo_callback_token = 'old' if existing else None
                b._callback_token = None
                b._callback_id = None
                b._offframe_seconds = 0.5
                stored = []
                def callback(delay, fn):
                    log.append(('bigworld', 'callback', rep(delay)))
                    if variant == 5: raise RuntimeError('callback refused')
                    stored.append(fn)
                    return 99
                def cancel(identifier):
                    log.append(('bigworld', 'cancelCallback', rep(identifier)))
                    if variant == 4: raise RuntimeError('cancel failed')
                class BigWorld(object):
                    pass
                bigworld = BigWorld()
                bigworld.callback, bigworld.cancelCallback = callback, cancel
                b._runtime = Log(log, 'runtime', bigworld=bigworld)
                def frame():
                    log.append(('frame',))
                    if variant == 3: raise ValueError('frame failed')
                b._frame = frame
                function = frame if variant % 2 == 0 else (lambda: log.append(('other',)))
                b._scheduled_store = stored
                return b, (0.016, function, ammo)
            def scenario(log, make=make, variant=variant):
                b, args = make(log)
                return b, args
            name = '_schedule'
            out = []
            for impl in ('pinned', 'native'):
                log = []
                b, args = make(log)
                fn = Pinned.__dict__[name].__get__(b) if impl == 'pinned' else lambda *a: NATIVE[name](b, *a)
                try:
                    result = ('ok', fn(*args))
                except Exception as error:
                    result = ('error', type(error).__name__, str(error))
                state = (b._callback_id, b._ammo_callback_id, b._callback_token is None, b._ammo_callback_token is None)
                invoked = []
                for stored in b._scheduled_store:
                    if variant == 2: b._generation = 4
                    if variant == 1: b._callback_token = b._ammo_callback_token = 'replaced'
                    try:
                        stored()
                        invoked.append('ok')
                    except Exception as error:
                        invoked.append((type(error).__name__, str(error)))
                after = (b._callback_id, b._ammo_callback_id, b._callback_token is None, b._ammo_callback_token is None,
                         b._callback_token == 'replaced', b._offframe_seconds)
                out.append((result, log, state, invoked, after))
            assert out[0] == out[1], ('_schedule', ammo, existing, variant, out)
            cases += 1
class Space(object):
    def __init__(self, log, mask, sticky):
        self.log, self._mask, self.sticky = log, mask, sticky
    @property
    def itemsVisibilityMask(self):
        return self._mask
    @itemsVisibilityMask.setter
    def itemsVisibilityMask(self, value):
        self.log.append(('space', 'set', value))
        if not self.sticky:
            self._mask = value
for boundary in (None, (5, 0x4, 0x3, 0xc)):
    for now in (1.0, 9.0):
        for variant in range(5):
            def make(log, boundary=boundary, now=now, variant=variant):
                b = battle(log)
                b._standard_space_visibility = boundary
                b._next_space_visibility_check = 2.0
                def live(space_id):
                    log.append(('battle', '_live_standard_space', space_id))
                    if variant == 3: raise _LiveSpaceVisibilityPending()
                    if variant == 4: raise KeyError('no space')
                    return Space(log, (0x1, 0x5, 0x9, 0x1)[variant], variant == 2)
                b._live_standard_space = live
                stub(b, log, '_warn_standard_space_visibility')
                return b, (now,)
            run('_maintain_standard_space_visibility', make, (boundary, now, variant)); cases += 1
for variant in range(9):
    for worker in (False, True):
        def make(log, variant=variant, worker=worker):
            b = battle(log, worker=worker)
            b._team_chat_initialized = variant == 1
            b.state = 'running' if variant != 2 else 'leaving'
            def allies():
                if variant == 5: raise ReferenceError('gone')
                if variant == 6: raise KeyError('bad')
                return ('3', 1, 'x', 2, 2, 0, 0, 4, 9)[variant]
            arena = Log(log, 'arena', {'getAlliesVehiclesNumber': allies}, missing=('getAlliesVehiclesNumber',) if variant == 3 else ())
            provider = Log(log, 'provider', {'getArenaDP': lambda: arena}, missing=('getArenaDP',) if variant == 4 else ())
            b._avatar = Log(log, 'avatar', guiSessionProvider=provider)
            b._server = Log(log, 'server')
            stub(b, log, '_run_optional_feature', variant != 7)
            return b, ()
        run('_start_team_chat_when_roster_ready', make, (variant, worker)); cases += 1
for variant in range(6):
    for worker in (True, False):
        def make(log, variant=variant, worker=worker):
            global critical_damage
            descriptor = Log(log, 'td')
            records = {'bot:2': {'engine_id': 2, 'ready': True}, 'bot:1': {'engine_id': 1, 'ready': True, 'critical_layout_prewarmed': descriptor},
                       'player:1': {'engine_id': 3, 'ready': True, 'local': True}, 'bot:3': {'engine_id': 4, 'ready': False}}
            b = battle(log, worker=worker, records=records)
            b._battle_live = variant == 1
            b._prebattle_deadline = None if variant == 2 else 5.0
            started = variant != 3
            stub(b, log, '_server_entity', lambda engine: Log(log, 'entity', isStarted=started, typeDescriptor=descriptor))
            stub(b, log, '_warn_optional_failure')
            def prewarm(td):
                log.append(('critical_damage', 'prewarm_layout'))
                if variant == 4: raise ValueError('layout broke')
                return variant != 5
            critical_damage = Log(log, 'critical_damage', {'prewarm_layout': prewarm})
            globals()['critical_damage'] = critical_damage
            return b, ()
        run('_prewarm_critical_layout', make, (variant, worker)); cases += 1
for variant in range(9):
    def make(log, variant=variant):
        records = {'player:1': {'kind': 'player', 'ready': variant != 1}, 'bot:2': {'kind': 'bot', 'ready': variant != 2},
                   'bot:3': {'kind': 'bot', 'ready': True, 'tombstone': variant == 3}}
        b = battle(log, records=records)
        b._ready_sent = variant == 4
        b._battle_live = False
        b._start_message = {'players': [1], 'bots': [2, 3] if variant != 5 else [2]}
        b._pending_bot_create_order = ['bot:9'] if variant == 6 else []
        b._pending_bot_creates = {}
        b.client = Log(log, 'client', {'send_battle_ready': lambda bases: variant != 7}, missing=('send_battle_ready',) if variant == 8 else ())
        b._spawn_planner = Log(log, 'planner', bases=['b'], missing=('capture_bases',) if variant % 2 else (), capture_bases=['c'])
        stub(b, log, '_report_lineup_windows')
        return b, ()
    run('_maybe_send_battle_ready', make, (variant,)); cases += 1
for variant in range(13):
    def make(log, variant=variant):
        b = battle(log, worker=variant != 0)
        b.state = 'running' if variant != 1 else 'stopped'
        b._generation = 2
        b._battle_result = {'w': 1} if variant == 2 else None
        b._start_message = {'round_id': 5}
        bots_answers = {'pending_manifest': (lambda: None) if variant == 8 else (lambda: [{'id': 1}])}
        b._bots = Log(log, 'bots', bots_answers, finished=variant == 3, authority_id=-1, round_id=5 if variant != 4 else 6) if variant != 12 else None
        b.client = Log(log, 'client', {'is_bot_authority': lambda: variant != 6}, authority_epoch=3,
                       round_id=5 if variant != 5 else 7, missing=('round_id',) if variant == 7 else ())
        b._bot_manifest_retry_identity = (2, 5, -1, 4) if variant == 9 else None
        b._bot_manifest_retry_deadline = 5.0 if variant == 10 else 0.0
        b._next_bot_manifest_retry = 50.0 if variant == 11 else 0.0
        stub(b, log, '_discard_pending_bot_manifest'); stub(b, log, '_enqueue_bot_manifest', 'queued')
        return b, (20.0,)
    run('_retry_bot_manifest', make, (variant,)); cases += 1
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
# Event-kind feedback handlers.
class Group(object):
    SHOT = 'shot_member'
class FeedbackTypes(Log):
    SPOTTED, TARGET_VISIBILITY, RADIO_ASSIST, TRACK_ASSIST, STUN_ASSIST = 11, 12.5, 21, 22, 23
def feedback_battle(log, variant, worker=False, records=None):
    b = battle(log, worker=worker, records=records if records is not None else {
        'player:3': {'engine_id': 30, 'local': True, 'state': {'team': 1, 'frags': 2}, 'spot_marker_visible': variant % 3 == 0},
        'bot:7': {'engine_id': 70, 'state': {'team': 2}, 'spot_visible': variant % 2 == 0},
        'bot:8': {'engine_id': '80', 'state': {'team': '2'}, 'spot_feedback_sent': variant % 5 == 0},
        'bot:9': {'engine_id': 90, 'state': {'team': 1 if variant % 4 else 'x'}},
        'player:4': {'engine_id': 40, 'state': None}})
    constants = Log(log, 'constants', ATTACK_REASON=Group() if variant % 3 else None,
                    ATTACK_REASON_INDICES={'shot_member': 6, None: 4} if variant % 4 else [1])
    types = FeedbackTypes(log, 'types', {'packDamage': lambda d, r: d * 4 + r, 'packVisibility': lambda a, c: 3})
    common = Log(log, 'common', BATTLE_EVENT_TYPE=types if variant % 7 else None)
    b._runtime = Log(log, 'runtime', constants=constants, battle_feedback_common=common)
    b._avatar = Log(log, 'avatar', missing=('onBattleEvents',) if variant % 11 == 10 else ())
    stub(b, log, '_run_optional_feature', lambda name, fn, args: fn(*args) if variant % 2 else None)
    stub(b, log, '_apply_vehicle_statistics', variant % 3 == 1)
    stub(b, log, '_present_combat_feedback', 'presented' if variant % 2 else None)
    stub(b, log, '_apply_destructible_event_visible', 'visible')
    fake_module('native_input_diagnostics', Log(log, 'diagnostics'))
    return b
for variant in range(24):
    for member, fallback in (('SHOT', 0), ('MISSING', 3), ('SHOT', '7'), ('SHOT', 'x')):
        run('_attack_reason', lambda log, v=variant: (feedback_battle(log, v), (member, fallback)), (variant, member, fallback)); cases += 1
    for target, attacker in (('bot:7', 'player:3'), ('bot:8', 'player:3'), ('bot:9', 'player:3'), ('player:3', 'bot:7'),
                             ('bot:7', 'bot:9'), ('player:4', 'player:3')):
        def make(log, v=variant, t=target, a=attacker):
            b = feedback_battle(log, v)
            return b, (b._records[t], b._records[a])
        run('_is_blind_local_attack', make, (variant, target, attacker)); cases += 1
    for event in ({'actor_kind': 'player', 'actor_id': 3, 'frags': 4}, {'actor_kind': 'bot', 'actor_id': '7', 'team_killer': 1},
                  {'actor_kind': 'bot', 'actor_id': 7.9, 'frags': '6', 'team_killer': 0}, {'actor_kind': 'bot', 'actor_id': None},
                  {'actor_kind': 'bot', 'actor_id': 'x'}, {'actor_kind': 'player', 'actor_id': 4}, {'actor_kind': 'bot', 'actor_id': 5},
                  {'actor_kind': 'player', 'actor_id': 3, 'frags': 'bad'}):
        run('_apply_vehicle_statistics_event', lambda log, v=variant, e=event: (feedback_battle(log, v), (dict(e),)),
            (variant, event)); cases += 1
    for key in ('bot:7', 'bot:8', 'player:4'):
        def make(log, v=variant, k=key):
            b = feedback_battle(log, v)
            if v % 6 == 5: del b._records[k]['engine_id']
            return b, (b._records[k],)
        run('_present_direct_spot', make, (variant, key)); cases += 1
    for worker in (False, True):
        for event in ({'observer_kind': 'player', 'observer_id': 3, 'target_kind': 'bot', 'target_id': 7},
                      {'observer_kind': 'bot', 'observer_id': 7, 'target_kind': 'bot', 'target_id': 8},
                      {'observer_kind': 'player', 'observer_id': 3, 'target_kind': 'bot', 'target_id': 99},
                      {'observer_kind': 'tank', 'observer_id': 3}, {'observer_kind': 'player', 'observer_id': None},
                      {'observer_kind': 'player', 'observer_id': 3, 'target_kind': 'bot'}):
            run('_apply_detection_event', lambda log, v=variant, w=worker, e=event: (feedback_battle(log, v, w), (dict(e),)),
                (variant, worker, event)); cases += 1
        for event in ({'assister_kind': 'player', 'assister_id': 3, 'target_kind': 'bot', 'target_id': 7, 'category': 'radio', 'damage': 40},
                      {'assister_kind': 'player', 'assister_id': 3, 'target_kind': 'bot', 'target_id': 8, 'category': 'track', 'damage': -5},
                      {'assister_kind': 'player', 'assister_id': 3, 'target_kind': 'bot', 'target_id': 7, 'category': 'stun', 'damage': None},
                      {'assister_kind': 'player', 'assister_id': 3, 'target_kind': 'bot', 'target_id': 7, 'category': 'smoke'},
                      {'assister_kind': 'player', 'assister_id': 3, 'target_kind': 'bot', 'target_id': 99, 'category': 'radio'},
                      {'assister_kind': 'bot', 'assister_id': 7, 'target_kind': 'player', 'target_id': 3, 'category': 'radio'},
                      {'assister_kind': 'player', 'assister_id': 3, 'target_kind': 'bot', 'target_id': 7, 'category': 'radio', 'damage': '9'}):
            run('_apply_assist_event', lambda log, v=variant, w=worker, e=event: (feedback_battle(log, v, w), (dict(e),)),
                (variant, worker, event)); cases += 1
        for event in ({'critical_count': 2, 'cause': 'fire', 'target_bot': 7, 'attacker': 3, 'event_id': 'e1', 'critical_mask': 5},
                      {'critical_count': 1, 'cause': 'drowning', 'target': 3, 'attacker_bot': 7},
                      {'critical_count': 2, 'cause': 'world_collision', 'target_bot': 7, 'attacker_bot': 9},
                      {'critical_count': True, 'cause': 'fire', 'target_bot': 7, 'attacker': 3},
                      {'critical_count': 2.0, 'cause': 'fire', 'target_bot': 7, 'attacker': 3},
                      {'critical_count': 0, 'cause': 'fire', 'target_bot': 7, 'attacker': 3},
                      {'critical_count': 65536, 'cause': 'fire', 'target_bot': 7, 'attacker': 3},
                      {'critical_count': 3L, 'cause': 'lava', 'target_bot': 7, 'attacker': 3},
                      {'critical_count': 3, 'cause': 'fire', 'target_bot': 99, 'attacker': 3},
                      {'critical_count': 3, 'cause': 'fire', 'target_bot': 7}):
            run('_apply_environment_critical_feedback', lambda log, v=variant, w=worker, e=event: (feedback_battle(log, v, w), (dict(e),)),
                (variant, worker, event)); cases += 1
    def make(log, v=variant):
        global native_client_drive
        native_client_drive = Log(log, 'drive', {'enabled': v % 2 == 0, 'ingress': 'ingested'})
        fake_module('native_client_drive', native_client_drive)
        return feedback_battle(log, v), ({'kind': 'destructible', 'id': v},)
    run('_apply_destructible_event', make, (variant,)); cases += 1
# Damage stickers and the round result.
class Finish(object):
    EXTERMINATION, BASE, TIMEOUT, UNKNOWN = 1, 2, 3, 9
class FinishFailure(Finish):
    FAILURE = 7
STICKER_DECODES = [('hull', 3, 'a', 'b'), ('chassis', 3, 'a', 'b'), ('', 3, 'a', 'b'), ('turret', 3, None, 'b'),
                   ('gun', 3, 'a', 'a'), ('hull', 3, 'a'), ('hull', 3, 'a', 'b', 'c'), 17, ('turret', 4, 'x', 'y')]
for variant in range(36):
    decode = STICKER_DECODES[variant % len(STICKER_DECODES)]
    for event in ({'damage_sticker': 55}, {'damage_sticker': 55, 'splash': True}, {'damage': 1}, {}):
        def make(log, v=variant, e=event, d=decode):
            b = battle(log, worker=v % 13 == 12)
            stub(b, log, '_optional_feature_enabled', v % 11 != 10)
            stub(b, log, '_warn_optional_failure')
            appearance = Log(log, 'appearance', missing=('addDamageSticker',) if v % 7 == 6 else ())
            target = Log(log, 'target', isStarted=v % 5 != 4, typeDescriptor=None if v % 17 == 16 else 'descriptor',
                         appearance=appearance)
            stub(b, log, '_server_entity', None if v % 19 == 18 else target)
            decoder = Log(log, 'decoder', {'decodeSegment': lambda code, desc: d})
            sys.modules['VehicleEffects'] = Log(log, 'VehicleEffects', DamageFromShotDecoder=decoder)
            return b, (dict(e), {'engine_id': 70})
        run('_present_damage_sticker', make, (variant, event)); cases += 1
for variant in range(48):
    for result in ({'reason': 'Team_Eliminated', 'winner': 1, 'base_team': 2}, {'reason': 'base captured', 'winner': '2', 'base_team': 1},
                   {'reason': 'time_out', 'winner': 5, 'base_team': -1}, {'reason': u'odd', 'winner': 0},
                   {'reason': None, 'winner': 'x'}, {}, ['not', 'dict'], {'reason': 'exterminated', 'winner': 2.9, 'base_team': 0}):
        def make(log, v=variant, r=result):
            b = battle(log, worker=v % 9 == 8)
            b.state = 'leaving' if v % 10 == 9 else 'running'
            b._round_finished_notified = v % 8 == 7
            b._battle_live = True
            arena = Log(log, 'arena', missing=('onTeamBaseCaptured',) if v % 3 == 2 else ())
            b._avatar = None if v % 12 == 11 else Log(log, 'avatar', missing=('onRoundFinished',) if v % 14 == 13 else (), arena=arena)
            finish = None if v % 15 == 14 else (FinishFailure() if v % 2 else Finish())
            b._runtime = Log(log, 'runtime', constants=Log(log, 'constants', FINISH_REASON=finish))
            stub(b, log, '_report_memory')
            stub(b, log, '_publish_afterbattle_period')
            stub(b, log, '_run_optional_feature', lambda name, fn, args: fn(*args))
            return b, (copy.deepcopy(r),)
        run('_apply_battle_result', make, (variant, result)); cases += 1
# Stock combat feedback RPCs.
class HitFlags(object):
    pass
for index, name in enumerate(('ATTACK_IS_EXTERNAL_EXPLOSION', 'ATTACK_IS_DIRECT_PROJECTILE',
                              'MATERIAL_WITH_POSITIVE_DF_PIERCED_BY_EXPLOSION', 'MATERIAL_WITH_POSITIVE_DF_NOT_PIERCED_BY_PROJECTILE',
                              'MATERIAL_WITH_POSITIVE_DF_PIERCED_BY_PROJECTILE', 'RICOCHET', 'DEVICE_PIERCED_BY_EXPLOSION',
                              'DEVICE_DAMAGED_BY_EXPLOSION', 'CHASSIS_DAMAGED_BY_EXPLOSION', 'GUN_DAMAGED_BY_EXPLOSION',
                              'DEVICE_PIERCED_BY_PROJECTILE', 'DEVICE_DAMAGED_BY_PROJECTILE', 'CHASSIS_DAMAGED_BY_PROJECTILE',
                              'GUN_DAMAGED_BY_PROJECTILE', 'FIRE_STARTED', 'VEHICLE_KILLED')):
    setattr(HitFlags, name, 1 << (index * 2 if index < 15 else 31))
class CombatTypes(FeedbackTypes):
    DAMAGE, CRIT, KILL, TANKING, RECEIVED_DAMAGE, RECEIVED_CRIT = 31, 32, 33.2, 34, 35, 36
CRITICAL_SETS = [
    None, {'events': [{'kind': 'crew', 'state': 'destroyed'}, {'kind': 'device', 'state': 'critical', 'name': 'engineHealth'}]},
    {'events': [{'kind': 'device', 'state': 'destroyed', 'name': 'gunHealth', 'old_state': 'critical'},
                {'kind': 'device', 'state': 'critical', 'name': 'leftTrackHealth', 'cause': 'explosion'},
                {'kind': 'fire', 'state': True}, {'kind': 'device', 'state': 'destroyed', 'old_state': 'destroyed'}]},
    {'events': [{'kind': 'device', 'state': 'destroyed', 'name': 'ammoBayHealth'}, {'kind': 'fire', 'state': 0, 'cause': 'explosion'}]},
    {'events': None}, {'events': [{'kind': 'device', 'state': 'repaired'}, {'kind': 'crew', 'state': 'critical'}]},
    {'events': [{'kind': 'device', 'state': 'critical', 'name': 'rightTrackHealth'},
                {'kind': 'device', 'state': 'critical', 'name': 'turretRotatorHealth', 'old_state': 'destroyed'},
                {'kind': 'device', 'state': 'destroyed', 'name': 'rightTrackHealth', 'cause': 'explosion'}]},
]
FEEDBACK_EVENTS = [
    {'kind': 'hit', 'source': 'shot', 'damage': 120, 'shot_result': 2},
    {'kind': 'bot_hit', 'source': 'shot', 'damage': 0, 'shot_result': 2, 'blocked_damage': 40},
    {'kind': 'bot_human_hit', 'source': 'shot', 'damage': 0, 'shot_result': 1, 'splash': True},
    {'kind': 'hit', 'source': 'shot', 'damage': 15, 'splash': 1, 'dead': True},
    {'kind': 'bot_bot_hit', 'source': 'shot', 'damage': None, 'shot_result': 0},
    {'kind': 'hit', 'source': 'ram', 'damage': 30, 'blocked_damage': '5'},
    {'kind': 'hit', 'source': 'environment', 'push_attributed': True, 'damage': 50},
    {'kind': 'environment_critical', 'source': 'environment', 'critical_count': '3', 'damage': -4},
    {'kind': 'hit', 'source': 'shot', 'damage': 9, 'shot_result': '7', 'critical_feedback_separate': True},
    {'kind': 'health', 'source': 'fire', 'damage': 3},
    {'kind': 'hit', 'source': 'shot', 'damage': 0, 'shot_result': 1},
]
for variant in range(30):
    for event_index, base_event in enumerate(FEEDBACK_EVENTS):
        for roles in (('player:3', 'bot:7'), ('bot:7', 'player:3'), ('bot:9', 'player:3'), ('player:3', 'bot:9')):
            def make(log, v=variant, e=base_event, roles=roles):
                b = feedback_battle(log, v)
                b._records['bot:7']['spot_marker_visible'] = v % 4 != 3
                types = CombatTypes(log, 'types', {'packDamage': lambda *a: sum(x for x in a[:2] if isinstance(x, int)) * 8,
                                                   'packCrits': lambda *a: a[0] * 3, 'packVisibility': lambda a, c: 3})
                indices = {'HIGH_EXPLOSIVE': '2'} if v % 5 else {}
                constants = Log(log, 'constants', ATTACK_REASON=Group(), ATTACK_REASON_INDICES={'shot_member': 6},
                                VEHICLE_HIT_FLAGS=None if v % 23 == 22 else HitFlags, SHELL_TYPES_INDICES=indices)
                compatibility = Log(log, 'compatibility', missing=('assert_vehicle_marker_damage_type',) if v % 19 == 18 else ())
                b._runtime = Log(log, 'runtime', constants=constants, compatibility=compatibility,
                                 battle_feedback_common=Log(log, 'common', BATTLE_EVENT_TYPE=None if v % 29 == 28 else types))
                b._avatar = Log(log, 'avatar', missing=('showShotResults',) if v % 17 == 16 else ())
                stub(b, log, '_feedback_shell_fields', (2, v % 2 == 1) if v % 3 else (1, False))
                stub(b, log, '_assert_player_identity')
                stub(b, log, '_combat_attack_reason', 4)
                voice = Log(log, 'critical_voice')
                sys.modules['gui.mods.offline_lan_0922.critical_voice'] = voice
                sys.modules['gui.mods.offline_lan_0922'].critical_voice = voice
                fake_module('native_input_diagnostics', Log(log, 'diagnostics'))
                event = copy.deepcopy(e)
                event['critical'] = copy.deepcopy(CRITICAL_SETS[v % len(CRITICAL_SETS)])
                event['dead'] = event.get('dead') or v % 7 == 6
                event['event_id'] = 'e%d' % v
                target, attacker = roles
                return b, (event, b._records[target], b._records[attacker]) + ((None,) if v % 2 else (v,))
            run('_present_combat_feedback', make, (variant, event_index, roles)); cases += 1
# Shot hit presentation.
class Vec(object):
    def __init__(self, xyz): self.xyz = tuple(float(v) for v in xyz)
    def __repr__(self): return 'Vec(%.4f,%.4f,%.4f)' % self.xyz
    @property
    def length(self): return math.sqrt(sum(v * v for v in self.xyz))
    def normalise(self):
        n = self.length
        if n > 0: self.xyz = tuple(v / n for v in self.xyz)
    def scale(self, f): return Vec([v * f for v in self.xyz])
    def __sub__(self, o): return Vec([a - b for a, b in zip(self.xyz, o.xyz)])
    def __add__(self, o): return Vec([a + b for a, b in zip(self.xyz, o.xyz)])
class Rules(object):
    @staticmethod
    def is_he(shot): return shot == 'he'
combat_rules = Rules()
HIT_EVENTS = [
    {'kind': 'hit', 'source': 'shot', 'world_pose': True, 'x': 3, 'y': 1, 'z': 4, 'damage': 50, 'shot_result': 2, 'critical': {'events': []}},
    {'kind': 'bot_hit', 'source': 'shot', 'world_pose': 1, 'x': '2', 'y': None, 'z': 9, 'damage': 0, 'shot_result': 0},
    {'kind': 'bot_bot_hit', 'source': 'shot', 'world_pose': True, 'x': 1, 'y': 1, 'z': 1, 'damage': 20, 'splash': True},
    {'kind': 'hit', 'source': 'shot', 'world_pose': True, 'x': 0, 'y': 0, 'z': 0, 'damage': 0, 'shot_result': '1'},
    {'kind': 'hit', 'source': 'shot', 'world_pose': None, 'damage': 5},
    {'kind': 'hit', 'source': 'ram', 'world_pose': True},
    {'kind': 'health', 'source': 'shot', 'world_pose': True},
    {'kind': 'hit', 'source': 'shot', 'world_pose': True, 'x': float('nan'), 'y': 2, 'z': 2, 'damage': 7, 'shot_result': 9},
]
for variant in range(36):
    for event_index, base in enumerate(HIT_EVENTS):
        for roles in (('player:3', 'bot:7'), ('bot:7', 'player:3'), ('bot:7', 'bot:7'), ('bot:8', 'bot:9')):
            def make(log, v=variant, e=base, roles=roles):
                b = feedback_battle(log, v, worker=v % 17 == 16)
                b._records['bot:7']['spot_visible'] = v % 5 != 4
                b._records['bot:7']['network_id'] = 7
                positions = {'player:3': (0.0, 0.0, 0.0), 'bot:7': (3.0, 1.0, 4.0) if v % 6 else (0.0, 0.0, 0.0),
                             'bot:8': (1.0, 2.0, 3.0), 'bot:9': (-2.0, 0.0, 5.0)}
                stub(b, log, '_event_shell', ('he' if v % 3 == 0 else 'ap', {'effectsIndex': None if v % 19 == 18 else v % 2}))
                stub(b, log, '_record_position', lambda r: positions[[k for k, x in b._records.items() if x is r][0]])
                b._vector = lambda xyz: (log.append(('battle', '_vector', repr(tuple(xyz)))), Vec(xyz))[1]
                stub(b, log, '_hit_indicator_marker', lambda e, shot, damage: (damage * 2, v % 2 == 0))
                stub(b, log, '_critical_hit_mask', 5)
                stub(b, log, '_projectile_cosmetic_allowed', v % 13 != 12)
                b._projectile_visual_meta = {'meta': v}
                effects = {'armorHit': ('stagesH', 'effectsH', 0), 'armorResisted': ('stagesR', 'effectsR', 0),
                           'armorRicochet': ('stagesC', 'effectsC', 0)}
                b._runtime.vehicles = Log(log, 'vehicles', g_cache=Log(log, 'cache', shotEffects=[effects, effects]))
                stub(b, log, '_present_hit_impulse')
                stub(b, log, '_hit_damage_factor', 0.5)
                stub(b, log, '_present_splash_hit', 'splash')
                stub(b, log, '_optional_feature_enabled', v % 11 != 10)
                stub(b, log, '_report_effect')
                stub(b, log, '_impact_sound_identity', {'sound': v} if v % 7 else {'dir': 1})
                stub(b, log, '_warn_optional_failure')
                terrain = Log(log, 'terrain', missing=('addNew',) if v % 23 == 22 else ())
                b._avatar = Log(log, 'avatar', terrainEffects=terrain)
                target, attacker = roles
                return b, (copy.deepcopy(e), b._records[target], b._records[attacker], v % 4 and 70 + v)
            run('_present_combat_hit', make, (variant, event_index, roles)); cases += 1
# Ordered shot presentation.
class Recorder(object):
    def __init__(self, log): self.log = log
    def write(self, text): self.log.append(('stdout', text))
class Gun(object):
    def __init__(self, burst): self.burst = burst
class Descriptor(object):
    def __init__(self, burst): self.gun = Gun(burst)
    def __repr__(self): return '<descriptor>'
class Entity(object):
    def __init__(self, log, v):
        object.__setattr__(self, '_log', log)
        object.__setattr__(self, 'typeDescriptor', Descriptor((3, 0.1) if v % 4 == 1 else ('x',) if v % 4 == 2 else () if v % 4 == 3 else (1,)))
        object.__setattr__(self, 'appearance', Log(log, 'appearance', turretMatrix=0.5, gunMatrix=None if v % 3 else 0.25))
        if v % 2: object.__setattr__(self, '_offlineLANPresentation', True)
    def __repr__(self): return '<entity>'
    def __setattr__(self, name, value):
        self._log.append(('entity', 'set', name, rep(value)))
        object.__setattr__(self, name, value)
    def __delattr__(self, name):
        self._log.append(('entity', 'del', name))
        object.__delattr__(self, name)
    def showShooting(self, *args): self._log.append(('entity', 'showShooting') + args)
class Mat(object):
    def __init__(self, value):
        if value is None: raise TypeError('no matrix')
        self.yaw, self.pitch = value, value / 2
SHOT_EVENTS = [
    {'kind': 'shot', 'attacker_bot': 7, 'projectile_id': 'p7', 'origin': (1, 2, 3), 'velocity': (0, 0, 9), 'gravity': '9.8',
     'maxDistance': 500, 'shot_yaw': 0.2, 'shot_pitch': -0.1, 'burst_index': 0, 'shell_index': 2, 'bot_launch_time_us': 5},
    {'kind': 'shot', 'attacker': 3, 'projectile_id': 'p3', 'burst_index': '1', 'shell_index': None, 'fire_intent_seq': 4, 'shot_seq': 2},
    {'kind': 'shot', 'attacker': 3, 'projectile_id': 'p4', 'origin': (0, 0, 0), 'velocity': (1, 0, 0), 'gravity': None,
     'maxDistance': 1, 'burst_index': 'x', 'burst_count': '4'},
    {'kind': 'shot', 'attacker_bot': 8, 'projectile_id': 'p8', 'shot_yaw': 1},
    {'kind': 'shot', 'projectile_id': 'p0'},
    {'kind': 'shot', 'attacker_bot': 99, 'projectile_id': 'p9'},
    {'kind': 'shot', 'attacker_bot': 7, 'projectile_id': 'done', 'origin': 1, 'velocity': 2, 'gravity': 3, 'maxDistance': 4},
]
for variant in range(40):
    for event_index, base in enumerate(SHOT_EVENTS):
        for update in (True, False):
            log_holder = []
            def make(log, v=variant, e=base, update=update):
                log_holder.append(log)
                sys.stdout = Recorder(log)
                b = feedback_battle(log, v, worker=v % 19 == 18)
                b._records['bot:7']['kind'] = 'bot'
                b._records['bot:7']['_remote_aim_signature'] = (1, 2, 3) if v % 5 else 7
                b._records['bot:8']['kind'] = 'bot'
                b._replay_mode = v % 7 == 6
                b._local_projectile_presented = set(['p3'] if v % 9 == 8 or v % 7 == 6 else [])
                b._local_projectile_presentations = {'p4': 1} if v % 10 == 9 else {}
                stub(b, log, '_clock', 2.5)
                fake_module('native_client_environment', Log(log, 'environment'))
                b._local_fire_intent = ({'player_burst': {'committed': (v // 6) % 2, 'count': 5}} if v % 6 == 1 else
                                        {'other': 1} if v % 6 == 2 else None)
                entity = Entity(log, v)
                stub(b, log, '_server_entity', None if v % 23 == 22 else entity)
                b._sync = None if v % 8 == 0 else Log(log, 'sync', _entities={'bot:7': {'interpolation_delay_us': 9, 'consumer_resume_pending': 1}})
                b._runtime = Log(log, 'runtime', math=Log(log, 'math', {'Matrix': Mat}))
                b._start_message = {'round_id': 3} if v % 2 else None
                b._fire_timeline = {'p3': {'trigger': 4.0}, 'p7': {'trigger': 6.0, 'shown': None}} if v % 4 else {}
                b._projectile_visual_terminals = set(['done'])
                meta = {'projectile_id': e.get('projectile_id'), 'burst_index': v % 2,
                        'burst_count': ('3', None, '-2')[v % 3], 'is_he': False,
                        'segment_origin': 1, 'segment_velocity': 2, 'segment_start_time_ms': 3, 'ricochet_count': 0,
                        'bot_presentation_time_us': 11 if v % 5 == 1 else None, 'shell': 'ap'}
                stub(b, log, '_projectile_wire_meta', None if v % 11 == 10 else (lambda ev: dict(meta)))
                b._projectile_meta = {'p7': dict(meta, is_he=v % 13 == 12)} if v % 2 else {}
                stub(b, log, '_install_projectile_meta', lambda m: dict(m, installed=True))
                stub(b, log, '_projectile_visual_start', 'start')
                def ensure(*args, **kwargs):
                    log.append(('battle', '_ensure_projectile_visual', len(args), sorted(kwargs)))
                    if v % 17 == 16: raise ValueError('visual failed')
                b._ensure_projectile_visual = ensure
                b._projectile_visual_meta = {'p7': {'admitted': v % 4 != 3}} if v % 3 else {}
                stub(b, log, '_optional_feature_enabled', v % 12 != 11)
                stub(b, log, '_shot_muzzle_drawn', v % 14 != 13)
                stub(b, log, '_run_optional_feature')
                stub(b, log, '_show_replay_local_muzzle')
                stub(b, log, '_show_local_shot_without_extra')
                return b, (copy.deepcopy(e), update)
            try:
                run('_show_shot', make, (variant, event_index, update)); cases += 1
            finally:
                sys.stdout = sys.__stdout__
# A failure after the transient entity attributes were set still removes them (try/finally).
for impl in ('pinned', 'native'):
    log = []
    b, values = make(log, v=16, e=SHOT_EVENTS[0], update=False)
    sys.stdout = sys.__stdout__
    entity = b._server_entity()
    try:
        (Pinned.__dict__['_show_shot'].__get__(b) if impl == 'pinned' else lambda *a: NATIVE['_show_shot'](b, *a))(*values)
    except ValueError:
        pass
    else:
        raise AssertionError('visual failure did not propagate')
    left = sorted(k for k in entity.__dict__ if k != '_offlineLANShotIndex' and k.startswith('_offlineLANShot') or k.startswith('_offlineLANProjectile') or k == '_offlineLANCanonicalTracerOwned')
    assert left == [], (impl, left)
cases += 2
# Projectile ownership, meta and descriptor leaves.
import collections
class Client(object):
    pass
def shot_battle(log, v):
    b = feedback_battle(log, v, worker=v % 4 == 1)
    b._replay_mode = v % 9 == 7
    b.client = Client()
    if v % 7: b.client.player_id = (3, 3L, True, 0, -1, 'x', None)[v % 7]
    if v % 5 == 1: b.client.is_bot_authority = lambda: (log.append(('client', 'is_bot_authority')), v % 3)[1]
    elif v % 5 == 2: b.client.is_bot_authority = lambda: 1 // 0
    elif v % 5 == 3: b.client.is_bot_authority = 'not callable'
    b._projectile_server_time_ms = None if v % 8 == 7 else (1000, 1000L, 2.5)[v % 3]
    b._projectile_server_local_time = None if v % 10 == 9 else (1.5, '2', 10)[v % 3]
    b._fire_timeline = collections.OrderedDict(('k%d' % i, i) for i in range(60 + v % 8))
    stub(b, log, '_resolve_descriptor', lambda vehicle: 1 // 0 if vehicle == 'bad' else ('descriptor', vehicle))
    entity = Log(log, 'source', typeDescriptor=None if v % 3 == 0 else Descriptor2(v))
    stub(b, log, '_server_entity', None if v % 6 == 5 else entity)
    return b
class Gun2(object):
    def __init__(self, v): self.shots = ([{'shell': {'effectsIndex': 4, 'isTracer': True}}, {'shell': None}], None, ())[v % 3]
class Descriptor2(object):
    def __init__(self, v):
        self.gun = Gun2(v)
        if v % 2: self.activeGunShotIndex = 1
    def __repr__(self): return '<descriptor2>'
METAS = [
    {'shooter_kind': 'player', 'shooter_id': 3, 'projectile_id': 'a', 'ricochet_count': 0, 'shell_index': 1,
     'source_shot': {'shell': {'damage': 5}, 'speed': 9}},
    {'shooter_kind': 'bot', 'shooter_id': 7, 'projectile_id': 'b', 'ricochet_count': 1, 'source_vehicle': 'v7', 'source_shot': None},
    {'shooter_kind': 'bot', 'shooter_id': 8, 'projectile_id': 'c', 'ricochet_count': 0.0, 'source_vehicle': 'bad', 'shell_index': '7'},
    {'shooter_kind': 'player', 'shooter_id': 4, 'source_descriptor': 'cached', 'source_shot': {'shell': None}},
    {'shooter_kind': 'player', 'shooter_id': 3, 'projectile_id': 'e', 'ricochet_count': 0, 'source_shot': ['frozen']},
    {'shooter_kind': 'bot', 'shooter_id': 99, 'projectile_id': 'd', 'ricochet_count': 2, 'source_vehicle': '', 'shell_index': 'x'},
]
for variant in range(30):
    run('_projectile_is_authority', lambda log, v=variant: (shot_battle(log, v), ()), (variant,)); cases += 1
    for now in (3.0, '4', 0):
        run('_projectile_estimated_server_time', lambda log, v=variant, now=now: (shot_battle(log, v), (now,)), (variant, now)); cases += 1
    for key in ('k1', 'new', 'k70'):
        run('_remember_fire_timeline', lambda log, v=variant, key=key: (shot_battle(log, v), (key, 9.5)), (variant, key)); cases += 1
    for index, meta in enumerate(METAS + [None, ['list']]):
        make = lambda log, v=variant, m=meta: (shot_battle(log, v), (copy.deepcopy(m),))
        run('_owns_projectile', make, (variant, index)); cases += 1
        if isinstance(meta, dict):
            for name in ('_new_projectile_meta', '_projectile_source_descriptor', '_projectile_attacker_engine_id',
                         '_projectile_source_entity', '_projectile_shot'):
                run(name, make, (variant, index)); cases += 1
# Muzzle and visual helpers.
class Extras(dict):
    def __init__(self, log, frozen=False, **items):
        dict.__init__(self, **items); self._log, self._frozen = log, frozen
    def __setitem__(self, key, value):
        self._log.append(('extras', 'set', key, type(value).__name__))
        if self._frozen: raise TypeError('frozen extras')
        dict.__setitem__(self, key, value)
class Shooter(object):
    def __init__(self, log, v, extras):
        self._log, self._v = log, v
        self.typeDescriptor = Log(log, 'descriptor', extrasDict=extras)
    def __repr__(self): return '<shooter>'
    def showShooting(self, count, flag):
        self._log.append(('shooter', 'showShooting', count, flag, type(self.typeDescriptor.extrasDict.get('shoot')).__name__))
        if self._v % 11 == 10: raise ValueError('shooting failed')
        return 'shot'
for variant in range(36):
    for meta in ({'shooter_kind': 'player', 'projectile_id': 'p', 'shooter_id': 3, 'launch_server_time_ms': 1200.7},
                 {'shooter_kind': 'bot'}, {'shooter_kind': 'player', 'launch_server_time_ms': 'x'}, {}):
        def make(log, v=variant, m=meta):
            sys.stdout = Recorder(log)
            return shot_battle(log, v), (dict(m),)
        try:
            run('_report_player_shot_timing', make, (variant, meta)); cases += 1
        finally:
            sys.stdout = sys.__stdout__
    for pid in (None, 'p1'):
        def make(log, v=variant, pid=pid):
            b = shot_battle(log, v)
            if v % 6 == 0: b._remote_factory = None
            elif v % 6 == 1: b._remote_factory = Log(log, 'factory', missing=('admit_projectile_visual',))
            else: b._remote_factory = Log(log, 'factory', {'admit_projectile_visual': (lambda a, p, n: 1 // 0) if v % 6 == 2 else (lambda a, p, n: v % 3)})
            stub(b, log, '_warn_optional_failure')
            return b, (7, pid, 2.5)
        run('_admit_projectile_visual', make, (variant, pid)); cases += 1
    for key in ('player:3', 'bot:7', 'bot:8'):
        def make(log, v=variant, key=key):
            b = shot_battle(log, v)
            b._records['bot:7']['spot_visible'] = v % 3 == 0
            b._records['player:3']['spot_visible'] = v % 3 == 1
            return b, (b._records[key],)
        run('_shot_muzzle_drawn', make, (variant, key)); cases += 1
    def make(log, v=variant):
        b = shot_battle(log, v)
        b._remote_factory = (None, Log(log, 'factory', missing=('reset_projectile_visuals',)),
                             Log(log, 'factory', {'reset_projectile_visuals': lambda: 1 // 0}),
                             Log(log, 'factory', {'reset_projectile_visuals': lambda: v % 2}))[v % 4]
        b._projectile_visual_meta = {'old': 1}
        stub(b, log, '_warn_optional_failure')
        return b, ()
    run('_reset_projectile_visuals', make, (variant,)); cases += 1
    def make(log, v=variant):
        b = shot_battle(log, v)
        stub(b, log, '_warn_optional_failure')
        extra = Log(log, 'extra', {'stopFor': (lambda e: 1 // 0) if v % 7 == 6 else None})
        extras = None if v % 9 == 8 else Extras(log, frozen=v % 5 == 4, **({} if v % 4 == 3 else {'shoot': extra}))
        return b, (Shooter(log, v, extras), 2 + v % 3)
    run('_show_local_shot_without_extra', make, (variant,)); cases += 1
    def make(log, v=variant):
        b = shot_battle(log, v)
        if v % 3: b._replay_muzzle_count = (1, 49, 99)[v % 3]
        fake_module('offline_replay', Log(log, 'offline_replay'))
        sys.modules['gui.mods.offline_lan_0922'].offline_replay = sys.modules['gui.mods.offline_lan_0922.offline_replay']
        extras = Extras(log, **({'shoot': Log(log, 'extra')} if v % 8 != 7 else {}))
        return b, (Shooter(log, v, extras), 1 + v % 4)
    run('_show_replay_local_muzzle', make, (variant,)); cases += 1
# Projectile launch normalization.
class Protocol(object):
    def _strict_burst_group(self, shot, group, index, count):
        if group == 'boom': raise KeyError('unexpected')
        if count == 0: return None
        return (int(group), int(index), int(count))
    def _strict_projectile_source_shot(self, value):
        if value == 'bad': raise ValueError('source shot')
        return value
    def _projectile_source_shot_matches_launch(self, shot, velocity, gravity, maximum, is_he, splash):
        return shot.get('ok', True)
lan_protocol = Protocol()
class Rules2(Rules):
    @staticmethod
    def first_ricochet_penetration_multiplier(kind):
        return {'ARMOR_PIERCING': 0.75, 'HOLLOW_CHARGE': None}.get(kind, 1.0)
combat_rules = Rules2()
BASE_LAUNCH = {'projectile_id': 'p1', 'source_vehicle': 'tank', 'shot_seq': 3, 'burst_group_seq': 3, 'burst_index': 0,
               'burst_count': 1, 'shell_index': 1, 'origin': [1, 2, 3], 'velocity': (0, 0, 800), 'gravity': 9.81,
               'max_time_ms': 5000, 'is_he': False, 'splash_radius': 0, 'penetration_factor': 1, 'launch_server_time_ms': 100,
               'source_shot': {'shell': {'kind': 'ARMOR_PIERCING'}}, 'range_origin': (1, 2, 3), 'segment_origin': (1, 2, 3),
               'segment_velocity': (0, 0, 800), 'segment_start_time_ms': 0, 'ricochet_count': 0,
               'base_penetration_multiplier': 1.0, 'max_distance': 700, 'shooter_kind': 'bot', 'shooter_id': 7}
LAUNCH_EDITS = [
    {}, {'shooter_kind': None, 'attacker': 3, 'fire_intent_seq': 2, 'fire_input_seq': '5'}, {'shooter_kind': None, 'attacker_bot': '9'},
    {'shooter_kind': 'player', 'shooter_id': 3}, {'shooter_kind': 'player', 'shooter_id': 3, 'fire_intent_seq': 0, 'fire_input_seq': 1},
    {'fire_intent_seq': 1}, {'max_distance': None, 'maxDistance': '650'}, {'max_distance': None}, {'origin': (1, 2)},
    {'velocity': 'abc'}, {'gravity': 'x'}, {'gravity': 0}, {'gravity': float('nan')}, {'max_time_ms': 10 ** 30},
    {'projectile_id': ''}, {'source_vehicle': 'v' * 129}, {'shooter_kind': 'tank'}, {'shooter_id': -1}, {'shot_seq': 0},
    {'burst_count': 0}, {'burst_group_seq': 'boom'}, {'shell_index': 10}, {'shell_index': '-1'}, {'is_he': 1},
    {'segment_start_time_ms': 6000}, {'ricochet_count': 2}, {'ricochet_count': 1, 'segment_start_time_ms': 5000},
    {'ricochet_count': 1, 'segment_start_time_ms': 40, 'base_penetration_multiplier': 0.75, 'segment_origin': (5, 5, 5),
     'checked_through_ms': 50},
    {'ricochet_count': 1, 'segment_start_time_ms': 40, 'base_penetration_multiplier': 0.9, 'checked_through_ms': 50},
    {'ricochet_count': 1, 'segment_start_time_ms': 40, 'source_shot': {'shell': {'kind': 'HOLLOW_CHARGE'}}, 'checked_through_ms': 50},
    {'ricochet_count': 1, 'segment_start_time_ms': 5000, 'base_penetration_multiplier': 0.75, 'checked_through_ms': 5000},
    {'segment_origin': (1, 2, 4)}, {'splash_radius': -1}, {'source_shot': 'bad'}, {'source_shot': {'ok': False}},
    {'checked_through_ms': 200, 'checked_distance': '12.5', 'piercing_loss': 3}, {'checked_through_ms': 9000},
    {'checked_distance': 701}, {'piercing_loss': 'x'}, {'piercing_loss': float('inf')},
    {'bot_presentation_time_us': 55}, {'bot_presentation_time_us': True}, {'bot_presentation_time_us': -3},
    {'shooter_kind': 'player', 'shooter_id': 3, 'fire_intent_seq': 1, 'fire_input_seq': 1, 'bot_presentation_time_us': 5},
    {'shot_seq': 10 ** 25}, {'max_time_ms': '4000', 'launch_server_time_ms': 99.9},
]
for index, edit in enumerate(LAUNCH_EDITS + ['raw', None]):
    def make(log, edit=edit):
        if not isinstance(edit, dict): return battle(log), (edit,)
        raw = dict(BASE_LAUNCH); raw.update(edit)
        return battle(log), (raw,)
    run('_projectile_wire_meta', make, (index,)); cases += 1
# Projectile poses, matrices, failure diagnostics and Bot launch acknowledgement.
POSES = [((1, 2, 3), {'yaw': 0.5, 'aim_yaw': 1.0, 'pitch': 0.1, 'siege_state': 2}), ([0, 0, 0], None),
         (('1', 2.5, None), {'yaw': 'x', 'turret_yaw': 0.2, 'siege_state': '1'}), ((4, 5, 6), {'gun_pitch': 0.3, 'siege_state': None}),
         ((1, 2, 3), {'yaw': float('nan'), 'aim_yaw': 3.5}), ((7, 8, 9), {'yaw': 1, 'siege_state': 'bad'}), ((1, 2), {}), ((2, 2, 2), {'yaw': 0.7})]
for index, (position, state) in enumerate(POSES):
    for name in ('_projectile_plain_pose', '_projectile_boundary_pose'):
        run(name, lambda log, position=position, state=state: (battle(log), (position, copy.deepcopy(state))), (index,)); cases += 1
for index, raw in enumerate([{'segment_start_time_ms': 100, 'base_checked_ms': 600, 'max_time_ms': 5000},
                             {'checked_through_ms': 99999}, {'segment_start_time_ms': 'x'}, {}, None, {'max_time_ms': 50, 'segment_start_time_ms': 80},
                             {'segment_start_time_ms': 10, 'checked_through_ms': 5}, {'max_time_ms': 10 ** 400}]):
    run('_projectile_visual_age', lambda log, raw=raw: (battle(log), (copy.deepcopy(raw),)), (index,)); cases += 1
class Started(object):
    def __init__(self, started, position=None):
        self.isStarted = started
        if position is not None: self.position = position
    def __repr__(self): return '<started %s>' % self.isStarted
for variant in range(24):
    def make(log, v=variant):
        b = shot_battle(log, v)
        b._records['bot:9']['tombstone'] = v % 3 == 0
        b._records['bot:7']['state'] = (1, 2, 3)
        b._local_position = [9.0, 8.0, 7.0]
        entities = {30: Started(True), 70: Started(v % 2 == 0, (5, 5, 5) if v % 4 else None), '80': None, 90: Started(True, (1, 1, 1)),
                    40: Started(v % 6 != 5, (4, 4, 4))}
        stub(b, log, '_server_entity', lambda engine: entities.get(engine))
        return b, ()
    run('_projectile_record_positions', make, (variant,)); cases += 1
    for ground in (None, 'ground'):
        def make(log, v=variant, ground=ground):
            b = shot_battle(log, v)
            record = {'native_remote': v % 3 != 0, 'engine_id': 70}
            b._remote_factory = (None, Log(log, 'factory', missing=('projectile_collision_matrices', 'projectile_collision_matrix')),
                                 Log(log, 'factory', {'projectile_collision_matrices': None, 'projectile_collision_matrix': 'body'}),
                                 Log(log, 'factory', {'projectile_collision_matrices': ('b', 'c')}),
                                 Log(log, 'factory', {'projectile_collision_matrices': None, 'projectile_collision_matrix': None}))[v % 5]
            return b, (record, Log(log, 'vehicle', matrix='vehicle-matrix'), ground)
        run('_projectile_vehicle_matrices', make, (variant, ground)); cases += 1
    for pose in ({'siege_state': 2}, {'siege_state': '1'}, {}, {'siege_state': 'x'}):
        def make(log, v=variant, pose=pose):
            b = shot_battle(log, v)
            descriptor = Log(log, 'descriptor', hasSiegeMode=v % 4 != 0, defaultVehicleDescr=None if v % 7 == 6 else 'default',
                             siegeVehicleDescr='siege')
            states = Log(log, 'states', ENABLED=1) if v % 3 == 1 else Log(log, 'states', missing=('ENABLED',))
            b._runtime = Log(log, 'runtime', constants=Log(log, 'constants', VEHICLE_SIEGE_STATE=states))
            target = Log(log, 'target', typeDescriptor=None if v % 5 == 4 else descriptor)
            return b, (target, pose)
        run('_projectile_descriptor_at_pose', make, (variant, pose)); cases += 1
    for error in (None, 'text\nwith\rbreaks' + 'x' * 200, ValueError('bad'), u'unicode'):
        def make(log, v=variant, error=error):
            sys.stdout = Recorder(log)
            b = shot_battle(log, v)
            meta = {'projectile_id': 'p%d' % v, 'terminal_failure_boundary': 'b'}
            if v % 3 == 0: meta['terminal_failures_reported'] = set([('stage', 'reason')])
            return b, (meta, {'elapsed': (0.0125, -1, 'x', 2.5)[v % 4]}, 'stage', 'reason' if v % 2 else 'other', error)
        try:
            run('_report_projectile_terminal_failure', make, (variant, repr(error))); cases += 1
        finally:
            sys.stdout = sys.__stdout__
    for normalized in ({'shooter_kind': 'bot', 'shooter_id': 7, 'shot_seq': 3}, {'shooter_kind': 'bot', 'shooter_id': '7', 'shot_seq': 2},
                       {'shooter_kind': 'bot', 'shooter_id': 7, 'shot_seq': 1}, {'shooter_kind': 'bot', 'shooter_id': 'x', 'shot_seq': 3},
                       {'shooter_kind': 'bot', 'shot_seq': 3}, {'shooter_kind': 'player', 'shooter_id': 7, 'shot_seq': 3}, None):
        def make(log, v=variant, n=normalized):
            sys.stdout = Recorder(log)
            b = shot_battle(log, v)
            b._bots = None if v % 11 == 10 else Log(log, 'bots', {'is_authority': v % 5 != 4,
                                                                   'ack_projectile_launch': (lambda bot, seq: seq != 3 or v % 7 != 6)},
                                                      missing=('ack_projectile_launch',) if v % 13 == 12 else ())
            b._bot_fire_seen = {7: 1} if v % 2 else {}
            b._bot_fire_confirmations = {7: set([2])} if v % 3 == 1 else {}
            b._bot_launch_payloads = {(7, 2): 'p2', (7, 3): 'p3'}
            b._fire_timeline = {('bot_publish', 7, 3): {'wall': 4.0}} if v % 4 else {}
            return b, (copy.deepcopy(n),)
        try:
            run('_confirm_bot_projectile_launch', make, (variant, normalized)); cases += 1
        finally:
            sys.stdout = sys.__stdout__
# Projectile visual start, epochs, poses and collision targets.
PROJECTILE_VISUAL_START_MAX_DIFF = 2.0
class _RecentIdSet(object):
    def __repr__(self): return '<recent>'
class _ProjectileCollisionAppearance(object):
    def __init__(self, *args): self.args = args
    def __repr__(self): return 'Appearance%r' % (self.args,)
class _ProjectileCollisionTarget(object):
    def __init__(self, *args): self.args = args
    def __repr__(self): return 'Target%r' % (self.args,)
class Mat2(object):
    def __init__(self, source=None): self.source, self.ypr, self.translation = source, None, (1.0, 2.0, 3.0)
    def setRotateYPR(self, ypr): self.ypr = ypr
    def __repr__(self): return 'Mat2(%r,%r,%r)' % (self.source, self.ypr, self.translation)
class Compound(object):
    def __init__(self, v): self.v = v
    def node(self, name):
        if self.v % 7 == 6: raise KeyError(name)
        return name
class Projectiles(object):
    def __init__(self, log, now, ok): self.log, self.now, self.ok = log, now, ok
    def reset(self, at): self.log.append(('projectiles', 'reset', at)); return self.ok
    def __repr__(self): return '<projectiles>'
for variant in range(30):
    for normalized in ({'ricochet_count': 0, 'segment_origin': (1.0, 2.0, 3.5)}, {'ricochet_count': 1},
                       {'ricochet_count': 0, 'segment_origin': (9, 9, 9)}, {'ricochet_count': 0, 'base_checked_ms': 10},
                       {'ricochet_count': 0, 'segment_origin': (1, 2)}, ['bad']):
        def make(log, v=variant, n=normalized):
            b = shot_battle(log, v)
            point = (1.0, 2.0, float('nan') if v % 11 == 10 else 3.0)
            math_module = Log(log, 'math', {'Matrix': lambda node: Log(log, 'matrix', translation=point)})
            cameras = Log(log, 'cameras', {'isPointOnScreen': v % 5 != 4}, missing=('isPointOnScreen',) if v % 9 == 8 else ())
            b._runtime = Log(log, 'runtime', math=math_module, cameras=cameras)
            entity = Log(log, 'entity', isStarted=v % 6 != 5, appearance=Log(log, 'appearance', compoundModel=Compound(v)))
            return b, (None if v % 13 == 12 else entity, copy.deepcopy(n))
        run('_projectile_visual_start', make, (variant, repr(normalized))); cases += 1
    for value in (3, '4', True, -1, 'x', 2):
        def make(log, v=variant, value=value):
            b = shot_battle(log, v)
            b._projectile_epoch = 2 if v % 3 == 0 else 1
            stub(b, log, '_reset_projectile_visuals')
            b._local_projectile_presentations = {'a': 1}
            b._projectiles = None if v % 4 == 0 else Projectiles(log, (9.0, 1, 'x')[v % 3], v % 5 != 4)
            return b, (value, 5.5)
        run('_set_projectile_epoch', make, (variant, value)); cases += 1
    for key in ('player:3', 'bot:7', 'bot:9'):
        def make(log, v=variant, key=key):
            b = shot_battle(log, v)
            record = b._records[key]
            if v % 3 == 0: record['projectile_collision_pose'] = {'x': 1, 'y': 2, 'z': 3, 'yaw': 0.1, 'siege_state': 1}
            elif v % 3 == 1: record['state'] = {'x': 4, 'y': 5, 'z': 6, 'yaw': '0.2', 'aim_yaw': 0.5}
            b._local_position = (7.0, 8.0, 9.0)
            b._local_yaw, b._local_pitch, b._local_roll = 0.3, 0.02, -0.01
            entity = Log(log, 'entity', {'getAimParams': (lambda: 1 // 0) if v % 7 == 6 else (lambda: (0.4, -0.05))},
                         missing=('getAimParams',) if v % 5 == 4 else ())
            stub(b, log, '_server_entity', None if v % 11 == 10 else entity)
            return b, (key, record, (0.0, 1.0, 2.0))
        run('_projectile_record_pose', make, (variant, key)); cases += 1
    def make(log, v=variant):
        b = shot_battle(log, v)
        b._projectile_position_history = [(0.0, {}), (1.0, {}), (4.9, {})][:v % 4]
        poses = None if v % 5 == 4 else {'bot:7': {'x': 1, 'yaw': 0.1}, 'bot:8': (1, 2, 3)}
        return b, ((5.0, 4.9 + 1e-10, 30.0)[v % 3], poses)
    run('_sample_projectile_positions', make, (variant,)); cases += 1
    for pose in ({'x': 1, 'y': 2, 'z': 3, 'yaw': 0.5, 'turret_yaw': 0.1, 'siege_state': 0}, {'x': 1, 'y': 2}, {'x': 0, 'y': 0, 'z': 0}):
        def make(log, v=variant, pose=pose):
            b = shot_battle(log, v)
            b._vector = lambda xyz: ('vec',) + tuple(xyz)
            b._runtime = Log(log, 'runtime', math=Log(log, 'math', Matrix=Mat2) if v % 4 else Log(log, 'math', missing=('Matrix',)))
            stub(b, log, '_projectile_descriptor_at_pose', None if v % 5 == 4 else 'descriptor')
            return b, (Log(log, 'target'), dict(pose))
        run('_projectile_frozen_target', make, (variant, repr(pose))); cases += 1
    for key in ('player:3', 'bot:7', 'bot:9'):
        def make(log, v=variant, key=key):
            b = shot_battle(log, v)
            b._records['bot:7']['native_remote'] = v % 2 == 0
            b._local_matrix = None if v % 3 == 0 else 'local-matrix'
            stub(b, log, '_local_body_pose', 'body-pose')
            stub(b, log, '_projectile_vehicle_matrices', ('vbody', None) if v % 4 == 1 else ('vbody', 'vchassis'))
            b._runtime = Log(log, 'runtime', math=Log(log, 'math', Matrix=Mat2))
            target = (Log(log, 'target', typeDescriptor='descriptor', position=(1, 2, 3), missing=('appearance',)) if v % 5 == 0 else
                      Log(log, 'target', typeDescriptor='descriptor', position=(1, 2, 3), appearance='appearance'))
            return b, (b._records[key], target)
        run('_projectile_live_critical_target', make, (variant, key)); cases += 1
# Projectile meta install, observation and target poses.
import time as _time
class FixedTime(object):
    def clock(self): return 100.25
sys.modules['time'] = time = FixedTime()
for variant in range(30):
    def modules(log, v):
        global native_client_projectile, native_bot_runtime
        native_client_projectile = Log(log, 'projectile', {'enabled': v % 4 == 0, 'observe': 'observed'})
        native_bot_runtime = Log(log, 'bot_runtime', {'enabled': v % 4 == 1, 'projectile_observe': None if v % 8 == 1 else 'bot-observed'})
        fake_module('native_client_projectile', native_client_projectile)
        fake_module('native_bot_runtime', native_bot_runtime)
    for pid in ('p1', 'missing'):
        def make(log, v=variant, pid=pid):
            modules(log, v)
            b = shot_battle(log, v)
            b._projectile_meta = {'p1': {'shooter_kind': 'player', 'shooter_id': 3, 'manager_key': 'k1' if v % 3 else None}}
            if v % 3 == 0: del b._projectile_meta['p1']['manager_key']
            b._projectile_terminal_data = {'p1': 1}
            b._projectiles = None if v % 5 == 4 else Log(log, 'projectiles')
            return b, (pid,)
        run('_discard_provisional_projectile', make, (variant, pid)); cases += 1
    for message in ({'server_time_ms': 500, 'motion_time_us': 7000, '_client_received_time': 99.75, 'authority_epoch': 2},
                    {'server_time_ms': '400'}, {'server_time_ms': -1}, {'server_time_ms': 'x'}, {'motion_time_us': 10},
                    {'_client_received_time': 'bad'}, {'_client_received_time': float('nan'), 'server_time_ms': 900},
                    {'_client_received_time': 0.0, 'server_time_ms': 900}, {}, ['not', 'dict'], {'motion_time_us': -5}):
        def make(log, v=variant, m=message):
            modules(log, v)
            b = shot_battle(log, v)
            stub(b, log, '_clock', (5.0, 120.0, 0.1)[v % 3])
            b._bots = None if v % 4 == 3 else Log(log, 'bots')
            if v % 3: b.client.minimum_rtt_ms = (None, 80, 'x', 9000)[v % 4]
            if v % 5 == 2: b.client.rtt_ms = 40
            if v % 6 == 3: b.client.authority_epoch = 4
            b._pose_motion_time_us = None if v % 2 else 6000
            b._projectile_server_time_ms = None if v % 7 == 0 else 450
            stub(b, log, '_set_projectile_epoch', True)
            return b, (copy.deepcopy(m),)
        run('_observe_projectile_message', make, (variant, repr(message))); cases += 1
    for incoming in ({'ricochet_count': 0, 'base_checked_ms': 50, 'checked_distance': 10.0, 'piercing_loss': 1.0},
                     {'ricochet_count': 1, 'base_checked_ms': 70, 'checked_distance': 20.0, 'piercing_loss': 2.0,
                      'segment_origin': (5, 5, 5), 'segment_velocity': (1, 0, 0), 'segment_start_time_ms': 40,
                      'base_penetration_multiplier': 0.75},
                     {'ricochet_count': 0, 'base_checked_ms': 10, 'checked_distance': 9.0, 'piercing_loss': 0.5},
                     {'ricochet_count': 2, 'base_checked_ms': 10, 'checked_distance': 1.0, 'piercing_loss': 0.0},
                     {'ricochet_count': 0, 'base_checked_ms': 50, 'checked_distance': 10.0, 'piercing_loss': 1.0, 'shot_seq': 99},
                     {'ricochet_count': 0, 'base_checked_ms': 60, 'checked_distance': 3.0, 'piercing_loss': 0.0,
                      'segment_origin': (9, 9, 9)}):
        def make(log, v=variant, incoming=incoming):
            b = shot_battle(log, v)
            launch = {'projectile_id': 'p1', 'shooter_kind': 'player', 'shooter_id': 3, 'shot_seq': 5,
                      'segment_origin': (1, 2, 3), 'segment_velocity': (0, 0, 9), 'segment_start_time_ms': 0,
                      'base_penetration_multiplier': 1.0}
            normalized = dict(launch); normalized.update(incoming)
            existing = dict(launch, ricochet_count=(0, 1, '0')[v % 3], base_checked_ms=(30, 80)[v % 2], checked_distance=(5.0, 15.0)[(v // 2) % 2],
                            piercing_loss=None, progress_pending={'checked_through_ms': 55} if v % 4 else None,
                            awaiting_ricochet=v % 5 == 3, pending_resolution=None if v % 6 else 'r')
            if v % 7 == 0: existing['manager_key'] = 'kept'
            b._projectile_meta = {} if v % 9 == 8 else {'p1': existing}
            b._projectiles = None if v % 10 == 9 else Log(log, 'projectiles', {'contains': v % 3 == 0})
            stub(b, log, '_new_projectile_meta', lambda n: dict(n, created=True))
            stub(b, log, '_report_player_shot_timing')
            stub(b, log, '_confirm_bot_projectile_launch')
            stub(b, log, '_owns_projectile', v % 2 == 0)
            return b, (normalized,)
        run('_install_projectile_meta', make, (variant, repr(incoming)[:40])); cases += 1
    def make(log, v=variant):
        b = shot_battle(log, v)
        stub(b, log, '_projectile_record_positions', {'player:3': (1, 2, 3), 'bot:7': (4, 5, 6), 'gone:1': (0, 0, 0)})
        stub(b, log, '_projectile_record_pose', lambda key, record, position: None if key == 'bot:7' and v % 2 else {'key': key, 'at': position})
        return b, ()
    run('_projectile_record_poses', make, (variant,)); cases += 1
sys.modules['time'] = time = _time
# World explosions and wreck hits.
class Vec3(Vec):
    pass
for variant in range(36):
    def terminal_battle(log, v):
        b = shot_battle(log, v)
        b._vector = lambda xyz: (log.append(('battle', '_vector', repr(tuple(xyz)))), Vec(xyz))[1]
        effects = {'armorHit': ('stagesH', 'effectsH', 0)} if v % 7 != 6 else {'armorHit': ('only', 'two')}
        shot_effects = [effects, effects] if v % 9 != 8 else []
        b._runtime = Log(log, 'runtime', vehicles=Log(log, 'vehicles', g_cache=Log(log, 'cache', shotEffects=shot_effects)))
        b._projectile_meta = {'p1': {'shooter_kind': 'bot', 'shooter_id': 7, 'hit_vehicle': (False, None, True)[v % 3] if v % 4 else False,
                                     'terminal_velocity': (None, (0, 0, 9), (0, 0), (0, 0, 0))[v % 4]}}
        b._projectile_visual_meta = {'p1': {'admitted': v % 10 != 9, 'velocity': (1.0, 0.0, 0.0) if v % 5 else None}}
        stub(b, log, '_projectile_shot', {'shell': {'effectsIndex': (1, '0', None, 'x')[(v // 4) % 4] if v % 11 else 1}})
        stub(b, log, '_surface_effect_material', None if v % 13 == 12 else 'ground')
        stub(b, log, '_report_effect')
        stub(b, log, '_record_alive', v % 6 == 5)
        stub(b, log, '_projectile_source_entity', Log(log, 'source', position=(10, 0, 10)) if v % 3 else None)
        stub(b, log, '_optional_feature_enabled', v % 12 != 11)
        stub(b, log, '_impact_sound_identity', {'sound': 1} if v % 8 else {'end': 1})
        stub(b, log, '_projectile_attacker_engine_id', 70)
        stub(b, log, '_warn_optional_failure')
        entity = Log(log, 'wreck', isStarted=v % 14 != 13)
        stub(b, log, '_server_entity', entity)
        b._avatar = Log(log, 'avatar', terrainEffects=Log(log, 'terrain', missing=('addNew',) if v % 15 == 14 else ()))
        return b
    for outcome in ('impact', 'vehicle'):
        for pid in ('p1', 'missing'):
            run('_projectile_explosion', lambda log, v=variant, o=outcome, pid=pid: (terminal_battle(log, v), (pid, (1, 2, 3), o)),
                (variant, outcome, pid)); cases += 1
    for wreck in ({'target_kind': 'bot', 'target_id': 7}, {'target_kind': 'bot', 'target_id': '9'}, {'target_kind': 'tank', 'target_id': 7},
                  {'target_kind': 'bot', 'target_id': 'x'}, {'target_kind': 'bot'}, {'target_kind': 'bot', 'target_id': 7, 'extra': 1},
                  {'target_kind': 'player', 'target_id': 3}, None):
        for impact in ((5, 0, 5), [1, 2], 'abc'):
            def make(log, v=variant, wreck=wreck, impact=impact):
                b = terminal_battle(log, v)
                b._records['bot:7']['spot_visible'] = v % 3 != 2
                return b, ('p1', {'wreck_hit': copy.deepcopy(wreck), 'impact': impact})
            run('_present_projectile_wreck_hit', make, (variant, repr(wreck), repr(impact))); cases += 1
# The wreck-hit direction from the shooter's position when no velocity is retained.
def make(log):
    b = terminal_battle(log, 2)
    b._projectile_meta['p1']['terminal_velocity'] = None
    b._projectile_visual_meta['p1'] = {'admitted': True}
    stub(b, log, '_projectile_shot', {'shell': {'effectsIndex': 1}})
    stub(b, log, '_projectile_source_entity', Log(log, 'source', position=(10, 0, 10)))
    stub(b, log, '_record_alive', False)
    b._records['bot:7']['spot_visible'] = True
    return b, ('p1', {'wreck_hit': {'target_kind': 'bot', 'target_id': 7}, 'impact': (5, 0, 5)})
run('_present_projectile_wreck_hit', make, ('source-direction',)); cases += 1
# Projectile visual launch and retirement.
def trajectory_position(origin, velocity, gravity, elapsed):
    return tuple(round(o + v * elapsed + 0.5 * a * elapsed * elapsed, 6) for o, v, a in zip(origin, velocity, gravity))
_PROFILE_CLOCK = lambda: 7.5
VISUAL_LAUNCH = {'projectile_id': 'p1', 'shooter_kind': 'bot', 'shooter_id': 7, 'segment_origin': (1.0, 2.0, 3.0),
                 'segment_velocity': (0.0, 10.0, 800.0), 'gravity': 9.81, 'segment_start_time_ms': 0, 'ricochet_count': 0,
                 'max_distance': 700.0, 'checked_distance': 12.5, 'shell_index': 1, 'launch_server_time_ms': 1000,
                 'max_time_ms': 5000, 'base_checked_ms': 0}
VISUAL_EDITS = [{}, {'bot_presentation_time_us': 500, 'muzzle_presented': True}, {'bot_presentation_time_us': 500},
                {'ricochet_count': 1, 'segment_start_time_ms': 300, 'base_checked_ms': 400},
                {'bot_presentation_time_us': 500, 'muzzle_presented': True, 'ricochet_count': 1, 'segment_start_time_ms': 200},
                {'base_checked_ms': 250}, {'max_distance': 5.0, 'checked_distance': 6.0}, {'shooter_kind': 'player', 'shooter_id': 3},
                {'bot_presentation_time_us': 500, 'muzzle_presented': True, 'base_checked_ms': 300}, {'shooter_id': 99}]
for variant in range(40):
    def visual_battle(log, v):
        global native_client_projectile
        native_client_projectile = Log(log, 'projectile', {'enabled': v % 9 == 4, 'visual': 'native-visual'})
        fake_module('native_client_projectile', native_client_projectile)
        b = shot_battle(log, v)
        sys.stdout = Recorder(log)
        b._remote_factory = None if v % 17 == 16 else Log(log, 'factory', {
            'play_projectile_tracer': (lambda *a, **k: 1 // 0) if v % 13 == 12 else (lambda *a, **k: v % 3 != 2),
            'stop_projectile_tracer': (lambda *a, **k: 1 // 0) if v % 11 == 10 else (lambda *a, **k: v % 2)})
        b._projectile_visual_terminals = set(['done'])
        owner = {'manager_key': 'k1', 'presented_ricochet_count': v % 2, 'visual_segment_distance': 40.0}
        if v % 5 == 1: owner['local_launch_time'] = 1.0
        if v % 7 == 3: owner['pending_resolution'] = 'r'
        if v % 6 == 2: owner['bot_visual_launch_time'] = 4.25
        b._projectile_meta = {} if v % 4 == 0 else {'p1': owner}
        b._projectile_visual_meta = ({} if v % 3 == 0 else
                                     {'p1': {'ricochet_count': (0, 1, 2)[v % 3], 'active': v % 2 == 0, 'admitted': v % 8 != 7,
                                             'origin': (0, 0, 0), 'velocity': (1, 1, 1), 'gravity': 9.8, 'segment_start_time_ms': 100}})
        b._fire_timeline = {'p1': {'trigger': 5.0, 'shown': 6.0 if v % 2 else None, 'cursor_logs': v % 10}} if v % 4 else {}
        b._start_message = {'server_time_ms': (900, 2000)[v % 2]} if v % 3 else None
        b._records['bot:7']['presentation_time_us'] = 9500 if v % 5 else None
        b._projectiles = None if v % 6 == 0 else Log(log, 'projectiles', {'get': ({'position': (9, 9, 9)} if v % 4 == 2 else None)})
        stub(b, log, '_owns_projectile', v % 2 == 0)
        stub(b, log, '_projectile_source_descriptor', None if v % 19 == 18 else 'descriptor')
        stub(b, log, '_admit_projectile_visual', v % 10 != 9)
        stub(b, log, '_optional_feature_enabled', v % 14 != 13)
        stub(b, log, '_projectile_explosion', 'boom')
        stub(b, log, '_stop_projectile_visual', True)
        stub(b, log, '_ensure_projectile_visual_native_presentation', 'python-visual')
        stub(b, log, '_warn_optional_failure')
        return b
    for edit_index, edit in enumerate(VISUAL_EDITS):
        def make(log, v=variant, edit=edit):
            b = visual_battle(log, v)
            normalized = dict(VISUAL_LAUNCH); normalized.update(edit)
            return b, (normalized, (5.0, 12.0)[v % 2], 'start' if v % 5 else None)
        try:
            run('_ensure_projectile_visual_native_presentation', make, (variant, edit_index)); cases += 1
            run('_ensure_projectile_visual', make, (variant, edit_index)); cases += 1
        finally:
            sys.stdout = sys.__stdout__
    for event in ({'impact': (1, 2, 3), 'outcome': 'impact', 'hit_vehicle': False}, {'resolved_time_ms': 600}, {'resolved_time_ms': 'x'},
                  {}, None, ['list'], {'resolved_time_ms': 50, 'hit_vehicle': True}):
        def make(log, v=variant, event=event):
            b = visual_battle(log, v)
            del b._stop_projectile_visual
            return b, ('p1', copy.deepcopy(event))
        try:
            run('_stop_projectile_visual', make, (variant, repr(event))); cases += 1
        finally:
            sys.stdout = sys.__stdout__
# Projectile advance, history pruning and pending resolutions.
class Manager(object):
    def __init__(self, log, v, states):
        self.log, self.v, self.states, self.now = log, v, states, (4.0, 6.5, 5)[v % 3]
    def __repr__(self): return '<manager>'
    def __len__(self): return len(self.states) if self.v % 5 else 0
    def snapshot(self): self.log.append(('manager', 'snapshot')); return list(self.states)
    def sustainable_chord_budget(self, seconds): return (10, 90, 300)[self.v % 3]
    def advance(self, now, chord, terminal, maximum_chords=None):
        self.log.append(('manager', 'advance', now, chord.__name__, terminal.__name__, maximum_chords))
        if self.v % 11 == 10: raise ValueError('advance failed')
        return 'advanced'
    def last_advance_metrics(self): return {'active': 2, 'chords': 7} if self.v % 2 else {}
for variant in range(44):
    def advance_battle(log, v):
        global native_client_projectile, native_bot_runtime
        native_client_projectile = Log(log, 'projectile', {'enabled': v % 15 == 14, 'frame': 'native-frame'})
        native_bot_runtime = Log(log, 'bot_runtime', {'enabled': v % 13 == 12, 'projectile_advance': 'bot-advance'})
        b = shot_battle(log, v)
        b._bots = None if v % 2 else Log(log, 'bots')
        states = [] if v % 7 == 0 else [{'cursor_time': 3.0}, {'cursor_time': (2.0, 3.0, 'x')[v % 3] if v % 17 else 2.0}]
        b._projectiles = None if v % 19 == 18 else Manager(log, v, states)
        b._projectile_meta = {'p': {'local_ricochet_admission': 1 if v % 6 == 0 else None, 'pending_resolution': 'r' if v % 4 == 1 else None,
                                    'awaiting_resolution': v % 8 == 5, 'pending_ricochet': 'q' if v % 3 == 2 else None}}
        b._projectile_visual_meta = {'p': {}} if v % 4 == 2 else {}
        b._projectile_position_history = [(1.0, {}), (2.5, {}), (3.5, {})][:(v % 4) + 1]
        b._projectile_target_positions = {'old': 1}
        b._next_projectile_progress_time = (0.0, 5.0, 3.95, -1)[v % 4]
        for name in ('_admit_local_projectile_ricochet', '_submit_projectile_resolution', '_submit_projectile_ricochet',
                     '_build_projectile_spatial_bins', '_clear_projectile_spatial_index', '_publish_projectile_progress'):
            stub(b, log, name, v % 2 == 0)
        b._projectile_chord = lambda *a: None
        b._projectile_terminal = lambda *a: None
        stub(b, log, '_projectile_record_poses', {'bot:7': {'x': 1, 'y': 2, 'z': 3}})
        stub(b, log, '_sample_projectile_positions')
        stub(b, log, '_projectile_is_authority', v % 3 != 1)
        return b
    for force in (False, True):
        run('_advance_projectiles', lambda log, v=variant, force=force: (advance_battle(log, v), ((4.0, 5.25, 9.0)[v % 3], force)),
            (variant, force)); cases += 1
    run('_flush_pending_projectile_resolutions', lambda log, v=variant: (advance_battle(log, v), ()), (variant,)); cases += 1
    for states in (None, [], [{'cursor_time': 2.0}, {'cursor_time': 1.5}], [{'cursor_time': '3'}], [{'cursor_time': 2.5}]):
        run('_prune_projectile_position_history', lambda log, v=variant, states=states: (advance_battle(log, v), (copy.deepcopy(states),)),
            (variant, repr(states))); cases += 1
# Projectile launch, ricochet and terminal events.
class Manager2(object):
    def __init__(self, log, v): self.log, self.v = log, v
    def __repr__(self): return '<manager2>'
    def contains(self, key): self.log.append(('manager', 'contains', key)); return self.v % 9 == 4
    def remove(self, key): self.log.append(('manager', 'remove', key))
for variant in range(48):
    def event_battle2(log, v):
        global native_client_projectile, native_bot_runtime
        native_client_projectile = Log(log, 'projectile', {'enabled': v % 16 == 15, 'event': 'native-event'})
        native_bot_runtime = Log(log, 'bot_runtime', {'enabled': v % 17 == 16, 'projectile_event': 'bot-event'})
        fake_module('native_client_projectile', native_client_projectile)
        fake_module('native_bot_runtime', native_bot_runtime)
        b = shot_battle(log, v)
        sys.stdout = Recorder(log)
        b._bots = None if v % 2 else Log(log, 'bots')
        b._projectiles = None if v % 19 == 18 else Manager2(log, v)
        b._projectile_epoch = None if v % 5 == 0 else 2
        b._projectile_visual_terminals = set(['done'])
        b._projectile_resolution_acks = set(['acked'])
        b._projectile_position_history = [] if v % 3 else [(1.0, {})]
        meta = {'manager_key': 'k1', 'ricochet_count': (0, 1)[v % 2], 'segment_start_time_ms': 100,
                'pending_resolution': 'r' if v % 7 == 3 else None, 'awaiting_ricochet': v % 11 == 5,
                'local_ricochet_admission': 1 if v % 13 == 6 else None}
        if v % 4 == 1: meta['local_launch_time'] = 1.0
        b._projectile_meta = {} if v % 6 == 0 else {'p1': meta}
        b._projectile_terminal_data = {'p1': 'data'}
        b._projectile_visual_meta = {'p1': {'velocity': (0.0, 10.0, 800.0), 'gravity': 9.81}} if v % 3 else {}
        normalized = {'projectile_id': 'p1' if v % 8 else ('done', 'acked')[v % 16 // 8], 'ricochet_count': 1 if v % 10 else 0,
                      'checked_distance': 42.0, 'manager_key': 'k1'}
        if v % 12 == 11: stub(b, log, '_projectile_wire_meta', None)
        elif v % 12 != 10: stub(b, log, '_projectile_wire_meta', lambda e: dict(normalized))
        stub(b, log, '_projectile_launch_mismatches', ('shot_seq', 'origin') if v % 4 == 1 and v % 8 == 5 else ())
        stub(b, log, '_owns_projectile', v % 3 != 2)
        stub(b, log, '_projectile_is_authority', v % 5 != 1)
        stub(b, log, '_install_projectile_meta', lambda n: b._projectile_meta.setdefault(n['projectile_id'], dict(n, manager_key='k2')))
        stub(b, log, '_report_projectile_terminal_failure')
        # _projectile_explosion reads the terminal velocity through the stop.
        stub(b, log, '_stop_projectile_visual',
             lambda pid, e: log.append(('terminal_velocity', repr((b._projectile_meta.get(pid) or {}).get('terminal_velocity')))))
        stub(b, log, '_ensure_projectile_visual', True)
        stub(b, log, '_accept_projectile_event', True)
        stub(b, log, '_present_projectile_wreck_hit', True)
        stub(b, log, '_set_projectile_epoch', v % 14 != 13)
        stub(b, log, '_reconcile_provisional_projectile', (None, False, {'manager_key': 'k3'})[v % 3] if v % 4 else None)
        for name in ('_submit_projectile_ricochet', '_submit_projectile_resolution', '_admit_local_projectile_ricochet'):
            stub(b, log, name)
        stub(b, log, '_admit_projectile_manager_state', v % 10 != 7)
        stub(b, log, '_projectile_record_poses', {'bot:7': {'x': 1, 'y': 2, 'z': 3}})
        stub(b, log, '_sample_projectile_positions')
        stub(b, log, '_clock', 3.5)
        return b
    for event in ({'projectile_id': 'p1', 'authority_epoch': (2, 3)[variant % 2], 'hit_vehicle': True, 'resolved_time_ms': 600},
                  {'projectile_id': 7, 'hit_vehicle': False, 'resolved_time_ms': 'x', 'wreck_hit': {'target_kind': 'bot'}},
                  {'projectile_id': 'p1', 'hit_vehicle': 1}, {}):
        for name in ('_accept_projectile_event', '_apply_projectile_ricochet_event', '_apply_projectile_terminal_event'):
            def make(log, v=variant, e=event):
                b = event_battle2(log, v)
                if name == '_accept_projectile_event': del b._accept_projectile_event
                return b, (copy.deepcopy(e),)
            try:
                run(name, make, (variant, repr(event)[:30])); cases += 1
            finally:
                sys.stdout = sys.__stdout__
# Rejected launches, deferred local presentation and snapshot reconciliation.
class AckSet(object):
    def __init__(self, log, fresh): self.log, self.fresh, self.ids = log, fresh, set(['acked'])
    def __repr__(self): return 'AckSet(%r)' % sorted(self.ids)
    def __contains__(self, value): return value in self.ids
    def add(self, value):
        self.log.append(('acks', 'add', value)); self.ids.add(value); return self.fresh
for variant in range(48):
    for message in ({'round_id': 4, 'accepted': False, 'projectile_id': 'p1', 'intent_seq': 5, 'reason': ''},
                    {'round_id': 4, 'accepted': False, 'projectile_id': 'p1', 'intent_seq': '5', 'reason': 'busy'},
                    {'round_id': 4, 'accepted': False, 'projectile_id': 'p1', 'intent_seq': 'x'},
                    {'round_id': 4, 'accepted': 0, 'projectile_id': 'p1', 'intent_seq': 5},
                    {'round_id': 3, 'accepted': False, 'projectile_id': 'p1', 'intent_seq': 5}, ['bad']):
        def make(log, v=variant, m=message):
            b = event_battle2(log, v)
            b._start_message = {'round_id': 4} if v % 7 else None
            b._projectile_meta = {'p1': {'fire_intent_seq': (5, '5', 6)[v % 3], 'burst_group_seq': 9}} if v % 5 else {}
            b._local_player_burst = None if v % 4 == 0 else {'group_seq': (9, 8)[v % 2]}
            stub(b, log, '_discard_provisional_projectile')
            stub(b, log, '_cancel_local_player_burst')
            return b, (copy.deepcopy(m),)
        try:
            run('on_fire_intent_result', make, (variant, repr(message)[:30])); cases += 1
        finally:
            sys.stdout = sys.__stdout__
    def make(log, v=variant):
        b = event_battle2(log, v)
        sys.stdout = sys.__stdout__
        b._generation = 3
        b._start_message = {'round_id': 4}
        b._local_projectile_presentations = {}
        b.state = 'running'
        b._server = 'server'
        callbacks = []
        b._runtime = Log(log, 'runtime', bigworld=Log(log, 'bigworld', {'callback': lambda delay, fn: callbacks.append(fn)}))
        stub(b, log, '_show_shot', (lambda e: 1 // 0) if v % 7 == 6 else None)
        stub(b, log, '_warn_optional_failure')
        event = {'projectile_id': 'p%d' % v, 'shooter_id': (3, 4, 99)[(v // 5) % 3], 'nested': [1, 2]}
        class Runner(object):
            def __repr__(self): return '<runner>'
        def drive():
            fn = callbacks[0]
            if v % 5 == 1: b._generation = 4
            elif v % 5 == 2: b._start_message = {'round_id': 5}
            elif v % 5 == 3: b._local_projectile_presentations['p%d' % v] = {'replaced': True}
            elif v % 5 == 4: b.state = 'leaving'
            if v % 11 == 10: b._records['player:3']['tombstone'] = True
            fn()
            log.append(('after', sorted(b._local_projectile_presentations)))
        b._drive_deferred = drive
        return b, (event,)
    def make_and_drive(log, v=variant):
        b, values = make(log, v)
        return b, values
    run('_defer_local_projectile_presentation', make_and_drive, (variant,)); cases += 1
    for rows in ([{'projectile_id': 'p1'}, {'projectile_id': 'done'}, 'junk', {'projectile_id': 'p2'}], [], 'notalist', None,
                 [{'projectile_id': 'p1'}, {'projectile_id': 'acked'}]):
        def make(log, v=variant, rows=rows):
            b = event_battle2(log, v)
            b._projectile_revision = 3
            b._event_journal = [{'kind': 'shot', 'projectile_id': 'old'}, {'kind': 'health'}]
            b._projectile_meta = {'p1': {'manager_key': 'k1'}, 'old': {'awaiting_resolution': v % 2 == 0, 'pending_ricochet': 'q' if v % 3 else None,
                                                                         'local_launch_pending': v % 7 == 1},
                                  'p2': {'pending_resolution': 'r', 'awaiting_resolution': True}}
            b._projectile_resolution_acks = AckSet(log, v % 4 != 3)
            normalized = lambda raw: (None if raw.get('projectile_id') == 'p2' and v % 3 == 0 else
                                      {'projectile_id': raw['projectile_id'], 'manager_key': 'k1'}) if isinstance(raw, dict) else None
            stub(b, log, '_projectile_wire_meta', normalized)
            stub(b, log, '_projectile_manager_snapshot', 'manager-snapshot')
            stub(b, log, '_projectile_source_entity', Log(log, 'source', typeDescriptor=None if v % 4 == 0 else 'd'))
            stub(b, log, '_projectile_source_descriptor', None if v % 8 == 0 else 'cached')
            b._projectiles = None if v % 19 == 18 else Log(log, 'projectiles', {'contains': v % 9 == 4, 'restore': v % 10 != 7})
            return b, ({'projectiles': copy.deepcopy(rows), 'projectile_revision': (5, 'x', 1)[v % 3]} if rows is not None else ['m'],)
        try:
            run('_reconcile_projectile_snapshot', make, (variant, repr(rows)[:20])); cases += 1

        finally:
            sys.stdout = sys.__stdout__

# Frame steps: _clock, _tick_critical_states, _advance_artillery_arcs, _advance_authority_worker_probe.
CRITICAL_REPAIR_NETWORK_SECONDS = 0.25
ARTILLERY_ARC_RAYS_PER_FRAME = 6
for variant in range(6):
    def make(log, v=variant):
        b = battle(log)
        answers = {0: 12.5, 1: 7, 2: 'x', 3: None}
        def bw_time():
            log.append(('bigworld', 'time'))
            if v == 5: raise ValueError('no clock')
            return answers.get(v)
        bigworld = Log(log, 'bigworld', missing=('time',) if v == 4 else ())
        if v != 4: bigworld.time = bw_time
        b._runtime = Log(log, 'runtime', bigworld=bigworld)
        globals()['time'] = Log(log, 'time', {'time': 77.25})
        return b, ()
    run('_clock', make, (variant,)); cases += 1
for variant in range(48):
    def make(log, v=variant):
        b = battle(log, records={'player:4': {'engine_id': 9, 'state': (None, {'alive': True}, {}, 0)[(v // 4) % 4]}} if v % 11 != 10 else {})
        b.client = Log(log, 'client', player_id=4)
        descriptor = Log(log, 'descriptor', maxHealth=300.9)
        entity = (None if v % 13 == 12 else
                  Log(log, 'entity', missing=() if v % 2 else ('maxHealth',), typeDescriptor=None if v % 17 == 16 else descriptor,
                      **({'maxHealth': 250} if v % 2 else {})))
        stub(b, log, '_server_entity', entity)
        stub(b, log, '_record_alive', v % 19 != 18)
        stub(b, log, '_local_repair_factor', 1.5)
        payload = [None, {'events': [{'device': 'leftTrack'}]}, {'events': None}, {'events': []}][v % 4]
        stub(b, log, '_tick_local_track_repair', lambda e, dt, factor: log.append(('maxHealth', e.__dict__.get('maxHealth'))) or payload)
        stub(b, log, '_critical_state', lambda p: {'repaired': True})
        stub(b, log, '_present_repair_progress')
        stub(b, log, '_present_critical')
        stub(b, log, '_queue_local_track_repair')
        stub(b, log, '_tick_equipment_cooldowns')
        stub(b, log, '_clock', 10.0)
        b._next_critical_report_time = (5.0, 10.0, 20.0)[v % 3]
        fake_module('native_client_critical', Log(log, 'critical', {'frame': v % 7 == 3}))
        return b, ((0.016, 0.0, -1.0, 0.016)[v % 4] if v < 40 else 0.016,)
    run('_tick_critical_states', make, (variant,)); cases += 1
for variant in range(8):
    def make(log, v=variant):
        b = battle(log)
        b._artillery = None if v % 4 == 0 else Log(log, 'artillery', {'advance': lambda now, rays, probe: ('arcs', now, rays)})
        b._artillery_arc_probe = Log(log, 'arc_probe')
        b._bots = None if v % 2 == 0 else Log(log, 'bots')
        fake_module('native_bot_runtime', Log(log, 'bot_runtime', {'enabled': v >= 4}))
        return b, (3.5 if v % 3 != 2 else 'late',)
    run('_advance_artillery_arcs', make, (variant,)); cases += 1
class FakeProbe(object):
    def __init__(self, log, start=True, tick_error=None, stop_error=None, active=True):
        self.log, self.start_ok, self.tick_error, self.stop_error, self.active = log, start, tick_error, stop_error, active
    def __repr__(self):
        return '<probe>'
    def start(self):
        self.log.append(('probe', 'start'))
        if self.start_ok == 'raise': raise RuntimeError('start broke')
        return self.start_ok
    def tick(self):
        self.log.append(('probe', 'tick'))
        if self.tick_error: raise self.tick_error
    def stop(self, reason):
        self.log.append(('probe', 'stop', reason))
        if self.stop_error: raise self.stop_error
for variant in range(288):
    def make(log, v=variant):
        b = battle(log, worker=v % 23 == 22)
        b._config = [None, {}, {'authority_worker_probe': {'enabled': True, 'stageSeconds': 4}, 'map': 'm'},
                     {'authority_worker_probe': {'enabled': True, 'stageSeconds': 'bad'}},
                     {'authority_worker_probe': ['enabled']}, {'authority_worker_probe': {'enabled': 0}}][v % 6]
        def is_authority():
            log.append(('bots', 'is_authority'))
            if v % 7 == 6: raise ValueError('not yet')
            return v % 5 != 4
        b._bots = Log(log, 'bots', missing=('is_authority',) if v % 9 == 8 else ())
        if v % 9 != 8: b._bots.is_authority = is_authority
        b._battle_live = v % 8 != 7
        b._worker_probe_attempted = v % 10 == 9
        b._worker_probe_bot_count = (3, 0, -1)[v // 96] if v % 11 != 10 else 0
        b._worker_probe_authority_callbacks = 2
        b._start_message = {'round_id': 4} if v % 3 else None
        b._runtime = Log(log, 'runtime', bigworld=Log(log, 'bigworld'))
        b._authority_worker_probe_sample = Log(log, 'sample')
        b.client = Log(log, 'client', player_id=4)
        existing = (v // 6) % 4
        b._worker_probe = (None if existing == 0 else
                           FakeProbe(log, active=existing != 1, tick_error=ValueError('tick') if existing == 3 else None,
                                     stop_error=RuntimeError('stop') if v % 2 else None))
        def created(bigworld, sample, stage_seconds=None, context=None):
            log.append(('AuthorityWorkerProbe', rep(bigworld), rep(sample), stage_seconds, sorted(context.items())))
            if v % 13 == 12: raise RuntimeError('cannot build')
            return FakeProbe(log, start=('raise', False, True, True)[v % 4], tick_error=ValueError('first') if v % 17 == 16 else None)
        globals()['AuthorityWorkerProbe'] = created
        globals()['write_probe_record'] = lambda record: log.append(('write_probe_record', sorted(record.items())))
        globals()['os'] = Log(log, 'os', {'getpid': 4242})
        return b, ()
    run('_advance_authority_worker_probe', make, (variant,)); cases += 1

# Hit, shell and kill feedback helpers.
log_sink = []
class FObj(object):
    """A plain engine-like object whose attributes may include 'name'."""
    def __init__(self, label, **attrs):
        self.__dict__.update(attrs)
        self._label = label
    def __repr__(self):
        return '<%s>' % self._label
class FVec(object):
    def __init__(self, x, y, z):
        self.x, self.y, self.z = x, y, z
    @property
    def length(self):
        return math.sqrt(self.x * self.x + self.y * self.y + self.z * self.z)
    def __sub__(self, other):
        return FVec(self.x - other.x, self.y - other.y, self.z - other.z)
    def __eq__(self, other):
        return isinstance(other, FVec) and (self.x, self.y, self.z) == (other.x, other.y, other.z)
    def __ne__(self, other):
        return not self == other
    def normalise(self):
        n = self.length
        self.x, self.y, self.z = self.x / n, self.y / n, self.z / n
    def __repr__(self):
        return 'FVec(%.4f,%.4f,%.4f)' % (self.x, self.y, self.z)
def feedback_battle(log, v):
    # Diagnostic lines are part of the compared log.
    sys.stdout = Recorder(log)
    b = battle(log, records={'player:4': {'engine_id': 4, 'state': {'team': 1}}, 'bot:7': {'engine_id': 7, 'state': {'team': 2 if v % 3 else 1}},
                             'bot:9': {'engine_id': 9, 'state': None}})
    descriptor = Log(log, 'descriptor', maxHealth=(500, 0, 'x')[v % 3],
                     gun={'shots': [Log(log, 'shot0', shell=FObj('shell0', kind='ARMOR_PIERCING', id=(1, 2), name='ap')),
                                    Log(log, 'shot1', shell=FObj('shell1', type=FObj('stype', name='HIGH_EXPLOSIVE'), id=[0], name='he')),
                                    {'shell': None if v % 7 == 6 else {'kind': 'BOGUS', 'id': 'x', 'name': None}}][: 1 + v % 4]})
    entity = Log(log, 'entity', typeDescriptor=None if v % 11 == 10 else descriptor, position=(10.0 + v, 0.0, 3.0 * v), isStarted=v % 5 != 4,
                 appearance=Log(log, 'appearance', compoundModel=None if v % 13 == 12 else Log(log, 'compound', {'node': lambda name: None if name == 'gun' else Log(log, 'node')}),
                                swingingAnimator='anim' if v % 2 else 'other'))
    stub(b, log, '_server_entity', lambda engine: None if v % 17 == 16 else entity)
    stub(b, log, '_spectator_record', lambda engine: (None, None) if engine == 9 and v % 2 else ({'engine_id': engine}, Log(log, 'spect', position=(1.0 if engine == 9 else float(engine), 0.0, float(v)))))
    stub(b, log, '_switch_postmortem_viewpoint', lambda flag, engine: engine in ((7,), (9, 4), (4,), (-1,))[v % 4])
    stub(b, log, '_release_postmortem_visibility')
    stub(b, log, '_warn_optional_failure')
    stub(b, log, '_vector', lambda value: FVec(*[float(x) for x in value]))
    stub(b, log, '_local_vehicle_appearance', lambda: entity.appearance)
    stub(b, log, '_optional_feature_enabled', lambda name: v % 9 != 8)
    b._local_swinging_animator = 'anim' if v % 4 else None
    b._local_position = (1.0, 0.0, 2.0)
    b._spectated_engine_id = (7, 8)[v % 5 == 4]
    b._server = None if v % 6 == 5 else Log(log, 'server', vehicle_id=4)
    b._effect_reports = (0, 11, 12)[v % 3]
    b._hit_impulse_reports = (0, 15, 16)[v % 3]
    b._feedback_shell_failures = set(['seen']) if v % 2 else set('abcdefghijklmnopqrstuvwxyz0123456789')
    def factory_present(engine, direction, impulse):
        log.append(('factory', 'present_hit_impulse', engine, repr(direction), impulse))
        return v % 3 != 1
    b._remote_factory = Log(log, 'factory', {'present_hit_impulse': factory_present}, missing=() if v % 8 else ('present_hit_impulse',))
    constants = Log(log, 'constants', SHELL_TYPES_INDICES={'ARMOR_PIERCING': 0, 'HIGH_EXPLOSIVE': 2} if v % 10 != 9 else {})
    def calc(at, normal, space):
        log.append(('calc', repr(at), repr(normal), space))
        if v % 7 == 5: raise ValueError('no surface')
        return Log(log, 'surface', effectIdx=(None, 1, 7)[v % 3])
    b._runtime = Log(log, 'runtime', bigworld=Log(log, 'bigworld', {'entity': lambda i: None if v % 9 == 7 else entity}),
                     constants=constants, nations=Log(log, 'nations', NAMES=('ussr', 'germany') if v % 5 else ()),
                     battle_feedback_common=Log(log, 'common', NONE_SHELL_TYPE=(255, None)[v % 19 == 18]),
                     compatibility=Log(log, 'compat', {'release_target_lock': lambda avatar, engine: v % 2}, missing=() if v % 3 else ('release_target_lock',)),
                     effect_material_calculation=Log(log, 'calc', {'calcSurfaceMaterialNearPoint': calc}),
                     material_kinds=Log(log, 'materials', EFFECT_MATERIALS=('ground', 'stone')),
                     math=Log(log, 'math', {'Matrix': lambda node: Log(log, 'matrix', {'applyVector': lambda d: FVec(d.x, d.y, -d.z)}),
                                            'Vector3': lambda d: FVec(d.x, d.y, d.z)}))
    def vehicle_info(i):
        if v % 4 == 0: raise ValueError('no info')
        return {'vehicleID': i, 'team': 1 if i == 4 else 2}
    arena_dp = Log(log, 'arena_dp', {'getVehicleInfo': vehicle_info,
                                     'getNumberOfTeam': lambda flag: ((1, 2)[(v // 4) % 2], 'x')[v % 7 == 3]})
    b._avatar = None if v % 23 == 22 else Log(log, 'avatar', playerVehicleID=(4, 0, None)[v % 3 if v % 5 == 1 else 0], team=(1, 2)[(v // 4) % 2], spaceID=3,
                                              arena=Log(log, 'arena', vehicles={4: {'id': 4, 'team': 1}, 7: Log(log, 'av7', vehicleID=7, team=2)}),
                                              guiSessionProvider=Log(log, 'gui', {'getArenaDP': lambda: arena_dp}))
    return b
globals()['present_shot_impulse'] = lambda math_module, appearance, direction, impulse: ('presented', repr(direction), impulse)
globals()['is_gold_shell'] = lambda nation, name: (nation, name) == ('germany', 'ap')
vehicle_effects = types.ModuleType('VehicleEffects')
class Decoder(object):
    @staticmethod
    def decodeSegment(sticker, descriptor):
        if sticker == 'boom': raise KeyError('sticker')
        return {'a': ('hull', None, FVec(0, 0, 0), FVec(1, 2, 3)), 'b': ('', None, None, None), 'c': ('gun', None, FVec(0, 0, 0), FVec(0, 0, 1)),
                'd': ('hull', None, FVec(1, 1, 1), FVec(1, 1, 1)), 'e': ('hull', None, FVec(0, 0, 0), FVec(0.0001, 0, 0))}[sticker]
vehicle_effects.DamageFromShotDecoder = Decoder
sys.modules['VehicleEffects'] = vehicle_effects
events = [{'kind': 'hit', 'source': 'shot', 'damage': 120, 'shell_index': 1, 'damage_sticker': 'a', 'projectile_id': 'p1', 'event_id': 5},
          {'kind': 'bot_hit', 'source': 'shot', 'damage': '7', 'shell_index': 9, 'splash': True, 'damage_sticker': 'c'},
          {'kind': 'hit', 'source': 'fire', 'damage': 0}, {'kind': 'hit', 'source': 'shot', 'damage': 5000, 'damage_sticker': 'a'},
          {'kind': 'bot_bot_hit', 'source': 'shot', 'damage': None, 'shell_index': -3, 'damage_sticker': 'boom', 'dead': False},
          {'kind': 'hit', 'source': 'ram'}, {'kind': 'health', 'source': 'shot', 'damage_sticker': 'd', 'projectile_id': 7},
          {'kind': 'hit', 'source': 'shot', 'damage': 40, 'damage_sticker': 'e', 'dead': True},
          {'kind': 'bot_human_hit', 'source': 'shot', 'damage_sticker': 'b'}, {'source': 'bogus'}, {}]
targets = [{'engine_id': 7, 'state': {'max_health': 800, 'alive': True, 'health': 300}, 'native_remote': True},
           {'engine_id': 4, 'local': True, 'state': {'max_health': 0}},
           {'engine_id': 9, 'state': None}, {'engine_id': '7', 'state': {'alive': False}}, {'state': {'health': 0}}]
for variant in range(40):
    for ei, event in enumerate(events):
        for ti, target in enumerate(targets):
            if (variant + ei + ti) % 3: continue
            mk = lambda log, v=variant: feedback_battle(log, v)
            run('_hit_damage_factor', lambda log, v=variant, e=event, t=target: (mk(log, v), (copy.deepcopy(e), copy.deepcopy(t))), (variant, ei, ti)); cases += 1
            run('_impact_sound_identity', lambda log, v=variant, t=target: (mk(log, v), (copy.deepcopy(t), (7, None, 'x', 2.5)[v % 4], FVec(0, 1, 0), 33.0)), (variant, ti)); cases += 1
            run('_decoded_hit_impulse_direction', lambda log, v=variant, e=event, t=target: (mk(log, v), (copy.deepcopy(e), copy.deepcopy(t))), (variant, ei, ti)); cases += 1
            run('_present_hit_impulse', lambda log, v=variant, e=event, t=target: (mk(log, v), (copy.deepcopy(e), copy.deepcopy(t),
                {'targetImpulse': (2.0, 0, 'x', -1)[(v // 4) % 4]} if v % 5 else Log(log, 'fx', targetImpulse=4.0), FVec(1, 0, 0) if v % 2 else None)), (variant, ei, ti)); cases += 1
            run('_report_effect', lambda log, v=variant, e=event: (mk(log, v), ('armour_hit', 'armorHit', v, (1.0, 2.0, 3.0), FVec(0, 1, 0) if v % 2 else (0, 1, 0)) +
                ((copy.deepcopy(e),) if v % 3 else ())), (variant, ei)); cases += 1
            run('_report_hit_impulse', lambda log, v=variant, t=target: (mk(log, v), (copy.deepcopy(t), FVec(0.1, 0.2, 0.3) if v % 2 else None, 2.5, v % 3)), (variant, ti)); cases += 1
            run('_feedback_shell_fields', lambda log, v=variant, e=event, t=target: (mk(log, v), (copy.deepcopy(e), None if v % 6 == 0 else copy.deepcopy(t))), (variant, ei, ti)); cases += 1
            run('_event_shell', lambda log, v=variant, e=event, t=target: (mk(log, v), (copy.deepcopy(t), copy.deepcopy(e))), (variant, ei, ti)); cases += 1
            run('_projectile_cosmetic_allowed', lambda log, v=variant, e=event: (mk(log, v), (copy.deepcopy(e) if v % 4 else 'raw',
                {'p1': {'admitted': v % 2}, '7': {}} if v % 3 else {'p1': None})), (variant, ei)); cases += 1
    run('_surface_effect_material', lambda log, v=variant: (feedback_battle(log, v), ((1.0, 2.0, 3.0) if v % 2 else {'x': 1, 'y': 2, 'z': 3},)), (variant,)); cases += 1
    run('_release_target_lock', lambda log, v=variant: (feedback_battle(log, v), (7,)), (variant,)); cases += 1
    for shell in (FObj('gold', id=(1, 3), name='ap'), {'id': [0], 'name': 'ap'}, {'id': (5,), 'name': 'x'}, {'id': ('x',), 'name': 'x'},
                  {'id': 3, 'name': 'x'}, {'id': (), 'name': 'x'}, {'name': None}):
        run('_shell_is_gold', lambda log, v=variant, sh=shell: (feedback_battle(log, v), (sh,)), (variant, repr(shell)[:20])); cases += 1
    run('_report_feedback_shell_failure', lambda log, v=variant: (feedback_battle(log, v), (('seen', 'new reason', 'z')[v % 3],)), (variant,)); cases += 1
    run('_fallback_postmortem_viewpoint', lambda log, v=variant: (feedback_battle(log, v), ((7, '7', 8)[v % 3],)), (variant,)); cases += 1
    for target in ({'engine_id': 7, 'state': {'team': 2}}, {'engine_id': 7, 'local': True}, {'engine_id': 'x'}, {'engine_id': 7, 'state': None, 'dead_marker_known': True}):
        run('_report_local_kill_contract', lambda log, v=variant, t=target: (feedback_battle(log, v), (copy.deepcopy(t), (4, 7, 'x', None)[v % 4], (2, 'r')[v % 11 == 10])), (variant, repr(target)[:20])); cases += 1
sys.stdout = sys.__stdout__

# Snapshot ingest.
def ingest_battle(log, v):
    b = battle(log)
    b.state = ('running', 'failed', 'leaving', 'loading')[v % 4 if v % 9 == 0 else 0]
    b._last_snapshot = {'previous': True}
    b._worker_mode = v % 5 == 4
    for name in ('_ack_local_ram_contacts', '_ack_local_destructible_contacts', '_observe_destructibles_disabled',
                 '_observe_projectile_message', '_reconcile_projectile_snapshot', '_reconcile_detached_turret_snapshot',
                 '_apply_battle_result', '_apply_destructible_state', '_reconcile_bot_authority', '_remember_ram_bot_snapshot',
                 '_ignore_live_payload', '_apply_rules'):
        fail = (v // 3) % 13 == len(name) % 13 and v % 2
        stub(b, log, name, (lambda fail=fail, name=name: (lambda *a, **k: (_ for _ in ()).throw(ValueError(name)) if fail else name))())
    def restore(snapshot, present=False):
        log.append(('restore', present))
        b._last_snapshot = dict(snapshot, restored=v % 7 == 3) if v % 7 == 3 else snapshot
    b._restore_local_equipment_snapshot = restore
    b._bots = None if v % 4 == 0 else Log(log, 'bots', {'apply_snapshot': (lambda s: (_ for _ in ()).throw(KeyError('bots'))) if v % 11 == 10 else None})
    b._sync = None if v % 6 == 0 else Log(log, 'sync', {'snapshot': (lambda m: (_ for _ in ()).throw(RuntimeError('sync'))) if v % 8 == 7 else []})
    callback = lambda *a: log.append(('onTeamBasePointsUpdate',) + a)
    arena = Log(log, 'arena', missing=('onTeamBasePointsUpdate',))
    if v % 7:
        arena.onTeamBasePointsUpdate = callback
    b._avatar = Log(log, 'avatar', arena=arena)
    b._rules_state = {} if v % 3 else {'bases': {'1': {'points': 5, 'time_left': 0.0, 'invaders': 1, 'stopped': False}}}
    return b
snapshots = [None, {}, {'rules': {'bases': {'1': {'points': 5, 'invaders': 1}}}, 'battle_result': {'winner': 1}, 'destructibles': [1]},
             {'bot_authority_id': 9, 'rules': None}, {'battle_result': None, 'bots': [1]}, 'not a dict']
rule_sets = [None, {}, {'bases': None}, {'bases': {'1': {'points': 120, 'time_left': -3, 'invaders': 2.9, 'stopped': 1}, 2: {'points': '7'}}},
             {'bases': {'1': {'points': 5, 'time_left': 0, 'invaders': 1}, '2': None}}, {'bases': {'1': {'points': 'x'}}},
             {'bases': {'2': {'points': -4, 'time_left': float('nan'), 'invaders': 'y'}}}]
for variant in range(60):
    for si, snapshot in enumerate(snapshots):
        run('on_snapshot', lambda log, v=variant, m=snapshot: (ingest_battle(log, v), (copy.deepcopy(m),)), (variant, si)); cases += 1
    for ri, rules in enumerate(rule_sets):
        def make(log, v=variant, r=rules):
            b = ingest_battle(log, v)
            del b.__dict__['_apply_rules']
            return b, (copy.deepcopy(r),)
        run('_apply_rules', make, (variant, ri)); cases += 1

# Local gun tick and observation.
AMMO_SECONDS = 0.1
for variant in range(48):
    def make(log, v=variant):
        b = battle(log)
        if v % 9 == 8: b._replay_mode = v % 2 == 0
        b.state = ('running', 'loading')[v % 7 == 6]
        b._server = None if v % 11 == 10 else Log(log, 'server', vehicle_id=4)
        stub(b, log, '_server_entity', 'entity')
        stub(b, log, '_advance_local_gun_to', (lambda e: (_ for _ in ()).throw(ValueError('gun'))) if v % 5 == 4 else 'gun')
        stub(b, log, '_sync_local_server_marker', True)
        stub(b, log, '_run_optional_feature', lambda name, fn, args=(), *rest, **kw: fn(*args))
        stub(b, log, '_fail')
        stub(b, log, '_schedule')
        return b, ()
    run('_ammo_tick', make, (variant,)); cases += 1
    def make(log, v=variant):
        b = battle(log)
        rotator = Log(log, 'rotator', showServerMarker=(True, False, 1, 0)[v % 4])
        b._avatar = Log(log, 'avatar', missing=('gunRotator',) if v % 5 == 4 else ())
        if v % 5 != 4: b._avatar.gunRotator = None if v % 7 == 6 else rotator
        b._entity_probe = rotator
        return b, ()
    run('_sync_local_server_marker', make, (variant,)); cases += 1
    for message in ({'type': 'bot_observation', 'contacts': [{'target_kind': 'human', 'target_id': 4, 'observing_team': 2, 'fresh': True}]},
                    {'type': 'bot_observation', 'contacts': [7, {'target_kind': 'bot'}, {'target_kind': 'human', 'target_id': '4', 'observing_team': 1, 'fresh': 1},
                                                             {'target_kind': 'human', 'target_id': 4, 'observing_team': 3, 'fresh': 0}]},
                    {'type': 'bot_observation', 'contacts': None}, {'type': 'snapshot'}, {'type': 'bot_observation', 'contacts': [{'target_kind': 'human', 'target_id': 'x'}]},
                    {'type': 'bot_observation', 'contacts': ({'target_kind': 'human', 'target_id': 4, 'observing_team': 2, 'fresh': True},)}):
        def make(log, v=variant, m=message):
            b = battle(log)
            b._sixth_sense = None if v % 6 == 5 else Log(log, 'sixth')
            b.client = Log(log, 'client', player_id=(4, '4')[v % 2], team=(1, 2)[v % 3 == 2])
            return b, (copy.deepcopy(m), 12.5)
        run('_observe_local_vehicle', make, (variant, repr(message)[:30])); cases += 1
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
    chunks = ['import json, math', '_INTEGER_TYPES = (int, long)', '_STRING_TYPES = (basestring,)',
              'def timed(stage):\n    return lambda method: method', 'class Pinned(object):',
              "    def __repr__(self):\n        return '<battle>'"]
    for node in tree.body:
        if isinstance(node, ast.Assign) and any(getattr(t, 'id', None) in GLOBALS for t in node.targets):
            chunks.insert(1, '\n'.join(lines[node.lineno - 1:node.end_lineno]))
        if isinstance(node, (ast.FunctionDef, ast.ClassDef)) and node.name in GLOBALS:
            chunks.insert(1, '\n'.join(lines[node.lineno - 1:node.end_lineno]))
        if isinstance(node, ast.ClassDef) and node.name == 'BattleRuntime':
            for item in node.body:
                if isinstance(item, ast.Assign) and any(getattr(t, 'id', None) in CLASS_ATTRIBUTES for t in item.targets):
                    chunks.append('\n'.join(lines[item.lineno - 1:item.end_lineno]))
                if isinstance(item, ast.FunctionDef) and item.name in METHODS:
                    start = min([d.lineno for d in item.decorator_list] + [item.lineno])
                    chunks.append('\n'.join(lines[start - 1:item.end_lineno]))
    text = '\n\n'.join(chunks) + '\nBattleRuntime = Pinned\n'
    # Package-relative imports resolve to the fixture's fakes.
    text = text.replace('from .destructibles_sensor import collision_preparation_scope_1513',
                        "collision_preparation_scope_1513 = sys.modules['gui.mods.offline_lan_0922.destructibles_sensor']"
                        ".collision_preparation_scope_1513")
    for module in ('native_client_environment', 'native_client_drive', 'native_client_replica', 'native_bot_runtime',
                   'native_client_projectile', 'native_client_critical'):
        text = text.replace('from . import %s' % module, '%s = globals()[%r]' % (module, module))
    text = text.replace('from .native_input_diagnostics import write_json_record',
                        "write_json_record = sys.modules['gui.mods.offline_lan_0922.native_input_diagnostics']"
                        ".write_json_record")
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
                            '-Wno-register', '-Wno-missing-field-initializers', '-bundle', '-undefined', 'dynamic_lookup',
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
