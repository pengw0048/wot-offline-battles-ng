"""Fixed-release Python/C++ differential tests for the complete drive stage."""
import copy
import importlib.util
import json
import math
import os
from pathlib import Path
import random
import subprocess
import sys
import tempfile
import types
import unittest

ROOT = Path(__file__).resolve().parents[1]
SOURCE = os.environ.get('WOT_FORK_0101_SOURCE')
LAW_NAMES = '''GRAVITY GRAVITY_FACTOR COHESION COH_DECAY_Y COH_DECAY_FACTOR
COH_DECAY_POW SLOPE_COH_DECAY_Y SLOPE_COH_DECAY COH_DECAY_BOUND POWER_FACTOR
BKWD_POWER_FRACTION ENGINE_MIN_V DRIVE_TRACTION SLOPE_GRIP_LNG_MIN_Y
SLOPE_GRIP_LNG_FULL_Y SLOPE_GRIP_LNG_MIN SLOPE_GRIP_LNG_FULL STEER_RESIST_MULT
SLIDE_HOLD_TAN SLIDE_KINETIC SLIP_THRESHOLD_TAN SLIP_DRAG COAST_BRAKE_SHARE
OVERSPEED_MAX_FACTOR ANG_ACCELERATION_TIME SPEED_AFFECT_ROT_DECREASE'''.split()

def case(op=0):
    return dict(op=op,params=dict(mass=25000.,powerW=280000.,nativePowerRatio=1.,
        speedFwd=14.,speedBwd=6.,specificFriction=.07,brakeDecel=4.,rotSpd=.7,
        terrainResist=[1.,1.2,1.8],trackCenter=1.3),
        state=dict(id=1,x=2.,y=0.,z=3.,yaw=0.,pitch=0.,roll=0.,speed=2.,
            _turn_speed=0.,_contact_forward_speed=0.,_direction_command=0.,last_drive_pitch=0.,
            push_x=0.,push_z=0.,push_yaw=0.,alive=True,airborne=False,service_brake=False,
            _hard_grinds=0,half_width=1.7,half_length=3.5,collision_shape=[1.7,3.5,-.8,2.],siege_state=0),
        input=dict(dt=.1,now=5.,throttle=1.,turn=0.,slope_pitch=0.,travel_sign=1.,path_clear=True,
            pose_frozen=False,probe_collision=False,exact_collision=True,brake=False,siege_locked=False,
            wet_escape=False,shallow_admitted=False,maximum_distance=None,move_target=[3.,0.,35.]),
        policy=dict(fraction=1.,pose=True,turret=True,rotate=True,clear=True,kill=False,statuses=[]),
        response=dict(correction=[0.,0.],delta_velocity=[0.,0.],delta_yaw=0.),
        options=dict(advance_push=True,apply_correction=True,apply_friction=True,advance_forward=False),
        siege_limit=None)

def encode(value,physics):
    def optional(v):
        return [v is not None,0. if v is None else v]
    d,s,i,p = value['params'],value['state'],value['input'],value['policy']
    out = [value['op']] + [getattr(physics,name) for name in LAW_NAMES]
    out += [d[name] for name in ('mass','powerW','nativePowerRatio','speedFwd','speedBwd','specificFriction','brakeDecel','rotSpd')]
    out += d['terrainResist']+[s['half_width'],s['half_length'],d['trackCenter']]+optional(value['siege_limit'])
    out += list(physics.HARD_CONTACT_YAW_DELTAS)
    out += [getattr(physics,name) for name in ('HARD_CONTACT_BRAKE_DECAY','HARD_CONTACT_STOP_SPEED',
        'HARD_CONTACT_ENTRY_FACTOR','HARD_CONTACT_SLIDE_DECAY','HARD_CONTACT_GRIND_TICKS',
        'SLOPE_GRIP_SDW_MIN_Y','SLOPE_GRIP_SDW_FULL_Y','SLOPE_GRIP_SDW_MIN','SLOPE_GRIP_SDW_FULL',
        'SERVER_PHYSICS_CONSTRAINT_ITERATIONS')]
    out += [s[name] for name in ('x','y','z','yaw','pitch','roll','speed','_turn_speed','_contact_forward_speed',
        '_direction_command','last_drive_pitch','push_x','push_z','push_yaw','alive','airborne','service_brake','_hard_grinds')]
    out += optional(s.get('destructible_contact_speed'))
    plane=s.get('_suspension_ground_plane')
    out += [plane is not None,plane['gradient_x'] if plane else 0.,plane['gradient_z'] if plane else 0.]
    out += [i[name] for name in ('dt','now','throttle','turn','slope_pitch','travel_sign','path_clear','pose_frozen',
        'probe_collision','exact_collision','brake','siege_locked','wet_escape','shallow_admitted')]
    out += optional(i['maximum_distance'])+[i['move_target'] is not None]+list(i['move_target'] or [0.,0.,0.])
    out += [p[name] for name in ('fraction','pose','turret','rotate','clear','kill')]
    out += [len(p['statuses'])]+p['statuses']
    r=value['response'];o=value['options']
    out += r['correction']+r['delta_velocity']+[r['delta_yaw']]
    out += [o[name] for name in ('advance_push','apply_correction','apply_friction','advance_forward')]
    return ' '.join(str(int(v)) if isinstance(v,bool) else str(v) for v in out)

@unittest.skipUnless(SOURCE, 'set WOT_FORK_0101_SOURCE to the fixed gameplay checkout')
class MotionDifferential(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.folder=tempfile.TemporaryDirectory(prefix='wot-native-motion-')
        path=Path(cls.folder.name)
        law=(Path(SOURCE)/'native/offline_release_drive_python.inc').read_text()
        marker='PyObject *WOT_CDECL release_drive(PyObject *,PyObject *args) {'
        if law.count(marker)!=1:
            raise AssertionError('Pinned release drive source changed')
        (path/'offline_release_drive_law.hpp').write_text('#pragma once\n#include <algorithm>\n#include <cmath>\n'+law.split(marker)[0])
        cls.binary=path/'motion'
        subprocess.run(['c++','-std=c++17','-O2','-Wall','-Wextra','-Werror','-I',str(path),
            '-I',str(ROOT/'bigworld'),str(ROOT/'bigworld/stock_native_bot_motion.cpp'),
            str(ROOT/'tests/stock_native_bot_motion_host.cpp'),'-o',str(cls.binary)],check=True)
        env=dict(os.environ,PYENV_VERSION='2.7.18')
        cls.python27=subprocess.check_output(['pyenv','which','python2.7'],env=env,text=True).strip()
        stub=types.ModuleType('gui.mods.offline_lan_0922.worker_diagnostics');stub.observed=lambda unused:lambda f:f
        previous=sys.modules.get(stub.__name__);sys.modules[stub.__name__]=stub
        try:
            spec=importlib.util.spec_from_file_location('motion_test_physics',Path(SOURCE)/'src/res/scripts/client/gui/mods/offline_lan_0922/vehicle_physics.py')
            cls.physics=importlib.util.module_from_spec(spec);spec.loader.exec_module(cls.physics)
        finally:
            if previous is None:del sys.modules[stub.__name__]
            else:sys.modules[stub.__name__]=previous
    @classmethod
    def tearDownClass(cls):
        cls.folder.cleanup()
    def compare(self,values):
        actual=subprocess.check_output([str(self.binary)],input='\n'.join(encode(v,self.physics) for v in values)+'\n',text=True)
        expected=subprocess.check_output([self.python27,str(ROOT/'tests/stock_native_bot_motion_oracle.py'),SOURCE],
            input='\n'.join(json.dumps(v) for v in values)+'\n',text=True,env=dict(os.environ,PYTHONDONTWRITEBYTECODE='1'))
        aa=[json.loads(v) for v in actual.splitlines()];ee=[json.loads(v) for v in expected.splitlines()]
        self.assertEqual(len(aa),len(values));self.assertEqual(len(aa),len(ee))
        def compare(a,e,path):
            if isinstance(e,list):
                self.assertEqual(len(a),len(e),path)
                for n,(left,right) in enumerate(zip(a,e)):compare(left,right,path+'/'+str(n))
            elif e is None:self.assertIsNone(a,path)
            elif isinstance(e,(int,float)):
                self.assertTrue(math.isclose(a,e,rel_tol=2.e-11,abs_tol=2.e-11),'%s: %r != %r'%(path,a,e))
            else:self.assertEqual(a,e,path)
        for index,(a,e) in enumerate(zip(aa,ee)):
            # Rotational invalidation is an internal typed notification; the
            # original performs the same dictionary eviction inline.
            a['calls']=[row for row in a['calls'] if not(row[0]==7 and row[2]==0)]
            for key in e:compare(a[key],e[key],'%d/%s'%(index,key))
    def test_drive_guards_and_contact_outcomes(self):
        values=[]
        for status in range(5):
            for reverse in (False,True):
                v=case();v['policy']['statuses']=[status]+[4,4,0,0]
                if reverse:v['input'].update(throttle=-1.,travel_sign=-1.);v['state']['speed']=-3.
                values.append(v)
        for key in ('pose','turret','rotate','clear'):
            v=case();v['input']['turn']=.8;v['policy'][key]=False;values.append(v)
        for key in ('pose_frozen','siege_locked','brake'):
            v=case();v['input'][key]=True;values.append(v)
        for exact in (False,True):
            v=case();v['input'].update(path_clear=False,probe_collision=True,exact_collision=exact);values.append(v)
        v=case();v['input']['turn']=.9;v['policy']['fraction']=.4;values.append(v)
        v=case();v['state']['_suspension_ground_plane']={'gradient_x':.2,'gradient_z':-.3};values.append(v)
        v=case();v['state']['_contact_forward_speed']=1.5;values.append(v)
        v=case();v['siege_limit']=.7;v['state']['siege_state']=2;values.append(v)
        self.compare(values)
    def test_seeded_drive_and_impulse_states(self):
        rng=random.Random(1513);values=[]
        for n in range(400):
            v=case(n%3);s=v['state'];i=v['input'];p=v['policy']
            s.update(x=rng.uniform(-20,20),z=rng.uniform(-20,20),yaw=rng.uniform(-math.pi,math.pi),
                pitch=rng.uniform(-.9,.9),roll=rng.uniform(-.7,.7),speed=rng.uniform(-5,15),
                _turn_speed=rng.uniform(-.5,.5),_contact_forward_speed=rng.uniform(-4,4),
                _direction_command=rng.choice([-1.,0.,1.]),push_x=rng.uniform(-3,3),push_z=rng.uniform(-3,3),
                push_yaw=rng.uniform(-.4,.4),airborne=n%13==0,service_brake=n%7==0,_hard_grinds=n%6)
            if v['op']==2:s['alive']=False
            i.update(dt=rng.choice([0.,.016,.1,.2]),throttle=rng.choice([-1.,-.1,0.,.3,1.]),
                turn=rng.choice([-1.,0.,.4,1.]),slope_pitch=rng.uniform(-.8,.8),travel_sign=rng.choice([-1.,1.]),
                path_clear=n%17!=0,pose_frozen=n%19==0,probe_collision=n%17==0,exact_collision=n%5!=0,
                brake=n%11==0,wet_escape=n%6==0,shallow_admitted=n%3==0)
            p.update(fraction=rng.choice([0.,.4,1.]),pose=n%31!=0,turret=n%29!=0,rotate=n%23!=0,
                clear=n%37!=0,statuses=[rng.randrange(5) for _ in range(12)])
            v['response']=dict(correction=[rng.uniform(-.3,.3),rng.uniform(-.3,.3)],
                delta_velocity=[rng.uniform(-2,2),rng.uniform(-2,2)],delta_yaw=rng.uniform(-.3,.3))
            if v['op']==1:
                v['options'].update(advance_push=n%4!=0,apply_correction=n%5!=0,apply_friction=n%7!=0,advance_forward=n%8==0)
            values.append(v)
        self.compare(values)

if __name__=='__main__':unittest.main()
