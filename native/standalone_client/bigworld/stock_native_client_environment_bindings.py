"""Install one visible native environment without a second scene/animator."""
import re


def once(source, before, after):
    if source.count(before) != 1:
        raise ValueError('The pinned environment seam changed: ' + before[:100])
    return source.replace(before, after, 1)


def prepend(source, name, statement):
    pattern = r'(^    def ' + re.escape(name) + r'\(\s*self[^:]*\):\n)'
    matches = list(re.finditer(pattern, source, re.M))
    if len(matches) != 1:
        raise ValueError('The pinned environment method changed: ' + name)
    index = matches[0].end()
    return source[:index] + statement + source[index:]


def battle(source):
    source = once(source, "            self.state = 'running'\n",
        "            from . import native_client_environment\n"
        "            native_client_environment.bind(self)\n"
        "            self.state = 'running'\n")
    source = prepend(source, '_cleanup', '''        from . import native_client_environment
        native_client_environment.retire(self)
''')
    for name, method in (('_tick_drowning', 'drowning'), ('_tick_overturn', 'overturn')):
        source = prepend(source, name, """        if not self._worker_mode and self.state == 'running':
            import offline_client_environment_native
            return offline_client_environment_native.%s(self, dt, now)
""" % method)
    source = prepend(source, '_update_spotting', '''        from . import native_client_environment
        native = native_client_environment.frame(self, now, hud_only)
        if native is not None:
            return native
''')
    source = prepend(source, '_refresh_fallen_tree_foliage', '''        from . import native_client_environment
        native = native_client_environment.foliage(self, now, force)
        if native is not None:
            return native
''')
    source = prepend(source, '_activate_fallen_tree_foliage', '''        from . import native_client_environment
        native = native_client_environment.activate(self, chunk_id, item_index)
        if native is not None:
            return native
''')
    source = once(source,
        "            holder['shot_penalty_until'] = deadline\n",
        "            holder['shot_penalty_until'] = deadline\n"
        "            from . import native_client_environment\n"
        "            native_client_environment.lifecycle(self, holder)\n")
    source = once(source,
        "            record['shot_penalty_until'] = (\n"
        "                self._clock() + spotting.SHOT_CAMOUFLAGE_SECONDS)\n",
        "            record['shot_penalty_until'] = (\n"
        "                self._clock() + spotting.SHOT_CAMOUFLAGE_SECONDS)\n"
        "            from . import native_client_environment\n"
        "            native_client_environment.lifecycle(self, record)\n")
    source = once(source,
        "            record['dead_marker_known'] = \\\n"
        "                self._combat_target_is_spotted(record)\n",
        "            record['dead_marker_known'] = \\\n"
        "                self._combat_target_is_spotted(record)\n"
        "            from . import native_client_environment\n"
        "            native_client_environment.lifecycle(self, record)\n")
    return prepend(source, '_apply_team_observation', '''        from . import native_client_environment
        native = native_client_environment.radio(self, message, now)
        if native is not None:
            return native
''')
