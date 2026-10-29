"""Replace the pinned visible turret producer, retaining cold stock effects."""
import re


def once(source, before, after):
    if source.count(before) != 1:
        raise ValueError('The pinned native turret seam changed: ' + before[:80])
    return source.replace(before, after, 1)


def prepend(source, name, statement):
    pattern = r'(^    def ' + re.escape(name) + r'\(\s*self[^:]*\):\n)'
    matches = list(re.finditer(pattern, source, re.M))
    if len(matches) != 1:
        raise ValueError('The pinned native turret method changed: ' + name)
    index = matches[0].end()
    return source[:index] + statement + source[index:]


def presenter(source):
    source = once(source, '        self._closed = False\n',
        '        self._closed = False\n'
        '        from gui.mods.offline_lan_0922 import native_client_turret_presentation\n'
        '        native_client_turret_presentation.bind(self)\n')
    source = once(source, '            self._turrets.append(turret)\n',
        '            self._turrets.append(turret)\n'
        '            from gui.mods.offline_lan_0922 import native_client_turret_presentation\n'
        '            native_client_turret_presentation.adopt(self, turret)\n')
    start = source.index("        if 'body' not in row['flight']:\n", source.index('    def _buffer_canonical('))
    end = source.index('\n    def launch(', start)
    source = source[:start] + '''        from gui.mods.offline_lan_0922 import native_client_turret_presentation
        return native_client_turret_presentation.push(
            turret, row, now, elapsed)
''' + source[end:]
    for name, body in (
            ('advance', 'return native_client_turret_presentation.advance(self, now)'),
            ('_retire_entities', 'return native_client_turret_presentation.retire(self)'),
            ('destroy_all', 'return native_client_turret_presentation.clear(self)')):
        source = prepend(source, name,
            '        from gui.mods.offline_lan_0922 import native_client_turret_presentation\n'
            '        ' + body + '\n')
    return source


def battle(source):
    source = prepend(source, '_advance_detached_turrets', '''        if not self._worker_mode:
            from . import native_client_turret_presentation
            return native_client_turret_presentation.frame(self, now)
''')
    start = source.index('    def _reconcile_detached_turret_snapshot(')
    end = source.index('\n    def _turret_server_time_ms(', start)
    method = source[start:end]
    method = once(method, '        return changed\n',
        '        if changed and not self._worker_mode:\n'
        '            from . import native_client_turret_presentation\n'
        '            native_client_turret_presentation.ingress(self)\n'
        '        return changed\n')
    return source[:start] + method + source[end:]
