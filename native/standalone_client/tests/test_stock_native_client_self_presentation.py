"""Full copied-pose producer differential against the pinned player methods."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SOURCE = os.environ.get('WOT_FORK_0101_SOURCE')
FIXTURE = r'''
import ast,math,random,struct,sys,types
import client_self_test as native
source=sys.argv[1]+'/src/res/scripts/client'
sys.path.insert(0,source)
for name in ('gui','gui.mods'):
 m=types.ModuleType(name);m.__path__=[source+'/'+name.replace('.','/')];sys.modules[name]=m
from gui.mods.offline_lan_0922 import hull_aiming,vehicle_physics
class O(object):
 def __init__(self,**values):self.__dict__.update(values)
def f32(v):return struct.unpack('f',struct.pack('f',v))[0]
class V(object):
 def __init__(self,*v):
  if len(v)==1:v=tuple(v[0])
  self.x,self.y,self.z=map(f32,v)
 def __iter__(self):return iter((self.x,self.y,self.z))
 def __getitem__(self,index):return tuple(self)[index]
 def __add__(self,v):return V(self.x+v.x,self.y+v.y,self.z+v.z)
 def __nonzero__(self):return True
class M(object):
 def __init__(self,provider=None):
  self._setIdentity()
  if provider is not None:self._set(provider)
  for name in ('setRotateYPR','setIdentity','set','preMultiply','invert'):setattr(self,name,native.wrap(self,name))
 def _setIdentity(self):self.rows=[[1.,0.,0.],[0.,1.,0.],[0.,0.,1.]];self.translation=V(0,0,0)
 def _set(self,provider):
  if isinstance(provider,Product):provider=provider.matrix()
  self.rows=[list(row) for row in provider.rows];self.translation=V(provider.translation)
 def _setRotateYPR(self,ypr):
  y,p,r=ypr;cy,sy=math.cos(y),math.sin(y);cp,sp=math.cos(p),math.sin(p);cr,sr=math.cos(r),math.sin(r)
  self.rows=[[cr*cy+sr*sp*sy,sr*cp,-cr*sy+sr*sp*cy],[-sr*cy+cr*sp*sy,cr*cp,sr*sy+cr*sp*cy],[cp*sy,-sp,cp*cy]]
  self.rows=[map(f32,row)for row in self.rows];self.translation=V(0,0,0)
 def _preMultiply(self,before):
  b=M(before);a=M(self)
  self.rows=[[f32(sum(b.rows[i][k]*a.rows[k][j] for k in range(3)))for j in range(3)]for i in range(3)]
  self.translation=V(*(sum(b.translation[k]*a.rows[k][j]for k in range(3))+a.translation[j]for j in range(3)))
 def _invert(self):
  rows=zip(*self.rows);t=self.translation;self.rows=map(list,rows);self.translation=V(*(-sum(t[k]*self.rows[k][j]for k in range(3))for j in range(3)))
 @property
 def yaw(self):return math.atan2(self.rows[2][0],self.rows[2][2])
 @property
 def pitch(self):return math.atan2(-self.rows[2][1],math.hypot(self.rows[2][0],self.rows[2][2]))
 @property
 def roll(self):return math.atan2(self.rows[0][1],self.rows[1][1])
class Product(object):
 def __init__(self,a,b):self.a=a;self.b=b
 def matrix(self):m=M(self.b);m._preMultiply(self.a);return m
native.configure(M,V)
mathapi=O(Matrix=native.Matrix,Vector3=native.Vector3)
def xyz(v):return tuple(float(n)for n in v)
def field(v,k,default=None):return v.get(k,default)if isinstance(v,dict)else getattr(v,k,default)
methods=set(('_simulated_rpm_and_gear','_publish_rpm','_update_local_presentation','_local_camera_motion','_local_motion_pose_at','_camera_motion_window','_refresh_local_stabilised_snapshot','_local_body_pose','_local_stabilised_pose','_local_steady_rotation','_bind_local_body_swinging','_update_local_tracks','_local_engine_mode_value','_local_acceleration_swing_parameters','_update_local_acceleration_swinging','_update_local_hull_aiming','_reset_full_turret_sniper_aim'))
parsed=ast.parse(open(source+'/gui/mods/offline_lan_0922/battle_runtime.py').read());cls=next(v for v in parsed.body if isinstance(v,ast.ClassDef)and v.name=='BattleRuntime');cls.body=[v for v in cls.body if isinstance(v,ast.FunctionDef)and v.name in methods]
for v in cls.body:v.decorator_list=[d for d in v.decorator_list if isinstance(d,ast.Name)and d.id=='staticmethod']
module=ast.Module(body=[cls]);ast.fix_missing_locations(module)
ns=dict(math=math,sys=sys,hull_aiming=hull_aiming,vehicle_physics=vehicle_physics,_number=float,_xyz=xyz,_field=field,ENGINE_MODE_OFF=0,ENGINE_MODE_IDLE=1,ENGINE_MODE_RUNNING=2,_MOVEMENT_FORWARD=1,_MOVEMENT_BACKWARD=2,_MOVEMENT_ROTATE_LEFT=4,_MOVEMENT_ROTATE_RIGHT=8,LOCAL_ACCEL_SWING_MOVE_SECONDS=2.,LOCAL_ACCEL_SWING_ROTATE_SECONDS=1.,LOCAL_ACCEL_SWING_REPORT_LIMIT=0,RPM_PRESENTATION_SECONDS=.1,TRACK_SCROLL_LIMITS=(-15.,30.))
exec(compile(module,'pinned-self-presentation','exec'),ns);Original=ns['BattleRuntime']
config=types.ModuleType('gui.mods.offline_lan_0922.battle_runtime');config.LOCAL_ACCEL_SWING_MOVE_SECONDS=2.;config.LOCAL_ACCEL_SWING_ROTATE_SECONDS=1.;config.TRACK_SCROLL_LIMITS=(-15.,30.);config.RPM_PRESENTATION_SECONDS=.1;sys.modules[config.__name__]=config
avatarModule=types.ModuleType('Avatar');avatarModule.VEHICLE_VIEW_STATE=O(RPM='rpm');sys.modules['Avatar']=avatarModule
bindingns={};exec(compile(open(source+'/gui/mods/offline_lan_0922/entities/bigworld_binding.py').read(),'original-aux-pack','exec'),bindingns)
def auxDecode(packed):
 def bits(v):return struct.unpack('f',struct.pack('I',v))[0]
 def field(value,scale,offset):return f32(f32(value*bits(scale))-bits(offset))
 return(field(packed&0x7fff,0x39490fdb,0x40490fdb),field((packed>>15)&0xfff,0x3a491c6d,0x3fc90fdb),field((packed>>27)&0x1fff,0x3a490fdb,0x40490fdb),field((packed>>40)&255,0x3e34b4b5,0x41700000),field((packed>>48)&255,0x3e34b4b5,0x41700000),f32((packed>>56)*bits(0x3b9a33ce)))
class Compat(O):
 def set_vehicle_pose_overlay(self,e,position,yaw,matrix,speed,turn,velocity,acceleration,steady_rotation_matrix=None,stabilised_matrix=None):
  self._vehicle_property_overlays[id(e)].update(_pose_active=True,position=position,yaw=yaw,matrix=matrix,speed=speed,turn_speed=turn,velocity=velocity,acceleration=acceleration,steady_rotation_matrix=steady_rotation_matrix,stabilised_matrix=stabilised_matrix)
def resetIdeal(a):
 m=M(a._SniperAimingSystem__vehicleMProv);m._invert();r=M();r._setRotateYPR((a._SniperAimingSystem__worldYaw,a._SniperAimingSystem__worldPitch,0.));m._preMultiply(r)
 y,p=m.yaw,m.pitch;limits=a._SniperAimingSystem__yawLimits
 if limits is not None:y=max(limits[0],min(limits[1],y))
 desc=a._SniperAimingSystem__vehicleTypeDescriptor.gun.combinedPitchLimits;low,high=native.pitch(y,desc['minPitch'],desc['maxPitch']);adjust=max(0.,a._SniperAimingSystem__returningOscillator.deviation.y)
 a._SniperAimingSystem__idealTurretYaw=y;a._SniperAimingSystem__idealGunPitch=max(low-adjust,min(high+adjust+a._SniperAimingSystem__pitchCompensating,p))
def prepare(hydraulic=False):
 b=Original();b._local_position=(2.,3.,4.);b._local_yaw=.2;b._local_pitch=.03;b._local_roll=-.01;b._local_matrix=M();b._local_matrix._setRotateYPR((.2,.03,-.01));b._local_matrix.translation=V(b._local_position)
 b._local_pose_matrix=b._local_matrix;b._local_stabilised_matrix=b._local_matrix;b._local_stabilised_snapshot=M(b._local_matrix);b._local_steady_rotation_matrix=b._local_matrix
 b._local_model=O(matrix=b._local_pose_matrix);b._local_motion_window=None;b._local_motion_history=[(0.,b._local_position)];b._local_motion_clock=0.;b._local_camera_velocity=V(0,0,0)
 b._local_placing_compensation=None;b._local_swinging_animator=None;b._local_swinging_restore=None;b._local_engine_state=None;b._local_engine_mode=None;b._local_swinging_move_flags=0;b._local_swinging_event_seq=0;b._local_swinging_events_reported=0;b._local_swinging_probe=None
 b._disabled_optional_features=set();b._replay_mode=False;b._local_siege_aim_matrix=None;b._local_siege_aim_pitch=0.;b._local_siege_aim_center_z=0.;b._detached_turret_obstacles=None
 b._local_physics=dict(rotationIsAroundCenter=False,trackCenter=1.25,speedFwd=20.)
 pitch=dict(wheelsCorrectionAngles=dict(pitchMin=-.2,pitchMax=.18),wheelsCorrectionSpeed=.07,wheelCorrectionCenterZ=1.2,isAvailable=True,isEnabled=True)
 descriptor=O(physics=O(speedLimits=(20.,10.)),hasSiegeMode=hydraulic,type=O(hullAimingParams=dict(pitch=pitch)),gun=O(turretYawLimits=None,pitchLimits=dict(absolute=(-.1,.08)),combinedPitchLimits=dict(minPitch=-.3,maxPitch=.2)),hull=O(turretPositions=[V(0,1,0)]),chassis=O(hullPosition=V(0,.5,0)),turret=O(gunPosition=V(0,.1,.2)),shot=O(speed=800.,gravity=9.81))
 b._local_descriptor=descriptor
 swing=O(placingCompensationMatrix=M(),worldMatrix=M(),accelSwingingPeriod=.3,accelSwingingDirection=.1)
 app=O(swingingAnimator=swing,detailedEngineState=O(),_CompoundAppearance__compoundModel=O(root=O()),_CompoundAppearance__filter=O(strafeSpeed=.15),engineAudition=O(attachToModel=native.capture,setSpeedInfo=native.capture),_CompoundAppearance__trackScrollCtl=O(setMode=native.capture,setExternal=native.capture))
 def mode(v,force):app._CompoundAppearance__engineMode=v;app.detailedEngineState.mode=v[0];app._CompoundAppearance__trackScrollCtl.setMode(v)
 def scroll(l,r):app._CompoundAppearance__leftTrackScroll=l;app._CompoundAppearance__rightTrackScroll=r;app._CompoundAppearance__trackScrollCtl.setExternal(l,r)
 app.changeEngineMode=mode;app.updateTracksScroll=scroll
 entity=O(isStarted=True,appearance=app,typeDescriptor=descriptor,model=b._local_model,hydraulic=hydraulic,siegeState=2);entity.isAlive=lambda:entity.alive
 b._sender=O(forward=0.,aim_point=None,aim_pitch=.1,gun_pitch=.1)
 aiming=O(_SniperAimingSystem__vehicleMProv=b._local_matrix,_SniperAimingSystem__worldYaw=.5,_SniperAimingSystem__worldPitch=.1,_SniperAimingSystem__yawLimits=None,_SniperAimingSystem__vehicleTypeDescriptor=descriptor,_SniperAimingSystem__returningOscillator=O(deviation=V(0,.02,0)),_SniperAimingSystem__pitchCompensating=.015)
 aiming.resetIdealDirection=lambda:resetIdeal(aiming)
 cam=O(_SniperCamera__aimingSystem=aiming,aimingSystem=aiming);control=O(_cam=cam,camera=cam)
 b._avatar=O(ownVehicleGear=0,ownVehicleAuxPhysicsData=0,guiSessionProvider=O(invalidateVehicleState=native.capture),inputHandler=O(_AvatarInputHandler__ctrlModeName='sniper',_AvatarInputHandler__curCtrl=control),gunRotator=O(_VehicleGunRotator__turretYaw=.2,_VehicleGunRotator__gunPitch=.1,turretYaw=.2,gunPitch=.1))
 compat=Compat(_battle_active=True,_vehicle_property_overlays={id(entity):{}})
 b._runtime=O(math=mathapi,constants=O(SERVER_TICK_LENGTH=.1,VEHICLE_SIEGE_STATE=O(DISABLED=0,ENABLED=2)),bigworld=O(time=native.time,wg_getShotAngles=native.shot,wg_calcGunPitchLimits=native.pitch),compatibility=compat,avatar_input_handler=O(_CTRL_MODE=O(SNIPER='sniper')))
 b._next_rpm_time=0.;b._last_presented_rpm=None;b._engine_audio_report_at=1e10
 def publish(yaw,pitch,roll,left,right,rpm,gear):
  packed=bindingns['_pack_aux_physics_data']((yaw,pitch,roll,left,right,rpm));b._avatar.ownVehicleGear=gear;b._avatar.ownVehicleAuxPhysicsData=packed
  decoded=auxDecode(packed);app.updateTracksScroll(decoded[3],decoded[4]);b._avatar.guiSessionProvider.invalidateVehicleState('rpm',decoded[5])
 b._binding=O(avatar_aux_physics=publish)
 b._runtime.get_shot_angles=lambda d,m,angles,point:(0.,angles[1]+.2)
 b._vector=lambda values:V(*values);b._report_local_body_swinging=lambda*args:None;b._sample_local_acceleration_swinging=lambda*args:None;b._report_track_surface=lambda*args:None
 b._optional_feature_enabled=lambda feature:feature not in b._disabled_optional_features
 b._run_optional_feature=lambda feature,fn,args=():fn(*args)if b._optional_feature_enabled(feature)else False
 def audio(e):
  detailed=e.appearance.detailedEngineState
  if detailed is b._local_engine_state:return True
  detailed.vehicleSpeedLink=lambda:b._local_speed;detailed.rotationSpeedLink=lambda:b._local_turn_speed;detailed.vehicleMatrixLink=e.appearance._CompoundAppearance__compoundModel.root;b._local_engine_state=detailed
 b._bind_local_engine_audio_motion=audio
 if hydraulic:
  b._local_siege_aim_matrix=M();b._local_siege_flat_body_matrix=b._local_matrix;b._local_siege_body_matrix=Product(M(),Product(b._local_siege_aim_matrix,b._local_matrix));b._local_pose_matrix=Product(b._local_siege_body_matrix,M());b._local_model.matrix=b._local_pose_matrix;b._local_stabilised_matrix=b._local_pose_matrix
 return b,entity,aiming

def snapshot(b,e,a):
 overlay=b._runtime.compatibility._vehicle_property_overlays[id(e)]
 return [tuple(overlay[k])for k in ('position','velocity','acceleration')]+[overlay[k]for k in ('yaw','speed','turn_speed')]+[tuple(row)for row in M(overlay['matrix']).rows]+[tuple(M(overlay['stabilised_matrix']).translation),e.engineMode,(e.appearance._CompoundAppearance__leftTrackScroll,e.appearance._CompoundAppearance__rightTrackScroll),b._local_siege_aim_pitch,b._local_swinging_event_seq,b._avatar.ownVehicleGear,b._avatar.ownVehicleAuxPhysicsData,(e.appearance.swingingAnimator.accelSwingingPeriod,e.appearance.swingingAnimator.accelSwingingDirection),(a._SniperAimingSystem__idealTurretYaw,a._SniperAimingSystem__idealGunPitch)]
def compare(a,b,path=''):
 if isinstance(a,(tuple,list)):
  assert len(a)==len(b),(path,a,b)
  for i,(x,y)in enumerate(zip(a,b)):compare(x,y,path+'/'+str(i))
 else:assert a==b or abs(a-b)<1e-8,(path,a,b)
for hydraulic in (False,True):
 old,e,a=prepare(hydraulic);new,ne,na=prepare(hydraulic);native.install(new,ne)
 # Every mod hot path on the native side is a trap; Math/native controllers
 # above are fixture primitives and not gameplay callbacks.
 def bomb(*args):raise AssertionError('Native presentation called Python gameplay')
 for name in methods:setattr(new,name,bomb)
 ne.appearance.changeEngineMode=bomb;ne.appearance.updateTracksScroll=bomb;na.resetIdealDirection=bomb
 rng=random.Random(1513);position=[2.,3.,4.]
 for i in range(600):
  dt=rng.choice([0.,.005,.016,.033,.12,.23]);position=[v+rng.uniform(-.5,.5)*dt for v in position]
  values=dict(position=tuple(position),yaw=rng.uniform(-3.,3.),pitch=rng.uniform(-.1,.1),roll=rng.uniform(-.1,.1),speed=rng.uniform(-10.,20.),turn=rng.uniform(-1.,1.),effective=rng.choice([-1.,0.,1.]),throttle=rng.choice([-1.,0.,1.]),airborne=i%13==0,forward=rng.choice([-1.,0.,1.]),aim_pitch=rng.uniform(-.5,.5),aim_point=(10.,5.,20.)if i%5 else None,alive=True)
  for name,value in [('position',values['position']),('yaw',values['yaw']),('pitch',values['pitch']),('roll',values['roll']),('speed',values['speed']),('turn_speed',values['turn']),('drive_turn',values['effective']),('drive_throttle',values['throttle']),('airborne',values['airborne'])]:setattr(old,'_local_'+name,value)
  old._sender.forward=values['forward'];old._sender.aim_pitch=values['aim_pitch'];old._sender.aim_point=values['aim_point'];e.alive=True
  ne.siegeState=e.siegeState=2 if i%11 else 0
  if i==211:
   for ent,b in ((e,old),(ne,new)):
    ent.appearance.swingingAnimator=O(placingCompensationMatrix=M(),worldMatrix=M(),accelSwingingPeriod=.7,accelSwingingDirection=-.1);ent.appearance.detailedEngineState=O()
  now=i*.037;native.set_time(now)
  old._update_local_presentation(e,dt);old._publish_rpm(now);native.frame(new,ne,values,dt)
  compare(snapshot(old,e,a),snapshot(new,ne,na),str((hydraulic,i)))
  assert new._runtime.compatibility._vehicle_property_overlays[id(ne)]['stabilised_matrix'] is new._local_stabilised_snapshot
  assert native.sample(ne.appearance.detailedEngineState.vehicleSpeedLink)==f32(values['speed'])
  assert native.sample(ne.appearance.detailedEngineState.rotationSpeedLink)==f32(values['turn'])
 link=ne.appearance.detailedEngineState.vehicleSpeedLink;native.close(new);native.close(new);assert native.sample(link)is None
for i in range(2000):
 values=[random.uniform(-4.,4.)for _ in range(3)]+[random.uniform(-30.,40.)for _ in range(2)]+[random.uniform(-1.,2.)]
 assert native.packed(values)==bindingns['_pack_aux_physics_data'](values)
new,entity,aiming=prepare(False);native.install(new,entity)
values=dict(position=(2.,3.,4.),yaw=.7,pitch=0.,roll=0.,speed=5.,turn=.3,effective=1.,throttle=1.,airborne=False,forward=1.,aim_pitch=0.,aim_point=None,alive=True)
def retireInHud(*args):
 native.close(new)
 raise RuntimeError('stock HUD retired this battle')
entity_dummy=entity
new._avatar.guiSessionProvider.invalidateVehicleState=retireInHud
native.set_time(30.);native.frame(new,entity,values,.02)
assert new._native_client_self_presentation is None
assert not new._disabled_optional_features
assert native.sample(entity.appearance.detailedEngineState.vehicleSpeedLink)is None
native.reset()
print('1200 complete player presentation frames matched, with hot callback traps and retired native links')
'''

@unittest.skipUnless(SOURCE, 'Set WOT_FORK_0101_SOURCE to the pinned gameplay source')
class SelfPresentationTests(unittest.TestCase):
    def test_full_native_producer(self):
        py27 = subprocess.check_output(['pyenv', 'which', 'python2.7'], env=dict(os.environ, PYENV_VERSION='2.7.18'), text=True).strip()
        prefix = subprocess.check_output([py27, '-c', 'import sys;print(sys.prefix)'], text=True).strip()
        with tempfile.TemporaryDirectory(prefix='client-self-') as folder:
            path = Path(folder)
            law = (Path(SOURCE) / 'native/offline_release_drive_python.inc').read_text().split('PyObject *WOT_CDECL release_drive(')[0]
            (path / 'offline_release_drive_law.hpp').write_text('#pragma once\n#include <algorithm>\n#include <cmath>\n'+law)
            command = ['c++', '-std=c++17', '-O0', '-ffp-contract=off', '-DWOT_CLIENT_SELF_HOST_BUILD', '-DWOT_CLIENT_REPLICA_HOST_BUILD', '-Wno-register', '-bundle', '-undefined', 'dynamic_lookup', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter', '-isystem', str(Path(prefix) / 'include/python2.7'), '-I'+folder, '-I'+str(ROOT / 'bigworld'), '-I'+str(Path(SOURCE) / 'native')]
            command += [str(ROOT / 'bigworld' / name) for name in ('stock_native_client_state.cpp', 'stock_native_client_drive_runtime.cpp', 'stock_native_bot_motion.cpp', 'stock_native_client_drive_presentation.cpp', 'stock_native_client_drive_presentation_python.cpp')]
            command += [str(ROOT / 'tests/stock_native_client_self_presentation_host.cpp'), '-o', str(path / 'client_self_test.so')]
            result = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout+result.stderr)
            result = subprocess.run([py27, '-B', '-c', FIXTURE, SOURCE], cwd=folder, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout+result.stderr)

if __name__ == '__main__':
    unittest.main()
