"""Replay reliable worker operation lifecycles against the actual staged server."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

CASES = r'''
import copy,sys,unittest
from types import SimpleNamespace
from unittest import mock
sys.path[:0]=sys.argv[1:]
import lan_battle_server as server
import battle_wire_worker_operations as operations
import test_port_0922_server_projectiles as fixture
import bot_state_rows

class Stop(Exception):pass

def setup():
    saved={}; original=server.BattleState.update_bot_states
    def capture(state,authority,message):
        result=original(state,authority,message)
        saved['count']=saved.get('count',0)+1
        if saved['count']==2:
            saved.update(state=state,message=copy.deepcopy(message))
            raise Stop()
        return result
    with mock.patch.object(server.BattleState,'update_bot_states',capture):
        try:fixture.ServerProjectileLedgerTests().test_bot_state_edge_waits_for_authorized_launch()
        except Stop:pass
    return saved['state'],saved['message']

def next_state(state,message,stamp=300000,fire=2):
    raw=bot_state_rows.decoded(message)
    raw['fire_seq']=fire
    raw.update(state._ordinary_bot_burst(fire,raw.get('shell_index',0)))
    publication=bot_state_rows.publication(dict(round_id=1,sample_time_us=stamp,
        source_batch_horizon_us=stamp,bots=[raw]))
    assert state.update_bot_states(-1,publication),state.last_bot_state_reject

def launch(sequence=1,stamp=200000):
    return fixture._launch(shooter_id=16,shooter_kind='bot',shot_seq=sequence,
        origin=[20.,1.,0.],launch_time_us=stamp)

class ReliableOperations(unittest.TestCase):
    def test_later_burst_cannot_revoke_an_admitted_edge(self):
        state,message=setup();next_state(state,message)
        state._prune_orphaned_bot_launch_edges()
        self.assertIn((16,1),state.bot_pending_projectile_launches)
        owner=operations.owner(state,server.__dict__)
        for seq,stamp in ((1,200000),(2,300000)):
            self.assertTrue(owner.launch(launch(seq,stamp)))
            self.assertIn('1:b:16:%s'%seq,state.projectiles)
        count=len(state.pending_events)
        self.assertTrue(owner.launch(launch()))
        self.assertEqual(count,len(state.pending_events))
        self.assertEqual(2,state._statistics_row('bot',16)['shots_fired'])

    def test_clock_rebase_uses_actual_events_instead_of_inventing_shots(self):
        state,message=setup();next_state(state,message,800000)
        self.assertNotIn((16,2),state.bot_pending_projectile_metadata)
        self.assertFalse(state.projectiles)
        owner=operations.owner(state,server.__dict__)
        self.assertTrue(owner.launch(launch()))
        self.assertTrue(owner.launch(launch(2,800000)))
        self.assertEqual({'1:b:16:1','1:b:16:2'},set(state.projectiles))
        self.assertEqual(2,sum(e['kind']=='bot_shot' for e in state.pending_events))

    def test_missing_state_waits_locally_and_does_not_replace_frozen_payload(self):
        state,message=setup();owner=operations.owner(state,server.__dict__)
        late=launch(2,300000)
        self.assertTrue(owner.launch(late));self.assertIn((16,2),owner.launches)
        self.assertFalse(owner.launch(dict(late,velocity=[99.,0.,0.])))
        self.assertFalse(state.projectiles)
        self.assertTrue(owner.launch(launch()))
        next_state(state,message);owner.advance()
        self.assertFalse(owner.launches)
        self.assertEqual(2,len(state.projectiles))

    def test_admitted_shot_survives_death_and_invalid_launch_has_one_terminal(self):
        state,message=setup();state.bot_states[16]['alive']=False
        state._prune_orphaned_bot_launch_edges()
        owner=operations.owner(state,server.__dict__)
        self.assertTrue(owner.launch(launch()))
        self.assertIn('1:b:16:1',state.projectiles)
        state.bot_states[16]['fire_seq']=2
        invalid=launch(2,300000)
        self.assertTrue(owner.launch(invalid))
        failures=[e for e in state.pending_events if e['kind']=='projectile_launch_failed']
        self.assertEqual(1,len(failures));self.assertEqual(2,failures[0]['shot_seq'])
        self.assertTrue(owner.launch(invalid))
        self.assertEqual(1,sum(e['kind']=='projectile_launch_failed' for e in state.pending_events))

    def test_actual_worker_dispatch_wakes_held_launch_after_state(self):
        state,message=setup()
        handler=object.__new__(server.ClientHandler)
        game=SimpleNamespace(state=state)
        late=launch(2,300000)
        self.assertTrue(handler._dispatch_simulation_worker_message(game,state.simulation_worker,late))
        owner=operations.owner(state,server.__dict__)
        self.assertIn((16,2),owner.launches)
        self.assertTrue(handler._dispatch_simulation_worker_message(game,state.simulation_worker,launch()))
        raw=bot_state_rows.decoded(message);raw['fire_seq']=2
        raw.update(state._ordinary_bot_burst(2,raw.get('shell_index',0)))
        publication=bot_state_rows.publication(dict(type='bot_state',round_id=1,
            sample_time_us=300000,source_batch_horizon_us=300000,bots=[raw]))
        self.assertTrue(handler._dispatch_simulation_worker_message(game,state.simulation_worker,publication))
        self.assertFalse(owner.launches);self.assertEqual(2,len(state.projectiles))

    def test_failure_is_local_and_missing_dependency_does_not_block_other_bot(self):
        state,message=setup();next_state(state,message)
        owner=operations.owner(state,server.__dict__)
        self.assertTrue(owner.launch(dict(launch(),velocity=[0.,0.,0.])))
        self.assertNotIn('1:b:16:1',state.projectiles)
        self.assertEqual('projectile_launch_failed',state.pending_events[-1]['kind'])
        self.assertTrue(owner.launch(launch(2,300000)))
        self.assertIn('1:b:16:2',state.projectiles)
        state,message=setup();owner=operations.owner(state,server.__dict__)
        self.assertTrue(owner.launch(launch(2,300000)))
        state.bot_states[17]=dict(state.bot_states[16],id=17)
        state.bot_pending_projectile_launches.add((17,1))
        state.bot_pending_projectile_metadata[(17,1)]=dict(state.bot_pending_projectile_metadata[(16,1)])
        self.assertTrue(owner.launch(dict(launch(),shooter_id=17)))
        self.assertIn('1:b:17:1',state.projectiles)
        self.assertIn((16,2),owner.launches)

    def test_temporary_capacity_waits_without_discarding_the_shot(self):
        state,message=setup();owner=operations.owner(state,server.__dict__)
        with mock.patch.object(server,'PROJECTILE_MAX_PER_SHOOTER',0):
            self.assertTrue(owner.launch(launch()))
        self.assertIn((16,1),owner.launches)
        self.assertFalse(any(e['kind']=='projectile_launch_failed' for e in state.pending_events))
        owner.advance()
        self.assertFalse(owner.launches);self.assertIn('1:b:16:1',state.projectiles)

    def test_contact_waits_for_other_owner_publication_without_network_retry(self):
        state=fixture._state(players=1);player=state.players[1]
        player.destructible_contact_seq=1
        player.destructible_contacts[1]=fixture._player_destructible_contact()
        message=dict(type='player_destructible_contact_result',round_id=1,
            player_id=1,contact_seq=1,accepted=True,token=[[7,3,None]])
        owner=operations.owner(state,server.__dict__)
        self.assertTrue(owner.contact(message));self.assertEqual(1,len(owner.contacts))
        self.assertFalse(owner.contact(dict(message,accepted=False)))
        self.assertIn(1,player.destructible_contacts)
        state.destructibles[('tree',7,3,None)]={
            'destructible_kind':'tree','chunk_id':7,'item_index':3,'mat_kind':None}
        owner.advance()
        self.assertFalse(owner.contacts);self.assertNotIn(1,player.destructible_contacts)
        self.assertTrue(owner.contact(message));self.assertFalse(owner.contacts)

    def test_finish_preserves_dependency_committed_by_terminal_projectile(self):
        for published in (False,True):
            state=fixture._state(players=1);player=state.players[1]
            player.destructible_contact_seq=1
            player.destructible_contacts[1]=fixture._player_destructible_contact()
            message=dict(type='player_destructible_contact_result',round_id=1,
                player_id=1,contact_seq=1,accepted=True,token=[[7,3,None]])
            owner=operations.owner(state,server.__dict__)
            self.assertTrue(owner.contact(message))
            if published:
                state.destructibles[('tree',7,3,None)]={
                    'destructible_kind':'tree','chunk_id':7,'item_index':3,'mat_kind':None}
            self.assertTrue(state._finish_battle(0,'battle_timeout',record_receipts=False))
            self.assertFalse(owner.contacts)
            self.assertEqual(1,player.destructible_contact_resolved_seq)
            self.assertEqual(not published,bool(player.destructible_contact_rejections))

    def test_round_terminal_retires_pending_operations_before_result(self):
        state,message=setup();owner=operations.owner(state,server.__dict__)
        self.assertTrue(owner.launch(launch(2,300000)))
        self.assertTrue(state._finish_battle(0,'battle_timeout',record_receipts=False))
        self.assertFalse(owner.launches)
        kinds=[e['kind'] for e in state.pending_events]
        self.assertLess(kinds.index('projectile_launch_failed'),kinds.index('battle_result'))
        self.assertTrue(owner.launch(launch(3,400000)))
        self.assertFalse(owner.launches)
        self.assertEqual(2,sum(e['kind']=='projectile_launch_failed' for e in state.pending_events))
        state.round_id=2
        self.assertIsNot(owner,operations.owner(state,server.__dict__))

unittest.main(argv=['worker-operations'],verbosity=2)
'''

class ShippingWorkerOperations(unittest.TestCase):
    def test_reliable_operations(self):
        fork=os.environ.get('WOT_FORK_SOURCE')
        if not fork:self.skipTest('Exact pinned gameplay source is required')
        sys.path.insert(0,str(ROOT/'wire'))
        import stage_server
        with tempfile.TemporaryDirectory(prefix='worker-operations-') as temporary:
            target=Path(temporary)/'server';client=Path(fork)/'src/res/scripts/client'
            stage_server.stage(str(Path(fork)/'server'),str(target),str(client))
            result=subprocess.run([sys.executable,'-B','-c',CASES,str(target),str(client),
                str(Path(fork)/'tests')],capture_output=True,text=True,timeout=60)
            self.assertEqual(0,result.returncode,result.stdout+result.stderr)

if __name__=='__main__':unittest.main()
