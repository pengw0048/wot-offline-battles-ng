"""Replace only the player's gun factories and hot player publication chain."""
import re


def once(source, before, after):
    if source.count(before) != 1:
        raise ValueError('The pinned weapon runtime seam changed: ' + before[:100])
    return source.replace(before, after, 1)


def prepend(source, name, statement):
    pattern = r'(^    def ' + re.escape(name) + r'\(\s*self[^:]*\):\n)'
    matches = list(re.finditer(pattern, source, re.M))
    if len(matches) != 1:
        raise ValueError('The pinned weapon runtime method changed: ' + name)
    index = matches[0].end()
    return source[:index] + statement + source[index:]


def battle(source):
    if source.count('gun_mechanics.GunState(') != 2:
        raise ValueError('The pinned player gun factory count changed')
    source = source.replace('gun_mechanics.GunState(',
        "__import__('offline_client_weapon_native').create(")
    source = once(source,
        '    def _restore_local_equipment_snapshot(self, snapshot, present=False):\n',
        '    def _restore_local_equipment_snapshot(self, snapshot, present=False):\n'
        '        from . import native_client_weapon_runtime\n'
        '        native = native_client_weapon_runtime.restore_equipment_snapshot(\n'
        '            self, snapshot, present)\n'
        '        if native is not None:\n'
        '            return native\n'
        '        return self._restore_local_equipment_snapshot_cold(snapshot, present)\n\n'
        '    def _restore_local_equipment_snapshot_cold(self, snapshot, present=False):\n')
    source = prepend(source, '_authority_players', '''        if (not self._worker_mode and not self._replay_mode and
                getattr(self, '_native_client_state', None) is not None):
            # Visible frames never simulate Bots. Do not build unused worker
            # inputs or decorate contacts from a duplicate Python history.
            return []
''')
    source = prepend(source, '_remember_ram_bot_snapshot', '''        if (not self._worker_mode and not self._replay_mode and
                getattr(self, '_native_client_state', None) is not None):
            # NativePeers/PlayerRam and native trigger ledgers retain the
            # accepted Actor receipts; no visible Python consumer remains.
            return False
''')
    source = once(source,
        '        self._equipment_revision = revision\n',
        '        self._equipment_revision = revision\n'
        '        from . import native_client_weapon_runtime\n'
        '        native_client_weapon_runtime.equipment_ingress(self, states)\n')
    source = prepend(source, '_cleanup', '''        from . import native_client_weapon_runtime
        native_client_weapon_runtime.retire(self)
''')
    source = prepend(source, '_advance_local_gun_to', '''        from . import native_client_weapon_runtime
        native = native_client_weapon_runtime.advance(self, entity, now)
        if native is not None:
            return native
''')
    # A startup targeting initialization before running is a cold stock edge.
    # Once running, no player gun/critical/equipment business helper can run.
    source = prepend(source, '_publish_targeting_info', '''        from . import native_client_weapon_runtime
        native = native_client_weapon_runtime.publish(self, 'targeting')
        if native is not None:
            return native
''')
    source = prepend(source, '_publish_ammo_state', '''        from . import native_client_weapon_runtime
        native = native_client_weapon_runtime.publish(
            self, 'ammo', force=force)
        if native is not None:
            return native
''')
    source = prepend(source, '_publish_reload_event', '''        from . import native_client_weapon_runtime
        native = native_client_weapon_runtime.publish(
            self, 'reload', force=force, remaining=time_left, duration=base_time)
        if native is not None:
            return native
''')
    source = prepend(source, '_present_equipments', '''        from . import native_client_weapon_runtime
        native = native_client_weapon_runtime.publish(self, 'equipment', now)
        if native is not None:
            return native
''')
    source = prepend(source, '_pump_live_reload_sound', '''        from . import native_client_weapon_runtime
        native = native_client_weapon_runtime.audio_frame(self, now)
        if native is not None:
            return native
''')
    return prepend(source, '_tick_equipment_cooldowns', '''        from . import native_client_weapon_runtime
        native = native_client_weapon_runtime.publish(self, 'equipment', now)
        if native is not None:
            return native
''')
