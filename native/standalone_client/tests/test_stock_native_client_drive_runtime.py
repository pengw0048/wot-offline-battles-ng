"""Whole local drive-slice ordering and laws against the pinned method body."""
import ast
import contextlib
import ctypes
import io
import math
import os
from pathlib import Path
import random
import subprocess
import types
from test_stock_native_client_drive_contacts import ContactDifferential,LAW_NAMES,ROOT,SOURCE

class PlayerDriverDifferential(ContactDifferential):
    @classmethod
    def setUpClass(cls):
        super().setUpClass();folder=Path(cls.folder.name);native=Path(SOURCE)/'native';big=ROOT/'bigworld';binary=folder/'driver.so'
        subprocess.run(['c++','-std=c++17','-O0','-shared','-fPIC','-ffp-contract=off','-Wall','-Wextra','-Werror',
            '-I'+str(folder),'-I'+str(big),'-I'+str(native),str(big/'stock_native_client_drive_runtime.cpp'),str(big/'stock_native_bot_motion.cpp'),
            str(ROOT/'tests/stock_native_client_drive_runtime_host.cpp'),'-o',str(binary)],check=True,capture_output=True,text=True)
        cls.driver=ctypes.CDLL(str(binary)).player_driver_fixture;ptr=ctypes.POINTER(ctypes.c_double);cls.driver.argtypes=[ptr]*6;cls.driver.restype=ctypes.c_int
        base=Path(SOURCE)/'src/res/scripts/client/gui/mods/offline_lan_0922';tree=ast.parse((base/'battle_runtime.py').read_text())
        wanted=set('''_drive_local_step _canonicalize_local_attitude _smoothed_drive_pitch _local_suspension_state_snapshot
_restore_local_suspension_state _apply_suspension_slope_slide _apply_slope_slide _settle_airborne_lateral_contact _resettle_local_suspension_endpoint'''.split())
        methods=[node for outer in tree.body if isinstance(outer,ast.ClassDef) for node in outer.body if isinstance(node,ast.FunctionDef) and node.name in wanted]
        for node in methods:node.decorator_list=[]
        env=dict(math=math,sys=__import__('sys'),vehicle_physics=cls.vehicle_physics,
            critical_damage=types.SimpleNamespace(stat_factor=lambda entity,name:entity.factors[name]),
            tank_collision=types.SimpleNamespace(rotation_fraction=lambda *args,**kwargs:env['fraction']),
            _number=lambda value,default=0.:default if value is None else float(value),
            _angle_delta=lambda current,target:(target-current+math.pi)%(2.*math.pi)-math.pi,
            _engine_rotation=lambda yaw:(0.,0.,yaw),GROUND_PLANE_EPSILON=.35)
        harness=ast.Module(body=[ast.ClassDef(name='Harness',bases=[],keywords=[],body=methods,decorator_list=[])],type_ignores=[])
        exec(compile(ast.fix_missing_locations(harness),'fixed-full-player-drive','exec'),env);cls.FullHarness=env['Harness'];cls.environment=env
    def drive_case(self,rng,index):
        vp=self.vehicle_physics
        params=dict(mass=25000.,powerW=280000.,nativePowerRatio=1.,speedFwd=14.,speedBwd=6.,specificFriction=.07,brakeDecel=4.,rotSpd=.7,terrainResist=[1.,1.2,1.8],trackCenter=1.4,rotationIsAroundCenter=index%3==0)
        state=dict(position=(1.,.15,2.),yaw=rng.uniform(-math.pi,math.pi),pitch=rng.uniform(-.3,.3),roll=rng.uniform(-.25,.25),speed=rng.uniform(-5.,13.),turn_speed=rng.uniform(-.4,.4),vertical_speed=rng.uniform(-2.,1.),
            suspension_pitch_velocity=.1,suspension_roll_velocity=-.1,airborne=index%9==0,direction_command=rng.choice([-1.,0.,1.]),service_brake=index%6==0,grind=index%4,
            air_lateral=(0.,0.),slide_speed=0.,slope_tangent=0.,downhill=(0.,0.,0.),ground_plane=None,siege_braking=None)
        if index%17==0:state['pitch']=2.;state['roll']=.7
        if index%7==0:state.update(air_lateral=(.5,-.2),slide_speed=.4,slope_tangent=.7,downhill=(-.6,0.,-.8),ground_plane=dict(gradient_x=.4,gradient_z=.3,center_x=1.,center_z=2.,center_y=.15))
        controls=[rng.choice([-1.,-.25,0.,.5,1.]),rng.choice([-1.,0.,1.]),index%11==0,index%13==0,rng.choice([-1.,0.,1.])]
        flags=[True,index%43!=0,index%47!=0,index%53==0,index%19==0,index%23==0,index%29==0,index%31==0,rng.choice([.5,1.]),rng.choice([.7,1.]),rng.choice([1.,1.1]),rng.choice([.8,1.])]
        dt=rng.choice([0.,.02,.04,.08,.1]);detail=index%4!=0;guard=index%6==0
        fraction=.4 if index%8==0 else 1.;arena=index%37!=0;sweep=index%5!=0;complete=index%41!=0;blocked=index%27==0;air=-1
        statuses=([5,0,0,0] if index%6==0 else [3,0,0] if index%10==0 else [4,0,0] if index%14==0 else [0]*12)
        normal=(1.,.2,-.5) if index%12==0 else None;held=index%16==0;recover=index%15==0
        correction=(.015,0.,-.012) if index%5==0 else (0.,0.,0.);impulse=.1 if index%5==0 else 0.;dy=-.05
        if index%21==0:state['siege_braking']=False
        h=self.FullHarness()
        for key,value in state.items():setattr(h,'_local_'+key,value)
        h._local_physics=params;h._local_descriptor=None;h._local_suspension_params={} if detail else None;h._local_suspension_disabled=False;h._local_suspension_failed_this_tick=False;h._local_suspension_support_vertical_speed=0.;h._local_suspension_support_gradient=None
        h._local_spring_ground_memory=h._local_pseudo_ground_memory=None;h._local_fall_armed=True;h._local_left_flying=h._local_right_flying=False;h._local_surface_up_cosine=None;h._pending_landing_impacts=[]
        h._local_drive_pitch_history=None;h._local_smooth_drive_pitch=0.;h._local_drive_turn=h._local_drive_throttle=h._local_reaction_rate=0.;h._local_support_tick_pose=h._local_support_motion_pose=None
        h._local_push_x=h._local_push_z=0.;h._local_siege_aim_pitch=0.;h._local_siege_pending=None
        h._sender=types.SimpleNamespace(forward=controls[0],turn=controls[1],handbrake=controls[2]);h._server=types.SimpleNamespace(vehicle_id=1)
        entity=types.SimpleNamespace(typeDescriptor={},isAlive=lambda:flags[1],isCrewActive=flags[2],is_tracked=flags[4],is_engine_dead=flags[5],factors={'mobility':flags[8],'traverse':flags[9]},_offlineStunFactors={'speed':flags[11]})
        h._server_entity=lambda unused:entity;h._battle_result=True if flags[3] else None;h._overturn_level=2 if flags[6] else 0
        h._local_model=h._local_matrix=object();h._binding=None;h._destructibles=None;h._detached_turret_obstacles=object() if guard else None
        h._local_turret_pose=lambda p,yaw,pitch,roll:dict(position=p,yaw=yaw,pitch=pitch,roll=roll)
        h._turret_motion_is_clear=lambda *args:complete
        h._drive_pitch=lambda p,yaw:.12;h._local_siege_drive_locked=lambda unused:flags[7];h._active_engine_power_factor=lambda:flags[10]
        def auto(entity,turn,throttle,tracks_blocked):
            if turn or throttle or tracks_blocked or not controls[3]:return turn
            return -1. if controls[4]<-.4001 else 1. if controls[4]>.4001 else 0.
        h._local_autorotation_turn=auto;cursor=[0]
        def motion(entity,p,yaw,speed,dt,allow_crush_drive=False,hull_yaw=None):
            status=statuses[cursor[0]] if cursor[0]<len(statuses) else 0;cursor[0]+=1
            h._local_motion_soft_block=status==3;h._local_motion_cap_crushed=status==4;h._local_motion_status=('clear','crushed','approach','soft','crushed','hard','pending')[status]
            h._local_world_collision_trace={} if normal is None else dict(hit=p,normal=normal)
            return status in (0,1,2)
        h._motion_is_clear=motion;h._apply_world_contact_impact=lambda *args:None;h._collision_shape=lambda unused:(1.5,3.,-.5,2.);h._contact_tanks=lambda *args,**kw:[]
        self.environment['fraction']=fraction
        def recovery(*args,**kwargs):
            p,yaw=args[1:3];h._local_reaction_rate=.12;h._local_contact_reaction_paid=True
            return ((p[0]+.02,p[1],p[2]),yaw+.01)
        h._recover_local_held_peer_turn=recovery if held else lambda *args:None;h._recover_local_world_turn=recovery if recover else lambda *args:None
        h._arena_rotation_is_clear=lambda *args:arena
        def pose(*args):h._local_motion_status='clear' if sweep else 'hard';h._local_world_collision_trace={};return sweep
        h._pose_sweep_is_clear=pose;h._ensure_local_suspension_params=lambda unused:{} if detail else None
        def vertical(entity,p,yaw,dt,support_height_delta=0.,support_speed_delta=0.):
            h._local_suspension_failed_this_tick=False;h._local_suspension_slide_motion_this_tick=False
            saved_slide=h._local_slide_speed;saved_lateral=h._local_air_lateral
            if detail and dt>0.:p=h._apply_suspension_slope_slide(p,yaw,dt,entity);h._local_suspension_slide_motion_this_tick=True
            p=(p[0],p[1]+dy*dt+support_height_delta,p[2]);h._local_support_rise_blocked=blocked
            if blocked:
                p=h._local_support_tick_pose or p
                if h._local_suspension_slide_motion_this_tick:h._local_slide_speed=saved_slide;h._local_air_lateral=saved_lateral
            if air>=0:h._local_airborne=bool(air)
            return p
        h._update_vertical_motion=vertical
        def contacts(entity,p,yaw,dt):h._local_speed+=impulse;return tuple(p[i]+correction[i] for i in range(3))
        h._resolve_local_tank_contacts=contacts;h._ground_pitch=lambda *args:None;h._sample_ground_plane=lambda *args:None;h._terrain_support=lambda *args:(None,None);h._clock=lambda:5.
        for name in ('_report_local_contact_tick','_report_local_hydraulic_motion','_report_local_motion_stall','_report_destructible_verdict'):
            setattr(h,name,lambda *args,**kwargs:None)
        h._vector=lambda p:p;h.presentations=0
        def present(entity,dt):h.presentations+=1;return h._local_position
        h._update_local_presentation=present;h._avatar=types.SimpleNamespace(updateOwnVehiclePosition=lambda *args:None)
        with contextlib.redirect_stdout(io.StringIO()):h._drive_local_step(dt)
        constants=[getattr(vp,name) for name in LAW_NAMES]+[params[name] for name in ('mass','powerW','nativePowerRatio','speedFwd','speedBwd','specificFriction','brakeDecel','rotSpd')]+params['terrainResist']+list(vp.HARD_CONTACT_YAW_DELTAS)
        constants += [getattr(vp,name) for name in ('HARD_CONTACT_BRAKE_DECAY','HARD_CONTACT_STOP_SPEED','HARD_CONTACT_ENTRY_FACTOR','HARD_CONTACT_SLIDE_DECAY','HARD_CONTACT_GRIND_TICKS','SLOPE_GRIP_SDW_MIN_Y','SLOPE_GRIP_SDW_FULL_Y','SLOPE_GRIP_SDW_MIN','SLOPE_GRIP_SDW_FULL','SLIDE_DRAG','SLIDE_MAX')]+[params['rotationIsAroundCenter']]
        initial=list(state['position'])+[state[k] for k in ('yaw','pitch','roll','speed','turn_speed','vertical_speed','suspension_pitch_velocity','suspension_roll_velocity','airborne','direction_command','service_brake','grind')]+list(state['air_lateral'])+[state['slide_speed'],state['slope_tangent'],state['downhill'][0],state['downhill'][2],state['ground_plane'] is not None]
        if state['ground_plane']:initial += [state['ground_plane']['gradient_x'],state['ground_plane']['gradient_z']]
        initial += [-1 if state['siege_braking'] is None else int(state['siege_braking'])]
        policy=[dt,.12,fraction,arena,sweep,complete,detail,blocked,guard,air,dy,impulse]+list(correction)+[held,recover,normal is not None]+list(normal or (0.,0.,0.))+[len(statuses)]+statuses
        convert=lambda v:(ctypes.c_double*len(v))(*v);output=(ctypes.c_double*100)();count=self.driver(convert(constants),convert(initial),convert(controls),convert(flags),convert(policy),output)
        self.assertGreater(count,0,index)
        target=list(h._local_position)+[getattr(h,'_local_'+name) for name in ('yaw','pitch','roll','speed','turn_speed','vertical_speed','suspension_pitch_velocity','suspension_roll_velocity','direction_command','service_brake','grind')]+list(h._local_air_lateral)+[h._local_slide_speed,h._local_drive_throttle,h._local_drive_turn,h._local_reaction_rate,h._local_airborne,getattr(h,'_local_support_rise_blocked',False),h.presentations]
        self.assertEqual(count,len(target),index)
        for n,(actual,wanted) in enumerate(zip(output,target)):
            self.assertTrue(math.isclose(actual,wanted,rel_tol=2e-10,abs_tol=2e-10),(index,n,actual,wanted,state,flags,policy))
    def test_full_player_drive_slice(self):
        rng=random.Random(1513)
        for index in range(180):self.drive_case(rng,index)
