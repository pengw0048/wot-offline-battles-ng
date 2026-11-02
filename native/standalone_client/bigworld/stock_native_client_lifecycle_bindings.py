"""Route the pinned remote vehicle lifecycle methods to their native ports."""
import ast
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
    ('_report_local_compound', 'report_local_compound(self, now, globals())'),
    ('_report_local_decals', 'report_local_decals(self, globals())'),
    ('_clock', 'clock(self, globals())'),
    ('_tick_critical_states', 'tick_critical_states(self, dt, native_client_critical, globals())'),
    ('_advance_artillery_arcs', 'advance_artillery_arcs(self, now, native_bot_runtime, globals())'),
    ('_advance_authority_worker_probe', 'advance_authority_worker_probe(self, globals())'),
    ('_hit_damage_factor', 'hit_damage_factor(self, event, target_record, globals())'),
    ('_impact_sound_identity',
     'impact_sound_identity(self, target_record, attacker_id, direction, damage_factor, globals())'),
    ('_surface_effect_material', 'surface_effect_material(self, impact, globals())'),
    ('_release_target_lock', 'release_target_lock(self, engine_id, globals())'),
    ('_report_effect', 'report_effect(self, kind, material, effects_index, where, direction, event, globals())'),
    ('_report_hit_impulse', 'report_hit_impulse(self, target_record, direction, impulse, presented, globals())'),
    ('_decoded_hit_impulse_direction', 'decoded_hit_impulse_direction(self, event, target_record, globals())'),
    ('_present_hit_impulse',
     'present_hit_impulse(self, event, target_record, effects_descr, splash_direction, globals())'),
    ('_event_shell', 'event_shell(self, attacker_record, event, globals())'),
    ('_shell_is_gold', 'shell_is_gold(self, shell, globals())'),
    ('_report_feedback_shell_failure', 'report_feedback_shell_failure(self, reason, globals())'),
    ('_feedback_shell_fields', 'feedback_shell_fields(self, event, attacker_record, globals())'),
    ('_fallback_postmortem_viewpoint', 'fallback_postmortem_viewpoint(self, excluded_engine_id, globals())'),
    ('_report_local_kill_contract',
     'report_local_kill_contract(self, target_record, attacker_id, reason_id, globals())'),
    ('on_snapshot', 'on_snapshot(self, message, globals())'),
    ('_apply_rules', 'apply_rules(self, rules, globals())'),
    ('_ammo_tick', 'ammo_tick(self, globals())'),
    ('_sync_local_server_marker', 'sync_local_server_marker(self, globals())'),
    ('_observe_local_vehicle', 'observe_local_vehicle(self, message, now, globals())'),
    ('_remove_ram_bot_history_index', 'remove_ram_bot_history_index(self, revision, sample_time_us, states, globals())'),
    ('_remember_ram_bot_snapshot', 'remember_ram_bot_snapshot(self, snapshot, globals())'),
    ('_ack_local_ram_contacts', 'ack_local_ram_contacts(self, snapshot, native_client_drive, globals())'),
    ('_ack_local_destructible_contacts', 'ack_local_destructible_contacts(self, snapshot, native_client_drive, globals())'),
    ('_reconcile_detached_turret_snapshot',
     'reconcile_detached_turret_snapshot(self, message, native_bot_runtime, globals())'),
    ('_record_worker_control_diagnostics', 'record_worker_control_diagnostics(self, sample_time_before, globals())'),
    ('_worker_diagnostic_totals', 'worker_diagnostic_totals(self, globals())'),
    ('_worker_control_snapshot', 'worker_control_snapshot(self, globals())'),
    ('_drain_worker_transport', 'drain_worker_transport(self, globals())'),
    ('_remember_remote_track_turn', 'remember_remote_track_turn(self, record, yaw, now, globals())'),
    ('_update_bot_tracks', 'update_bot_tracks(self, record, state, now, turn_override, globals())'),
    ('_send_bot_message', 'send_bot_message(self, message, native_bot_runtime, globals())'),
    ('_enqueue_bot_message', 'enqueue_bot_message(self, message, native_bot_runtime, globals())'),
    ('_simulated_rpm_and_gear', 'simulated_rpm_and_gear(self, globals())'),
    ('_publish_rpm', 'publish_rpm(self, now, force, globals())'),
    ('_report_track_surface', 'report_track_surface(self, entity, owner, receipt, now, globals())'),
    ('_bot_track_params', 'bot_track_params(self, record, entity, globals())'),
    ('_bot_engine_mode', 'bot_engine_mode(self, alive, speed, turn, globals())'),
    ('_report_bot_tracks', 'report_bot_tracks(self, vehicle, left, right, mode, now, globals())'),
    ('on_battle_live', 'on_battle_live(self, message, globals())'),
    ('on_bot_observation', 'on_bot_observation(self, message, globals())'),
    ('_collection_counts', 'collection_counts(self, globals())'),
    ('_present_splash_hit', 'present_splash_hit(self, target_record, effects_descr, effects_index, impact_position, '
                            'direction, damage_factor, globals())'),
    ('_log_bot_motion_probes', 'log_bot_motion_probes(self, now, globals())'),
    ('_spotting_profile', 'spotting_profile(self, descriptor, local, globals())'),
    ('_vision_radius', 'vision_radius(self, descriptor, entity, still_seconds, local, globals())'),
    ('acknowledge_local_damage_report',
     'acknowledge_local_damage_report(self, base_revision, ack_seq, server_revision, globals())'),
    ('_reconcile_critical_authority',
     'reconcile_critical_authority(self, record, source, canonical, native_client_critical, globals())'),
    ('_authority_worker_probe_sample', 'authority_worker_probe_sample(self, globals())'),
    ('authority_worker_ready_for_draw_off', 'authority_worker_ready_for_draw_off(self, globals())'),
    ('_enqueue_bot_manifest', 'enqueue_bot_manifest(self, message, now, native_bot_runtime, globals())'),
    ('_resolve_player_descriptor', 'resolve_player_descriptor(self, state, globals())'),
    ('_server_entity', 'server_entity(self, entity_id)'),
    ('_server_clock', 'server_clock(self)'),
    ('_record_position', 'record_position(self, record, globals())'),
    ('_bot_authority_is_local', 'bot_authority_is_local(self, player_id)'),
    ('_local_factors', 'local_factors(self, descriptor, globals())'),
    ('_local_state', 'battle_local_state(self)'),
    ('_set_gun_locked', 'set_gun_locked(self, locked)'),
    ('_echo_local_gun_angles', 'echo_local_gun_angles(self, turret_yaw, gun_pitch)'),
    ('_enable_expert_visibility', 'enable_expert_visibility(self)'),
    ('_advance_local_gun_edge', 'advance_local_gun_edge(self, state, now)'),
    ('_apply_authority_bot_poses', 'apply_authority_bot_poses(self, states, native_bot_runtime)'),
    ('_present_authority_bot_poses', 'present_authority_bot_poses(self, presentation_states, now)'),
    ('_tick_drowning', 'tick_drowning(self, dt, now)'),
    ('_tick_overturn', 'tick_overturn(self, dt, now)'),
    ('_tick_expert_target', 'tick_expert_target(self, now)'),
    ('_update_target_outline', 'update_target_outline(self, now)'),
    ('_refresh_fallen_tree_foliage',
     'refresh_fallen_tree_foliage(self, now, force, native_client_environment, native_client_drive, native_bot_runtime)'),
    ('_advance_detached_turrets',
     'advance_detached_turrets(self, now, native_client_turret_presentation, native_bot_runtime)'),
)


# Static methods keep their decorator; the native port takes their arguments.
STATIC_PORTS = (
    ('_projectile_wire_meta', 'projectile_wire_meta(raw, globals())'),
    ('_projectile_plain_pose', 'projectile_plain_pose(position, state, globals())'),
    ('_projectile_boundary_pose', 'projectile_boundary_pose(position, state, globals())'),
    ('_projectile_visual_age', 'projectile_visual_age(raw, globals())'),
    ('_projectile_cosmetic_allowed', 'projectile_cosmetic_allowed(event, visual_meta, globals())'),
    ('_event_entity_key', 'event_entity_key(event, role, globals())'),
    ('_combat_event_source', 'combat_event_source(event, globals())'),
    ('_record_alive', 'record_alive(record, entity)'),
    ('_critical_state', 'critical_state_copy(payload)'),
    ('_combat_target_is_spotted', 'combat_target_is_spotted(record)'),
    ('_store_remote_visual_components', 'store_remote_visual_components(record, world_marker, minimap)'),
    ('_shot_invisibility_factor', 'shot_invisibility_factor(descriptor, globals())'),
    ('_base_invisibility', 'base_invisibility(descriptor, profile, camouflage_id, globals())'),
)


_PACKAGE_MODULES = ('native_client_query', 'native_client_environment', 'native_client_drive', 'native_client_replica',
                    'native_client_projectile', 'native_bot_runtime', 'native_client_critical',
                    'native_client_turret_presentation')


def battle(source):
    # Each ported method keeps its signature and decorators; its pinned body is replaced by the native route.
    routes = dict((name, (call, False)) for name, call in PORTS)
    routes.update((name, (call, True)) for name, call in STATIC_PORTS)
    tree = ast.parse(source)
    runtime = [node for node in tree.body if isinstance(node, ast.ClassDef) and node.name == 'BattleRuntime']
    if len(runtime) != 1:
        raise ValueError('The pinned lifecycle class seam changed')
    names = [node.name for node in runtime[0].body if isinstance(node, ast.FunctionDef)]
    if any(names.count(name) != 1 for name in routes):
        raise ValueError('The pinned lifecycle seam changed: ' + ', '.join(sorted(name for name in routes if names.count(name) != 1)))
    lines = source.splitlines(True)
    edits = []
    for node in runtime[0].body:
        if not isinstance(node, ast.FunctionDef) or node.name not in routes:
            continue
        call, static = routes.pop(node.name)
        decorators = [getattr(item, 'id', None) for item in node.decorator_list]
        if static != ('staticmethod' in decorators) or (not static and node.args.args[0].arg != 'self'):
            raise ValueError('The pinned lifecycle seam changed: ' + node.name)
        imports = ''.join('        from . import %s\n' % module for module in _PACKAGE_MODULES
                          if re.search(r'\b%s\b' % module, call))
        edits.append((node.body[0].lineno - 1, node.end_lineno, imports + '        return _offline_lifecycle_native.' + call + '\n'))
    if routes:
        raise ValueError('The pinned lifecycle seam changed: ' + ', '.join(sorted(routes)))
    for start, end, body in sorted(edits, reverse=True):
        lines[start:end] = [body]
    source = ''.join(lines)
    seam = '            _FrameDiagnostics(\n                initial_window_seconds='
    if source.count(seam) != 1:
        raise ValueError('The pinned frame diagnostics seam changed')
    source = source.replace(seam, '            _offline_lifecycle_native.FrameDiagnostics(globals(),\n'
                                  '                initial_window_seconds=')
    # One module-level import ahead of the module's own g_battle_runtime = BattleRuntime().
    future = 'from __future__ import print_function\n'
    if not source.startswith(future):
        raise ValueError('The pinned lifecycle import seam changed')
    return future + 'import offline_client_lifecycle_native as _offline_lifecycle_native\n' + source[len(future):]


RECORDING_PORTS = ('poll_process_status(client)', 'ensure_recording(runtime)', 'local_state(runtime)', 'record_local(runtime)')


def offline_replay(source):
    """Route the per-frame offline replay recording functions to their native ports."""
    tree = ast.parse(source)
    lines = source.splitlines(True)
    edits = []
    for call in RECORDING_PORTS:
        definition = 'def %s:\n' % call
        if source.count(definition) != 1:
            raise ValueError('The pinned replay recording seam changed: ' + call)
        nodes = [node for node in tree.body if isinstance(node, ast.FunctionDef)
                 and node.name == call.split('(')[0]]
        if len(nodes) != 1:
            raise ValueError('The pinned replay recording function seam changed: ' + call)
        node = nodes[0]
        edits.append((node.body[0].lineno - 1, node.end_lineno,
                      '    return _offline_lifecycle_native.%s, globals())\n' % call[:-1]))
    for start, end, body in sorted(edits, reverse=True):
        lines[start:end] = [body]
    source = ''.join(lines)
    future = 'from __future__ import print_function\n'
    if source.count(future) != 1:
        raise ValueError('The pinned replay recording import seam changed')
    return source.replace(future, future + '\nimport offline_client_lifecycle_native as _offline_lifecycle_native\n')


LAN_POLL_PORTS = (
    ('_poll', 'lan_poll(self, globals())'),
    ('_poll_messages', 'lan_poll_messages(self, globals())'),
    ('_take_ready_messages', 'lan_take_ready_messages(self, generation, globals())'),
    ('_dispatch_message', 'lan_dispatch_message(self, message, generation, globals())'),
    ('_notify', 'lan_notify(self, kind, message, replay_hint, globals())'),
    ('_notify_message', 'lan_notify_message(self, kind, message, replay_hint, globals())'),
    ('transport_performance_snapshot', 'lan_performance_snapshot(self, globals())'),
    ('_report_snapshot_stall', 'lan_report_snapshot_stall(self, now, globals())'),
)


def lan_client(source):
    """Route the transport timing and the main-thread poll chain to their native ports."""
    tree = ast.parse(source)
    performance = [node for node in tree.body if isinstance(node, ast.ClassDef)
                   and node.name == '_TransportPerformance']
    clients = [node for node in tree.body if isinstance(node, ast.ClassDef) and node.name == 'LANClient']
    if len(performance) != 1 or len(clients) != 1:
        raise ValueError('The pinned transport performance seam changed')
    lines = source.splitlines(True)
    edits = [(performance[0].lineno - 1, performance[0].end_lineno,
              'def _TransportPerformance(generation):\n'
              '    return _offline_lifecycle_native.TransportPerformance(globals(), generation)\n')]
    for name, call in LAN_POLL_PORTS:
        nodes = [node for node in clients[0].body if isinstance(node, ast.FunctionDef) and node.name == name]
        if len(nodes) != 1 or not nodes[0].args.args or nodes[0].args.args[0].arg != 'self':
            raise ValueError('The pinned LAN poll seam changed: ' + name)
        node = nodes[0]
        edits.append((node.body[0].lineno - 1, node.end_lineno,
                      '        return _offline_lifecycle_native.' + call + '\n'))
    for start, end, body in sorted(edits, reverse=True):
        lines[start:end] = [body]
    source = ''.join(lines)
    future = 'from __future__ import print_function\n'
    if not source.startswith(future):
        raise ValueError('The pinned transport performance import seam changed')
    return future + 'import offline_client_lifecycle_native as _offline_lifecycle_native\n' + source[len(future):]
