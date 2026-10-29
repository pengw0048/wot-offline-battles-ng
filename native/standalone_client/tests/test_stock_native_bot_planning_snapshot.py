"""Cold CAPI reader ownership, exact descriptor projection and startup guards."""
import json
import importlib.util
import sys
import types
from unittest.mock import patch
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]

FIXTURE=r'''
import types,sys,json,gc
import planning_snapshot_test as native
class Box(object):
    def __init__(self,**kw):self.__dict__.update(kw)
calls=[]
sys.path.insert(0,sys.argv[2]+'/src/res/scripts/client')
for name in ('gui','gui.mods'):
    m=types.ModuleType(name);m.__path__=[sys.path[0]+'/'+name.replace('.','/')];sys.modules[name]=m
from gui.mods.offline_lan_0922 import vehicle_physics as physics,destructibles_sensor as destructibles,spotting,loadout
for name in ('items','items.utils','items.tankmen','items.vehicles','items.qualifiers','items.VehicleDescrCrew','VehicleQualifiersApplier','constants'):
    sys.modules[name]=types.ModuleType(name)
sys.modules['items'].__path__=[]
for name in ('utils','tankmen','vehicles','qualifiers','VehicleDescrCrew'):setattr(sys.modules['items'],name,sys.modules['items.'+name])
utils=sys.modules['items.utils'];utils.generateDefaultCrew=lambda descriptor,level:['crew']
utils.makeDefaultVehicleAttributeFactors=lambda:{'invisibility':[.01,1.]}
sys.modules['items.tankmen'].MAX_SKILL_LEVEL=100
sys.modules['constants'].VEHICLE_TTC_ASPECTS=Box(DEFAULT=0,WHEN_STILL=1)
sys.modules['items.qualifiers'].QUALIFIER_TYPE=Box(MAIN_SKILL=0)
sys.modules['VehicleQualifiersApplier'].VehicleQualifiersApplier=lambda *args:{0:{}}
class Crew(object):
    camouflageFactor=1.
    def __init__(self,descriptor,*args,**kwargs):self.d=descriptor
    def onCollectFactors(self,f):f.update(self.d.factor_values)
    def onCollectShotDispersionFactors(self,f):pass
sys.modules['items.VehicleDescrCrew'].VehicleDescrCrew=Crew
class Device(object):
    def __init__(self,name):self.name=name;self.circularVisionRadiusFactor=370./350.;self.activateWhenStillSec=5. if name=='stereoscope' else 3.
    def updateVehicleAttrFactors(self,descriptor,factors,aspect):
        if self.name=='camouflageNet':factors['invisibility']=[.11,1.2] if aspect else [.01,1.]
def bomb(*args,**kwargs):raise AssertionError('Offline Python cold business callback')
physics.derive_params=bomb;destructibles._vehicle_hull_bbox=bomb;spotting.descriptor_check_points=bomb
class Control(object):_open=False
state=dict(id=7,team=1,slot=2,x=3.,y=4.,z=5.,yaw=.2,pitch=.1,roll=.05,speed=2.,alive=True,half_length=3.5,half_width=1.7,collision_shape=(1.8,3.6,-.1,2.),fire_seq=9)
profile=dict(class_tag='mediumTank',dominant_role='support',desired_range=220.,fire_range=450.,armor=100.,roles={'support':.8},shells=[dict(index=0,kind='ARMOR_PIERCING',penetration=150.,damage=200.)])
agent=dict(id=7,team=1,seed=1513,profile=profile,personality=dict(aggression=.5,caution=.6,initiative=.7,teamwork=.8),route=dict(id='r',waypoints=[(0.,0.),(12.25,40.75)]),waypoint_index=1,route_join_index=1,route_join_anchor=(1.,2.,3.))
grid=Box(prebaked=True,_baked_origin=(0.,0.),cell_size=4.,_baked_width=2,_baked_height=2,_baked_heights=[1000,None,2000,3000],_baked_links=[1,0,3,2],_baked_hazards=[0,0,4,0],bounds=(-4.,-4.,8.,8.),_baked_max_grade=.3,heuristic_weight=1.7,max_grade_up=.4,max_grade_down=.6,_failed_edges={},_static_hull_edges={},_native_review_cells=set())
navigator=Box(grid=grid,searches={},bot_states={},paths={},bot_direct_progress={},bot_failed_edges={},bot_macro_edges={},search_frame_open=False)
driver=Box(states={7:dict(team_slot=2,last_position=(3.,5.),stuck_time=1.2,clock=3.,short_translation_escape=dict(start=(1.,2.,3.),yaw=.2,sign=1.,distance=4.,until=5.))})
adapter=Box(director=Box(agents={7:agent},bases={1:(0.,0.),2:(100.,200.)},contacts={1:{},2:{}}),driver=driver)
descriptor=Box(chassis=Box(hullPosition=(0.,1.,0.),rotationSpeed=.5,hitTester=Box(bbox=((-2.,0.,-3.),(2.,2.,3.)))),hull=Box(turretPositions=[(0.,2.,0.)],hitTester=Box(bbox=((-2.,0.,-3.),(2.,2.,3.)))),turret=Box(circularVisionRadius=350.,gunPosition=(0.,1.,1.),hitTester=Box(bbox=((-1.,0.,-1.),(1.,1.,1.)))),gun=Box(staticTurretYaw=None,invisibilityFactorAtShot=.7),type=Box(crewRoles=[('commander',)],invisibility=(.2,.3)),optionalDevices=[Device('stereoscope'),Device('camouflageNet')],miscAttrs={},radio=Box(distance=500.),physics=dict(weight=42000.,speedLimits=(12.,4.)),factor_values={'circularVisionRadius':370./350.,'camouflage':1.,'radio/distance':1.})
runtime=Box(adapter=adapter,navigator=navigator,baked_graph={'directions':[(1,0),(1,1),(0,1),(-1,1),(-1,0),(-1,-1),(0,-1),(1,-1)],'bake':dict(vehicle_half_width=2.15,edge_clearance_radii=[1.,3.,2.])},states={7:state},_descriptors={7:descriptor},_native_simulation=Box(control=Control()),_traffic_coordinator=Box(),_server_orders={7:dict(target_id=100,combat_mode='engage',aim_position=(0.,0.,100.),route_id='r',route_index=1,route_join=True,defense_base_id='enemy')},_server_order_tokens={7:12})
runtime._bot_ratings={7:.34};runtime._bot_behavior={};runtime.bot_crew_level=lambda actor:90
import ast,math,copy,imp
laws=types.ModuleType('gui.mods.offline_lan_0922.bot_runtime');laws.__dict__.update(loadout=loadout,spotting=spotting,math=math)
laws._bot_default_crew_factors=lambda d,level:loadout.attribute_factors(d,crew_level=level)
wanted=set(('_number','_value','_bot_profile','_base_invisibility','_shot_invisibility_factor','_view_range','_vision_range_pair'))
tree=ast.parse(open(sys.path[0]+'/gui/mods/offline_lan_0922/bot_runtime.py').read());exec(compile(ast.Module([node for node in tree.body if isinstance(node,ast.FunctionDef) and node.name in wanted]),'<fixed perception laws>','exec'),laws.__dict__)
sys.modules[laws.__name__]=laws;sys.modules['gui.mods.offline_lan_0922'].bot_runtime=laws
helper=imp.load_source('fixed_perception',sys.argv[3]);cases=[]
for view in (20.,350.,500.):
 for binocular in (False,True):
  for net in (False,True):
   d=copy.deepcopy(descriptor);d.turret.circularVisionRadius=view;d.optionalDevices=([Device('stereoscope')] if binocular else [])+([Device('camouflageNet')] if net else []);d.miscAttrs={'circularVisionRadiusFactor':1.1,'invisibilityFactor':.8};d.factor_values['circularVisionRadius']=.85;d.factor_values['radio/distance']=1.3
   expected=helper.freeze_perception(runtime,state,d)[0][:17];cases.append((d,expected))
for module in (laws,loadout,spotting):
 for name,v in list(vars(module).items()):
  if callable(v):setattr(module,name,bomb)
runtime.bot_crew_level=bomb
for d,expected in cases:
 actual=native.perception(runtime,state,d);assert len(actual)==17
 assert all(abs(a-b)<1.e-12 for a,b in zip(actual,expected)),(actual,expected)
result=native.inspect(runtime)
assert result==(2,2,4,1.,2.15,3.,12.25,40.75,42000.,12.,1,1,1,350.,.3,5.,12,1,1),result
assert calls==[],calls
assert native.orders({})==0
assert native.orders({7:dict(move_position=(1.,2.,3.),throttle_override=0.,fire_allowed=True)})==1
for owner,key,value in [(navigator,'searches',{1:object()}),(navigator,'search_frame_open',True),(runtime,'_motion_probe_cache',{7:{}}),(runtime._native_simulation.control,'_open',True)]:
    old=getattr(owner,key,None);setattr(owner,key,value)
    try:native.inspect(runtime)
    except ValueError as error:assert 'bootstrap' in str(error),str(error)
    else:raise AssertionError('active legacy state accepted')
    if old is None:delattr(owner,key)
    else:setattr(owner,key,old)
for bad in ({7:dict(move_position=(1.,2.))},{7:dict(fire_range=float('nan'))}):
    try:native.orders(bad)
    except ValueError:pass
    else:raise AssertionError('bad order accepted')
# Exercise the exact six baked Fjords corridors, including their hold flag.
with open(sys.argv[1], 'rb') as source:fjords=json.load(source)
assert fjords['map']=='33_fjord'
routes=dict((int(team),values) for team,values in fjords['routes'].items())
adapter.director.map_data={'routes':routes}
for team,values in routes.items():
    for route in values:
        agent['route']=route
        parsed=native.inspect(runtime)
        assert parsed[6:8]==tuple(route['waypoints'][1][:2]),(route['id'],parsed[6:8])
parsed_routes=native.routes(runtime)
assert len(parsed_routes)==sum(len(values) for values in routes.values())
for team,values in routes.items():
    for route in values:assert parsed_routes[(team,route['id'])]==[tuple(p[:2]) for p in route['waypoints']]
# The fixed validator/consumer deliberately ignores all trailing fields.
for extra in ((), (False,), (7,), (None, 'unused')):
    agent['route']={'id':'valid','waypoints':[(0.,0.)+extra,(12.25,40.75)+extra]}
    assert native.inspect(runtime)[6:8]==(12.25,40.75)
agent['route']={'id':'numeric_strings','waypoints':[('0','0'),('12.25','40.75')]}
assert native.inspect(runtime)[6:8]==(12.25,40.75)
for bad in ([1.], [None,2.], [float('nan'),2.], ['not-a-number',2.], {'x':1.,'z':2.}):
    agent['route']={'id':'invalid','waypoints':[bad]}
    try:native.inspect(runtime)
    except ValueError:pass
    else:raise AssertionError('invalid route schema accepted')
print(json.dumps({'snapshot':result,'cold_calls':calls}))
'''

class PlanningSnapshotTests(unittest.TestCase):
    def test_clean_perception_coefficients_are_derived_per_mode(self):
        spec=importlib.util.spec_from_file_location('planning_freeze',ROOT/'tests/stock_native_bot_planning_reference.py')
        helper=importlib.util.module_from_spec(spec);spec.loader.exec_module(helper)
        calls=[]
        profile=dict(invisibility_moving=(.1,1.1),invisibility_still=(.3,1.2),has_camouflage_net=True,camouflage_net_delay=3.)
        laws=types.SimpleNamespace(_bot_profile=lambda d,c:profile,
            _base_invisibility=lambda d,p:(d['moving'],d['still']),
            _shot_invisibility_factor=lambda d:d['shot'],
            _vision_range_pair=lambda d,c:(d['view'],d['view']*1.1,5.),
            _bot_default_crew_factors=lambda d,c:dict(crew=c),
            _number=float,_value=lambda d,k,default=None:d.get(k,default),
            loadout=types.SimpleNamespace(modifiers=lambda d,factors:dict(radio_factor=d['radio_factor'])))
        packages={}
        for name in ('gui','gui.mods','gui.mods.offline_lan_0922'):
            packages[name]=types.ModuleType(name);packages[name].__path__=[]
        packages['gui.mods.offline_lan_0922'].bot_runtime=laws
        runtime=types.SimpleNamespace(bot_crew_level=lambda actor:100.)
        state=dict(id=7,team=1,slot=2,critical={'destroyed':['radioHealth','surveyingDeviceHealth']})
        with patch.dict(sys.modules,packages):
            rows=[helper.freeze_perception(runtime,state,dict(moving=.2,still=.4,shot=.7,view=view,radio=dict(distance=500.),radio_factor=1.2))[0] for view in (350.,420.)]
        self.assertEqual([row[4] for row in rows],[350.,420.])
        self.assertEqual([row[15] for row in rows],[600.,600.])
        self.assertEqual(rows[0][7:9],(.4,.2))
    def test_cpython27_cold_snapshot_and_preconditions(self):
        source=os.environ.get('WOT_FORK_0101_SOURCE')
        if not source:self.skipTest('Fixed v0.10.1 source input is required')
        py27=os.environ.get('WOT_PYTHON27') or subprocess.check_output(['pyenv','which','python2.7'],env=dict(os.environ,PYENV_VERSION='2.7.18'),text=True).strip()
        prefix=subprocess.check_output([py27,'-c','import sys;print(sys.prefix)'],text=True).strip()
        with tempfile.TemporaryDirectory(prefix='planning-snapshot-') as folder:
            binary=Path(folder)/'planning_snapshot_test.so';script=Path(folder)/'fixture.py';script.write_text(FIXTURE)
            law=(Path(source)/'native/offline_release_drive_python.inc').read_text().split('PyObject *WOT_CDECL release_drive(')[0]
            (Path(folder)/'offline_release_drive_law.hpp').write_text('#pragma once\n#include <cmath>\n#include <algorithm>\n'+law)
            command=['c++','-std=c++17','-O1','-Wall','-Wextra','-Werror','-bundle','-undefined','dynamic_lookup','-isystem',str(Path(prefix)/'include/python2.7'),'-I'+str(folder),'-I'+str(Path(source)/'native'),'-I'+str(ROOT/'bigworld'),str(ROOT/'bigworld/stock_native_bot_planning_snapshot.cpp'),str(ROOT/'bigworld/stock_native_bot_parking_snapshot.cpp'),str(ROOT/'bigworld/stock_native_bot_parking.cpp'),str(ROOT/'bigworld/stock_native_bot_descriptor.cpp'),str(ROOT/'bigworld/stock_native_bot_motion_snapshot.cpp'),str(ROOT/'bigworld/stock_native_bot_motion.cpp'),str(ROOT/'tests/stock_native_bot_planning_snapshot_host.cpp'),'-o',str(binary)]
            built=subprocess.run(command,capture_output=True,text=True);self.assertEqual(built.returncode,0,built.stderr)
            run=subprocess.run([py27,'-B',str(script),str(Path(source)/'navgraphs/33_fjord.json'),source,str(ROOT/'tests/stock_native_bot_planning_reference.py')],capture_output=True,text=True);self.assertEqual(run.returncode,0,run.stdout+run.stderr)
            result=json.loads(run.stdout);self.assertEqual(result['snapshot'][-1],1)

if __name__=='__main__':unittest.main()
