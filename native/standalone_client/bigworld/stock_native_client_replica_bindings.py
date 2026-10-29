"""Install the native visible replica at the pinned snapshot lifecycle seams."""
import re


def once(source, before, after):
    if source.count(before) != 1:
        raise ValueError('The pinned client replica seam changed: ' + before[:100])
    return source.replace(before, after, 1)


def prepend(source, name, statement):
    pattern = r'(^    def ' + re.escape(name) + r'\(self[^\n]*\):\n)'
    matches = list(re.finditer(pattern, source, re.M))
    if len(matches) != 1:
        raise ValueError('The pinned client replica method changed: ' + name)
    index = matches[0].end()
    return source[:index] + statement + source[index:]


def snapshot(source):
    source = once(source,
        '        return output\n\n    def _sequence(self, message):',
        '        from . import native_client_replica\n'
        '        native_client_replica.ingest(self)\n'
        '        return output\n\n'
        '    def _sequence(self, message):')
    source = once(source,
        '        return output\n\n    def _advance_wreck(self, record, key, alpha, output):',
        '        from . import native_client_replica\n'
        '        native_client_replica.ingest(self)\n'
        '        return output\n\n'
        '    def _advance_wreck(self, record, key, alpha, output):')
    return prepend(source, 'advance', '''        from . import native_client_replica
        native_output = native_client_replica.advance(self, now)
        if native_output is not None:
            return native_output
''')


def battle(source):
    source = once(source,
        '                clock=self._clock, pose_safe=self._baked_pose_safe)\n',
        '                clock=self._clock, pose_safe=self._baked_pose_safe)\n'
        '            from . import native_client_replica\n'
        '            native_client_replica.bind(self._sync, self)\n')
    source = prepend(source, '_apply_record_pose', '''        from . import native_client_replica
        if native_client_replica.owns(self, record):
            return False
''')
    return prepend(source, '_cleanup', '''        from . import native_client_replica
        native_client_replica.retire(self)
''')
