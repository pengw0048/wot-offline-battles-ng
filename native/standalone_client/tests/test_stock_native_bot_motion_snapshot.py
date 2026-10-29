"""Exercise the install reader through CPython 2.7 and fixed descriptor readers."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
FIXTURE=r'''
import sys,types
sys.path.insert(0,sys.argv[1]+'/src/res/scripts/client')
gui=types.ModuleType('gui');gui.__path__=[sys.path[0]+'/gui'];sys.modules['gui']=gui
mods=types.ModuleType('gui.mods');mods.__path__=[sys.path[0]+'/gui/mods'];sys.modules['gui.mods']=mods
import motion_snapshot_test as native
from gui.mods.offline_lan_0922 import vehicle_physics
class Box(object):
    def __init__(self,**kw):self.__dict__.update(kw)
descriptor=Box(chassis=Box(hitTester=Box(bbox=((-2.,-.2,-4.),(2.,.7,4.))),hullPosition=(0.,1.,0.)),
               hull=Box(hitTester=Box(bbox=((-1.8,0.,-3.5),(1.8,1.4,3.5)))))
params=vehicle_physics.derive_params({});params.update(mass=42000.,powerW=360000.)
runtime=Box(_turn_speeds={7:.12},_hard_contact_grinds={7:3},_physics_params={7:params})
runtime.bot_crew_level=lambda actor:100
runtime._physics_params_for=lambda actor:params
runtime._suspension_params_for=lambda actor:None
state=dict(id=7,team=1,slot=0,x=12.,y=0.,z=3.,yaw=.4,speed=3.,pitch=.1,roll=.2,
    terrain_pitch=.1,suspension_pitch=.03,half_length=3.5,half_width=1.8,
    collision_shape=(1.8,3.5,-.2,2.4),alive=True,_legacy_straddle_reference_y=1.2,
    _basic_slope_basis_yaw=.45,_slope_pose_target=(.1,.2),
    ram_profile=dict(spall_coefficient=1.2,ramming_bonus=.15),
    contact_push_acks=[[2,4,1200.,20.,0.,0.,10.]],
    ram_motion=[[2,4,100000]+[0.]*18],_ram_motion_seq=4,
    _spring_ground_memory=[(1.,2.,3.,4.,5.,.1,False),None])
result=native.inspect(runtime,state,descriptor)
assert result==(12.,.4,.12,.03,1.2,.45,42000.,360000.,-.2,2.4,1.2,.15,1,1,2,0),result
for key,bad in [('collision_shape',(1.,2.,3.)),('ram_motion',[[2,4,0]+[0.]*17]),('contact_push_acks',[[2,3,0.,0.,0.,0.,0.]]*2),('x',float('nan'))]:
    previous=state[key];state[key]=bad
    try:native.inspect(runtime,state,descriptor)
    except (ValueError,TypeError):pass
    else:raise AssertionError('invalid field accepted: '+key)
    finally:state[key]=previous
assert native.inspect(runtime,state,descriptor)==result
from gui.mods.offline_lan_0922 import destructibles_sensor,tank_collision
expected_body=destructibles_sensor._vehicle_body_bbox(descriptor)
def bomb(*args): raise AssertionError('offline descriptor callback')
destructibles_sensor._vehicle_hull_bbox=bomb
destructibles_sensor._vehicle_body_bbox=bomb
tank_collision.descriptor_ram_profile=bomb
assert native.inspect(runtime,state,descriptor)==result
for name in ('items','items.utils','items.tankmen','items.qualifiers','items.VehicleDescrCrew','VehicleQualifiersApplier','constants'):
    sys.modules[name]=types.ModuleType(name)
sys.modules['items'].__path__=[]
utils=sys.modules['items.utils'];utils.generateDefaultCrew=lambda descriptor,level:['crew']
utils.makeDefaultVehicleAttributeFactors=lambda:{'invisibility':[0.,1.]}
sys.modules['items.tankmen'].MAX_SKILL_LEVEL=100
sys.modules['constants'].VEHICLE_TTC_ASPECTS=Box(DEFAULT=0,WHEN_STILL=1)
sys.modules['items.qualifiers'].QUALIFIER_TYPE=Box(MAIN_SKILL=0)
sys.modules['VehicleQualifiersApplier'].VehicleQualifiersApplier=lambda *args:{0:{}}
class Crew(object):
    camouflageFactor=0.
    def __init__(self,*args,**kwargs):pass
    def onCollectFactors(self,f):f.update({'vehicle/rotationSpeed':1.2,'engine/power':1.1,'chassis/terrainResistance':(.9,1.1,1.3)})
    def onCollectShotDispersionFactors(self,f):pass
sys.modules['items.VehicleDescrCrew'].VehicleDescrCrew=Crew
descriptor.type=Box(xphysics={'detailed':{'engines':{'e':{'smplEnginePower':100.}}}})
descriptor.engine=Box(name='e');descriptor.optionalDevices=[]
descriptor.chassis.rotationSpeed=.5;descriptor.chassis.rotationIsAroundCenter=False
descriptor.physics={'weight':42000.,'enginePower':360000.,'speedLimits':(11.,4.),'terrainResistance':(1.,1.2,2.),'specificFriction':.7,'brakeForce':40000.,'trackCenterOffset':1.8,'rollingFrictionFactors':(1.2,1.,.8)}
factors={'vehicle/rotationSpeed':1.2,'engine/power':1.1,'chassis/terrainResistance':(.9,1.1,1.3)}
expected=vehicle_physics.derive_params(descriptor,factors)
keys=('mass','powerW','nativePowerRatio','speedFwd','speedBwd','specificFriction','brakeDecel','rotSpd')
want=tuple(expected[k] for k in keys)+expected['terrainResist']+(expected['trackCenter'],)
got=native.inspect_mode(runtime,state,descriptor)
raw_expected=vehicle_physics.derive_params(descriptor)
raw=native.inspect_raw(descriptor)
for key in ('mass','powerW','nativePowerRatio','speedFwd','speedBwd','specificFriction','brakeDecel','rotSpd','trackCenter'):
    assert abs(raw[key]-raw_expected[key])<1.e-10,(key,raw[key],raw_expected[key])
assert all(abs(a-b)<1.e-10 for a,b in zip(raw['terrainResist'],raw_expected['terrainResist']))
assert all(abs(a-b)<1.e-10 for a,b in zip(got,want)),(got,want)
for around,projected,brake in ((True,1.35,0.),(False,.75,90000.)):
    descriptor.chassis.rotationIsAroundCenter=around
    descriptor.physics['nativePowerRatio']=projected;descriptor.physics['brakeForce']=brake
    oracle=vehicle_physics.derive_params(descriptor,factors)
    expected=tuple(oracle[k] for k in keys)+oracle['terrainResist']+(oracle['trackCenter'],)
    current=native.inspect_mode(runtime,state,descriptor)
    assert all(abs(a-b)<1.e-10 for a,b in zip(current,expected)),(current,expected)
descriptor.chassis.rotationIsAroundCenter=None
try: native.inspect_mode(runtime,state,descriptor)
except ValueError: pass
else: raise AssertionError('missing pivot evidence was accepted')
descriptor.chassis.rotationIsAroundCenter=False
descriptor.physics.pop('nativePowerRatio');descriptor.physics['brakeForce']=40000.
# The native reader must not call any offline projection function.
for module,name in ((vehicle_physics,'derive_params'),(vehicle_physics,'derive_suspension_params')):
    setattr(module,name,lambda *a: (_ for _ in ()).throw(AssertionError('offline callback')))
assert native.inspect_mode(runtime,state,descriptor)==got
from gui.mods.offline_lan_0922 import bot_runtime
assert bot_runtime.BOT_FULL_SUPPORT_ENABLED is False
bot_runtime.BOT_FULL_SUPPORT_ENABLED=True
try: native.inspect(runtime,state,descriptor)
except ValueError as error: assert 'unsupported' in str(error)
else: raise AssertionError('unsupported detailed suspension was accepted')
finally: bot_runtime.BOT_FULL_SUPPORT_ENABLED=False
print('motion snapshot passed')
'''
class MotionSnapshotTests(unittest.TestCase):
    def test_exact_descriptor_and_state_copy(self):
        source=os.environ.get('WOT_FORK_0101_SOURCE')
        if not source:self.skipTest('fixed gameplay source is required')
        py27=subprocess.check_output(['pyenv','which','python2.7'],text=True,env=dict(os.environ,PYENV_VERSION='2.7.18')).strip()
        prefix=subprocess.check_output([py27,'-c','import sys;print(sys.prefix)'],text=True).strip()
        with tempfile.TemporaryDirectory(prefix='motion-snapshot-') as directory:
            folder=Path(directory);script=folder/'fixture.py';script.write_text(FIXTURE)
            law=(Path(source)/'native/offline_release_drive_python.inc').read_text().split('PyObject *WOT_CDECL release_drive(')[0]
            (folder/'offline_release_drive_law.hpp').write_text('#pragma once\n#include <cmath>\n#include <algorithm>\n'+law)
            command=['c++','-std=c++17','-O1','-Wall','-Wextra','-Werror','-bundle','-undefined','dynamic_lookup',
                '-isystem',str(Path(prefix)/'include/python2.7'),'-I'+str(folder),'-I'+str(Path(source)/'native'),'-I'+str(ROOT/'bigworld'),
                str(ROOT/'bigworld/stock_native_bot_motion_snapshot.cpp'),str(ROOT/'bigworld/stock_native_bot_motion.cpp'),str(ROOT/'bigworld/stock_native_bot_descriptor.cpp'),
                str(ROOT/'tests/stock_native_bot_motion_snapshot_host.cpp'),'-o',str(folder/'motion_snapshot_test.so')]
            built=subprocess.run(command,text=True,capture_output=True);self.assertEqual(built.returncode,0,built.stderr)
            run=subprocess.run([py27,'-B',str(script),source],text=True,capture_output=True)
            self.assertEqual(run.returncode,0,run.stderr);self.assertIn('motion snapshot passed',run.stdout)
if __name__=='__main__':unittest.main()
