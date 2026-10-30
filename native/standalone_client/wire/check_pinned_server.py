"""Round-trip every message the pinned server writes in its own tests through the struct codec."""
import collections
import os
import sys
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from codec import WireError, same  # noqa: E402
from schema import SCHEMA  # noqa: E402

PIN = os.path.abspath(sys.argv[1])
PATTERNS = sys.argv[2:] or ['test_port_0922_server*.py', 'test_port_0922_snapshot*.py',
                            'test_port_0922_radio_server.py', 'test_port_0922_turret_obstacle_wire.py']
sys.path.insert(0, os.path.join(PIN, 'tests'))
sys.path.insert(0, os.path.join(PIN, 'server'))
os.chdir(os.path.join(PIN, 'tests'))
import lan_battle_server  # noqa: E402

errors = collections.Counter()
examples = {}
counts = collections.Counter()


def check(message):
    if not isinstance(message, dict):
        return
    kind = message.get('type')
    counts[kind] += 1
    try:
        decoded = SCHEMA.decode(SCHEMA.encode(message))
        if not same(decoded, message):
            raise WireError('round trip differs')
        if os.environ.get('WIRE_FIXTURES') and counts[kind] <= 6:
            import json
            with open(os.environ['WIRE_FIXTURES'], 'a') as target:
                target.write(json.dumps(message) + '\n')
    except WireError as error:
        key = '%s: %s' % (kind, str(error)[:200])
        errors[key] += 1
        examples.setdefault(key, message)


def hook(owner, name, position=1):
    original = getattr(owner, name)

    def wrapped(*args, **kwargs):
        if len(args) > position:
            check(args[position])
        return original(*args, **kwargs)
    setattr(owner, name, wrapped)


for name in ('offer_reliable', 'offer_snapshot', 'offer_snapshot_repair', 'send'):
    hook(lan_battle_server._EndpointSendMixin, name)
hook(lan_battle_server.BattleState, 'broadcast') if hasattr(lan_battle_server.BattleState, 'broadcast') else None
loader = unittest.TestLoader()
suite = unittest.TestSuite()
for pattern in PATTERNS:
    suite.addTests(loader.discover('.', pattern=pattern))
result = unittest.TextTestRunner(stream=open(os.devnull, 'w'), verbosity=0).run(suite)
print('tests run=%d failures=%d errors=%d' % (result.testsRun, len(result.failures), len(result.errors)))
print('messages:', dict(counts))
for key, n in errors.most_common(60):
    print('%5d %s' % (n, key))


def diff(a, b, path=''):
    if type(a) is not type(b) and not (isinstance(a, dict) and isinstance(b, dict)):
        return ['%s: %r(%s) vs %r(%s)' % (path, a, type(a).__name__, b, type(b).__name__)]
    if isinstance(a, dict):
        out = []
        for k in set(a) | set(b):
            if k not in a or k not in b:
                out.append('%s.%s presence %s/%s' % (path, k, k in a, k in b))
            else:
                out += diff(a[k], b[k], path + '.' + k)
        return out
    if isinstance(a, list):
        if len(a) != len(b):
            return ['%s len %d/%d' % (path, len(a), len(b))]
        return [d for i, (x, y) in enumerate(zip(a, b)) for d in diff(x, y, '%s[%d]' % (path, i))]
    return [] if same(a, b) else ['%s: %r vs %r' % (path, a, b)]


for key, message in examples.items():
    if 'differs' in key:
        print(key, diff(message, SCHEMA.decode(SCHEMA.encode(message)))[:5])
        break
