"""The staged tick freezes mutable state while reusing bounded static sections."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

CASES = r'''
import copy,sys,unittest
sys.path[:0]=sys.argv[1:]
import lan_battle_server as server
from battle_wire_schema import SCHEMA
from test_port_0922_server_projectiles import _state

class SnapshotFreezeTests(unittest.TestCase):
 def setUp(self):
  self.state=_state(players=1);self.messages=[]
  state=self.state
  state._next_bot_planner_tick=state.tick+1000000
  state.bot_manifest=[dict(id=9,static=dict(values=[1,2]))]
  state.bot_orders=dict(revision=7,orders=[dict(id=9,shell_index=0,
      route_assignment=dict(history=[dict(value='server only')]),
      tactical_evidence=dict(value='server only'),route_attempt=dict(value='server only'))])
  state.destructibles={('tree',7,3,None):dict(kind='destructible',revision=1,
      reported_by=-1,destructible_kind='tree',chunk_id=7,item_index=3,
      x=1.,y=2.,z=3.,fall_yaw=0.,speed=0.,is_shot=False)}
  state.destructible_revision=1
  state.simulation_worker.offer_snapshot=lambda message:self.messages.append(message) or True
  state.simulation_worker.offer_reliable=lambda message:self.messages.append(message) or True
  for player in state.players.values():
   player.offer_snapshot=lambda message:True
   player.offer_reliable=lambda message:True
 def tick(self):
  before=len(self.messages)
  worker=self.state.simulation_worker
  worker.bot_manifest_round_id_sent=self.state.round_id
  worker.bot_manifest_revision_sent=self.state.bot_manifest_revision
  worker.bot_manifest_authority_epoch_sent=self.state.authority_epoch
  worker.bot_manifest_tick_sent=self.state.tick
  self.state.tick_once(1./server.TICK_HZ)
  snapshots=[message for message in self.messages[before:] if message.get('type')=='snapshot']
  self.assertEqual(1,len(snapshots))
  return snapshots[0]
 def test_same_revision_static_sections_reuse_and_queued_payload_isolated(self):
  first=self.tick();second=self.tick()
  first_wire=SCHEMA.encode(first)
  self.assertIs(first['bot_manifest'],second['bot_manifest'])
  self.assertIs(first['destructibles'][0],second['destructibles'][0])
  self.state.bot_manifest[0]['static']['values'].append(3)
  self.state.destructibles[('tree',7,3,None)]['x']=99.
  self.state.players[1].x=17.
  third=self.tick()
  self.assertEqual([1,2],first['bot_manifest'][0]['static']['values'])
  self.assertEqual(1.,first['destructibles'][0]['x'])
  self.assertEqual([1,2],third['bot_manifest'][0]['static']['values'])
  self.assertEqual(1.,third['destructibles'][0]['x'])
  self.assertNotEqual(first['players'][0]['x'],third['players'][0]['x'])
  self.assertEqual(first_wire,SCHEMA.encode(first))
 def test_revision_and_scope_changes_refresh_only_current_cache(self):
  previous=self.tick()
  for field in ('bot_manifest_revision','destructible_revision','authority_epoch','round_id'):
   frozen=copy.deepcopy(previous)
   self.state.bot_manifest[0]['static']['values'].append(field)
   self.state.destructibles[('tree',7,3,None)]['x']+=1.
   setattr(self.state,field,getattr(self.state,field)+1)
   current=self.tick()
   if field!='destructible_revision':
    self.assertIsNot(previous['bot_manifest'],current['bot_manifest'])
    self.assertEqual(self.state.bot_manifest,current['bot_manifest'])
   if field!='bot_manifest_revision':
    self.assertIsNot(previous['destructibles'][0],current['destructibles'][0])
    self.assertEqual(self.state.destructibles[('tree',7,3,None)],current['destructibles'][0])
   self.assertEqual(frozen,previous)
   previous=current
 def test_orders_copy_current_ammo_without_mutating_diagnostics(self):
  canonical=copy.deepcopy(self.state.bot_orders)
  first=self.tick()
  self.assertEqual(canonical,self.state.bot_orders)
  self.assertEqual([dict(id=9,shell_index=0)],first['bot_orders'])
  self.state.bot_orders['orders'][0]['shell_index']=1
  second=self.tick()
  self.assertEqual(7,self.state.bot_orders['revision'])
  self.assertEqual([dict(id=9,shell_index=1)],second['bot_orders'])
  self.assertEqual(0,first['bot_orders'][0]['shell_index'])
  self.assertIn('route_assignment',self.state.bot_orders['orders'][0])
  self.assertIn('tactical_evidence',self.state.bot_orders['orders'][0])
  self.assertIn('route_attempt',self.state.bot_orders['orders'][0])

unittest.main(argv=['snapshot-freeze'],verbosity=2)
'''


class ShippingSnapshotFreeze(unittest.TestCase):
    def test_staged_tick_section_freeze(self):
        fork = os.environ.get('WOT_FORK_SOURCE')
        if not fork:
            self.skipTest('Exact pinned gameplay source is required')
        sys.path.insert(0, str(ROOT / 'wire'))
        import stage_server
        with tempfile.TemporaryDirectory(prefix='snapshot-freeze-') as temporary:
            target = Path(temporary) / 'server'
            client = Path(fork) / 'src/res/scripts/client'
            stage_server.stage(str(Path(fork) / 'server'), str(target), str(client))
            result = subprocess.run([sys.executable, '-B', '-c', CASES, str(target),
                str(client), str(Path(fork) / 'tests')], text=True, capture_output=True, timeout=30,
                env=dict(os.environ, PYTHONDONTWRITEBYTECODE='1'))
            self.assertEqual(0, result.returncode, result.stdout + result.stderr)


if __name__ == '__main__':
    unittest.main()
