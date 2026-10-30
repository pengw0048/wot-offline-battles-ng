"""Native Vehicle descriptors against the pinned closures and caller guards."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
FIXTURE = r'''from __future__ import print_function
import ast,gc,math,random,sys,types,weakref
import offline_client_vehicle_hooks_native as native
path=sys.argv[1]+'/src/res/scripts/client/gui/mods/offline_lan_0922/compat.py'
tree=ast.parse(open(path,'rb').read())
cls=next(n for n in tree.body if isinstance(n,ast.ClassDef) and n.name=='OfflineCompatibility')
install=next(n for n in cls.body if isinstance(n,ast.FunctionDef) and n.name=='install')
names=('vehicle_getattribute','vehicle_setattr','vehicle_get_speed','avatar_get_speeds')
body=[n for n in install.body if isinstance(n,ast.FunctionDef) and n.name in names or isinstance(n,ast.Assign) and any(isinstance(t,ast.Name) and t.id in ('vehicle_overlay_names','vehicle_special_names') for t in n.targets)]
factory=ast.parse('def factory(compatibility,runtime,avatar_type):\n pass').body[0]
factory.body=body+[ast.Return(ast.Tuple([ast.Name(n,ast.Load()) for n in names],ast.Load()))]
proxies=[n for n in tree.body if isinstance(n,ast.ClassDef) and n.name in ('_OfflineVehicleFilterSyncProxy','_OfflineInitialVehicleVisualProvider')]
remote=sys.argv[1]+'/src/res/scripts/client/gui/mods/offline_lan_0922/entities/remote_vehicle.py'
remote_class=next(n for n in ast.parse(open(remote,'rb').read()).body if isinstance(n,ast.ClassDef) and n.name=='_RemoteFilter')
env={'sys':sys};exec(compile(ast.fix_missing_locations(ast.Module(proxies+[remote_class,factory])),path,'exec'),env)
class Bag(object):pass
class Vector(object):
 def __init__(self,*value):
  value=value if len(value)==3 else value[0]
  if isinstance(value,Vector):value=(value.x,value.y,value.z)
  self.x,self.y,self.z=value
 @property
 def length(self):return math.sqrt(self.x*self.x+self.y*self.y+self.z*self.z)
class Matrix(object):
 def __init__(self,value):self.translation=Vector(value.translation if isinstance(value,Matrix) else value)
class Filter(object):
 velocity='stock velocity';acceleration='stock acceleration';groundPlacingMatrix='stock ground'
 def interpolateStabilisedMatrix(self,timestamp):return ('stock matrix',timestamp)
 def syncGunAngles(self,*args):raise AssertionError('unsafe stock gun sync')
 def syncStabilisedYPR(self,*args):raise AssertionError('unsafe stock pose sync')
class Provider(object):
 def __init__(self,trace):self.trace=trace;self.ordinary='provider ordinary'
 def startVehicleVisual(self,*args):self.trace.append(('marker',args))
class Vehicle(object):
 def __init__(self,trace):
  self.health=900;self.isCrewActive=True;self.position=Vector(1.,2.,3.);self.yaw=.2;self.matrix=Matrix((4.,5.,6.));self.speedInfo=('native speed info',);self.filter=Filter();self.trace=trace;self.guiSessionProvider=Provider(trace);self.ordinary=37;self.cell='native cell'
 def show(self,visible):self.trace.append(('show',visible));return visible
 def getSpeed(self):return 99.
class Avatar(object):
 playerVehicleID=7
 def getOwnVehicleSpeeds(self,instantaneous=False):return (91.,int(bool(instantaneous)))
def caller(vehicle,name):return getattr(vehicle,name)
def other_caller(vehicle,name):return getattr(vehicle,name)
def predict(vehicle):return vehicle.position
def visual(vehicle):
 result=vehicle.show(True);vehicle.guiSessionProvider.startVehicleVisual('v',True);return result
def own_speed(avatar,**kwargs):return avatar.getOwnVehicleSpeeds(**kwargs)
runtime=Bag();runtime.math=Bag();runtime.math.Matrix=Matrix;runtime.math.Vector3=Vector;runtime.remote_filter_type=env['_RemoteFilter'];runtime.avatar_module=Bag();runtime.avatar_module.PlayerAvatar=Avatar
code_names=('_gun_rotator_predict_locked_target_code','_vehicle_start_visual_code','_vehicle_start_wg_physics_code','_vehicle_set_gun_angles_code','_avatar_aux_physics_code','_avatar_vehicle_enter_code','_avatar_start_vehicle_visual_code','_gun_rotator_stabilised_code','_camera_acceleration_update_code','_arcade_oscillator_acceleration_code','_sniper_oscillator_acceleration_code','_custom_effect_update_code','_crashed_track_setup_assembler_code','_crashed_track_model_loaded_code','_projectile_segment_may_hit_code')
identity_names=('_vehicle_starting_visual','_vehicle_starting_wg_physics','_vehicle_syncing_gun_angles','_avatar_syncing_aux_physics','_avatar_entering_vehicle')
def setup(use_native):
 c=Bag();c._runtime=runtime;c._battle_active=True;c._vehicle_property_overlays={}
 for n in code_names+identity_names:setattr(c,n,None)
 c._original_vehicle_getattribute=object.__getattribute__;c._original_vehicle_setattr=object.__setattr__;c._original_vehicle_get_speed=Vehicle.getSpeed;c._original_avatar_getattribute=object.__getattribute__;c._original_avatar_get_speeds=Avatar.getOwnVehicleSpeeds
 hooks=[native.create(c,i) for i in range(4)] if use_native else env['factory'](c,runtime,Avatar)
 V=type('FixtureVehicle',(Vehicle,),{'__getattribute__':hooks[0],'__setattr__':hooks[1],'getSpeed':hooks[2]})
 A=type('FixtureAvatar',(Avatar,),{'getOwnVehicleSpeeds':hooks[3]})
 c._battle_active=False;trace=[];v=V(trace);a=A();c._battle_active=True
 return c,v,a,trace
def normalized(value):
 if isinstance(value,Vector):return ('vector',value.x,value.y,value.z)
 if isinstance(value,Matrix):return ('matrix',normalized(value.translation))
 if isinstance(value,(tuple,list)):return tuple(normalized(v) for v in value)
 return value
random.seed(1513)
for iteration in range(1200):
 before=[]
 seed=random.uniform(-10.,10.)
 for use_native in (False,True):
  c,v,a,trace=setup(use_native);runtime.bigworld=Bag();runtime.bigworld.player=lambda:a;runtime.bigworld.entity=lambda unused:v
  c._vehicle_property_overlays[id(v)]={'health':73,'isCrewActive':False,'position':Vector(seed,0.,1.),'yaw':seed,'matrix':Matrix((seed,4.,5.)),'speedInfo':('overlay speed',),'speed':seed,'turn_speed':seed/2.,'_pose_active':bool(iteration%2),'velocity':Vector(3.,0.,4.),'acceleration':Vector(1.,0.,2.),'stabilised_matrix':Matrix((seed+1.,2.,3.))}
  row=c._vehicle_property_overlays[id(v)];row['_collision_filter']=None;c._battle_active=bool(iteration%5)
  case=iteration%len(code_names);setattr(c,code_names[case],caller.func_code)
  c._vehicle_starting_wg_physics=v;c._vehicle_syncing_gun_angles=v;c._avatar_syncing_aux_physics=a;c._avatar_entering_vehicle=v
  object.__setattr__(v,'_offlineNativeRemote',bool(iteration%3))
  out=[normalized(getattr(v,name)) for name in ('ordinary','health','isCrewActive','position','yaw','matrix','speedInfo')]
  if iteration%7:object.__setattr__(v,'fakeCell','fake cell')
  elif iteration%11:a.fakeServer='fake server'
  else:object.__setattr__(v,'cell','native cell')
  out.append(v.cell)
  f=caller(v,'filter');out.extend(normalized(getattr(f,name)) for name in ('velocity','acceleration') if hasattr(f,name))
  if hasattr(f,'interpolateStabilisedMatrix'):out.append(normalized(f.interpolateStabilisedMatrix(7.)))
  if hasattr(f,'groundPlacingMatrix'):out.append(normalized(f.groundPlacingMatrix))
  if hasattr(f,'segmentMayHitEntity'):out.extend(f.segmentMayHitEntity(Vector(0.,0.,0.),Vector(x,0.,0.),False) for x in (-100.,0.,100.))
  out.extend((v.getSpeed(),a.getOwnVehicleSpeeds(),a.getOwnVehicleSpeeds(True),own_speed(a,get_instantaneous=True)))
  for name,value in (('health',81),('isCrewActive',True),('position',Vector(9.,8.,7.)),('yaw',seed+1.),('matrix',Matrix((3.,2.,1.))),('ordinary',49)):setattr(v,name,value)
  out.extend(normalized(getattr(v,name)) for name in ('health','isCrewActive','position','yaw','matrix','ordinary'))
  c._vehicle_starting_visual=v;c._vehicle_start_visual_code=visual.func_code
  object.__setattr__(v,'_offlineNativeDrawVisible',bool(iteration%3));object.__setattr__(v,'_offlineNativeMarkerVisible',bool(iteration%7))
  out.append(visual(v));out.append(trace)
  c._gun_rotator_predict_locked_target_code=predict.func_code
  out.append(normalized(predict(v)))
  before.append(out)
 assert before[0]==before[1],(iteration,before)
# Caller identity is the stock frame, with no extra Python wrapper frame.
c,v,a,trace=setup(True);c._vehicle_property_overlays[id(v)]={'_pose_active':True,'matrix':Matrix((1.,2.,3.)),'velocity':Vector(1.,2.,3.)}
c._camera_acceleration_update_code=caller.func_code
assert normalized(caller(v,'filter').velocity)==('vector',1.,2.,3.)
assert other_caller(v,'filter').velocity=='stock velocity'
# Class access is an unbound descriptor and compatibility lifetime is weak.
assert type(v).__getattribute__(v,'ordinary')==37
reference=weakref.ref(c);del c;gc.collect();assert reference() is None
try:v.ordinary
except RuntimeError:pass
else:raise AssertionError('retired descriptor owner was used')
# Avatar and CompoundAppearance __getattribute__ against the pinned closures.
names2=('avatar_getattribute','compound_getattribute')
factory2=ast.parse('def factory2(compatibility,runtime):\n pass').body[0]
factory2.body=[n for n in install.body if isinstance(n,ast.FunctionDef) and n.name in names2]+[ast.Return(ast.Tuple([ast.Name(n,ast.Load()) for n in names2],ast.Load()))]
exec(compile(ast.fix_missing_locations(ast.Module([factory2])),path,'exec'),env)
class StockAvatar(object):
 playerVehicleID=7
 def __init__(self):self.base='base';self.cell='cell';self.server='server';self.bwProto='proto';self.vehicle='attached';self.ordinary=5
class StockCompound(object):
 def __init__(self,f):self._CompoundAppearance__filter=f;self.ordinary=6
def sniper(avatar):return avatar.vehicle
def refresh(appearance):return appearance._CompoundAppearance__filter
def nested(appearance):return refresh(appearance)
def describe(value):
 if isinstance(value,Vehicle):return 'vehicle'
 if isinstance(value,Filter):return 'filter'
 if hasattr(value,'syncGunAngles') and hasattr(value,'_vehicle_filter'):
  return ('proxy',value.syncGunAngles(1,2),value.syncStabilisedYPR(1,2,3),value.velocity,value.acceleration,value.groundPlacingMatrix,describe(value._vehicle_filter),value._pose_matrix)
 return value
def probe(obj,name):
 try:return describe(getattr(obj,name))
 except AttributeError as e:return ('AttributeError',str(e))
random.seed(1514)
for iteration in range(1500):
 results=[]
 for use_native in (False,True):
  c=Bag();c._runtime=runtime;c._vehicle_property_overlays={}
  c._original_avatar_getattribute=object.__getattribute__;c._original_compound_getattribute=object.__getattribute__
  c._battle_active=bool(iteration%3);c._postmortem_vehicle_id=iteration%4 and 9;c._sniper_oscillator_acceleration_code=None
  c._compound_refreshing_models=None;c._compound_models_refresh_code=None
  hooks=[native.create(c,4),native.create(c,5)] if use_native else env['factory2'](c,runtime)
  A=type('FixtureStockAvatar',(StockAvatar,),{'__getattribute__':hooks[0]})
  C=type('FixtureStockCompound',(StockCompound,),{'__getattribute__':hooks[1]})
  trace=[];v=Vehicle(trace);a=A();f=Filter();ap=C(f)
  if iteration%5:object.__setattr__(a,'fakeServer','fake server')
  if iteration%7==0:object.__delattr__(a,'vehicle')
  entities={9:v if iteration%10<7 else None,7:v if iteration%8 else None}
  runtime.bigworld=Bag();runtime.bigworld.entity=lambda i:entities.get(i)
  if iteration%9:c._vehicle_property_overlays[id(v)]={'_pose_active':bool(iteration%2)}
  if iteration%10>2:c._sniper_oscillator_acceleration_code=sniper.func_code
  if iteration%11>3:c._compound_refreshing_models=ap
  if iteration%13>2:c._compound_models_refresh_code=refresh.func_code
  out=[probe(a,n) for n in ('base','cell','server','bwProto','vehicle','ordinary','missing','playerVehicleID')]
  try:out.append(describe(sniper(a)))
  except AttributeError as e:out.append(('AttributeError',str(e)))
  out.extend(probe(ap,n) for n in ('_CompoundAppearance__filter','ordinary','missing'))
  out.extend((describe(refresh(ap)),describe(nested(ap))))
  out.append(describe(type(a).__getattribute__(a,'cell')))
  results.append(out)
 assert results[0]==results[1],(iteration,results)
print('1200 pinned Vehicle hook traces, 1500 Avatar/CompoundAppearance traces, caller identity, proxy lifetimes passed')
'''


class NativeVehicleHooksTests(unittest.TestCase):
    def test_pinned_descriptors_and_caller_identity(self):
        source = os.environ.get('WOT_FORK_0101_SOURCE')
        if not source:
            self.skipTest('Fixed v0.10.1 source input is required')
        py27 = subprocess.check_output(['pyenv', 'which', 'python2.7'],
                                      env=dict(os.environ, PYENV_VERSION='2.7.18'), text=True).strip()
        prefix = subprocess.check_output([py27, '-c', 'import sys;print(sys.prefix)'], text=True).strip()
        with tempfile.TemporaryDirectory(prefix='native-vehicle-hooks-') as folder:
            binary = Path(folder) / 'offline_client_vehicle_hooks_native.so'
            result = subprocess.run(['c++', '-std=c++17', '-O1', '-ffp-contract=off', '-Wall', '-Wextra', '-Werror',
                                     '-Wno-missing-field-initializers', '-bundle', '-undefined', 'dynamic_lookup',
                                     '-isystem', str(Path(prefix) / 'include/python2.7'),
                                     str(ROOT / 'tests/stock_native_client_vehicle_hooks_host.cpp'), '-o', str(binary)],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            script = Path(folder) / 'fixture.py'
            script.write_text(FIXTURE)
            result = subprocess.run([py27, '-B', str(script), source], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            print(result.stdout.strip())


if __name__ == '__main__':
    unittest.main()
