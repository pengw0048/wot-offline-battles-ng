"""The full original local tank response versus the native persistent owner."""
import ast
import contextlib
import ctypes
import importlib.util
import io
import math
import os
from pathlib import Path
import random
import subprocess
import sys
import tempfile
import types
import unittest

ROOT=Path(__file__).resolve().parents[1]
SOURCE=os.environ.get('WOT_FORK_0101_SOURCE')
LAW_NAMES='''GRAVITY GRAVITY_FACTOR COHESION COH_DECAY_Y COH_DECAY_FACTOR COH_DECAY_POW
SLOPE_COH_DECAY_Y SLOPE_COH_DECAY COH_DECAY_BOUND POWER_FACTOR BKWD_POWER_FRACTION ENGINE_MIN_V
DRIVE_TRACTION SLOPE_GRIP_LNG_MIN_Y SLOPE_GRIP_LNG_FULL_Y SLOPE_GRIP_LNG_MIN SLOPE_GRIP_LNG_FULL
STEER_RESIST_MULT SLIDE_HOLD_TAN SLIDE_KINETIC SLIP_THRESHOLD_TAN SLIP_DRAG COAST_BRAKE_SHARE
OVERSPEED_MAX_FACTOR ANG_ACCELERATION_TIME SPEED_AFFECT_ROT_DECREASE'''.split()

@unittest.skipUnless(SOURCE,'fixed gameplay source is required')
class ContactDifferential(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.folder=tempfile.TemporaryDirectory(prefix='native-player-contact-');folder=Path(cls.folder.name);native=Path(SOURCE)/'native';big=ROOT/'bigworld'
        law=(native/'offline_release_drive_python.inc').read_text().split('PyObject *WOT_CDECL release_drive(')[0]
        (folder/'offline_release_drive_law.hpp').write_text('#pragma once\n#include <cmath>\n#include <algorithm>\n'+law)
        binary=folder/'contacts.so'
        command=['c++','-std=c++17','-O0','-shared','-fPIC','-ffp-contract=off','-Wall','-Wextra','-Werror','-I'+str(folder),'-I'+str(big),'-I'+str(native)]
        command += [str(big/(name+'.cpp')) for name in ('stock_native_client_drive_runtime','stock_native_client_drive_contacts','stock_native_client_drive_roster','stock_native_bot_motion')]
        command += [str(native/(name+'.cpp')) for name in ('offline_math_batch_geometry',)]
        command += [str(ROOT/'tests/stock_native_client_drive_contacts_host.cpp'),'-o',str(binary)]
        subprocess.run(command,check=True,capture_output=True,text=True)
        cls.operation=ctypes.CDLL(str(binary)).player_contacts_fixture
        ptr=ctypes.POINTER(ctypes.c_double);cls.operation.argtypes=[ptr,ptr,ptr,ctypes.c_int,ptr,ptr];cls.operation.restype=ctypes.c_int
        cls.previous=dict(sys.modules)
        for name in ('gui','gui.mods','gui.mods.offline_lan_0922'):
            sys.modules[name]=types.ModuleType(name)
        package=sys.modules['gui.mods.offline_lan_0922']
        diagnostic=types.ModuleType('gui.mods.offline_lan_0922.worker_diagnostics');diagnostic.observed=lambda unused:lambda fn:fn;sys.modules[diagnostic.__name__]=diagnostic
        native_math=types.ModuleType('gui.mods.offline_lan_0922.native_math');native_math.call=lambda *args:None;native_math._load=lambda:None;package.native_math=native_math;sys.modules[native_math.__name__]=native_math
        base=Path(SOURCE)/'src/res/scripts/client/gui/mods/offline_lan_0922'
        for name in ('vehicle_physics','tank_collision','tank_contact_ledger'):
            spec=importlib.util.spec_from_file_location('gui.mods.offline_lan_0922.'+name,base/(name+'.py'));module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module);setattr(package,name,module);sys.modules[module.__name__]=module;setattr(cls,name,module)
        tree=ast.parse((base/'battle_runtime.py').read_text());method=next(node for outer in tree.body if isinstance(outer,ast.ClassDef) for node in outer.body if isinstance(node,ast.FunctionDef) and node.name=='_resolve_local_tank_contacts')
        env=dict(math=math,sys=sys,vehicle_physics=cls.vehicle_physics,tank_collision=cls.tank_collision,tank_contact_ledger=cls.tank_contact_ledger,_number=lambda v,default=0.:default if v is None else float(v))
        harness=ast.Module(body=[ast.ClassDef(name='Harness',bases=[],keywords=[],body=[method],decorator_list=[])],type_ignores=[])
        method.decorator_list=[];exec(compile(ast.fix_missing_locations(harness),'fixed-player-contact','exec'),env);cls.Harness=env['Harness']
    @classmethod
    def tearDownClass(cls):
        cls.folder.cleanup()
        for name in set(sys.modules)-set(cls.previous):
            if name.startswith('gui'):del sys.modules[name]
        for name,value in cls.previous.items():
            if name.startswith('gui'):sys.modules[name]=value
    def compare(self,rng,index):
        vp=self.vehicle_physics
        params=dict(mass=rng.uniform(6000.,80000.),powerW=rng.uniform(90000.,800000.),nativePowerRatio=1.,speedFwd=15.,speedBwd=6.,specificFriction=.07,brakeDecel=8.,rotSpd=.7,terrainResist=[1.,1.2,1.8],trackCenter=1.4)
        shape=[1.5,3.,-.5,2.]
        position=(rng.uniform(-2.,2.),0.,rng.uniform(-2.,2.));yaw=rng.uniform(-math.pi,math.pi);pitch=rng.uniform(-.4,.4);roll=rng.uniform(-.3,.3)
        speed=rng.uniform(-4.,12.);push=(rng.uniform(-2.,2.),rng.uniform(-2.,2.));throttle=rng.choice([-1.,0.,1.]);turn=rng.choice([-1.,0.,1.]);dt=rng.choice([0.,.02,.08,.1]);reaction=index%9==0
        start=(position[0]-rng.uniform(-1.,1.),position[1],position[2]-rng.uniform(-1.,1.)) if index%3 else None
        peers=[]
        for number in range(index%6):
            alive=number%3!=1;bot=number%2==0;immovable=not alive and not bot
            body=dict(id=1000020+number,network_id=20+number,engine_id=20+number,kind='bot' if bot else 'player',alive=alive,team=2,impulse=True,
                immovable=immovable,x=position[0]+rng.uniform(-3.,3.),y=rng.choice([0.,0.,5.]),z=position[2]+rng.uniform(-5.,5.),yaw=rng.uniform(-math.pi,math.pi),mass=rng.uniform(5000.,100000.),
                vx=rng.uniform(-4.,4.),vy=0.,vz=rng.uniform(-4.,4.),pitch=rng.uniform(-.4,.4),roll=rng.uniform(-.3,.3),shape=shape,
                contact_decel=(rng.uniform(.5,4.),rng.uniform(2.,10.)),push_yaw=rng.uniform(-.2,.2) if not alive else 0.,traverse_speed=rng.uniform(-.3,.3),traverse_torque=rng.uniform(0.,200000.),
                physical_velocity=(rng.uniform(-3.,3.),rng.uniform(-3.,3.)) if bot else None,ram_profile={})
            peers.append(body)
        h=self.Harness();h._local_physics=params;h._local_pitch=pitch;h._local_roll=roll;h._local_speed=speed;h._local_push_x,h._local_push_z=push;h._local_vertical_speed=0.
        h._local_drive_throttle=throttle;h._local_drive_turn=turn;h._local_contact_reaction_paid=reaction;h._local_contact_start_position=start
        h._sender=types.SimpleNamespace(forward=throttle);h.client=types.SimpleNamespace(team=1);h._local_ram_cooldowns={};h._local_ram_contacts=set();h._local_contact_pushes={};h._local_contact_impulses={};h._local_contact_log_time=1e30
        h._retry_native_ram_contact_proofs=lambda:None;h._collision_shape=lambda unused:shape;h._ram_profile=lambda *args,**kw:{};h._contact_tanks=lambda *args,**kw:peers
        h._poll_local_ram_contact_episodes=lambda *args:None;h._clock=lambda:5.;h._present_tank_collision=lambda *args:None;h._run_optional_feature=lambda *args,**kw:False;h._estimated_motion_time_us=lambda unused:5000000
        clear=index%11!=0;safe=index%13!=0;outside=index%17==0
        h._arena_pose_is_outside=lambda *args:outside;h._baked_pose_safe=lambda *args:safe;h._motion_is_clear=lambda *args,**kw:clear;h._resolve_local_turret_contacts=lambda entity,p,yaw,dt:p
        expected=h._resolve_local_tank_contacts(types.SimpleNamespace(typeDescriptor={}),position,yaw,dt)
        constants=[getattr(vp,name) for name in LAW_NAMES]+[params[name] for name in ('mass','powerW','nativePowerRatio','speedFwd','speedBwd','specificFriction','brakeDecel','rotSpd')]+params['terrainResist']
        constants += [getattr(vp,name) for name in ('SLOPE_GRIP_SDW_MIN_Y','SLOPE_GRIP_SDW_FULL_Y','SLOPE_GRIP_SDW_MIN','SLOPE_GRIP_SDW_FULL')]+shape+[1]
        state=list(position)+[yaw,pitch,roll,speed,0.]+list(push)+[0.,throttle,turn,throttle,index%7==0,reaction]
        raw=[]
        for p in peers:
            flags=(1 if p['alive'] else 0)|(2 if p['immovable'] else 0)|4|32|64
            raw += [p['id'],p['network_id'],p['engine_id'],p['kind']=='bot',flags]+[p[k] for k in ('x','y','z','yaw','mass','vx','vy','vz','pitch','roll')]+shape+list(p['contact_decel'])+[p['push_yaw'],p['traverse_speed'],p['traverse_torque'],p['physical_velocity'] is not None]+list(p['physical_velocity'] or (0.,0.))
        options=[dt,5.,start is not None]+list(start or (0.,0.,0.))+[clear,safe,outside]
        convert=lambda values:(ctypes.c_double*len(values))(*values);output=(ctypes.c_double*300)()
        count=self.operation(convert(constants),convert(state),convert(raw),len(peers),convert(options),output);self.assertGreater(count,0,index)
        target=list(expected)+[h._local_speed,h._local_push_x,h._local_push_z,len(h._local_contact_pushes)]
        for key,row in sorted(h._local_contact_pushes.items()):target+=row
        self.assertEqual(count,len(target),(index,dt,peers,list(output[:count]),target))
        for n,(actual,wanted) in enumerate(zip(output,target)):
            self.assertTrue(math.isclose(actual,wanted,rel_tol=2e-10,abs_tol=2e-10),(index,n,actual,wanted))
    def test_complete_player_contact_response(self):
        rng=random.Random(1513)
        for index in range(120):self.compare(rng,index)

if __name__=='__main__':unittest.main()
