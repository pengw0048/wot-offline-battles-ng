"""Native human crew/fire/module matrix versus the fixed control producer."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
FIXTURE=r'''
import ast,copy,imp,math,os,random,sys,types
import human_projection_test as native
base=os.path.join(sys.argv[1],'src/res/scripts/client/gui/mods/offline_lan_0922')
class Box(object):
    def __init__(self,**kw):self.__dict__.update(kw)
for name in ('gui','gui.mods','gui.mods.offline_lan_0922'):
    module=types.ModuleType(name);module.__path__=[];sys.modules[name]=module
def functions(file,names,env):
    tree=ast.parse(open(os.path.join(base,file)).read())
    nodes=[n for n in tree.body if isinstance(n,ast.FunctionDef) and n.name in names]
    exec compile(ast.Module(nodes),file,'exec') in env
effects=dict(math=math,_valid_skill_name=lambda name:True)
functions('effective_params.py',('_crew_projection','_knocked_out_instances','living_skill_states','living_skill_count','living_skill_level'),effects)
spotting=dict(PROXIMITY_SPOT_DISTANCE=50.)
functions('spotting.py',('effective_view_range',),spotting)
damage=imp.load_source('projection_device_damage',os.path.join(base,'device_damage.py'))
laws=types.ModuleType('gui.mods.offline_lan_0922.bot_runtime');sys.modules[laws.__name__]=laws;sys.modules['gui.mods.offline_lan_0922'].bot_runtime=laws
laws.math=math;laws.effective_params=Box(**effects);laws.spotting=Box(**spotting);laws.device_damage=damage
laws._value=lambda obj,name,default=None:obj.get(name,default) if isinstance(obj,dict) else getattr(obj,name,default)
laws._number=lambda value,default=0.:float(value) if value is not None else default
laws._CRITICAL_PARTS_TICK_CACHE='_test_critical_cache'
laws._player_effective_params=lambda source,tick=None:source['effective_params']
functions('bot_runtime.py',('_player_dynamic_spotting','_player_spotting_perk','_parse_critical_parts','_critical_parts'),laws.__dict__)
tree=ast.parse(open(os.path.join(base,'bot_runtime.py')).read());methods=set(('_spotting_profile','_source_radio_range'))
owner=next(n for n in tree.body if isinstance(n,ast.ClassDef) and any(isinstance(m,ast.FunctionDef) and m.name in methods for m in n.body))
owner.name='Runtime';owner.bases=[ast.Name(id='object',ctx=ast.Load())];owner.decorator_list=[];owner.body=[n for n in owner.body if isinstance(n,ast.FunctionDef) and n.name in methods]
exec compile(ast.fix_missing_locations(ast.Module([owner])),'runtime-projection','exec') in laws.__dict__
control={ '_key':lambda source:(0,int(source.get('network_id',source['id']))) }
tree=ast.parse(open(os.path.join(base,'native_control_core.py')).read())
owner=next(n for n in tree.body if isinstance(n,ast.ClassDef) and any(isinstance(m,ast.FunctionDef) and m.name=='_project_actor_config' for m in n.body))
owner.name='Control';owner.bases=[ast.Name(id='object',ctx=ast.Load())];owner.decorator_list=[];owner.body=[n for n in owner.body if isinstance(n,ast.FunctionDef) and n.name=='_project_actor_config']
exec compile(ast.fix_missing_locations(ast.Module([owner])),'fixed-projection','exec') in control
rng=random.Random(1513);roster=['commander','driver','gunner','radioman','loader','loader1'];rows_checked=0
for crew_count in range(1,7):
 for variant in range(5):
    names=roster[:crew_count];members=[]
    for name in names:
        skills=[dict(name=skill,level=rng.choice((0.,50.,99.,100.)),active=rng.choice((False,True)),enabled=rng.choice((False,True))) for skill in ('radioman_retransmitter','radioman_lasteffort','gunner_rancorous')]
        members.append(dict(instance=name,roles=['commander','driver','gunner','radioman','loader'],skills=skills))
    states={}
    for mask in range(1<<crew_count):
      for fire in (0,1):states['%d:%d'%(mask,fire)]=dict(vision=rng.uniform(0.,2.),signal=rng.uniform(0.,2.),camouflage=rng.uniform(0.,2.),base_moving=rng.uniform(0.,.5),base_still=rng.uniform(0.,.5),invisibility_moving=[rng.uniform(-.2,.5),rng.uniform(.2,2.)],invisibility_still=[rng.uniform(-.2,.5),rng.uniform(.2,2.)])
    snapshot=dict(crew=dict(members=members,dynamic_spotting=dict(crew=names,states=states)),spotting=dict(camouflage_factor=.57,vision_factor=rng.uniform(.5,2.),binocular_factor=rng.uniform(.5,1.5),binocular_delay=3.,camouflage_net_delay=3.,has_binoculars=variant%2==0,has_camouflage_net=variant%3==0),camouflage=dict(shot_factor=rng.uniform(.1,1.)),loadout=dict(radio_factor=rng.uniform(.2,2.)))
    descriptor=dict(turret=dict(circularVisionRadius=rng.uniform(20.,500.),surveyingDeviceHealth=dict(maxHealth=100.,maxRegenHealth=50.)),miscAttrs=dict(circularVisionRadiusFactor=rng.uniform(.5,2.)),radio=dict(distance=rng.uniform(0.,1000.)))
    if variant==4:descriptor.pop('miscAttrs');descriptor['turret'].pop('circularVisionRadius')
    raw=dict(id=7,team=1,slot=2,vehicle='ussr:test',effective_params=snapshot)
    runtime=laws.Runtime();runtime._spotting_profiles={};runtime._player_vehicle_profile=lambda source,tick=None:dict(descriptor=descriptor)
    oracle=control['Control']();oracle.runtime=runtime;expected=[]
    for mask in range(1<<crew_count):
      for fire in (False,True):
       for state in range(3):
        critical=dict(crew_ko=[name for index,name in enumerate(names) if mask&(1<<index)],fire=fire,devices=[],destroyed=[])
        if state:critical['devices']=[dict(name='surveyingDeviceHealth',hp=0. if state==2 else 25.,state='destroyed' if state==2 else 'critical')]
        if state==2:critical['destroyed']=['surveyingDeviceHealth']
        expected.append(oracle._project_actor_config(dict(raw,kind='human',network_id=7,critical=critical),None))
    actual=native.project(raw,snapshot,descriptor)
    assert len(actual)==len(expected)
    for a,b in zip(actual,expected):
        assert a[1:]==b[1:],(a,b)
        for x,y in zip(a[0],b[0]):assert abs(x-y)<=1.e-10*max(1.,abs(x),abs(y)),(crew_count,variant,x,y)
    rows_checked+=len(actual)
    # No Python producer or control object participates in native projection.
    laws._player_effective_params=lambda *args:(_ for _ in ()).throw(AssertionError('Python producer'))
    assert native.project(raw,snapshot,descriptor)==actual
    laws._player_effective_params=lambda source,tick=None:source['effective_params']
    malformed=copy.deepcopy(snapshot);del malformed['crew']['dynamic_spotting']['states']['0:0']
    try:native.project(raw,malformed,descriptor)
    except (ValueError,TypeError):pass
    else:raise AssertionError('missing exact crew/fire state was fabricated')

# Review the remaining cold producer as raw descriptor data, with all mod
# producers deliberately unavailable. NativeVectorLike reproduces the real
# #1513 indexing ABI rather than passing every point as a tuple.
class Vector(object):
    def __init__(self,*values):self.values=values
    def __getitem__(self,index):return self.values[index]
physics=types.ModuleType('gui.mods.offline_lan_0922.vehicle_physics')
physics_env=dict(math=math)
physics_names=('GRAVITY','SLIDE_HOLD_TAN','SLOPE_GRIP_SDW_MIN_Y','SLOPE_GRIP_SDW_FULL_Y','SLOPE_GRIP_SDW_MIN','SLOPE_GRIP_SDW_FULL','GRAVITY_FACTOR')
tree=ast.parse(open(os.path.join(base,'vehicle_physics.py')).read())
for node in tree.body:
    if isinstance(node,ast.Assign) and len(node.targets)==1 and isinstance(node.targets[0],ast.Name):
        try:physics_env[node.targets[0].id]=eval(compile(ast.Expression(node.value),'physics-constant','eval'),physics_env)
        except Exception:pass
for key in physics_names:setattr(physics,key,physics_env[key])
sys.modules[physics.__name__]=physics
functions('vehicle_physics.py',('destructible_drive_speed_cap','_value'),physics_env)
checkpoints_env={}
functions('spotting.py',('descriptor_check_points',),checkpoints_env)
shot=imp.load_source('gui.mods.offline_lan_0922.shot_geometry',os.path.join(base,'shot_geometry.py'))
sys.modules['gui.mods.offline_lan_0922'].shot_geometry=shot
sensor={}
functions('destructibles_sensor.py',('_descriptor_value','_vehicle_hull_bbox','_vehicle_body_bbox'),sensor)
class MappingDescriptor(dict):
    def __getattr__(self,key):
        try:return self[key]
        except KeyError:raise AttributeError(key)
laws._player_effective_params=lambda *args:(_ for _ in ()).throw(AssertionError('Python effective producer'))
physics.derive_params=physics.track_pivot_descriptor_params=lambda *args:(_ for _ in ()).throw(AssertionError('Python physics producer'))
for index in range(60):
    lo=tuple(rng.uniform(-3.,-.5) for unused in range(3));hi=tuple(rng.uniform(.5,3.) for unused in range(3))
    def component(lower,upper):return MappingDescriptor(hitTester=Box(bbox=(Vector(*lower),Vector(*upper))),primaryArmor=[rng.uniform(0.,150.),rng.uniform(0.,150.)])
    ch=component(lo,hi);ch.hullPosition=Vector(.2,1.1,-.3);ch.rotationIsAroundCenter=index%2==0
    # Use dictionary fields, matching the component mapping adapter.
    ch['hullPosition']=ch.hullPosition;ch['rotationIsAroundCenter']=ch.rotationIsAroundCenter
    hull=component(tuple(v*.7 for v in lo),tuple(v*.6 for v in hi));hull['turretPositions']=[Vector(.1,.9,-.2)]
    turret=component(tuple(v*.5 for v in lo),tuple(v*.5 for v in hi));turret['gunPosition']=Vector(.1,.2,1.4);turret['circularVisionRadius']=410.;turret['surveyingDeviceHealth']=MappingDescriptor(maxHealth=90.,maxRegenHealth=45.)
    d=MappingDescriptor(chassis=ch,hull=hull,turret=turret,gun=MappingDescriptor(),physics={'weight':23000.,'speedLimits':(18.,7.),'trackCenterOffset':-1.2},radio=MappingDescriptor(distance=700.,maxHealth=60.,maxRegenHealth=30.),type=MappingDescriptor(tags=frozenset(('heavyTank','SPG'))),miscAttrs={})
    if index%3==0:d['defaultVehicleDescr']=MappingDescriptor(physics={'speedLimits':(25.,9.)})
    if index%4==0:d['physics'].pop('speedLimits')
    snapshot['physics']=dict(mass=20000.,rotSpd=.4,terrainResist=(1.,1.1,1.2),specificFriction=.7,speedFwd=0. if index%5==0 else 12.,speedBwd=0. if index%7==0 else 4.)
    snapshot['ramming']=dict(spall_coefficient=1.,ramming_bonus=.1);snapshot['version']=1
    raw['effective_params']=snapshot
    actual=native.descriptor(raw,d)
    minimum,maximum,unused=sensor['_vehicle_body_bbox'](d)
    assert actual['minimum']==list(minimum) and actual['maximum']==list(maximum)
    expected_shape=[max(abs(lo[0]),abs(hi[0]),.8),max(abs(lo[2]),abs(hi[2]),1.),lo[1],max(hi[1],1.1+hull.hitTester.bbox[1][1])]
    assert actual['shape']==expected_shape
    assert actual['mass']==20000. and actual['contact_mass']==23000.
    assert actual['aim_samples']==8
    assert actual['class_tag']=='heavyTank'
    assert actual['armor']==max(max(hull.primaryArmor),max(turret.primaryArmor))
    assert actual['hp']==[damage.device_max_hp(d,'surveyingDeviceHealth'),damage.device_max_hp(d,'radioHealth')]
    expected_points=checkpoints_env['descriptor_check_points'](d)
    assert actual['checkpoints']==[list(v) for v in expected_points],(actual['checkpoints'],expected_points)
    cap=physics_env['destructible_drive_speed_cap']
    expected_caps=[cap(d,dict(speedFwd=d['physics'].get('speedLimits',(32./3.6,12./3.6))[0]),0.),cap(d,snapshot['physics'],1.),cap(d,snapshot['physics'],-1.)]
    assert actual['caps']==expected_caps,(actual['caps'],expected_caps)
    assert actual['pivot']==[1.,float(ch.rotationIsAroundCenter),1.2]

print('human projection source rows passed: %d'%rows_checked)
'''
class HumanProjection(unittest.TestCase):
    def test_native_projection_matches_python27_producer(self):
        source=os.environ.get('WOT_FORK_0101_SOURCE')
        if not source:self.skipTest('set WOT_FORK_0101_SOURCE')
        py27=subprocess.check_output(['pyenv','which','python2.7'],env=dict(os.environ,PYENV_VERSION='2.7.18'),text=True).strip()
        prefix=subprocess.check_output([py27,'-c','import sys;print(sys.prefix)'],text=True).strip()
        with tempfile.TemporaryDirectory(prefix='human-projection-') as directory:
            folder=Path(directory);script=folder/'fixture.py';script.write_text(FIXTURE)
            law=(Path(source)/'native/offline_release_drive_python.inc').read_text().split('PyObject *WOT_CDECL release_drive(')[0]
            (folder/'offline_release_drive_law.hpp').write_text('#pragma once\n#include <algorithm>\n#include <cmath>\n'+law)
            command=['c++','-std=c++17','-O1','-ffp-contract=off','-Wall','-Wextra','-Werror','-bundle','-undefined','dynamic_lookup','-isystem',str(Path(prefix)/'include/python2.7'),'-I'+str(folder),'-I'+str(ROOT/'bigworld'),'-I'+str(Path(source)/'native'),str(ROOT/'bigworld/stock_native_bot_human_snapshot.cpp'),str(ROOT/'bigworld/stock_native_bot_descriptor.cpp'),str(ROOT/'tests/stock_native_human_projection_host.cpp'),'-o',str(folder/'human_projection_test.so')]
            result=subprocess.run(command,capture_output=True,text=True);self.assertEqual(result.returncode,0,result.stderr)
            result=subprocess.run([py27,'-B',str(script),source],capture_output=True,text=True);self.assertEqual(result.returncode,0,result.stderr);self.assertIn('human projection source rows passed',result.stdout)
if __name__=='__main__':unittest.main()
