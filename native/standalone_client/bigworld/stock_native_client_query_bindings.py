"""Install native query ownership at exact pinned #1513 gameplay seams."""
import re


def once(source, before, after):
    if source.count(before) != 1:
        raise ValueError('The pinned native query seam changed: ' + before[:100])
    return source.replace(before, after, 1)


def prepend(source, name, statement):
    pattern = r'(^    def ' + re.escape(name) + r'\(\s*self[^:]*\):\n)'
    matches = list(re.finditer(pattern, source, re.M))
    if len(matches) != 1:
        raise ValueError('The pinned native query method changed: ' + name)
    index = matches[0].end()
    return source[:index] + statement + source[index:]


def battle(source):
    source = once(source, "            self.state = 'running'\n",
        "            from . import native_client_query\n"
        "            native_client_query.bind(self)\n"
        "            if not self._worker_mode:\n"
        "                import offline_client_query_native\n"
        "                offline_client_query_native.targeting_prepare(self)\n"
        "            self.state = 'running'\n")
    source = prepend(source, '_cleanup', '''        from . import native_client_query
        native_client_query.retire(self)
''')
    methods = (
        ('_tick_expert_target', 'tick_expert(self, now)'),
        ('monitor_vehicle_damaged_devices',
         'monitor_expert(self, vehicle_id, clear_reason)'),
        ('_disable_expert_presentation', 'disable_expert(self)'),
        ('_update_target_outline', 'target_outline(self, now)'),
        ('_clear_target_outline', 'clear_outline(self)'),
        ('_disable_target_outline_presentation', 'disable_outline(self)'),
    )
    for name, invocation in methods:
        source = prepend(source, name, '''        if not self._worker_mode:
            import offline_client_query_native
            return offline_client_query_native.%s
''' % invocation)
    start = source.index('        held_seen = False\n', source.index('    def _update_target_outline('))
    end = source.index('        # Retail drops the target', start)
    source = source[:start] + '''        from . import native_client_query
        pick = native_client_query.pick(
            self, start, direction, TARGET_MAX_DISTANCE, held_id,
            deselection_angle, now, _SHOT_OCCLUSION_EPSILON)
        if pick is None:
            self._clear_target_outline()
            return
        held_seen = pick['held_seen']
        held_reason = pick['held_reason']
        chosen = pick['chosen']
        chosen_depth = pick['depth']
        miss = pick['miss']
        decline = pick['decline']
        blocked = pick['blocked']
''' + source[end:]
    # Visibility changes outside snapshots must be published immediately. The
    # native lifecycle update runs only on an edge, not for every visible row.
    source = once(source, "        record['spot_visible'] = visible\n",
        "        if bool(record.get('spot_visible', True)) != visible:\n"
        "            record['spot_visible'] = visible\n"
        "            from . import native_client_query\n"
        "            native_client_query.lifecycle(self, record)\n"
        "        record['spot_visible'] = visible\n")
    return source


def targeting_cleanup(source):
    # This outermost seam releases GUI state before replica/query resources.
    return prepend(source, '_cleanup', '''        if not self._worker_mode:
            import offline_client_query_native
            offline_client_query_native.targeting_retire(self)
''')


def compat(source):
    source = prepend(source, 'validate_target_lock', '''        import offline_client_query_native
        return offline_client_query_native.validate_target_lock(self, avatar)
''')
    source = once(source,
        '            entities = compatibility._original_projectile_collidable_entities(\n',
        '            if compatibility._battle_active:\n'
        '                from . import native_client_query\n'
        '                native = native_client_query.collidable(\n'
        '                    compatibility, exceptIDs, startPoint, endPoint)\n'
        '                if native is not None:\n'
        '                    return native\n'
        '            entities = compatibility._original_projectile_collidable_entities(\n')
    source = once(source,
        '            if (compatibility._battle_active and\n'
        '                    undrawn_lan_remote(entity)):\n',
        '            if compatibility._battle_active:\n'
        '                from . import native_client_query\n'
        '                native = native_client_query.segment_may_hit(\n'
        '                    compatibility, entity, startPoint, endPoint)\n'
        '                if native is not None:\n'
        '                    return native\n'
        '            if (compatibility._battle_active and\n'
        '                    undrawn_lan_remote(entity)):\n')
    source = once(source,
        '            overlay = compatibility._vehicle_property_overlays.get(\n'
        '                id(vehicle))\n'
        "            if overlay is None or not overlay.get('_pose_active'):\n",
        '            from . import native_client_query\n'
        '            native = native_client_query.collisions(\n'
        '                compatibility, vehicle, start_point, end_point)\n'
        '            if native is not None:\n'
        '                return native\n'
        '            overlay = compatibility._vehicle_property_overlays.get(\n'
        '                id(vehicle))\n'
        "            if overlay is None or not overlay.get('_pose_active'):\n")
    source = once(source,
        '            runtime.projectile_mover_module.getCollidableEntities = (\n'
        '                projectile_collidable_entities)\n',
        '            runtime.projectile_mover_module.getCollidableEntities = (\n'
        '                projectile_collidable_entities)\n'
        '            from . import native_client_query\n'
        '            native_client_query.install_aliases(\n'
        '                self, self._original_projectile_collidable_entities,\n'
        '                projectile_collidable_entities)\n')
    return prepend(source, '_rollback_install', '''        from . import native_client_query
        native_client_query.restore_aliases(self)
''')


def turrets(source):
    # This owner already accepted and transformed the exact component boxes.
    # Notify only after a successful add/remove, never from a cursor query.
    before = "            self._turrets.pop(key, None)\n            return previous is not None\n"
    after = "            self._turrets.pop(key, None)\n            if previous is not None:\n                self._native_query_notify()\n            return previous is not None\n"
    source = once(source, before, after)
    source = once(source, "        return True\n\n    def _ready(self, server_time_ms):\n",
        "        self._native_query_notify()\n        return True\n\n"
        "    def _native_query_notify(self):\n"
        "        callback = getattr(self, '_native_query_changed', None)\n"
        "        if callback is not None:\n"
        "            callback()\n\n"
        "    def _ready(self, server_time_ms):\n")
    return once(source, '        self._turrets.clear()\n        return count\n',
        '        self._turrets.clear()\n        self._native_query_notify()\n'
        '        return count\n')
