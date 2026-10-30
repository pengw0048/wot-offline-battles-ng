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
           '_event_entity_key', '_remote_visual_components', '_store_remote_visual_components',
           '_apply_critical_state', '_apply_stun_state', '_apply_siege_state', '_present_critical')
GLOBALS = ('_number', '_COMBAT_EVENT_KINDS')

FIXTURE = r'''
import copy, math, sys
sys.path.insert(0, sys.argv[1])
import offline_client_lifecycle_native as native
exec(open(sys.argv[2]).read())

class Log(object):
    """Fake engine object: records every call and answers from a script."""
    def __init__(self, log, name, answers=None, missing=(), **attrs):
        self._log, self._name, self._answers, self._missing = log, name, answers or {}, missing
        self.__dict__.update(attrs)
    def __repr__(self):
        return '<%s>' % self._name
    def __getattr__(self, attr):
        if attr.startswith('__') or attr in self._missing:
            raise AttributeError(attr)
        def call(*args, **kwargs):
            self._log.append((self._name, attr) + tuple(repr(a) for a in args) +
                             tuple(sorted((k, repr(v)) for k, v in kwargs.items())))
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
                result = ('error', type(error).__name__, str(error))
        else:
            try:
                result = ('ok', NATIVE[name](b, *values))
            except Exception as error:
                result = ('error', type(error).__name__, str(error))
        battle_after = tuple(repr(getattr(b, name, '-')) for name in (
            '_local_siege_pending', '_targeting_signature', '_local_physics', '_local_suspension_params',
            '_local_suspension_probe_trace', '_local_suspension_disabled', '_local_suspension_pitch_velocity'))
        log.append(('after',) + battle_after)
        probe = getattr(b, '_entity_probe', None)
        entity_after = None if probe is None else (getattr(probe, 'stunInfo', None),
                                                   copy.deepcopy(probe.__dict__.get('_offlineStunFactors')))
        out.append((result, log, copy.deepcopy([v for v in values if isinstance(v, dict)]),
                    copy.deepcopy(b._records), entity_after))
    (pr, pl, pv, prec, pm), (nr, nl, nv, nrec, nm) = out
    TALLY.setdefault(name, [0, 0, 0])[0 if pr[0] == 'error' else 1 if not pl else 2] += 1
    if pr[0] == 'error':
        # Pinned validation errors keep their type and message; other failures only need to fail.
        assert nr[0] == 'error' and (pr[1] != 'RuntimeError' or pr == nr), (name, args, pr, nr)
    else:
        assert (pr, pl, pv, prec, pm) == (nr, nl, nv, nrec, nm), (name, args, out)

KINDS = _COMBAT_EVENT_KINDS
TALLY = {}
NATIVE = {
    '_pending_combat_for_record': lambda b, r: native.pending_combat(b, r, KINDS),
    '_apply_vehicle_statistics': native.vehicle_statistics,
    '_death_attacker_engine_id': native.death_attacker_engine_id,
    '_should_suppress_postmortem_killer': native.suppress_postmortem_killer,
    '_sync_fire_effect': lambda b, e, burning=None: native.sync_fire_effect(b, e, burning),
    '_stop_remote_visual': native.stop_remote_visual,
    '_apply_critical_state': lambda b, r, payload, authority=None: native.critical_state(b, r, payload, authority, critical_damage),
    '_apply_stun_state': native.stun_state,
    '_apply_siege_state': lambda b, r, st: native.siege_state(b, r, st, vehicle_physics),
    '_present_critical': native.present_critical,
}
vehicle_physics = None
critical_damage = None

def stub(b, log, name, answer=None):
    def call(*args, **kwargs):
        log.append(('battle', name) + tuple(repr(a) for a in args) + tuple(sorted((k, repr(v)) for k, v in kwargs.items())))
        return answer(*args) if callable(answer) else answer
    setattr(b, name, call)
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
for canonical in (None, {'fire': True}):
    for authority in (None, {'critical_revision': 2}):
        for reconciled in (True, False):
            for local in (True, False):
                for existing in (None, {'fire': True}, {'fire': False}):
                    for entity_ok in (True, False):
                        for gun in (None, 'gun'):
                            for worker in (False, True):
                                def make(log, canonical=canonical, authority=authority, reconciled=reconciled, local=local,
                                         existing=existing, entity_ok=entity_ok, gun=gun, worker=worker):
                                    global critical_damage
                                    b = battle(log, worker=worker)
                                    critical_damage = Log(log, 'critical_damage', {'apply_payload': lambda e, c: ['event']})
                                    globals()['critical_damage'] = critical_damage
                                    stub(b, log, '_critical_state', lambda payload: None if canonical is None else dict(canonical))
                                    stub(b, log, '_reconcile_critical_authority', reconciled)
                                    stub(b, log, '_server_entity', Log(log, 'entity') if entity_ok else None)
                                    stub(b, log, '_advance_local_gun_edge', (0, 'edge'))
                                    stub(b, log, '_advance_local_gun_to')
                                    stub(b, log, '_present_critical')
                                    b._gun_state = gun
                                    record = {'engine_id': 41, 'local': local, 'state': {'health': 5}}
                                    if existing is not None: record['critical_state'] = existing
                                    args = (record, {'raw': 1}) if authority is None else (record, {'raw': 1}, authority)
                                    return b, args
                                run('_apply_critical_state', make, (canonical, authority, reconciled, local, existing, entity_ok, gun, worker)); cases += 1
for end in (0, 5000, -1, True, 2.5):
    for kind in ('', 'bot', u'player', 'x'):
        for attacker in (0, 7, True):
            for factors in (None, {'reload': 1.2, 'dispersion': 1.5}):
                for presented in ('none', 'same', 'other'):
                    for estimated in (None, 1000.0):
                        for local in (True, False):
                            for variant in range(4):
                                def make(log, end=end, kind=kind, attacker=attacker, factors=factors, presented=presented,
                                         estimated=estimated, local=local, variant=variant):
                                    worker, entity_ok, gun, native_cb = variant == 3, variant != 2, variant == 1, variant == 0
                                    b = battle(log, worker=worker)
                                    stub(b, log, '_clock', 50.0)
                                    stub(b, log, '_projectile_estimated_server_time', estimated)
                                    stub(b, log, '_server_clock', 200.0)
                                    entity = Log(log, 'entity', missing=() if native_cb else ('set_stunInfo',), stunInfo=1.5)
                                    stub(b, log, '_server_entity', entity if entity_ok else None)
                                    stub(b, log, '_advance_local_gun_edge'); stub(b, log, '_apply_current_reload_factor', True)
                                    stub(b, log, '_publish_reload_event'); stub(b, log, '_publish_targeting_info')
                                    b._gun_state = Log(log, 'gun', reload_time=1.0, reload_duration=4.0) if gun else None
                                    view = Log(log, 'view', STUN=12)
                                    b._runtime = Log(log, 'runtime', vehicle_view_state=view)
                                    feedback = Log(log, 'feedback')
                                    provider = Log(log, 'provider', shared=Log(log, 'shared', feedback=feedback))
                                    b._avatar = Log(log, 'avatar', guiSessionProvider=provider)
                                    state = {'stun_end_server_time_ms': end, 'stun_attacker_kind': kind, 'stun_attacker_id': attacker}
                                    if factors is not None: state['stun_factors'] = dict(factors)
                                    record = {'engine_id': 41, 'local': local}
                                    signature = (int(end), str(kind), int(attacker), tuple(sorted((factors or {}).items())))
                                    if presented == 'same': record['presented_stun_state'] = signature
                                    if presented == 'other': record['presented_stun_state'] = (1, 'bot', 2, ())
                                    return b, (record, state)
                                def make_checked(log, make=make):
                                    b, args = make(log)
                                    entity = b._server_entity(41)
                                    del log[:]
                                    original = b._server_entity
                                    b._server_entity = lambda engine: (log.append(('battle', '_server_entity', repr(engine))), entity)[1]
                                    b._entity_probe = entity
                                    return b, args
                                run('_apply_stun_state', make_checked, (end, kind, attacker, factors, presented, estimated, local, variant)); cases += 1
EVENT_SETS = [
    [], [{'kind': 'ammo_rack', 'state': 'destroyed'}],
    [{'kind': 'device', 'name': 'engine', 'state': 'destroyed', 'cause': 'fire'},
     {'kind': 'device', 'name': 'gun', 'state': 'normal', 'old_state': 'critical'},
     {'kind': 'device', 'name': 'radio', 'state': 'critical', 'old_state': 'destroyed'},
     {'kind': 'device', 'name': 'track', 'state': 'critical', 'cause': 'repair'}],
    [{'kind': 'crew', 'name': 'driver', 'state': 'normal'}, {'kind': 'crew', 'name': 'gunner', 'state': 'destroyed', 'cause': 'drowning'},
     {'kind': 'crew', 'name': 'loader', 'state': 'destroyed', 'cause': 'fire'}, {'kind': 'crew', 'name': 'commander', 'state': 'destroyed'}],
    [{'kind': 'fire', 'state': True, 'cause': 'ramming'}, {'kind': 'fire', 'state': True}, {'kind': 'fire', 'state': False},
     {'kind': 'other'}, {'kind': 'ammo_rack', 'state': 'damaged'}],
    [{'kind': 'device', 'name': 'bad', 'state': 'critical'}],
    [{'kind': 'device', 'name': 'engine', 'state': 'critical', 'cause': 'world_collision'}],
]
CODES = ['DEVICE_REPAIRED', 'DEVICE_REPAIRED_TO_CRITICAL', 'DEVICE_DESTROYED_AT_FIRE', 'DEVICE_CRITICAL_AT_SHOT',
         'TANKMAN_RESTORED', 'TANKMAN_HIT_AT_DROWNING', 'TANKMAN_HIT', 'TANKMAN_HIT_AT_SHOT',
         'DEVICE_STARTED_FIRE_AT_RAMMING', 'DEVICE_STARTED_FIRE_AT_SHOT', 'FIRE_STOPPED', 'DEVICE_CRITICAL_AT_WORLD_COLLISION']
for events_index in range(len(EVENT_SETS)):
    for local in (True, False):
        for spot in (True, False, None):
            for attacker in (0, 9, None):
                for variant in range(6):
                    def make(log, events_index=events_index, local=local, spot=spot, attacker=attacker, variant=variant):
                        b = battle(log, worker=variant == 5)
                        indices = dict((code, i + 1) for i, code in enumerate(CODES)) if variant != 3 else {'FIRE_STOPPED': 2}
                        modes = Log(log, 'modes', HE_DETONATION=4) if variant != 4 else Log(log, 'modes', missing=('HE_DETONATION',))
                        constants = Log(log, 'constants', AMMOBAY_DESTRUCTION_MODE=modes, DAMAGE_INFO_INDICES=indices)
                        b._runtime = Log(log, 'runtime', constants=constants)
                        b._avatar = None if variant == 2 else Log(log, 'avatar')
                        entity = Log(log, 'entity', typeDescriptor=Log(log, 'td'))
                        stub(b, log, '_server_entity', entity if variant != 1 else None)
                        stub(b, log, '_sync_fire_effect'); stub(b, log, '_damage_info_is_serviceable', True)
                        stub(b, log, '_critical_extra_index', lambda td, name: -1 if name == 'bad' else len(name or ''))
                        stub(b, log, '_show_damage_info', lambda *a: a[1] % 2 == 0); stub(b, log, '_report_critical_presentation')
                        record = {'engine_id': 41, 'local': local}
                        if spot is not None: record['spot_visible'] = spot
                        return b, (record, [dict(e) for e in EVENT_SETS[events_index]], attacker)
                    run('_present_critical', make, (events_index, local, spot, attacker, variant)); cases += 1
SIEGE = Log([], 'siege', DISABLED=0, SWITCHING_ON=1, ENABLED=2, SWITCHING_OFF=3)
for siege in (0, 1, 2, 3, 7, True, None):
    for left in (0, 1500, -5, 2.0):
        for presented in (None, 0, 2):
            for pending in (None, (True, None), (True, 9), (False, 30)):
                for seq in (None, 10, 40):
                    for local in (True, False):
                        for variant in range(5):
                            def make(log, siege=siege, left=left, presented=presented, pending=pending, seq=seq, local=local, variant=variant):
                                global vehicle_physics
                                b = battle(log, worker=variant == 4)
                                vehicle_physics = Log(log, 'vehicle_physics', {'derive_params': lambda d, f: 'physics'})
                                b._runtime = Log(log, 'runtime', constants=Log(log, 'constants', VEHICLE_SIEGE_STATE=SIEGE))
                                b._local_siege_pending = pending
                                has_siege = variant != 1
                                descriptor = Log(log, 'descriptor', hasSiegeMode=has_siege) if variant != 2 else None
                                entity = Log(log, 'entity', typeDescriptor=descriptor) if descriptor is not None else Log(log, 'entity', missing=('typeDescriptor',))
                                stub(b, log, '_server_entity', entity)
                                stub(b, log, '_report_local_siege_edge'); stub(b, log, '_advance_local_gun_edge', (0, 'edge'))
                                stub(b, log, '_advance_local_gun_to'); stub(b, log, '_select_local_siege_pose')
                                stub(b, log, '_local_factors', 'factors'); stub(b, log, '_update_local_hull_aiming')
                                b._gun_state = Log(log, 'gun', {'adopt_descriptor': lambda d: True}) if variant in (0, 3) else None
                                b._sender = Log(log, 'sender') if variant == 0 else None
                                b._local_matrix = 'matrix' if variant in (0, 3) else None
                                b._local_physics = 'old' if variant in (0, 3) else None
                                b._remote_factory = Log(log, 'factory')
                                record = {'engine_id': 41, 'local': local, 'native_remote': variant == 4}
                                if presented is not None: record['presented_siege_state'] = presented
                                state = {}
                                if siege is not None: state['siege_state'] = siege
                                state['siege_time_left_ms'] = left
                                if seq is not None: state['input_seq'] = seq
                                b._entity_probe = None
                                return b, (record, state)
                            def checked(log, make=make):
                                b, args = make(log)
                                b._battle_probe = b
                                return b, args
                            run('_apply_siege_state', checked, (siege, left, presented, pending, seq, local, variant)); cases += 1
print('lifecycle differential ok %d' % cases)
for name, (errors, quiet, effects) in sorted(TALLY.items()):
    print('%s errors=%d no_calls=%d with_calls=%d' % (name, errors, quiet, effects))
    assert quiet + effects, name
'''


def pinned_source(fork):
    source = (Path(fork) / RUNTIME).read_text()
    tree = ast.parse(source)
    lines = source.splitlines()
    chunks = ['import math', '_INTEGER_TYPES = (int, long)', 'class Pinned(object):']
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
            print(run.stdout)


if __name__ == '__main__':
    unittest.main()
