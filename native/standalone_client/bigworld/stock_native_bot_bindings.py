"""Replace production worker entry points in the prepared gameplay copy."""
import re


def once(source, before, after):
    if source.count(before) != 1:
        raise ValueError('The pinned native Bot lifecycle seam changed: ' + before[:100])
    return source.replace(before, after, 1)


def prepend(source, name, statement):
    pattern = r'(^    def ' + re.escape(name) + r'\(self[^\n]*\):\n)'
    matches = list(re.finditer(pattern, source, re.M))
    if len(matches) != 1:
        raise ValueError('The pinned native Bot method changed: ' + name)
    index = matches[0].end()
    return source[:index] + statement + source[index:]


def bots(source):
    source = prepend(source, 'update', '''        from . import native_bot_runtime
        if native_bot_runtime.enabled(self):
            return native_bot_runtime.update(self, dt, now, players, neighbours)
''')
    source = prepend(source, '_retire_native_simulation', '''        from . import native_bot_runtime
        native_bot_runtime.retire(self)
''')
    source = prepend(source, 'prewarm_world_receipts', '''        from . import native_bot_runtime
        if native_bot_runtime.enabled(self):
            native_bot_runtime.prepare(self)
            return False
''')
    source = prepend(source, 'set_planning_snapshot', '''        from . import native_bot_runtime
        if native_bot_runtime.enabled(self):
            return False
''')
    source = prepend(source, 'apply_snapshot', '''        from . import native_bot_runtime
        if native_bot_runtime.snapshot(self, message):
            return
''')
    source = prepend(source, 'presentation_states', '''        from . import native_bot_runtime
        if native_bot_runtime.enabled(self):
            return native_bot_runtime.presentation(self, now)
''')
    source = prepend(source, 'ack_projectile_launch', '''        from . import native_bot_runtime
        if native_bot_runtime.enabled(self):
            return native_bot_runtime.acknowledge_launch(self, bot_id, fire_seq)
''')
    source = prepend(source, 'ack_human_ram_receipt', '''        from . import native_bot_runtime
        if native_bot_runtime.enabled(self):
            return native_bot_runtime.acknowledge_ram(self, player_id, seq)
''')
    return source


def battle(source):
    source = once(source,
        '                combat_diagnostics=self._combat_diagnostics)\n',
        '                combat_diagnostics=self._combat_diagnostics)\n'
        '            self._bots._standalone_battle = self\n')
    source = once(source,
        "                self._remember_ram_bot_snapshot(self._last_snapshot)\n            self.state = 'running'\n",
        "                self._remember_ram_bot_snapshot(self._last_snapshot)\n"
        "            if self._worker_mode:\n"
        "                from . import native_bot_runtime\n"
        "                native_bot_runtime.prepare(self._bots)\n"
        "            self.state = 'running'\n")
    source = prepend(source, '_advance_artillery_arcs', '''        from . import native_bot_runtime
        if self._bots is not None and native_bot_runtime.enabled(self._bots):
            return 0
''')
    source = prepend(source, '_apply_authority_bot_poses', '''        from . import native_bot_runtime
        if self._bots is not None and native_bot_runtime.enabled(self._bots):
            return native_bot_runtime.apply_presentation(self._bots, states, self._clock())
''')
    source = prepend(source, '_prewarm_bot_destructible_registries', '''        from . import native_bot_runtime
        if self._bots is not None and native_bot_runtime.enabled(self._bots):
            return native_bot_runtime.prewarm(self._bots, now)
''')
    source = prepend(source, '_refresh_fallen_tree_foliage', '''        from . import native_bot_runtime
        if self._bots is not None and native_bot_runtime.enabled(self._bots):
            return native_bot_runtime.frame(self._bots, now, force)
''')
    source = prepend(source, '_prewarm_player_tree_registries', '''        from . import native_bot_runtime
        if self._bots is not None and native_bot_runtime.enabled(self._bots):
            return native_bot_runtime.prewarm_players(self._bots, now)
''')
    source = prepend(source, '_authority_players', '''        from . import native_bot_runtime
        if self._bots is not None and native_bot_runtime.enabled(self._bots):
            return native_bot_runtime.players(self._bots)
''')
    source = prepend(source, '_resolve_player_destructible_contacts', '''        from . import native_bot_runtime
        if self._bots is not None and native_bot_runtime.enabled(self._bots):
            return native_bot_runtime.player_contacts(self._bots, players, now)
''')
    source = prepend(source, '_publish_player_environment', '''        from . import native_bot_runtime
        if self._bots is not None and native_bot_runtime.enabled(self._bots):
            return native_bot_runtime.player_environment(self._bots, dt, now)
''')
    source = prepend(source, '_human_ram_armor_results', '''        from . import native_bot_runtime
        if self._bots is not None and native_bot_runtime.enabled(self._bots):
            return native_bot_runtime.human_ram(self._bots)
''')
    source = prepend(source, '_apply_destructible_event', '''        from . import native_bot_runtime
        if self._bots is not None and native_bot_runtime.enabled(self._bots):
            return native_bot_runtime.world_ingress(self._bots, event, self._clock())
''')
    source = prepend(source, '_queue_detached_turret', '''        from . import native_bot_runtime
        if self._bots is not None and native_bot_runtime.enabled(self._bots):
            return native_bot_runtime.detached_queue(self._bots, record, entity, state, self._clock())
''')
    source = prepend(source, '_advance_detached_turrets', '''        from . import native_bot_runtime
        if self._bots is not None and native_bot_runtime.enabled(self._bots):
            return native_bot_runtime.detached_advance(self._bots, now)
''')
    source = prepend(source, '_reconcile_detached_turret_snapshot', '''        from . import native_bot_runtime
        if self._bots is not None and native_bot_runtime.enabled(self._bots):
            accepted = native_bot_runtime.detached_ingress(self._bots, message, self._clock())
            if accepted is not None:
                return accepted
''')
    source = once(source, '''                turrets = dict(self._detached_turret_rows)
                turrets.update(self._detached_turret_proposals)
                if turrets:
                    state_kwargs['detached_turrets'] = [
                        turrets[key] for key in sorted(turrets)]
''', '''                from . import native_bot_runtime
                turrets = native_bot_runtime.detached_rows(self._bots)
                if turrets:
                    state_kwargs['detached_turrets'] = turrets
''')
    source = prepend(source, '_advance_projectiles', '''        from . import native_bot_runtime
        if self._bots is not None and native_bot_runtime.enabled(self._bots):
            return native_bot_runtime.projectile_advance(self._bots, now, force_progress)
''')
    source = prepend(source, '_resolve_bot_fire', '''        from . import native_bot_runtime
        if self._bots is not None and native_bot_runtime.enabled(self._bots):
            return native_bot_runtime.projectile_launches(self._bots, message)
''')
    source = prepend(source, '_reconcile_projectile_snapshot', '''        from . import native_bot_runtime
        if self._bots is not None and native_bot_runtime.enabled(self._bots):
            accepted = native_bot_runtime.projectile_ingress(self._bots, message, self._clock())
            if accepted is not None:
                return accepted
''')
    source = prepend(source, '_observe_projectile_message', '''        from . import native_bot_runtime
        if self._bots is not None and native_bot_runtime.enabled(self._bots):
            accepted = native_bot_runtime.projectile_observe(self._bots, message, self._clock())
            if accepted is not None:
                return accepted
''')
    for method in ('_accept_projectile_event', '_apply_projectile_ricochet_event',
                   '_apply_projectile_terminal_event'):
        source = prepend(source, method, '''        from . import native_bot_runtime
        if self._bots is not None and native_bot_runtime.enabled(self._bots):
            return native_bot_runtime.projectile_event(self._bots, event, self._clock())
''')
    source = once(source, '''        if kind == 'authority':
            self._set_projectile_epoch(
''', '''        if kind == 'authority':
            if self._worker_mode:
                from . import native_bot_runtime
                native_bot_runtime.projectile_event(self._bots, event, self._clock())
            self._set_projectile_epoch(
''')
    return source
