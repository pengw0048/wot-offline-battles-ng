"""Real CPython 2.7 journal, raw mirrors, trusted reports, echo and stream replay."""
import os,subprocess,tempfile,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
FIXTURE=r'''
import ast,copy,os,sys,types
import world_events_test as native
class Box(object):
    def __init__(self,**kw):self.__dict__.update(kw)
for name in ('gui','gui.mods','gui.mods.offline_lan_0922'):
    m=types.ModuleType(name);m.__path__=[];sys.modules[name]=m
sensor=types.ModuleType('gui.mods.offline_lan_0922.destructibles_sensor');sensor.g_offh_destr_runtime_space=41
authority=types.ModuleType('gui.mods.offline_lan_0922.destructibles_authority');authority._state=dict(spaceID=41,chunks={},entities={})
geometry=types.ModuleType('gui.mods.offline_lan_0922.native_destructibles');geometry._owner=None;geometry._disabled=False
area=types.ModuleType('AreaDestructibles');area.g_destructiblesAnimator=Box(_DestructiblesAnimator__bodies=[],_DestructiblesAnimator__isUpdating=True,_DestructiblesAnimator__updateCallbackID=42)
bigworld=types.ModuleType('BigWorld');bigworld.cancelCallback=native.cancel;sys.modules['BigWorld']=bigworld
for m in (sensor,authority,geometry,area):sys.modules[m.__name__]=m
battle=Box();packets=native.run(battle)
assert native.cancelled==[(42,)] and area.g_destructiblesAnimator._DestructiblesAnimator__isUpdating is False
assert area.g_destructiblesAnimator._DestructiblesAnimator__updateCallbackID is None
for packet in packets:
    for seq,event in packet:
        assert seq>0 and set(event)==set(('destructible_kind','chunk_id','item_index','x','y','z','fall_yaw','speed','is_shot'))
        assert event['is_shot'] is False
assert packets[0][0][1]==dict(destructible_kind='fragile',chunk_id=1,item_index=7,x=0.,y=1.,z=0.,fall_yaw=0.,speed=20.,is_shot=False)
assert packets[1][0][1]==dict(destructible_kind='tree',chunk_id=1,item_index=10,x=4.,y=0.,z=0.,fall_yaw=0.,speed=2.,is_shot=False)
chunk=authority._state['chunks'][1]
assert set(((7,None),(8,None),(9,None),(10,None),(11,None))).issubset(chunk['keys'])
assert all(chunk[key]==[] for key in ('fallenTrees','fallenColumns','destroyedFragiles','destroyedModules'))
assert chunk['native_records'][(7,None)]==7<<8
assert (7,None) in chunk['contactCollisionReady'] and (8,None) in chunk['contactCollisionReady']
assert (9,None) not in chunk['treePresentations']
assert (1,10) in sensor.g_offh_tree_state['canonical_published']
assert sensor.g_offh_destr_instances[(1,11)]['boxes'][0][0][0]==16.
assert sensor.g_offh_destr_spatial_revision==sensor.g_offh_destr_spatial_published_revision
assert sensor.g_offh_destr_native_replacement_bsp_active is True
before=copy.deepcopy(authority._state);native.install(battle);assert authority._state==before
area.g_destructiblesAnimator._DestructiblesAnimator__bodies=[dict(spaceID=41)]
try:native.install(battle)
except ValueError as error:assert 'active stock destructible bodies' in str(error)
else:raise AssertionError('stock animator body silently discarded')
assert area.g_destructiblesAnimator._DestructiblesAnimator__bodies==[dict(spaceID=41)]
area.g_destructiblesAnimator._DestructiblesAnimator__bodies=[]
authority._state=dict(spaceID=40,chunks={9:{'keys':set([(8,None)])}},entities={})
sensor.g_offh_destr_runtime_space=40;sensor.g_offh_destr_instances={(9,8):{}}
sensor._destructible_catalog={'preserve':True}
native.install(battle)
assert authority._state==dict(spaceID=41,chunks={},entities={})
assert not hasattr(sensor,'g_offh_destr_instances') and sensor._destructible_catalog=={'preserve':True}
print('native world events passed')
'''
class WorldEventTests(unittest.TestCase):
    def test_wire_raw_mirrors_echo_and_stream_restore(self):
        source=os.environ.get('WOT_FORK_0101_SOURCE')
        if not source:self.skipTest('fixed gameplay source is required')
        py27=subprocess.check_output(['pyenv','which','python2.7'],env=dict(os.environ,PYENV_VERSION='2.7.18'),text=True).strip();prefix=subprocess.check_output([py27,'-c','import sys;print(sys.prefix)'],text=True).strip()
        with tempfile.TemporaryDirectory(prefix='world-events-') as directory:
            folder=Path(directory);script=folder/'fixture.py';script.write_text(FIXTURE);big=ROOT/'bigworld';native=Path(source)/'native'
            command=['c++','-std=c++17','-O1','-ffp-contract=off','-Wall','-Wextra','-Werror','-bundle','-undefined','dynamic_lookup','-isystem',str(Path(prefix)/'include/python2.7'),'-I'+str(big),'-I'+str(native)]
            command += [str(big/(s+'.cpp')) for s in ('stock_native_world_events','stock_native_destructible_runtime','stock_native_world_pipeline','stock_motion_rotation_batch','stock_offline_query_skin')]
            command += [str(native/'offline_destructible_geometry.cpp'),str(native/'world_stage.cpp'),str(ROOT/'tests/stock_native_world_events_host.cpp'),'-o',str(folder/'world_events_test.so')]
            built=subprocess.run(command,text=True,capture_output=True);self.assertEqual(built.returncode,0,built.stderr)
            run=subprocess.run([py27,'-B',str(script),source],text=True,capture_output=True);self.assertEqual(run.returncode,0,run.stderr);self.assertIn('native world events passed',run.stdout)
if __name__=='__main__':unittest.main()
