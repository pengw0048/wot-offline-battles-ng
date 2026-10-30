"""The native lifecycle ports match the pinned Python methods on identical fakes."""
import ast
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
RUNTIME = 'src/res/scripts/client/gui/mods/offline_lan_0922/battle_runtime.py'
METHODS = ('_pending_combat_for_record', '_apply_vehicle_statistics', '_death_attacker_engine_id',
           '_should_suppress_postmortem_killer', '_sync_fire_effect', '_stop_remote_visual',
           '_event_entity_key', '_remote_visual_components', '_store_remote_visual_components')
GLOBALS = ('_number', '_COMBAT_EVENT_KINDS')

FIXTURE = r'''
import copy, math, sys
sys.path.insert(0, sys.argv[1])
import offline_client_lifecycle_native as native
exec(open(sys.argv[2]).read())

class Log(object):
    """Fake engine object: records every call and answers from a script."""
    def __init__(self, log, name, answers=None, **attrs):
        self._log, self._name, self._answers = log, name, answers or {}
        self.__dict__.update(attrs)
    def __repr__(self):
        return '<%s>' % self._name
    def __getattr__(self, attr):
        if attr.startswith('__'):
            raise AttributeError(attr)
        def call(*args):
            self._log.append((self._name, attr) + tuple(repr(a) for a in args))
            answer = self._answers.get(attr)
            return answer(*args) if callable(answer) else answer
        return call

def battle(log, **extra):
    b = Pinned()
    b._worker_mode = extra.get('worker', False)
    b._binding = Log(log, 'binding')
    b._records = extra.get('records', {})
    b._event_journal = extra.get('journal', [])
    b._outlined_engine_id = extra.get('outlined')
    vehicle = Log(log, 'vehicle')
    b._remote_factory = Log(log, 'factory', {'get': lambda engine: vehicle})
    b._clear_target_outline = lambda: log.append(('battle', 'clear_outline'))
    return b

def run(name, make, args):
    """Run pinned and native on deep-copied inputs; compare result, mutations and calls."""
    out = []
    for impl in ('pinned', 'native'):
        log = []
        b, values = make(log)
        if impl == 'pinned':
            fn = getattr(b, name)
            try:
                result = ('ok', fn(*values))
            except Exception as error:
                result = ('error', type(error).__name__)
        else:
            try:
                result = ('ok', NATIVE[name](b, *values))
            except Exception as error:
                result = ('error', 'native')
        out.append((result, log, copy.deepcopy([v for v in values if isinstance(v, dict)]),
                    copy.deepcopy(b._records), getattr(b, '_marker', None)))
    (pr, pl, pv, prec, pm), (nr, nl, nv, nrec, nm) = out
    if pr[0] == 'error':
        assert nr[0] == 'error', (name, args, pr, nr)
    else:
        assert (pr, pl, pv, prec) == (nr, nl, nv, nrec), (name, args, out)

KINDS = _COMBAT_EVENT_KINDS
NATIVE = {
    '_pending_combat_for_record': lambda b, r: native.pending_combat(b, r, KINDS),
    '_apply_vehicle_statistics': native.vehicle_statistics,
    '_death_attacker_engine_id': native.death_attacker_engine_id,
    '_should_suppress_postmortem_killer': native.suppress_postmortem_killer,
    '_sync_fire_effect': lambda b, e, burning=None: native.sync_fire_effect(b, e, burning),
    '_stop_remote_visual': native.stop_remote_visual,
}
cases = 0
frag_values = [0, 3, 3.7, '5', 'x', None, True]
for worker in (False, True):
    for frags in frag_values:
        for presented in (None, 0, 3, 5):
            for killer in (False, True, None):
                for presented_killer in (None, True):
                    def make(log, worker=worker, frags=frags, presented=presented, killer=killer, pk=presented_killer):
                        record = {'engine_id': 41}
                        if presented is not None: record['presented_frags'] = presented
                        if pk is not None: record['presented_team_killer'] = pk
                        state = {} if frags is None else {'frags': frags}
                        if killer is not None: state['team_killer'] = killer
                        return battle(log, worker=worker), (record, state)
                    run('_apply_vehicle_statistics', make, (worker, frags, presented, killer, presented_killer)); cases += 1
records = {'bot:7': {'engine_id': 70, 'state': {'team': 2}, 'spot_visible': False},
           'player:3': {'engine_id': 30, 'state': {'team': 1}},
           'bot:8': {'engine_id': 80, 'state': {'team': 1}, 'spot_visible': True}}
for kind in ('bot', 'player', None, u'bot'):
    for ident in (7, 3, '7', 'x', None, 99):
        def make(log, kind=kind, ident=ident):
            state = {'death_attacker_kind': kind}
            if ident is not None: state['death_attacker_id'] = ident
            return battle(log, records=copy.deepcopy(records)), (state,)
        run('_death_attacker_engine_id', make, (kind, ident)); cases += 1
for local in (True, False):
    for health in (0, 5, None, 'x'):
        for alive in (True, False, None):
            for attacker in (70, 30, 80, 0, None, 'q', 10**30):
                for team in (1, 2, None, 'z'):
                    def make(log, local=local, health=health, alive=alive, attacker=attacker, team=team):
                        victim = {'local': local}
                        state = {}
                        if health is not None: state['health'] = health
                        if alive is not None: state['alive'] = alive
                        if team is not None: state['team'] = team
                        return battle(log, records=copy.deepcopy(records)), (victim, state, attacker)
                    run('_should_suppress_postmortem_killer', make, (local, health, alive, attacker, team)); cases += 1
journal_rows = [{'kind': 'hit', 'target_bot': 7}, {'kind': 'hit', 'target': 3, 'cosmetic_only': True},
                {'kind': 'shot', 'target_bot': 7}, {'kind': 'health', 'target': 3}, {'kind': 'bot_hit'}]
for subset in range(1 << len(journal_rows)):
    for target in ('bot:7', 'player:3', 'bot:8'):
        def make(log, subset=subset, target=target):
            recs = copy.deepcopy(records)
            journal = [dict(row) for i, row in enumerate(journal_rows) if subset >> i & 1]
            return battle(log, records=recs, journal=journal), (recs[target],)
        run('_pending_combat_for_record', make, (subset, target)); cases += 1
for has_fire in (True, False):
    for running in (True, False):
        for burning in (None, True, False, 0, 1):
            for on_fire in (True, False, None):
                for compound in (True, False):
                    def make(log, has_fire=has_fire, running=running, burning=burning, on_fire=on_fire, compound=compound):
                        extra = Log(log, 'fire', {'isRunningFor': lambda e: running})
                        extras = {'fire': extra} if has_fire else {}
                        attrs = {'typeDescriptor': Log(log, 'descriptor', extrasDict=extras),
                                 'appearance': Log(log, 'appearance', compoundModel=object() if compound else None)}
                        if on_fire is not None: attrs['is_on_fire'] = on_fire
                        entity = Log(log, 'entity', **attrs)
                        args = (entity,) if burning is None else (entity, burning)
                        return battle(log), args
                    run('_sync_fire_effect', make, (has_fire, running, burning, on_fire, compound)); cases += 1
for outlined in (41, 5, None):
    for components in ({}, {'visual_started': True}, {'world_marker_started': True},
                       {'minimap_started': True}, {'world_marker_started': True, 'minimap_started': True},
                       {'world_marker_started': False, 'visual_started': True}):
        for native_remote in (True, False):
            def make(log, outlined=outlined, components=components, native_remote=native_remote):
                record = dict(components, engine_id=41, native_remote=native_remote)
                return battle(log, outlined=outlined), (record,)
            run('_stop_remote_visual', make, (outlined, components, native_remote)); cases += 1
print('lifecycle differential ok %d' % cases)
'''


def pinned_source(fork):
    source = (Path(fork) / RUNTIME).read_text()
    tree = ast.parse(source)
    lines = source.splitlines()
    chunks = ['import math', 'class Pinned(object):']
    for node in tree.body:
        if isinstance(node, ast.Assign) and any(getattr(t, 'id', None) in GLOBALS for t in node.targets):
            chunks.insert(1, '\n'.join(lines[node.lineno - 1:node.end_lineno]))
        if isinstance(node, ast.FunctionDef) and node.name in GLOBALS:
            chunks.insert(1, '\n'.join(lines[node.lineno - 1:node.end_lineno]))
        if isinstance(node, ast.ClassDef) and node.name == 'BattleRuntime':
            for item in node.body:
                if isinstance(item, ast.FunctionDef) and item.name in METHODS:
                    start = min([d.lineno for d in item.decorator_list] + [item.lineno])
                    chunks.append('\n'.join(lines[start - 1:item.end_lineno]))
    return '\n\n'.join(chunks) + '\n'


class LifecycleDifferential(unittest.TestCase):
    def test_native_ports_match_pinned_methods(self):
        fork = os.environ.get('WOT_FORK_0101_SOURCE')
        if not fork:
            self.skipTest('Pinned gameplay source is required')
        py27 = subprocess.check_output(['pyenv', 'which', 'python2.7'], text=True,
                                       env=dict(os.environ, PYENV_VERSION='2.7.18')).strip()
        prefix = subprocess.check_output([py27, '-c', 'import sys;print(sys.prefix)'], text=True).strip()
        with tempfile.TemporaryDirectory(prefix='lifecycle-') as folder:
            folder = Path(folder)
            subprocess.run(['c++', '-std=c++17', '-O1', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter',
                            '-Wno-register', '-bundle', '-undefined', 'dynamic_lookup',
                            '-isystem', str(Path(prefix) / 'include/python2.7'), '-I' + str(ROOT / 'bigworld'),
                            str(ROOT / 'tests/stock_native_client_lifecycle_host.cpp'),
                            '-o', str(folder / 'offline_client_lifecycle_native.so')], check=True)
            (folder / 'pinned.py').write_text(pinned_source(fork))
            (folder / 'fixture.py').write_text(FIXTURE)
            run = subprocess.run([py27, '-B', str(folder / 'fixture.py'), str(folder), str(folder / 'pinned.py')],
                                 capture_output=True, text=True, timeout=120)
            self.assertEqual(run.returncode, 0, run.stdout[-3000:] + run.stderr[-6000:])
            self.assertIn('lifecycle differential ok', run.stdout)


if __name__ == '__main__':
    unittest.main()
