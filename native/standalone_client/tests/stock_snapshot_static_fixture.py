"""Python 2/3 differential tests against the exact adapted fork payload."""
from __future__ import print_function
import copy
import os
import gc
import json
import sys
import time
import unittest

sys.path.insert(0, sys.argv.pop(1))
sys.path.insert(0, sys.argv.pop(1))
from effective_params_fixture import effective_params as _effective_params


def effective_params():
    value = _effective_params()
    value['physics']['rotationIsAroundCenter'] = False
    return value
from gui.mods.offline_lan_0922 import lan_client as lan
from gui.mods.offline_lan_0922 import snapshot_delta as delta
from gui.mods.offline_lan_0922 import stock_snapshot_static as static
from gui.mods.offline_lan_0922 import _stock_snapshot_original_snapshot_delta as original_delta
from gui.mods.offline_lan_0922 import _stock_snapshot_original_lan_client as original_lan


def snapshot(value=1, **changes):
    result = dict(type='snapshot', round_id=7, authority_epoch=1,
                  map='map', bot_authority_id=-1, bot_manifest_revision=1,
                  players=[dict(id=1, outfits={'1': 'YWJj'},
                                effective_params=effective_params(),
                                pose={'x': value}, ledger=[{'seq': value}])],
                  bots=[dict(id=2, pose={'x': value})])
    result.update(changes)
    return result


class StaticTests(unittest.TestCase):
    def client(self):
        return lan.LANClient('127.0.0.1', 28782, 'P', 'ussr:R11_MS-1')

    def deliver(self, message, previous=None, seq=1, encoded=None, full=False):
        wire, sent = delta.encode(message, encoded, seq, force_full=full)
        pending, baseline = delta.decode(wire, previous, return_pending_actors=True)
        return delta.materialize(pending), baseline, sent, pending, wire

    def test_actual_canonicalizers_accept_frozen_nested_json(self):
        params = effective_params()
        self.assertIsNotNone(lan._canonical_effective_params(params))
        self.assertEqual(lan._canonical_effective_params(params),
                         lan._canonical_effective_params(static.freeze(params)))
        self.assertEqual({'1': 'YWJj'},
                         lan._canonical_wire_outfits(static.freeze({'1': 'YWJj'})))
        client = self.client()
        message, unused, unused2, unused3, unused4 = self.deliver(snapshot())
        result = client._prepare_player_static_inputs(message['players'])
        self.assertTrue(result[1] and result[2])
        client._commit_player_static_inputs(result)
        self.assertEqual(lan._canonical_effective_params(params), result[0][0]['effective_params'])

    def test_python_boundary_consumers_copy_before_mutating_static_inputs(self):
        from gui.mods.offline_lan_0922 import critical_damage, crew_battle
        from gui.mods.offline_lan_0922 import player_critical_mechanics, equipment_mechanics
        class Battle(object):
            pass
        raw_params = effective_params()
        raw_params['equipment'] = [equipment_mechanics.project_equipment(dict(
            name='largeRepairkit', id=5, compactDescr=11005, tags=['repairkit'],
            reuseCount=-1, cooldownSeconds=90., repairAll=True, bonusValue=.1))]
        self.assertIsNotNone(lan._canonical_effective_params(raw_params))
        state = dict(effective_params=static.freeze(raw_params),
                     outfits=static.freeze({'1': 'YWJj'}))
        params = state['effective_params']
        # Canonical snapshots remain explicitly detachable at Python boundaries.
        local = static.detach(params)
        local['physics']['mass'] = 1.
        local['crew']['members'].append({})
        self.assertEqual(5730., params['physics']['mass'])
        self.assertEqual(lan.effective_params_wire.skill_summary(params),
                         lan.effective_params_wire.skill_summary(static.detach(params)))
        self.assertEqual(crew_battle.for_critical(params, {}),
                         crew_battle.for_critical(static.detach(params), {}))
        vehicle = Battle()
        vehicle.typeDescriptor = object()
        critical_damage.bind_device_profile(vehicle, params['critical'])
        self.assertEqual((100., 50.),
                         vehicle._offline_critical_descriptor._offline_critical_device_pools['engineHealth'])
        descriptor = player_critical_mechanics.descriptor_from_profile(params['critical'])
        self.assertEqual(100., descriptor.engine.maxHealth)
        equipment = equipment_mechanics.EquipmentState(params['equipment'][0])
        equipment.contract['tags'].append('local')
        equipment.uses_left = 0
        self.assertEqual(['repairkit'], params['equipment'][0]['tags'])
        # Explicit Python copies remain writable without changing the version.
        shot = copy.deepcopy(params['gun']['shots'][0]['source_shot'])
        shot['deadeye'] = True
        self.assertFalse(params['gun']['shots'][0]['source_shot']['deadeye'])

    def test_dynamic_isolation_static_readonly_and_explicit_deepcopy(self):
        source = snapshot()
        first, baseline, sent, pending, wire = self.deliver(source)
        source['players'][0]['effective_params']['physics']['mass'] = 1
        wire['players'][0]['effective_params']['physics']['mass'] = 2
        params = first['players'][0]['effective_params']
        self.assertEqual(5730., params['physics']['mass'])
        for operation in (
                lambda: params.update(version=2),
                lambda: params['physics'].__setitem__('mass', 3),
                lambda: params['physics']['terrainResist'].append(3),
                lambda: params['physics']['terrainResist'].__setslice__(0, 1, [3]),
                lambda: params.__init__({}),
                lambda: params['physics']['terrainResist'].__init__([])):
            self.assertRaises(TypeError, operation)
        first['players'][0]['pose']['x'] = 999
        first['players'][0]['ledger'][0]['seq'] = 999
        first['bots'][0]['pose']['x'] = 999
        first['players'].append({'id': 99})
        again = delta.materialize(pending)
        self.assertEqual(1, again['players'][0]['pose']['x'])
        self.assertEqual(1, again['players'][0]['ledger'][0]['seq'])
        self.assertEqual(1, again['bots'][0]['pose']['x'])
        self.assertEqual(1, len(again['players']))
        detached = copy.deepcopy(params)
        detached['physics']['terrainResist'].append(9)
        self.assertEqual(3, len(params['physics']['terrainResist']))
        self.assertEqual(json.loads(json.dumps(params)), params)
        # The complete-message mode keeps its historical mutable JSON output.
        ordinary, unused = delta.decode(wire, None)
        ordinary['players'][0]['effective_params']['physics']['mass'] = 3
        self.assertEqual(5730., params['physics']['mass'])

    def test_coalescing_rejection_and_repeated_prepare_reuse_value_version(self):
        client = self.client()
        first, baseline, sent, unused, unused2 = self.deliver(snapshot())
        initial = client._prepare_player_static_inputs(first['players'])
        client._commit_player_static_inputs(initial)
        changed = snapshot(2)
        changed['players'][0]['effective_params']['physics']['mass'] = 6000.
        dropped, baseline, sent, unused, unused2 = self.deliver(changed, baseline, 2, sent)
        # A consumer rejects the changed sample: staging must not publish it.
        rejected = client._prepare_player_static_inputs(dropped['players'])
        self.assertEqual(5730., client._published_player_effective_params[1]['physics']['mass'])
        latest = snapshot(3)
        latest['players'][0]['effective_params']['physics']['mass'] = 6000.
        latest, baseline, sent, unused, unused2 = self.deliver(latest, baseline, 3, sent)
        original_same = lan._same_canonical_source
        def fail(*unused):
            raise AssertionError('unchanged owned value traversed again')
        # Preserve the normalizer identity: its body cannot run after memo hit.
        original_wire = lan.effective_params_wire.canonical
        lan._same_canonical_source = fail
        lan.effective_params_wire.canonical = fail
        try:
            prepared = client._prepare_player_static_inputs(latest['players'])
            trusted = client._prepare_player_static_inputs(
                prepared[0], trusted_prepared=prepared)
        finally:
            lan._same_canonical_source = original_same
            lan.effective_params_wire.canonical = original_wire
        self.assertIs(rejected[4][1], prepared[4][1])
        self.assertIs(prepared[4][1], trusted[4][1])
        client._commit_player_static_inputs(prepared)
        self.assertEqual(6000., client._published_player_effective_params[1]['physics']['mass'])
        self.assertIsNot(initial[4][1], prepared[4][1])

    def test_skipped_changed_snapshot_is_not_a_last_patch_dirty_flag(self):
        client = self.client()
        first, baseline, sent, unused, unused2 = self.deliver(snapshot())
        client._commit_player_static_inputs(client._prepare_player_static_inputs(first['players']))
        changed = snapshot(2)
        changed['players'][0]['outfits'] = {'2': 'ZGVm'}
        unused, baseline, sent, unused2, unused3 = self.deliver(changed, baseline, 2, sent)
        latest, baseline, sent, unused, wire = self.deliver(changed, baseline, 3, sent)
        self.assertEqual({}, wire['players'])
        prepared = client._prepare_player_static_inputs(latest['players'])
        self.assertEqual({'2': 'ZGVm'}, prepared[3][1])
        self.assertEqual({'1': 'YWJj'}, client._published_player_outfits[1])
        client._commit_player_static_inputs(prepared)
        self.assertEqual({'2': 'ZGVm'}, client._published_player_outfits[1])

    def test_invalid_delta_and_mutable_update_alias_leave_owned_baseline_intact(self):
        first, baseline, sent, unused, unused2 = self.deliver(snapshot())
        changed = snapshot(2)
        changed['players'][0]['effective_params']['physics']['mass'] = 6000.
        wire, unused = delta.encode(changed, sent, 2)
        broken = copy.deepcopy(wire)
        broken['bots'] = {'update': [{'id': 999, 'set': {'pose': {'x': 2}}}]}
        self.assertRaises(delta.SnapshotDeltaError, delta.decode, broken, baseline, True)
        pending, next_baseline = delta.decode(wire, baseline, True)
        wire['players']['update'][0]['set']['effective_params']['physics']['mass'] = 1.
        materialized = delta.materialize(pending)
        self.assertEqual(6000., materialized['players'][0]['effective_params']['physics']['mass'])
        self.assertEqual(5730., first['players'][0]['effective_params']['physics']['mass'])
        self.assertEqual(1, baseline.sequence)
        self.assertEqual(2, next_baseline.sequence)

    def test_reconnect_full_resync_round_epoch_and_actor_readdition(self):
        first, baseline, sent, unused, unused2 = self.deliver(snapshot())
        prior = first['players'][0]['effective_params']
        for message, reset, force in (
                (snapshot(), True, False), (snapshot(), False, True),
                (snapshot(round_id=8), False, False),
                (snapshot(authority_epoch=2), False, False)):
            result, unused, unused2, unused3, unused4 = self.deliver(
                message, None if reset else baseline, 2,
                None if reset else sent, full=force)
            self.assertEqual(prior, result['players'][0]['effective_params'])
            self.assertIsNot(prior, result['players'][0]['effective_params'])
        empty, baseline, sent, unused, unused2 = self.deliver(
            snapshot(players=[]), baseline, 2, sent)
        result, baseline, sent, unused, unused2 = self.deliver(snapshot(), baseline, 3, sent)
        self.assertIsNot(prior, result['players'][0]['effective_params'])

    def test_field_remove_set_invalid_and_numeric_type_changes(self):
        client = self.client()
        first, baseline, sent, unused, unused2 = self.deliver(snapshot())
        client._commit_player_static_inputs(client._prepare_player_static_inputs(first['players']))
        changed = snapshot()
        changed['players'][0].pop('effective_params')
        missing, baseline, sent, unused, unused2 = self.deliver(changed, baseline, 2, sent)
        inherited = client._prepare_player_static_inputs(missing['players'], allow_lean=True)
        self.assertTrue(inherited[2])
        self.assertIs(client._published_player_effective_params[1],
                      inherited[0][0]['effective_params'])
        for invalid in (True, 1., None, {'version': True}):
            changed['players'][0]['effective_params'] = invalid
            result, baseline, sent, unused, unused2 = self.deliver(changed, baseline, baseline.sequence + 1, sent)
            self.assertFalse(client._prepare_player_static_inputs(result['players'])[2])
            self.assertEqual(5730., client._published_player_effective_params[1]['physics']['mass'])
        changed['players'][0]['effective_params'] = effective_params()
        result, unused, unused2, unused3, unused4 = self.deliver(changed, baseline, baseline.sequence + 1, sent)
        self.assertTrue(client._prepare_player_static_inputs(result['players'])[2])
        for left, right in ((True, 1), (1, 1.), (0., -0.)):
            self.assertFalse(delta._same(static.freeze({'x': left}), static.freeze({'x': right})))
        self.assertFalse(lan._same_canonical_source([1], (1,)))

    def test_algorithm_work_count_and_local_microbenchmark(self):
        first, baseline, sent, pending, unused = self.deliver(snapshot())
        client = self.client()
        calls = [0]
        original = lan._same_canonical_source
        def count(left, right):
            calls[0] += 1
            return original(left, right)
        lan._same_canonical_source = count
        try:
            plain = static.detach(first['players'])
            prepared = client._prepare_player_static_inputs(plain)
            client._commit_player_static_inputs(prepared)
            visits = []
            original_clone = delta._clone
            def counted_clone(value):
                visits.append(value)
                return original_clone(value)
            delta._clone = counted_clone
            try:
                materialized = delta.materialize(pending)
            finally:
                delta._clone = original_clone
            self.assertIs(first['players'][0]['effective_params'],
                          materialized['players'][0]['effective_params'])
            self.assertFalse(any(isinstance(value, static._StaticDict) for value in visits))
            self.assertTrue(any(value == {'x': 1} for value in visits))
            rounds = 80
            started = time.time()
            for unused in range(rounds):
                client._prepare_player_static_inputs(delta._clone(baseline.actors['players']))
            ordinary_elapsed = time.time() - started
            ordinary_calls = calls[0]
            calls[0] = 0
            client._prepare_player_static_inputs(first['players'])
            started = time.time()
            for unused in range(rounds):
                materialized = delta.materialize(pending)
                client._prepare_player_static_inputs(materialized['players'])
            owned_elapsed = time.time() - started
            self.assertEqual(0, calls[0])
            self.assertGreater(ordinary_calls, rounds * 100)
            print('local static benchmark: rounds=%d recursive_comparisons=%d->0 old_clone_prepare_ms=%.3f owned_materialize_prepare_ms=%.3f' %
                  (rounds, ordinary_calls, ordinary_elapsed * 1000, owned_elapsed * 1000))
        finally:
            lan._same_canonical_source = original


    def test_representative_wide_actor_materialization_and_preparation(self):
        def actor(actor_id):
            # A full Bot publication contains over a hundred scalar fields;
            # retain its nested mutable critical/equipment/contact ledgers too.
            row = dict(('dynamic_%d' % index, index * .125) for index in range(128))
            row.update(id=actor_id, x=1., y=2., z=3., yaw=.5, alive=True,
                       critical=dict(devices=[dict(name='engineHealth', hp=70., max_hp=100., state='normal')],
                                     crew_ko=[], destroyed=[], fire=False, events=[]),
                       equipment_states=[dict(equipment=dict(kind='repairkit', tags=['repairkit']),
                                              usesLeft=1, cooldownTimeLeft=20.)],
                       contact_push_acks=[[1, 2, 1., 2., 3., 4., 5.]],
                       ram_motion=[[1, 2, 10000, 1., 2., 3.]], velocity=[1., 2., 3.])
            return row
        for humans, bots in ((1, 29), (15, 15), (0, 30)):
            message = snapshot(players=[], bots=[])
            for index in range(humans):
                row = actor(index + 1)
                row.update(outfits={'1': 'YWJj'}, effective_params=effective_params())
                message['players'].append(row)
            message['bots'] = [actor(index + 100) for index in range(bots)]
            old_wire, unused = original_delta.encode(message, None, 1)
            old_pending, unused = original_delta.decode(old_wire, None, True)
            wire, unused = delta.encode(message, None, 1)
            pending, unused = delta.decode(wire, None, True)
            client = self.client()
            old_client = original_lan.LANClient('127.0.0.1', 28782, 'P', 'ussr:R11_MS-1')
            client._commit_player_static_inputs(client._prepare_player_static_inputs(delta.materialize(pending)['players']))
            old_client._commit_player_static_inputs(old_client._prepare_player_static_inputs(original_delta.materialize(old_pending)['players']))
            self.assertEqual(original_delta.materialize(old_pending), delta.materialize(pending))
            cloned = delta.materialize(pending)
            cloned['bots'][0]['critical']['devices'][0]['hp'] = -1.
            cloned['bots'][0]['equipment_states'][0]['equipment']['tags'].append('changed')
            cloned['bots'][0]['contact_push_acks'][0][0] = -1
            fresh = delta.materialize(pending)
            self.assertEqual(70., fresh['bots'][0]['critical']['devices'][0]['hp'])
            self.assertEqual(['repairkit'], fresh['bots'][0]['equipment_states'][0]['equipment']['tags'])
            self.assertEqual(1, fresh['bots'][0]['contact_push_acks'][0][0])
            def cloned_nodes(codec, queued):
                count = [0]
                clone = codec._clone
                def counted(value):
                    count[0] += 1
                    return clone(value)
                codec._clone = counted
                try:
                    codec.materialize(queued)
                finally:
                    codec._clone = clone
                return count[0]
            old_nodes = cloned_nodes(original_delta, old_pending)
            new_nodes = cloned_nodes(delta, pending)
            self.assertLess(new_nodes, old_nodes)
            rounds = 100
            def sample(codec, queued, consumer):
                start = time.time()
                for unused in range(rounds):
                    codec.materialize(queued)
                materialize_seconds = time.time() - start
                start = time.time()
                for unused in range(rounds):
                    value = codec.materialize(queued)
                    consumer._prepare_player_static_inputs(value['players'])
                return materialize_seconds, time.time() - start
            gc.collect()
            old_times = [sample(original_delta, old_pending, old_client) for unused in range(3)]
            new_times = [sample(delta, pending, client) for unused in range(3)]
            old_materialize, old_combined = (min(row[index] for row in old_times) for index in (0, 1))
            new_materialize, new_combined = (min(row[index] for row in new_times) for index in (0, 1))
            print('wide snapshot benchmark: humans=%d bots=%d rounds=%d clone_calls=%d->%d materialize_ms=%.3f->%.3f combined_ms=%.3f->%.3f' %
                  (humans, bots, rounds, old_nodes, new_nodes, old_materialize * 1000., new_materialize * 1000.,
                   old_combined * 1000., new_combined * 1000.))
            # Timing is diagnostic evidence, never a scheduler-dependent test
            # assertion. Dynamic isolation and exact values above are the gate.



if __name__ == '__main__':
    unittest.main()
