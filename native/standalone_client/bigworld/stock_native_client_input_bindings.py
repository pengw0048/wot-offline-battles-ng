"""Install native input decisions at the exact player mailbox boundary."""
import re


def _once(source, before, after):
    if source.count(before) != 1:
        raise ValueError('The pinned native input seam changed: ' + before[:80])
    return source.replace(before, after, 1)


def _prepend(source, name, body):
    pattern = r'(^    def ' + re.escape(name) + r'\([^\n]*\):\n)'
    matches = list(re.finditer(pattern, source, re.M))
    if len(matches) != 1:
        raise ValueError('The pinned input mailbox changed: ' + name)
    index = matches[0].end()
    return source[:index] + body + source[index:]


def battle(source):
    source = _once(source, 'class _LANInputSender(object):\n', '''class _LANInputSender(object):

    def __setattr__(self, name, value):
        if name != 'owner':
            import offline_client_transport_native
            offline_client_transport_native.assign_control(
                getattr(self, 'owner', None), name, value)
        object.__setattr__(self, name, value)
''')
    source = _prepend(source, 'send_avatar_input', '''        import offline_client_transport_native
        result = offline_client_transport_native.control(
            self.owner, kind, payload)
        if result is not None:
            return result
''')
    source = _prepend(source, 'align_aim', '''        import offline_client_transport_native
        result = offline_client_transport_native.align_control(
            self.owner, turret_yaw, gun_pitch)
        if result is not None:
            return result
''')
    return _prepend(source, 'send_current', '''        if (self.owner.state == 'running' and
                not self.owner._worker_mode):
            import offline_client_transport_native
            return offline_client_transport_native.send_current(
                self.owner, siege_enabled)
''')
