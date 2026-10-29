"""Install the sole native local repair/CAS owner at pinned player seams."""
import re


def prepend(source, name, body):
    pattern = r'(^    def ' + re.escape(name) + r'\(\s*self[^:]*\):\n)'
    matches = list(re.finditer(pattern, source, re.M))
    if len(matches) != 1:
        raise ValueError('The pinned native critical seam changed: ' + name)
    index = matches[0].end()
    return source[:index] + body + source[index:]


def battle(source):
    source = prepend(source, '_tick_critical_states', '''        from . import native_client_critical
        if native_client_critical.frame(self, dt):
            return
''')
    source = prepend(source, '_reconcile_critical_authority', '''        from . import native_client_critical
        native = native_client_critical.reconcile(
            self, record, source, canonical)
        if native is not None:
            return native
''')
    return prepend(source, '_cleanup', '''        from . import native_client_critical
        native_client_critical.retire(self)
''')
