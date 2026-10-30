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
)


def battle(source):
    for name, call in PORTS:
        pattern = r'(^    def ' + re.escape(name) + r'\(\s*self[^:]*\):\n)'
        matches = list(re.finditer(pattern, source, re.M))
        if len(matches) != 1:
            raise ValueError('The pinned lifecycle seam changed: ' + name)
        index = matches[0].end()
        imports = ''.join('        from . import %s\n' % module for module in (
            'native_client_query', 'native_client_environment', 'native_client_drive',
            'native_client_replica') if module in call)
        source = (source[:index] + imports + '        import offline_client_lifecycle_native\n'
                  '        return offline_client_lifecycle_native.' + call + '\n' + source[index:])
    return source
