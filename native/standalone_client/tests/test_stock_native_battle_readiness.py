"""A refused local ready send preserves terminal outcomes and reports local state."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
FORK = os.environ.get('WOT_FORK_0101_SOURCE')

FIXTURE = r'''
import sys, types, StringIO
import battle_readiness_test as native
root = sys.argv[1] + '/src/res/scripts/client'
sys.path.insert(0, root)
for name in ('gui', 'gui.mods', 'gui.mods.offline_lan_0922'):
    module = types.ModuleType(name)
    module.__path__ = [root + '/' + name.replace('.', '/')]
    sys.modules[name] = module
from gui.mods.offline_lan_0922.lan_client import LANClient
class Client(LANClient):
    def __init__(self, phase='loading', connected=True, ready=True, round_id=7, accepts=True):
        LANClient.__init__(self, 'localhost', 28792, 'P', 'ussr:R11_MS-1')
        self.phase, self.connected, self.ready, self.round_id = phase, connected, ready, round_id
        self.combat_phase = 'loading'
        self.last_error = ''
        self.accepts = accepts
        self.sent, self.calls = [], 0
    def send_battle_ready(self, bases=None):
        self.calls += 1
        return LANClient.send_battle_ready(self, bases)
    def _send(self, message):
        self.sent.append(message)
        return self.accepts
def publish(*args):
    original, output = sys.stdout, StringIO.StringIO()
    sys.stdout = output
    try:
        state = native.run(*args)
    finally:
        sys.stdout = original
    state['log'] = output.getvalue()
    return state
for worker in (0, 1):
    for radius in (0, 1):
        client = Client()
        state = publish(client, worker, radius, 0, 1, 1)
        assert state['sent'] and not state['error'], state
        message = state['outgoing'] if worker else client.sent[0]
        expected = {'type': 'battle_ready', 'round_id': 7, 'bases': {
            '1': [{'x': 10., 'z': 20., 'radius': 35.}] if radius else ((10., 20.),),
            '2': [{'x': 30., 'z': 40., 'radius': 45.}] if radius else ((30., 40.),)}}
        # The native worker already owns plain arrays; the retained client
        # freezes these stock tuples into the same wire arrays before encoding.
        import json
        assert json.loads(json.dumps(message)) == json.loads(json.dumps(expected)), message
        assert (state['attempts'] if worker else client.calls) == 1
    client = Client(phase='battle', accepts=False)
    client.combat_phase = 'finished'
    state = publish(client, worker, 1, 1, 1, 0)
    assert not state['sent'] and not state['error'], state
    assert state['result']['reason'] == 'worker_disconnected'
    assert state['attempts'] == client.calls == 0
    client = Client(accepts=False)
    state = publish(client, worker, 0, 0, 0, 0)
    assert not state['sent'] and not state['error'], state
    assert state['attempts'] == client.calls == 0
    for attributes, reason in (({'connected': False}, 'connection closed'),
                               ({'phase': 'battle'}, 'no longer loading'),
                               ({'ready': False}, 'connection is not ready'),
                               ({}, 'Could not send')):
        client = Client(accepts=False, **attributes)
        client.last_error = 'upstream transport detail'
        state = publish(client, worker, 1, 0, 1, 0)
        assert not state['sent'] and reason in state['error'], state
        for detail in ('role=' + ('worker' if worker else 'player'), 'round=7', 'source_round=7',
                       'client_phase=', 'transport=', 'ready='):
            assert detail in state['log'] and detail not in state['error'], state
        assert 'server did not accept' not in state['error'], state
        if not worker:
            assert 'upstream transport detail' in state['log']
            assert 'upstream transport detail' not in state['error']
client = Client(round_id=8, accepts=False)
state = publish(client, 0, 0, 0, 1, 0)
assert not state['sent'] and 'battle changed' in state['error'] and 'source_round=8' in state['log'], state
assert 'source_round=' not in state['error'], state
def raised(bases):
    raise RuntimeError('original native startup failure')
client = Client(); client.send_battle_ready = raised
state = publish(client, 0, 0, 0, 1, 0)
assert 'original native startup failure' in state['error'], state
print('Readiness terminal containment, local diagnostics and both real producer shapes passed')
'''


class NativeBattleReadiness(unittest.TestCase):
    def test_readiness_preserves_terminal_result_and_local_failure_evidence(self):
        if not FORK:
            self.skipTest('Pinned gameplay native headers and retained client are required')
        py27 = subprocess.check_output(['pyenv', 'which', 'python2.7'], text=True,
                                      env=dict(os.environ, PYENV_VERSION='2.7.18')).strip()
        prefix = subprocess.check_output([py27, '-c', 'import sys;print(sys.prefix)'], text=True).strip()
        source = (ROOT / 'bigworld/stock_native_battle_startup.cpp').read_text()
        # The publisher is the last function in the namespace; keep its body
        # unchanged while replacing the engine and socket boundaries.
        begin = source.index('void maybeSendBattleReady(Battle& b)')
        end, depth = source.index('{', begin), 1
        while depth:
            end += 1
            depth += (source[end] == '{') - (source[end] == '}')
        with tempfile.TemporaryDirectory(prefix='battle-readiness-') as directory:
            folder = Path(directory)
            (folder / 'battle_readiness.inc').write_text(source[begin:end + 1])
            command = ['c++', '-std=c++17', '-O1', '-DWOT_NATIVE_BATTLE_HOST_BUILD',
                       '-DWOT_CLIENT_REPLICA_HOST_BUILD', '-Wno-register', '-bundle', '-undefined',
                       'dynamic_lookup', '-isystem', str(Path(prefix) / 'include/python2.7'),
                       '-I' + str(folder), '-I' + str(ROOT / 'bigworld'), '-I' + str(ROOT.parent),
                       '-I' + str(Path(FORK) / 'native'),
                       str(ROOT / 'tests/stock_native_battle_readiness_host.cpp'),
                       str(ROOT / 'bigworld/stock_native_battle_link_send.cpp'),
                       str(ROOT / 'bigworld/stock_native_client_state.cpp'),
                       '-o', str(folder / 'battle_readiness_test.so')]
            compiled = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(0, compiled.returncode, compiled.stdout + compiled.stderr)
            script = folder / 'fixture.py'
            script.write_text(FIXTURE)
            run = subprocess.run([py27, '-B', str(script), FORK], cwd=folder,
                                 capture_output=True, text=True)
            self.assertEqual(0, run.returncode, run.stdout + run.stderr)
            self.assertIn('Readiness terminal containment', run.stdout)


if __name__ == '__main__':
    unittest.main()
