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
    # These early returns remove the Python header/upsert/history pipeline for
    # visible clients. The hidden worker retains its independent native adapter.
    for name, manifest in (('manifest', True), ('snapshot', False)):
        source = prepend(source, name, """        from . import native_client_replica
        native_output = native_client_replica.receive(self, message, manifest=%s)
        if native_output is not None:
            return native_output
""" % manifest)
    source = prepend(source, 'take_motion_probes', '''        from . import native_client_replica
        native_output = native_client_replica.probes(self, now)
        if native_output is not None:
            return native_output
''')
    source = prepend(source, 'advance', """        from . import native_client_replica
        native_output = native_client_replica.advance(self, now)
        if native_output is not None:
            return native_output
""")
    # The Bot authority presents its own newer local poses and strips this echo's pose once a
    # Bot exists, so its timed interpolation is never read. Keep the newest pose for a Bot
    # still pending creation; advance() then has no target to interpolate.
    return once(source,
        "        if sample_time_us is not None:\n"
        "            previous_motion_time_us = record.get('motion_anchor_time_us')\n",
        "        authority = getattr(self, '_authority_bots', None)\n"
        "        if kind == 'bot' and authority is not None and authority():\n"
        "            self._emit({'type': 'update', 'entity': key, 'kind': kind,\n"
        "                        'id': state['id'], 'state': _copy_state(state),\n"
        "                        'pose': pose, 'target': None, 'remote': True,\n"
        "                        'snap': False}, output)\n"
        "            return\n"
        "        if sample_time_us is not None:\n"
        "            previous_motion_time_us = record.get('motion_anchor_time_us')\n")


def battle(source):
    source = prepend(source, '_flush_pending_entities', '''        from . import native_client_replica
        native = native_client_replica.flush_pending(self, now)
        if native is not None:
            return native
''')
    source = once(source,
        '                clock=self._clock, pose_safe=self._baked_pose_safe)\n',
        '                clock=self._clock, pose_safe=self._baked_pose_safe)\n'
        '            from . import native_client_replica\n'
        '            native_client_replica.bind(self._sync, self)\n'
        '            self._sync._authority_bots = (\n'
        '                lambda: self._bots is not None and self._bots.is_authority())\n')
    source = prepend(source, '_apply_record_pose', '''        from . import native_client_replica
        if native_client_replica.owns(self, record):
            return False
''')
    return prepend(source, '_cleanup', '''        from . import native_client_replica
        native_client_replica.retire(self)
''')
