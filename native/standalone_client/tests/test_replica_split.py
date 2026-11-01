"""The replica snapshot split carries every remote actor field a merged receiver needs."""
import copy
import json
import os
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'wire'))
import replica_split  # noqa: E402
from schema import (BOT_MOTION_NAMES, COUNTDOWN_NAMES, EQUIPMENT_COUNTDOWN_NAMES,  # noqa: E402
                    PLAYER_MOTION_NAMES, PLAYER_OWNER_NAMES, SCHEMA)


def merge(model, message):
    """Reference receiver: static/state records plus motion into complete rows."""
    if message['full']:
        model.clear()
    for kind, removed in (('player', 'removed_players'), ('bot', 'removed_bots')):
        for key in message[removed]:
            model.pop((kind, key), None)
        for state in message[kind + '_states']:
            row = model.setdefault((kind, state['id']), {})
            motion = dict((k, row[k]) for k in row if k in (
                PLAYER_MOTION_NAMES if kind == 'player' else BOT_MOTION_NAMES))
            row.clear()
            row.update(state)
            row.update(motion)
        for motion in message[kind + '_motion']:
            model.setdefault((kind, motion['id']), {}).update(motion)
    return model


def comparable(row, kind, contracts=()):
    excluded = COUNTDOWN_NAMES + (PLAYER_OWNER_NAMES if kind == 'player' else ())
    out = dict((k, v) for k, v in row.items() if k not in excluded)
    if isinstance(out.get('equipment_states'), list):
        out['equipment_states'] = [dict((k, v) for k, v in item.items()
                                        if k not in EQUIPMENT_COUNTDOWN_NAMES)
                                   for item in out['equipment_states']]
        # The native receiver materializes the wire's Bot contract indexes.
        # Compare complete contracts on both sides without dropping any fields.
        if kind == 'bot':
            for item in out['equipment_states']:
                if type(item.get('equipment')) is int:
                    index = item['equipment']
                    assert 0 <= index < len(contracts), 'Unavailable Bot equipment contract'
                    item['equipment'] = contracts[index]
    return out


def server_snapshot():
    path = os.environ.get('WIRE_FIXTURES')
    if path and os.path.exists(path):
        with open(path) as source:
            for line in source:
                message = json.loads(line)
                if (message.get('type') == 'snapshot' and 'client_build' not in message and
                        message.get('bots') and message.get('players')):
                    return message
    return None


class _Stream(unittest.TestCase):
    def setUp(self):
        self.base = server_snapshot()
        if self.base is None:
            self.skipTest('A recorded server snapshot (WIRE_FIXTURES) is required')
        self.own = self.base['players'][0]['id']

    def stream(self, snapshots, write=None):
        baseline, model, sizes = None, {}, []
        for index, message in enumerate(snapshots):
            out, next_baseline = replica_split.encode(message, baseline, self.own, False)
            frame = SCHEMA.encode(out)
            decoded = SCHEMA.decode(frame)
            sizes.append(len(frame))
            if write is None or write(index):
                baseline = next_baseline
                merge(model, decoded)
                yield message, decoded, copy.deepcopy(model)
            # An unwritten candidate leaves the receiver and baseline unchanged.

    def assertMerged(self, message, model):
        expected = {}
        contracts = message.get('bot_equipment_contracts', ())
        for row in message['players']:
            if row['id'] != self.own:
                expected[('player', row['id'])] = comparable(row, 'player', contracts)
        for row in message['bots']:
            expected[('bot', row['id'])] = comparable(row, 'bot', contracts)
        got = dict((key, comparable(row, key[0], contracts)) for key, row in model.items())
        self.assertEqual(got, expected)

class ReplicaSplit(_Stream):
    def test_first_snapshot_is_full_and_complete(self):
        message, decoded, model = next(self.stream([self.base]))
        self.assertTrue(decoded['full'])
        self.assertEqual(decoded['own']['id'], self.own)
        self.assertMerged(message, model)

    def test_unchanged_actors_send_nothing_and_changes_send_only_their_part(self):
        moved = copy.deepcopy(self.base)
        moved['server_tick'] += 1
        moved['bots'][0]['x'] += 1.5
        for name in COUNTDOWN_NAMES:
            if name in moved['bots'][1]:
                moved['bots'][1][name] = type(moved['bots'][1][name])(250)
        hit = copy.deepcopy(moved)
        hit['server_tick'] += 1
        hit['bots'][1]['health'] -= 10
        rows = list(self.stream([self.base, copy.deepcopy(self.base), moved, hit]))
        self.assertEqual((rows[1][1]['bot_motion'], rows[1][1]['bot_states']), ([], []))
        self.assertEqual([m['id'] for m in rows[2][1]['bot_motion']], [moved['bots'][0]['id']])
        self.assertEqual(rows[2][1]['bot_states'], [])
        self.assertEqual([s['id'] for s in rows[3][1]['bot_states']], [hit['bots'][1]['id']])
        self.assertEqual(rows[3][1]['bot_states'][0]['health'], hit['bots'][1]['health'])
        for message, unused_decoded, model in rows:
            self.assertMerged(message, model)

    def test_dropped_candidate_is_resent_against_the_last_write(self):
        hit = copy.deepcopy(self.base)
        hit['server_tick'] += 1
        hit['bots'][0]['health'] -= 25
        later = copy.deepcopy(hit)
        later['server_tick'] += 1
        rows = list(self.stream([self.base, hit, later], write=lambda index: index != 1))
        self.assertEqual([s['id'] for s in rows[1][1]['bot_states']], [hit['bots'][0]['id']])
        self.assertMerged(later, rows[1][2])

    def test_departed_actor_is_removed_and_new_round_is_full(self):
        gone = copy.deepcopy(self.base)
        gone['server_tick'] += 1
        departed = gone['bots'].pop()['id']
        next_round = copy.deepcopy(self.base)
        next_round['round_id'] += 1
        rows = list(self.stream([self.base, gone, next_round]))
        self.assertEqual(rows[1][1]['removed_bots'], [departed])
        self.assertTrue(rows[2][1]['full'])
        self.assertMerged(next_round, rows[2][2])


if __name__ == '__main__':
    unittest.main()


NATIVE_MERGE = r'''
import json, sys
sys.path.insert(0, sys.argv[1])
import wire_replica_host
frames = [line.strip().decode('hex') for line in open(sys.argv[2])]
json.dump(wire_replica_host.merge(frames), open(sys.argv[3], 'w'))
'''


class NativeReplicaStore(_Stream):
    """The C++ receiver (GIL-free decode + merge) rebuilds the complete rows."""

    def test_native_merge_rebuilds_complete_snapshots(self):
        import subprocess
        import tempfile
        hit = copy.deepcopy(self.base)
        hit['server_tick'] += 1
        hit['bots'][0]['health'] -= 25
        hit['bots'][1]['x'] += 2.0
        gone = copy.deepcopy(hit)
        gone['server_tick'] += 1
        gone['bots'].pop()
        messages = [self.base, copy.deepcopy(self.base), hit, gone]
        baseline, frames = None, []
        for message in messages:
            out, baseline = replica_split.encode(message, baseline, self.own, False)
            frames.append(SCHEMA.encode(out))
        py27 = subprocess.check_output(['pyenv', 'which', 'python2.7'], text=True,
                                       env=dict(os.environ, PYENV_VERSION='2.7.18')).strip()
        prefix = subprocess.check_output([py27, '-c', 'import sys;print(sys.prefix)'], text=True).strip()
        with tempfile.TemporaryDirectory(prefix='wire-replica-') as folder:
            folder = Path(folder)
            subprocess.run(['c++', '-std=c++17', '-O1', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter',
                            '-Wno-missing-field-initializers', '-Wno-register', '-bundle', '-undefined',
                            'dynamic_lookup', '-isystem', str(Path(prefix) / 'include/python2.7'),
                            '-I' + str(ROOT / 'bigworld'), str(ROOT / 'tests/stock_wire_replica_host.cpp'),
                            '-o', str(folder / 'wire_replica_host.so')], check=True)
            (folder / 'frames.txt').write_text('\n'.join(frame.hex() for frame in frames))
            (folder / 'merge.py').write_text(NATIVE_MERGE)
            subprocess.run([py27, '-B', str(folder / 'merge.py'), str(folder), str(folder / 'frames.txt'),
                            str(folder / 'out.json')], check=True)
            snapshots = json.loads((folder / 'out.json').read_text())
        for message, snapshot in zip(messages, snapshots):
            self.assertEqual(snapshot['type'], 'snapshot')
            self.assertEqual(snapshot['server_tick'], message['server_tick'])
            own = [row for row in snapshot['players'] if row['id'] == self.own]
            self.assertEqual(own, [r for r in message['players'] if r['id'] == self.own])
            model = dict((('player', row['id']), row) for row in snapshot['players'] if row['id'] != self.own)
            model.update((('bot', row['id']), row) for row in snapshot['bots'])
            self.assertMerged(message, model)
