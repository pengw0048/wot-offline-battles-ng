"""Native player receipt terminal/cache behavior and fixed-source parity."""
import ast
import collections
import math
import os
from pathlib import Path
import random
import subprocess
import tempfile
import textwrap
import types
import unittest
from native_test_compile_flags import native_test_compile_flags

ROOT = Path(__file__).resolve().parents[1]
STATUSES = ['clear', 'crushed', 'approach', 'kinetic', 'hard', 'pending', 'invalid']
IDENTITIES = ['tree', 'pending', 'other', 'hard']


def token(mask):
    return tuple((1, index + 1, 73 if index == 2 else None)
                 for index in range(3) if mask & (1 << index))


def reference(source):
    text = source.read_text()
    cls = next(node for node in ast.parse(text).body
               if isinstance(node, ast.ClassDef) and any(
                   isinstance(child, ast.FunctionDef) and
                   child.name == '_resolve_player_destructible_contacts'
                   for child in node.body))
    method = next(node for node in cls.body if isinstance(node, ast.FunctionDef)
                  and node.name == '_resolve_player_destructible_contacts')
    env = dict(math=math, collections=collections, DESTRUCTIBLE_CONTACT_WINDOW=16,
               GROUND_RAW_TILT_RADIANS=.61,
               _angle_delta=lambda a, b: (b - a + math.pi) % (2 * math.pi) - math.pi)
    exec(textwrap.dedent(ast.get_source_segment(text, method)), env)
    resolve = env['_resolve_player_destructible_contacts']

    def run(case):
        requested, t0, t1, ts, tm, ps, pm, pending_proposal, cs, cm, pending_commit = case
        counts = [0, 0, 0]
        sent = []
        def tree_commit(*args, **kwargs):
            counts[0] += 1
            return dict(status=STATUSES[ts], token=token(tm) or None)
        def proposal(*args, **kwargs):
            counts[1] += 1
            return dict(status=STATUSES[ps], token=token(pm) or None, _pending=bool(pending_proposal))
        def commit(*args, **kwargs):
            counts[2] += 1
            return dict(status=STATUSES[cs], token=token(cm) or None, _pending=bool(pending_commit))
        def send(*args):
            sent.append(args)
            return True
        sensor = types.SimpleNamespace(
            trusted_tree_identity_status_1513=lambda space, chunk, item: IDENTITIES[(t0, t1)[item - 1]],
            commit_trusted_tree_contacts_1513=tree_commit,
            _catalog_motion_proposal=proposal, _catalog_motion_blocked=commit)
        runtime = types.SimpleNamespace(_worker_mode=True, _destructibles=sensor,
            _generation=1, _start_message={'round_id': 1}, _avatar=types.SimpleNamespace(spaceID=1),
            client=types.SimpleNamespace(send_player_destructible_contact_result=send),
            _destructible_contact_token=lambda value: tuple(value) if value else None,
            _vector=lambda value: value, _resolve_player_descriptor=lambda state: object(),
            _player_effective_snapshot=lambda state: {'physics': {}},
            _destructible_drive_speed_cap=lambda *args: 20.,
            _report_destructible_verdict=lambda *args: None)
        contact = dict(seq=1, token=token(requested), x=0., y=0., z=0., yaw=0.,
                       end_x=0., end_y=0., end_z=.1, end_yaw=0., speed=2., dt=.05)
        resolve(runtime, [{'id': 1, 'destructible_contacts': [contact]}], 1.)
        return [-1 if not sent else int(sent[-1][2])] + counts
    return run


class PlayerContactsTest(unittest.TestCase):
    def test_plain_python27_boundary(self):
        source = os.environ.get('WOT_FORK_0101_SOURCE')
        if not source:
            self.skipTest('Fixed gameplay source is required')
        py27 = subprocess.check_output(['pyenv', 'which', 'python2.7'], text=True,
                                      env=dict(os.environ, PYENV_VERSION='2.7.18')).strip()
        prefix = subprocess.check_output([py27, '-c', 'import sys;print(sys.prefix)'], text=True).strip()
        with tempfile.TemporaryDirectory(prefix='player-contacts-python-') as directory:
            folder = Path(directory)
            law = (Path(source) / 'native/offline_release_drive_python.inc').read_text().split('PyObject *WOT_CDECL release_drive(')[0]
            (folder / 'offline_release_drive_law.hpp').write_text('#pragma once\n#include <algorithm>\n#include <cmath>\n' + law)
            command = ['c++', '-std=c++17', '-O0', '-Wall', '-Wextra', '-Werror',
                       '-bundle', '-undefined', 'dynamic_lookup', '-isystem', str(Path(prefix) / 'include/python2.7'),
                       '-I' + str(folder), '-I' + str(Path(source) / 'native'), '-I' + str(ROOT / 'bigworld'),
                       str(ROOT / 'bigworld/stock_native_player_contacts_python.cpp'),
                       str(ROOT / 'tests/stock_native_player_contacts_python_host.cpp'), str(ROOT / 'tests/stock_native_client_state_host.cpp'), '-o', str(folder / 'player_contacts_test.so')]
            built = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stderr)
            script = folder / 'test.py'
            script.write_text('''import player_contacts_test as native
calls=[]
class Custom(object):
 def __float__(self):
  calls.append(1)
  return 1.
contact=dict(seq=1,token=[[7,9,None]],x=0.,y=0.,z=0.,yaw=0.,end_x=0.,end_y=0.,end_z=.1,end_yaw=0.,speed=2.,dt=.05)
bad=dict(contact,seq=2,speed=Custom())
player=dict(id=1,participating=True,world_pose=True,destructible_contacts=[contact,bad,dict(contact,seq=3)],critical={'devices':[]})
output=native.convert([player])
assert output['rejected']==1 and not calls
assert [row['seq'] for row in output['verdicts']]==[1,3]
assert output['verdicts'][0]==dict(serial=1,player_id=1,seq=1,accepted=True,token=[[7,9,None]])
assert output['traverse_valid']==[True,True]
player['critical']={'devices':[{'hp':None}]}
output=native.convert([player])
assert output['traverse_valid']==[False,False]
player['destructible_contacts']=[dict(contact,seq=i+1) for i in range(20)]
assert len(native.convert([player])['verdicts'])==16
assert native.convert([dict(player,participating=False)])['verdicts']==[]
assert native.convert([dict(player,world_pose=False)])['verdicts']==[]
assert native.convert([dict(player,id=-1)])['verdicts']==[]
print('Native player plain boundary passed')
''')
            ran = subprocess.run([py27, '-B', str(script)], capture_output=True, text=True)
            self.assertEqual(ran.returncode, 0, ran.stderr)
            self.assertIn('Native player plain boundary passed', ran.stdout)

    def test_native_owner_and_fixed_verdict_law(self):
        source = os.environ.get('WOT_FORK_0101_SOURCE')
        if not source:
            self.skipTest('Fixed gameplay source is required')
        fork = Path(source); big = ROOT / 'bigworld'
        with tempfile.TemporaryDirectory(prefix='native-player-contacts-') as folder:
            binary = Path(folder) / 'contacts'
            files = [big / (name + '.cpp') for name in ('stock_native_player_contacts',
                'stock_native_world_pipeline', 'stock_motion_rotation_batch', 'stock_offline_query_skin')]
            files += [fork / 'native' / (name + '.cpp') for name in ('offline_destructible_geometry', 'world_stage')]
            files += [ROOT / 'tests/test_stock_native_player_contacts.cpp']
            command = ['c++', '-std=c++17', '-O0', '-ffp-contract=off', '-Wall', '-Wextra', '-Werror',
                       *native_test_compile_flags('c++'), '-I' + str(big), '-I' + str(fork / 'native')]
            built = subprocess.run(command + list(map(str, files)) + ['-o', str(binary)], capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stderr)
            ran = subprocess.run([str(binary)], capture_output=True, text=True)
            self.assertEqual(ran.returncode, 0, ran.stderr)
            self.assertIn('Native player contacts passed', ran.stdout)
            oracle = reference(fork / 'src/res/scripts/client/gui/mods/offline_lan_0922/battle_runtime.py')
            rng = random.Random(1513)
            cases = [(rng.randrange(1, 8), rng.randrange(4), rng.randrange(4),
                      rng.randrange(6), rng.randrange(8), rng.randrange(6), rng.randrange(8),
                      rng.randrange(2), rng.randrange(6), rng.randrange(8), rng.randrange(2))
                     for _ in range(600)]
            expected = [oracle(case) for case in cases]
            ran = subprocess.run([str(binary), 'oracle'], input='\n'.join(' '.join(map(str, row)) for row in cases), capture_output=True, text=True)
            self.assertEqual(ran.returncode, 0, ran.stderr)
            self.assertEqual(expected, [list(map(int, line.split())) for line in ran.stdout.splitlines()])


if __name__ == '__main__':
    unittest.main()
