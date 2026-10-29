"""Install the measured local support/vertical chain on its native owner."""
import re


def once(source, before, after):
    if source.count(before) != 1:
        raise ValueError('The pinned client-drive seam changed: ' + before[:100])
    return source.replace(before, after, 1)


def prepend(source, name, statement):
    pattern = r'(^    def ' + re.escape(name) + r'\(\s*self[^:]*\):\n)'
    matches = list(re.finditer(pattern, source, re.M))
    if len(matches) != 1:
        raise ValueError('The pinned client-drive method changed: ' + name)
    index = matches[0].end()
    return source[:index] + statement + source[index:]


def battle(source):
    if "return native_client_drive.advance(" in source:
        raise ValueError("The native client-drive binding is already installed")
    source = prepend(source, '_update_suspension_vertical_motion', '''        from . import native_client_drive
        if native_client_drive.enabled(self):
            return native_client_drive.advance(
                self, entity, position, yaw, dt,
                support_height_delta, support_speed_delta)
''')
    source = once(source, '''    def _restore_local_suspension_state(self, snapshot):
        for name, value in snapshot.items():
            setattr(self, name, value)
''', '''    def _restore_local_suspension_state(self, snapshot):
        for name, value in snapshot.items():
            setattr(self, name, value)
        from . import native_client_drive
        native_client_drive.restore(self)
''')
    source = prepend(source, '_cleanup', '''        from . import native_client_drive
        native_client_drive.retire(self)
''')
    source = prepend(source, '_refresh_fallen_tree_foliage', '''        from . import native_client_drive
        if native_client_drive.enabled(self):
            native_client_drive.frame(self, now)
''')
    # Keep canonical destruction, local-prediction retirement and foliage under
    # the existing visible owner. Native support sees its ledger after commit.
    source = once(source, '    def _apply_destructible_event(self, event):\n',
        '    def _apply_destructible_event(self, event):\n'
        '        result = self._apply_destructible_event_visible(event)\n'
        '        from . import native_client_drive\n'
        '        if native_client_drive.enabled(self):\n'
        '            native_client_drive.ingress(self, event)\n'
        '        return result\n\n'
        '    def _apply_destructible_event_visible(self, event):\n')
    source = once(source, "            self.state = 'running'\n", "            from . import native_client_drive\n"
        "            if native_client_drive.enabled(self):\n"
        "                native_client_drive.prepare(self)\n"
        "            self.state = 'running'\n")
    return source


_LEDGER_REVISION = '_native_client_drive_ledger_revision'


def _ledger_mutations(source, pattern, expected):
    matches = list(re.finditer(pattern, source, re.M))
    if len(matches) != expected:
        raise ValueError('The pinned client-drive ledger mutations changed')
    for match in reversed(matches):
        indent = match.group('indent')
        bump = (indent + "globals()['" + _LEDGER_REVISION + "'] = "
                "globals().get('" + _LEDGER_REVISION + "', 0) + 1\n")
        source = source[:match.end()] + bump + source[match.end():]
    reset = re.compile(r'(^def reset\(spaceID=None\):\n)', re.M)
    matches = list(reset.finditer(source))
    if len(matches) != 1:
        raise ValueError('The pinned client-drive ledger reset changed')
    index = matches[0].end()
    source = (source[:index] + "\tglobals()['" + _LEDGER_REVISION + "'] = "
              "globals().get('" + _LEDGER_REVISION + "', 0) + 1\n" + source[index:])
    return source + "\n" + _LEDGER_REVISION + " = 0\n"


def authority(source):
    return _ledger_mutations(source,
        r"^(?P<indent>[^\S\n]*)c\['keys'\]\.add\((?:key|dedupKey)\)\n", 2)


def sensor(source):
    return _ledger_mutations(source,
        r"^(?P<indent>[^\S\n]*)(?:state\['native_committed'\]\.add\([^\n]+\)|"
        r"_st\['felled'\]\.add\(_key\)|predicted\.(?:add|remove)\(key\)|"
        r"speculative\.discard\(key\))\n", 10)
