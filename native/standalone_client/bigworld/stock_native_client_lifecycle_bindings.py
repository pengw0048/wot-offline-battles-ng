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
)


def battle(source):
    for name, call in PORTS:
        pattern = r'(^    def ' + re.escape(name) + r'\(\s*self[^:]*\):\n)'
        matches = list(re.finditer(pattern, source, re.M))
        if len(matches) != 1:
            raise ValueError('The pinned lifecycle seam changed: ' + name)
        index = matches[0].end()
        source = (source[:index] + '        import offline_client_lifecycle_native\n'
                  '        return offline_client_lifecycle_native.' + call + '\n' + source[index:])
    return source
