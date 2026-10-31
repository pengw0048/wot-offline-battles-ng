"""Route the pinned remote vehicle lifecycle methods to their native ports."""
import re

PORTS = (
    ('_pending_combat_for_record', 'pending_combat(self, record, _COMBAT_EVENT_KINDS)'),
    ('_apply_vehicle_statistics', 'vehicle_statistics(self, record, state)'),
    ('_death_attacker_engine_id', 'death_attacker_engine_id(self, state)'),
    ('_should_suppress_postmortem_killer',
     'suppress_postmortem_killer(self, victim_record, death_state, attacker_id)'),
    ('_sync_fire_effect', 'sync_fire_effect(self, entity, burning)'),
    ('_stop_remote_visual', 'stop_remote_visual(self, record)'),
    ('_apply_critical_state', 'critical_state(self, record, payload, authority, critical_damage)'),
    ('_apply_stun_state', 'stun_state(self, record, state)'),
    ('_apply_siege_state', 'siege_state(self, record, state, vehicle_physics)'),
    ('_present_critical', 'present_critical(self, record, events, attacker_id)'),
    ('_sync_remote_visual_components',
     'remote_visual_components(self, record, vehicle, team_visible, world_visible)'),
    ('_set_record_spot_visibility',
     'spot_visibility(self, record, visible, marker_visible, native_client_query, set_draw_visibility, ENGINE_MODE_IDLE)'),
    ('_apply_spot_presentation', 'spot_presentation(self, record, entity, remembered)'),
    ('_apply_turret_detachment', 'turret_detachment(self, record, entity, state)'),
    ('_finalize_dead_remote_wreck', 'finalize_dead_wreck(self, record, entity)'),
    ('_apply_health',
     'apply_health(self, record, state, attacker_id, reason_id, force_cause, attack_reason_id, '
     'suppress_combat_presentation, suppress_postmortem_killer, critical_damage, '
     'native_client_environment, native_client_drive)'),
    ('_apply_record_pose',
     'record_pose(self, record, pose, native_client_replica, _engine_rotation, BOT_MOVING_SPEED)'),
    ('_flush_tombstone', 'flush_tombstone(self, record)'),
    ('_destroy_entity', 'destroy_entity(self, event)'),
    ('_update_entity', 'update_entity(self, event)'),
    ('_apply_sync_event', 'sync_event(self, event)'),
    ('_queue_bot_create', 'queue_bot_create(self, event)'),
    ('_flush_pending_bot_create', 'flush_bot_create(self, now)'),
    ('_flush_pending_entities', 'flush_pending_entities(self, now, native_client_replica)'),
    ('_create_remote', 'create_remote(self, event, _marks_on_gun, _engine_rotation, bot_planner)'),
    ('_materialize_record', 'materialize_record(self, record, set_draw_visibility)'),
    ('on_events', 'on_events(self, message)'),
    ('_prepare_ordered_event', 'prepare_ordered_event(self, event, globals())'),
    ('_event_is_ready', 'event_is_ready(self, event, globals())'),
    ('_apply_ordered_event', 'apply_ordered_event(self, event, globals())'),
    ('_drain_event_journal', 'drain_event_journal(self, globals())'),
    ('_known_event_state', 'known_event_state(self, key)'),
    ('_merge_shot_event_state', 'merge_shot_event_state(self, event, globals())'),
    ('_merge_combat_event_state', 'merge_combat_event_state(self, event)'),
    ('_merge_stun_event_state', 'merge_stun_event_state(self, event)'),
    ('_combat_event_state', 'combat_event_state(self, event, state, target_key)'),
    ('_combat_attack_reason', 'combat_attack_reason(self, event)'),
    ('_validate_combat_event_contract', 'validate_combat_event_contract(self, event, globals())'),
    ('_missing_projectile_attacker_allowed', 'missing_projectile_attacker_allowed(self, event)'),
    ('_pending_event_references', 'pending_event_references(self, key)'),
    ('_apply_combat_event', 'apply_combat_event(self, event, update_state, globals())'),
    ('_frame', 'frame(self, globals())'),
    ('_optional_feature_enabled', 'optional_feature_enabled(self, feature)'),
    ('_run_optional_feature',
     'run_optional_feature(self, feature, callback, args, on_error, disable, kwargs)'),
    ('_schedule', 'schedule(self, delay, function, ammo, globals())'),
    ('_maintain_standard_space_visibility', 'maintain_space_visibility(self, now, globals())'),
    ('_start_team_chat_when_roster_ready', 'start_team_chat(self)'),
    ('_prewarm_critical_layout', 'prewarm_critical_layout(self, globals())'),
    ('_maybe_send_battle_ready', 'maybe_send_battle_ready(self)'),
    ('_retry_bot_manifest', 'retry_bot_manifest(self, now)'),
    ('_attack_reason', 'attack_reason(self, member, fallback)'),
    ('_is_blind_local_attack', 'blind_local_attack(self, target_record, attacker_record)'),
    ('_apply_vehicle_statistics_event', 'vehicle_statistics_event(self, event)'),
    ('_present_direct_spot', 'present_direct_spot(self, record)'),
    ('_apply_detection_event', 'detection_event(self, event)'),
    ('_apply_assist_event', 'assist_event(self, event)'),
    ('_apply_environment_critical_feedback', 'environment_critical_feedback(self, event, globals())'),
    ('_apply_destructible_event', 'destructible_event(self, event, native_client_drive)'),
    ('_present_damage_sticker', 'present_damage_sticker(self, event, target_record)'),
    ('_apply_battle_result', 'battle_result(self, result)'),
    ('_present_combat_feedback', 'present_combat_feedback(self, event, target_record, attacker_record, reason_id)'),
    ('_present_combat_hit', 'present_combat_hit(self, event, target_record, attacker_record, attacker_id, globals())'),
    ('_show_shot', 'show_shot(self, event, update_state, globals())'),
    ('_projectile_is_authority', 'projectile_is_authority(self, globals())'),
    ('_owns_projectile', 'owns_projectile(self, meta, globals())'),
    ('_projectile_estimated_server_time', 'projectile_estimated_server_time(self, now)'),
    ('_new_projectile_meta', 'new_projectile_meta(self, normalized)'),
    ('_projectile_source_descriptor', 'projectile_source_descriptor(self, meta)'),
    ('_projectile_attacker_engine_id', 'projectile_attacker_engine_id(self, meta)'),
    ('_projectile_source_entity', 'projectile_source_entity(self, meta)'),
    ('_remember_fire_timeline', 'remember_fire_timeline(self, key, value)'),
    ('_projectile_shot', 'projectile_shot(self, meta, globals())'),
    ('_admit_projectile_visual', 'admit_projectile_visual(self, attacker_id, projectile_id, now)'),
    ('_show_local_shot_without_extra', 'show_local_shot_without_extra(self, entity, burst_count, globals())'),
    ('_shot_muzzle_drawn', 'shot_muzzle_drawn(self, record)'),
    ('_show_replay_local_muzzle', 'show_replay_local_muzzle(self, entity, burst_count)'),
    ('_reset_projectile_visuals', 'reset_projectile_visuals(self)'),
    ('_report_player_shot_timing', 'report_player_shot_timing(self, meta)'),
    ('_projectile_record_positions', 'projectile_record_positions(self, globals())'),
    ('_projectile_vehicle_matrices', 'projectile_vehicle_matrices(self, record, vehicle, ground_matrix)'),
    ('_projectile_descriptor_at_pose', 'projectile_descriptor_at_pose(self, target, pose)'),
    ('_report_projectile_terminal_failure',
     'report_projectile_terminal_failure(self, meta, state, stage, reason, error, globals())'),
    ('_confirm_bot_projectile_launch', 'confirm_bot_projectile_launch(self, normalized, globals())'),
    ('_projectile_visual_start', 'projectile_visual_start(self, entity, normalized, globals())'),
    ('_set_projectile_epoch', 'set_projectile_epoch(self, value, now, globals())'),
    ('_projectile_record_pose', 'projectile_record_pose(self, key, record, position, globals())'),
    ('_sample_projectile_positions', 'sample_projectile_positions(self, now, poses, globals())'),
    ('_projectile_frozen_target', 'projectile_frozen_target(self, target, pose, globals())'),
    ('_projectile_live_critical_target', 'projectile_live_critical_target(self, record, target, globals())'),
    ('_discard_provisional_projectile',
     'discard_provisional_projectile(self, projectile_id, native_client_projectile, globals())'),
    ('_observe_projectile_message',
     'observe_projectile_message(self, message, native_client_projectile, native_bot_runtime, globals())'),
    ('_install_projectile_meta', 'install_projectile_meta(self, normalized, globals())'),
    ('_projectile_record_poses', 'projectile_record_poses(self, globals())'),
    ('_projectile_explosion', 'projectile_explosion(self, projectile_id, impact, outcome, globals())'),
    ('_present_projectile_wreck_hit', 'present_projectile_wreck_hit(self, projectile_id, event, globals())'),
    ('_stop_projectile_visual', 'stop_projectile_visual(self, projectile_id, event, globals())'),
    ('_ensure_projectile_visual_native_presentation',
     'ensure_projectile_visual_native_presentation(self, normalized, now, visual_start, globals())'),
    ('_ensure_projectile_visual',
     'ensure_projectile_visual(self, normalized, now, visual_start, native_client_projectile, globals())'),
    ('_flush_pending_projectile_resolutions', 'flush_pending_projectile_resolutions(self, globals())'),
    ('_prune_projectile_position_history', 'prune_projectile_position_history(self, states, globals())'),
    ('_advance_projectiles',
     'advance_projectiles(self, now, force_progress, native_client_projectile, native_bot_runtime, globals())'),
    ('_accept_projectile_event',
     'accept_projectile_event(self, event, native_client_projectile, native_bot_runtime, globals())'),
    ('_apply_projectile_ricochet_event', 'apply_projectile_ricochet_event(self, event, native_bot_runtime, globals())'),
    ('_apply_projectile_terminal_event',
     'apply_projectile_terminal_event(self, event, native_client_projectile, native_bot_runtime, globals())'),
    ('on_fire_intent_result', 'on_fire_intent_result(self, message, globals())'),
    ('_defer_local_projectile_presentation', 'defer_local_projectile_presentation(self, event, globals())'),
    ('_reconcile_projectile_snapshot',
     'reconcile_projectile_snapshot(self, message, native_client_projectile, native_bot_runtime, globals())'),
)


# Static methods keep their decorator; the native port takes their arguments.
STATIC_PORTS = (
    ('_projectile_wire_meta', 'projectile_wire_meta(raw, globals())'),
    ('_projectile_plain_pose', 'projectile_plain_pose(position, state, globals())'),
    ('_projectile_boundary_pose', 'projectile_boundary_pose(position, state, globals())'),
    ('_projectile_visual_age', 'projectile_visual_age(raw, globals())'),
)


def battle(source):
    for name, call in STATIC_PORTS:
        pattern = r'(^    @staticmethod\n    def ' + re.escape(name) + r'\([^)]*\):\n)'
        matches = list(re.finditer(pattern, source, re.M))
        if len(matches) != 1:
            raise ValueError('The pinned lifecycle static seam changed: ' + name)
        index = matches[0].end()
        source = source[:index] + '        return _offline_lifecycle_native.' + call + '\n' + source[index:]
    for name, call in PORTS:
        pattern = r'(^    def ' + re.escape(name) + r'\(\s*self[^:]*\):\n)'
        matches = list(re.finditer(pattern, source, re.M))
        if len(matches) != 1:
            raise ValueError('The pinned lifecycle seam changed: ' + name)
        index = matches[0].end()
        imports = ''.join('        from . import %s\n' % module for module in (
            'native_client_query', 'native_client_environment', 'native_client_drive',
            'native_client_replica', 'native_client_projectile', 'native_bot_runtime') if module in call)
        source = (source[:index] + imports + '        return _offline_lifecycle_native.' + call + '\n' + source[index:])
    # One module-level import; each routed method only reads the global.
    return source + '\nimport offline_client_lifecycle_native as _offline_lifecycle_native\n'
