"""Install the persistent visible projectile owner on exact pinned seams."""
import re


def prepend(source, name, statement):
    pattern = r'(^    def ' + re.escape(name) + r'\(\s*self[^:]*\):\n)'
    matches = list(re.finditer(pattern, source, re.M))
    if len(matches) != 1:
        raise ValueError('The pinned native projectile seam changed: ' + name)
    index = matches[0].end()
    return source[:index] + statement + source[index:]


def battle(source):
    source = prepend(source, '_discard_provisional_projectile',
        '        from . import native_client_projectile\n'
        '        if native_client_projectile.enabled(self):\n'
        '            native_client_projectile.discard(self, projectile_id)\n')
    before = ('                self._sample_projectile_positions(\n'
              '                    now, self._projectile_record_poses())\n')
    if source.count(before) != 1:
        raise ValueError('The pinned prebattle projectile sampling seam changed')
    source = source.replace(before,
        '                from . import native_client_projectile\n'
        '                if native_client_projectile.enabled(self):\n'
        '                    native_client_projectile.frame_sample(self, now)\n'
        '                else:\n'
        '                    self._sample_projectile_positions(\n'
        '                        now, self._projectile_record_poses())\n', 1)
    seams = (
        ('shoot', 'return native_client_projectile.shoot(self, player_burst is not None)'),
        ('_advance_local_player_burst', 'return native_client_projectile.advance_burst(self)'),
        ('_cancel_local_player_burst', 'return native_client_projectile.cancel(self, apply_settings)'),
        ('_observe_projectile_message', 'return native_client_projectile.observe(self, message)'),
        ('_accept_projectile_event', 'return native_client_projectile.event(self, event)'),
        ('_reconcile_projectile_snapshot', 'return native_client_projectile.ingress(self, message)'),
        ('_advance_projectiles', 'return native_client_projectile.frame(self, now, force_progress)'),
    )
    for name, dispatch in seams:
        source = prepend(source, name,
            '        from . import native_client_projectile\n'
            '        if native_client_projectile.enabled(self):\n'
            '            ' + dispatch + '\n')
    original = '    def _ensure_projectile_visual(self, normalized, now,\n                                  visual_start=None):\n'
    if source.count(original) != 1:
        raise ValueError('The pinned native projectile visual seam changed')
    source = source.replace(original,
        '    def _ensure_projectile_visual(self, normalized, now,\n'
        '                                  visual_start=None):\n'
        '        from . import native_client_projectile\n'
        '        if native_client_projectile.enabled(self):\n'
        '            return native_client_projectile.visual(\n'
        '                self, normalized, now, visual_start)\n'
        '        return self._ensure_projectile_visual_native_presentation(\n'
        '            normalized, now, visual_start)\n\n'
        '    def _ensure_projectile_visual_native_presentation(\n'
        '            self, normalized, now, visual_start=None):\n', 1)
    source = prepend(source, '_apply_projectile_terminal_event',
        '        from . import native_client_projectile\n'
        '        if native_client_projectile.enabled(self):\n'
        '            native_client_projectile.event(self, event)\n')
    source = prepend(source, '_cleanup',
        '        from . import native_client_projectile\n'
        '        native_client_projectile.retire(self)\n')
    return source
