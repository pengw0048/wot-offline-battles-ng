"""Differential local spotting schedule/AOI and native-environment seam tests."""
import importlib.util
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
FIXTURE = r'''
import ast,math,sys,types
import environment_test as native
source=sys.argv[1]+'/src/res/scripts/client/gui/mods/offline_lan_0922'
spotting=types.ModuleType('spotting');exec(compile(open(source+'/spotting.py').read(),source+'/spotting.py','exec'),spotting.__dict__)
parsed=ast.parse(open(source+'/battle_runtime.py').read())
methods=set(('_update_spotting','_spotting_probe_phase','_spotting_probe_due','_record_still_seconds','_spot_presentation_visibility','_apply_spot_presentation','_publish_spotted_targets','_apply_team_observation'))
cls=next(n for n in parsed.body if isinstance(n,ast.ClassDef) and n.name=='BattleRuntime')
cls.body=[n for n in cls.body if isinstance(n,ast.FunctionDef) and n.name in methods]
module=ast.Module(body=[cls]);ast.fix_missing_locations(module)
ns=dict(spotting=spotting,math=math,SPOTTING_PHASE_BUCKETS=5,SPOTTING_UPDATE_SECONDS=.1,SPOTTING_PROBE_SECONDS=.5,
 _number=lambda v,f=0.:float(v if v is not None else f),_xyz=lambda v:v,
 _distance_2d=lambda a,b:math.hypot(a[0]-b[0],a[2]-b[2]))
exec(compile(module,'original-spotting-subset','exec'),ns)
class O(object):
 def __init__(self,**kw):self.__dict__.update(kw)
old=ns['BattleRuntime']();old.client=O(team=1,player_id=1);old._next_spotting_time=0.;old._radio_ally_contacts=set();old._spotted_signature=None
clock=[0.];old._clock=lambda:clock[0];probes=[];transitions=[];entities={12:O(position=(0.,0.,0.),typeDescriptor=O()),13:O(position=(0.,0.,0.),typeDescriptor=O())}
old._server_entity=lambda id:entities.get(id)
old._records={'player:1':dict(local=True,state=dict(team=1,alive=True)),
 'bot:2':dict(kind='bot',network_id=2,engine_id=12,presentation=True,ready=True,spot_visible=False,spot_marker_visible=False,state=dict(team=2,alive=True,speed=0.)),
 'bot:3':dict(kind='bot',network_id=3,engine_id=13,presentation=True,ready=True,spot_visible=False,spot_marker_visible=False,state=dict(team=1,alive=True,speed=0.))}
old._record_alive=lambda record,entity:record.get('state',{}).get('alive',True)
old._presentation_origin=lambda:origin[0];old._spg_aiming_view_active=lambda:spg[0]
old._spotting_observers=lambda:((origin[0],None,None,0.,True) if living[0] else None,)
old._player_effective_snapshot=lambda state:None
old._start_message=dict(round_id=1)
def sight(observer,target,descriptor,*args,**kwargs):
 key='bot:2' if target is entities[12].position else 'bot:3'
 probes.append(key)
 return target[0]<400. and clock[0]%3.<.8
old._spot_line_of_sight=sight
def apply(record,visible,marker_visible=None):
 if marker_visible is None:marker_visible=record.pop('_spot_marker_transition',visible)
 previous=(record.get('spot_visible',False),record.get('spot_marker_visible',False))
 record['spot_visible']=bool(visible);record['spot_marker_visible']=bool(marker_visible)
 if previous!=(bool(visible),bool(marker_visible)):transitions.append((record['network_id'],bool(visible),bool(marker_visible)))
 return visible
old._set_record_spot_visibility=apply
origin=[(0.,0.,0.)];spg=[False];living=[True];owner=native.create();native_state={2:(False,False),3:(False,False)}
def compare(result,index,radio=False):
 for key,model,marker in result['transitions']:native_state[int(key.split(':')[1])]=(model,marker)
 for id in (2,3):
  record=old._records['bot:'+str(id)]
  expected=(bool(record.get('spot_visible')),bool(record.get('spot_marker_visible')))
  assert native_state[id]==expected,(index,radio,id,native_state[id],expected,result,record)
 if not radio:assert sorted(row[0] for row in result['probes'])==sorted(probes),(index,result['probes'],probes)
for i in range(1400):
 clock[0]=i*.037+(1.75 if i>=600 else 0.)
 living[0]=not (800<=i<920);spg[0]=500<=i<570
 origin[0]=(450. if 820<=i<920 else 0.,0.,0.)
 x=568. if 350<=i<430 else 571. if 430<=i<500 else 250. if i%160<80 else 600.
 entities[12].position=(x,0.,0.);entities[13].position=(575. if i%100<50 else 200.,0.,0.)
 old._records['player:1']['state']['alive']=living[0]
 enemy=old._records['bot:2'];enemy['state']['alive']=not(1000<=i<1100);enemy['dead_marker_known']=1000<=i<1100
 probes[:]=[]
 old._update_spotting(clock[0])
 result=native.frame(owner,clock[0],int(living[0]),int(spg[0]),0,origin[0][0],x,entities[13].position[0],int(enemy['state']['alive']),int(enemy['dead_marker_known']),0.)
 compare(result,i)
 if i%47==0:
  lease=5. if i%94==0 else 0.;ally=i%141==0
  old._apply_team_observation(dict(type='bot_observation',contacts=[dict(observing_team=1,target_kind='bot',target_id=2,time_left=lease,visible=lease>0.,visible_by_player_ids=[1])],radio_links=[dict(kind='human',id=1,allies=[dict(kind='bot',id=3)] if ally else [])]),clock[0])
  native.radio(owner,clock[0],lease,int(ally))
  result=native.frame(owner,clock[0],int(living[0]),int(spg[0]),2,origin[0][0],x,entities[13].position[0],int(enemy['state']['alive']),int(enemy['dead_marker_known']),0.)
  compare(result,i,True)
print('native visible spotting original-law differential passed: 1400 frames')
'''


class NativeClientEnvironmentTest(unittest.TestCase):
    def test_pinned_seams(self):
        source = os.environ.get('WOT_FORK_0101_SOURCE')
        if not source:
            self.skipTest('fixed gameplay source is required')
        spec = importlib.util.spec_from_file_location('environment_bindings', ROOT / 'bigworld/stock_native_client_environment_bindings.py')
        bindings = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(bindings)
        path = Path(source) / 'src/res/scripts/client/gui/mods/offline_lan_0922/battle_runtime.py'
        prepared = bindings.battle(path.read_text())
        compile(prepared, str(path), 'exec')
        self.assertIn('native_client_environment.radio(self, message, now)', prepared)
        self.assertIn('native_client_environment.activate(self, chunk_id, item_index)', prepared)

    def test_spotting_differential(self):
        source = os.environ.get('WOT_FORK_0101_SOURCE')
        if not source:
            self.skipTest('fixed gameplay source is required')
        py27 = subprocess.check_output(['pyenv', 'which', 'python2.7'], text=True, env=dict(os.environ, PYENV_VERSION='2.7.18')).strip()
        prefix = subprocess.check_output([py27, '-c', 'import sys;print(sys.prefix)'], text=True).strip()
        with tempfile.TemporaryDirectory(prefix='client-environment-') as directory:
            folder = Path(directory)
            (folder / 'fixture.py').write_text(FIXTURE)
            result = subprocess.run(['c++', '-std=c++17', '-Wno-register', '-bundle', '-undefined', 'dynamic_lookup', '-isystem', str(Path(prefix) / 'include/python2.7'), '-I' + str(ROOT / 'bigworld'), '-I' + str(Path(source) / 'native'), str(ROOT / 'tests/stock_native_client_environment_host.cpp'), '-o', str(folder / 'environment_test.so')], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            result = subprocess.run([py27, '-B', str(folder / 'fixture.py'), source], cwd=str(folder), capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == '__main__':
    unittest.main()
