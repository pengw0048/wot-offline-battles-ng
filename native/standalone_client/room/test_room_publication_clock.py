"""Source-extracted clock proposal oracle, independent of native engine timing."""
import copy
import importlib
import json
import os
from pathlib import Path
import random
import subprocess
import sys
import tempfile
import textwrap
import types
import unittest
ROOM=Path(__file__).resolve().parent
PIN=os.environ.get('WOT_FORK_SOURCE')
@unittest.skipUnless(PIN,'Pinned source is required')
class PublicationClock(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.folder=tempfile.TemporaryDirectory(prefix='room-clock-');cls.addClassCleanup(cls.folder.cleanup)
  cls.binary=Path(cls.folder.name)/'clock';stage=Path(cls.folder.name)/'server'
  subprocess.run([sys.executable,'-B',str(ROOM.parent/'wire/stage_server.py'),str(Path(PIN)/'server'),str(stage)],check=True)
  sys.path[:0]=[str(stage),str(Path(PIN)/'src/res/scripts/client')]
  cls.server=importlib.import_module('lan_battle_server')
  source=(stage/'lan_battle_server.py').read_text()
  method=source[source.index('    def update_bot_states('):]
  block=method[method.index('            previous_source_time_us ='):method.index('            # One row per Bot')]
  suffix='''\n    return dict(mapped=next_bot_state_time_us,motion_offset=next_motion_time_offset_us,launch_offset=next_launch_clock_offset_us,source=source_time_us,horizon=source_batch_horizon_us,rebase=source_clock_rebase,same_batch=same_source_batch)\n'''
  function='def actual_source(self,message,received_raw_motion_time_us):\n    received_motion_time_us=self._logical_motion_time_us(received_raw_motion_time_us)\n'+textwrap.indent(textwrap.dedent(block),'    ')+suffix
  namespace=dict(cls.server.__dict__);exec(compile(function,'<actual staged clock block>','exec'),namespace)
  cls.actual=staticmethod(namespace['actual_source'])
  prebuilt=os.environ.get('WOT_ROOM_TEST_BIN')
  if prebuilt:
      cls.binary=Path(prebuilt)/'clock'
  else:
      subprocess.run(['c++','-std=c++17','-O0','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(ROOM/'room_publication_clock_probe.cpp'),str(ROOM/'room_publication_clock.cpp'),str(ROOM/'room_json.cpp'),'-o',str(cls.binary)],check=True)
 def oracle(self,scenario):
  state=types.SimpleNamespace(bot_source_time_us=None,bot_source_receipt_time_us=None,
   bot_source_batch_horizon_us=None,motion_time_offset_us=0,bot_launch_clock_offset_us=None,
   bot_state_time_us=scenario.get('mapped',0),time=scenario.get('time',10000))
  state._logical_motion_time_us=lambda raw:min(10**16,max(0,raw)+state.motion_time_offset_us)
  state._server_time_ms=lambda:state.time
  state._set_protocol_reject=lambda kind,code,detail:dict(rejected=code)
  output=[]
  for op in scenario['operations']:
   receipt=op.get('receipt',0)
   if op.get('producer_changed'):
    state.bot_source_time_us=state.bot_source_receipt_time_us=state.bot_source_batch_horizon_us=state.bot_launch_clock_offset_us=None
    row=dict(changed=True)
   else:
    state.time=op.get('time',state.time)
    row=self.actual(state,op['message'],receipt)
    if 'rejected' not in row and op.get('commit',True):
     state.bot_state_time_us=row['mapped'];state.motion_time_offset_us=row['motion_offset'];state.bot_launch_clock_offset_us=row['launch_offset']
     if row['source'] is not None:
      state.bot_source_time_us=row['source'];state.bot_source_batch_horizon_us=row['horizon'];state.bot_source_receipt_time_us=receipt
   row.update(state_mapped=state.bot_state_time_us,motion_now=state._logical_motion_time_us(receipt),state_launch_offset=state.bot_launch_clock_offset_us)
   output.append(row)
  return output
 def compare(self,scenario):
  expected=self.oracle(scenario)
  result=subprocess.run([str(self.binary)],input=json.dumps(scenario)+'\n',capture_output=True,text=True,check=True)
  self.assertEqual(expected,json.loads(result.stdout))
 def test_new_source_batch_rebase_uncommitted_and_producer_replacement(self):
  def sample(source,horizon,receipt,**kwargs):return dict(message=dict(sample_time_us=source,source_batch_horizon_us=horizon),receipt=receipt,**kwargs)
  self.compare(dict(mapped=500,operations=[sample(0,500000,1000),sample(300000,500000,1001),sample(500000,500000,1002),sample(900000,900000,1100,commit=False),sample(500001,500001,1101),sample(900000,900000,1102),dict(producer_changed=True,receipt=2000),sample(0,0,2001),sample(100,100,2101)]))
 def test_horizon_order_and_numeric_rejections_do_not_poison_clock(self):
  messages=[{},dict(source_batch_horizon_us=1),dict(sample_time_us=True,source_batch_horizon_us=1),dict(sample_time_us=2,source_batch_horizon_us=1),dict(sample_time_us=0,source_batch_horizon_us=100),dict(sample_time_us=0,source_batch_horizon_us=100),dict(sample_time_us=1,source_batch_horizon_us=99),{},dict(sample_time_us=1,source_batch_horizon_us=100),dict(sample_time_us=2,source_batch_horizon_us=100)]
  self.compare(dict(operations=[dict(message=m,receipt=1000+n) for n,m in enumerate(messages)]))
 def test_random_ordered_batches_and_deferred_rebases(self):
  rng=random.Random(1513);source=0;receipt=0;operations=[]
  for n in range(200):
   source+=rng.choice((1,10000,300000,900000));receipt+=rng.randrange(1,60000)
   operations.append(dict(message=dict(sample_time_us=source,source_batch_horizon_us=source),receipt=receipt,commit=rng.randrange(4)!=0))
  self.compare(dict(operations=operations))
if __name__=='__main__':unittest.main()
