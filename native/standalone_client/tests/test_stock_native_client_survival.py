"""The exact visible warning clocks, without authority damage or Python policy."""
import ast
import ctypes
import math
import os
from pathlib import Path
import random
import subprocess
import tempfile
import types
import unittest
ROOT=Path(__file__).resolve().parents[1]
class NativeClientSurvivalTests(unittest.TestCase):
 def test_original_warning_clocks_and_publication_edges(self):
  source=os.environ.get('WOT_FORK_0101_SOURCE')
  if not source:self.skipTest('Fixed v0.10.1 source input is required')
  base=Path(source)/'src/res/scripts/client/gui/mods/offline_lan_0922'
  tree=ast.parse((base/'battle_runtime.py').read_text())
  methods=[node for cls in tree.body if isinstance(cls,ast.ClassDef) and cls.name=='BattleRuntime' for node in cls.body if isinstance(node,ast.FunctionDef) and node.name in ('_tick_drowning','_tick_overturn')]
  self.assertEqual(len(methods),2)
  physics=ast.parse((base/'vehicle_physics.py').read_text());helper=next(node for node in physics.body if isinstance(node,ast.FunctionDef) and node.name=='overturn_level_from_up_cosine')
  env=dict(math=math,OVERTURN_WARNING_COSINE=math.cos(math.radians(70.)),OVERTURN_DANGER_COSINE=math.cos(math.radians(80.)))
  exec(compile(ast.Module(body=[helper],type_ignores=[]),'original-overturn-helper','exec'),env)
  env.update(vehicle_physics=types.SimpleNamespace(overturn_level_from_up_cosine=env['overturn_level_from_up_cosine']),_number=lambda value,default=0.:default if value is None else float(value))
  harness=ast.ClassDef(name='Original',bases=[],keywords=[],body=methods,decorator_list=[])
  exec(compile(ast.fix_missing_locations(ast.Module(body=[harness],type_ignores=[])),'original-survival-clocks','exec'),env)
  with tempfile.TemporaryDirectory(prefix='native-player-survival-') as directory:
   binary=Path(directory)/'survival.so';cmd=['c++','-std=c++17','-O1','-shared','-fPIC','-ffp-contract=off','-Wall','-Wextra','-Werror','-I'+str(ROOT/'bigworld'),str(ROOT/'bigworld/stock_native_client_survival.cpp'),str(ROOT/'tests/stock_native_client_survival_host.cpp'),'-o',str(binary)]
   result=subprocess.run(cmd,capture_output=True,text=True);self.assertEqual(result.returncode,0,result.stderr)
   operation=ctypes.CDLL(str(binary)).player_survival_step;ptr=ctypes.POINTER(ctypes.c_double);operation.argtypes=[ptr,ptr,ctypes.c_int,ctypes.c_double,ctypes.c_int,ctypes.c_int,ctypes.c_int,ctypes.c_double,ctypes.c_double,ptr];operation.restype=ctypes.c_int
   original=env['Original']();original.client=types.SimpleNamespace(player_id=1);original._server=types.SimpleNamespace();original._records={'player:1':dict(engine_id=101,state=dict(alive=True,health=100.))};entity=types.SimpleNamespace(health=100.);original._avatar=types.SimpleNamespace();original._server_entity=lambda unused:entity;original._local_position=(0.,0.,0.);original._local_yaw=original._local_pitch=original._local_roll=0.
   fields=('_drown_check','_drown_time','_drown_level','_drown_started','_overturn_check','_overturn_time','_overturn_level','_overturn_started')
   def reset(initial=None):
    values=initial or (0.,0.,0,None,0.,0.,0,None)
    for name,value in zip(fields,values):setattr(original,name,value)
   reset();rng=random.Random(1513);steps=0
   def state():
    return [original._drown_check,original._drown_time,original._drown_level,float(original._drown_started is not None),original._drown_started or 0.,original._overturn_check,original._overturn_time,original._overturn_level,float(original._overturn_started is not None),original._overturn_started or 0.]
   def run(kind,dt,eligible=True,level=0,up=1.,ignore=.1,warning=math.cos(math.radians(70.)),danger=math.cos(math.radians(80.)),guard=None):
    nonlocal steps
    steps+=1;raw=state();original._runtime=types.SimpleNamespace(constants=types.SimpleNamespace(OVERTURN_CONDITION=types.SimpleNamespace(IGNOR_DELAY=ignore,WARNING_COSINE=warning,ONBOARD_COSINE=danger)))
    original._server=types.SimpleNamespace() if eligible else None;entity.health=100.;original._records={'player:1':dict(engine_id=101,state=dict(alive=True,health=100.))};original._server_entity=lambda unused:entity
    if guard=='record':original._records={};eligible=False
    if guard=='dead':original._records['player:1']['state']['alive']=False;eligible=False
    if guard=='health':original._records['player:1']['state']['health']=0.;eligible=False
    if guard=='entity':original._server_entity=lambda unused:None;eligible=False
    if guard=='entity_health':entity.health=0.;eligible=False
    samples=[];clocks=[];events=[];clock=1000.+steps*.037
    original._vehicle_drowning_level=lambda *args:(samples.append(1) or level)
    original._server_clock=lambda:(clocks.append(1) or clock)
    original._present_drowning_level=lambda value,now:events.append(value)
    original._present_overturn_level=lambda value:events.append(value)
    marker=object();entity._offh_drowning=entity._offh_overturned=marker;original._avatar._offh_drowning=original._avatar._offh_overturned=marker
    original._local_surface_up_cosine=up
    if up is None:original._local_pitch=rng.uniform(-4.,4.);original._local_roll=rng.uniform(-4.,4.);actual_up=math.cos(original._local_pitch)*math.cos(original._local_roll)
    else:actual_up=up
    result=original._tick_drowning(dt,clock) if kind==0 else original._tick_overturn(dt,clock)
    arr=lambda values:(ctypes.c_double*len(values))(*values)
    output=(ctypes.c_double*16)();self.assertEqual(operation(arr([ignore,warning,danger]),arr(raw),kind,dt,int(eligible),int(level is not None),level or 0,actual_up,clock,output),1)
    actual=list(output);self.assertEqual(state(),actual[:10],(steps,kind,dt,up,raw,state(),actual))
    flagname='_offh_drowning' if kind==0 else '_offh_overturned';sampled=getattr(entity,flagname) is not marker
    self.assertEqual(sampled,bool(actual[10]));self.assertIs(getattr(original._avatar,flagname),getattr(entity,flagname))
    if sampled:self.assertEqual(getattr(entity,flagname),bool(actual[11]))
    self.assertEqual(events,[int(actual[13])] if actual[12] else [])
    self.assertEqual(len(samples),int(actual[14]));self.assertEqual(len(clocks),int(actual[15]))
    self.assertEqual(result,bool(events) if kind==0 else False)
   # Cadence boundaries reset to zero, retaining every elapsed hitch second.
   for level in (0,1,2,None,2,0):
    for dt in (0.,-.1,.099999,.1,.100001,.299999,.3,.300001,.7,12.,40.):run(0,dt,level=level)
   # Persistent danger spans unavailable samples and dead/ineligible intervals.
   reset();run(0,.3,level=2);run(0,9.,level=None)
   for guard in ('record','dead','health','entity','entity_health'):run(0,20.,level=0,guard=guard)
   run(0,2.,level=2);self.assertEqual(original._drown_time,2.3)
   # Exact cosine equality is danger/caution. The ignore-delay epsilon applies
   # cumulatively; caution/danger transitions do not reset that check clock.
   for ignore in (-.1,0.,.1,.3):
    for threshold in (-1.,math.cos(math.radians(80.)),math.cos(math.radians(70.)),1.):
     reset()
     for up in (threshold-1.e-12,threshold,threshold+1.e-12,None,2.,-2.):
      for dt in (0.,.000001,.099998,.000001,.1,.3,5.,31.):run(1,dt,up=up,ignore=ignore)
   reset();run(1,.1,up=-1.);run(1,100.,up=-1.);self.assertEqual(original._overturn_time,30.)
   for guard in ('record','dead','health','entity','entity_health'):run(1,20.,up=1.,guard=guard)
   run(1,.1,up=1.);self.assertEqual(original._overturn_time,0.)
   for index in range(2400):
    kind=index%2;dt=rng.choice((0.,.016,.033,.1,.3,.7,2.,35.));eligible=index%29!=0
    run(kind,dt,eligible,level=rng.choice((None,0,1,2)),up=rng.choice((None,rng.uniform(-1.3,1.3))))
   print('Visible survival AST differential passed: %d edges' % steps)
if __name__=='__main__':unittest.main()
