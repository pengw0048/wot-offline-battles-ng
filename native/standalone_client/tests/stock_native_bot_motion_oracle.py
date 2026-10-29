"""Execute the fixed release's actual motion blocks under CPython 2.7."""
from __future__ import print_function
import copy
import imp
import json
import math
import os
import re
import sys
import textwrap
import types

base = os.path.join(sys.argv[1], 'src/res/scripts/client/gui/mods/offline_lan_0922')
for name in ('gui','gui.mods','gui.mods.offline_lan_0922'):
    package=types.ModuleType(name);package.__path__=[];sys.modules[name]=package
stub = types.ModuleType('gui.mods.offline_lan_0922.worker_diagnostics')
stub.observed = lambda unused: (lambda function: function)
sys.modules[stub.__name__] = stub
physics = imp.load_source('motion_oracle_physics', os.path.join(base, 'vehicle_physics.py'))
# These adapters exercise the fixed release's Python laws, independently from
# the generated C++ law used by the implementation under test.
physics._release_drive = lambda *unused: None
physics._native_contact_ground = lambda *unused: None
source = open(os.path.join(base, 'bot_runtime.py')).read()
begin = source.index('            elif not self.native_motion:\n                params = self._physics_params_for')
begin = source.index('\n', begin) + 1
end = source.index("            if diagnostic is not None:\n                diagnostic.phase('bot.aim_fire')", begin)
horizontal = compile(textwrap.dedent(source[begin:end]), 'pinned_horizontal_step', 'exec')

def position(state):
    return state['x'], state['y'], state['z']

def delta(a, b):
    return (a-b+math.pi) % (2.*math.pi)-math.pi

class Stub(object):
    pass

def reference(value):
    state = copy.deepcopy(value['state'])
    params = value['params']
    config = value['input']
    policy = value['policy']
    calls = []
    statuses = list(policy['statuses'])
    status_names = ('clear', 'crushed', 'soft', 'cap_crushed', 'hard')
    def record(index, *args):
        calls.append([index] + list(args))
    class Owner(object):
        def _physics_params_for(self, unused):
            return params
        def _rotation_fraction_for(self, unused, p, a, b, peers):
            record(0, *(list(p)+[a,b,0.,0.]))
            return policy['fraction']
        def _baked_pose_progress_clear(self, unused, p, a, q, b):
            record(1, *(list(p)+[a]+list(q)+[b]))
            return policy['pose']
        def _turret_pose_is_clear(self, unused, p, a, q, b):
            record(2, *(list(p)+[a]+list(q)+[b]))
            return policy['turret']
        def rotation_resolver(self, unused, p, a, b, descriptor, dt, now, cap, translation=(0.,0.)):
            record(3, *(list(p)+[a,b,dt,now,cap]+list(translation)))
            return policy['rotate']
        _wreck_rotation_probe = rotation_resolver
        def _planner_corridor_clear(self, p, yaw, speed, wet_escape=False,
                allow_shallow=False, hazard_only=False, maximum_distance=None):
            record(4, yaw, int(wet_escape), int(allow_shallow), -1. if maximum_distance is None else maximum_distance)
            return policy['clear']
        def motion_resolver(self, unused, p, yaw, speed, descriptor, dt, now):
            return self.motion(p, yaw, speed, dt, now, False, True)
        def motion(self, p, yaw, speed, dt, now, passive, commit):
            record(5, *(list(p)+[yaw,speed,dt,now,int(passive),int(commit)]))
            return status_names[statuses.pop(0) if statuses else 0]
        def _passive_motion_status(self, unused, p, yaw, speed, descriptor, dt, now, commit_enabled=True):
            return self.motion(p,yaw,speed,dt,now,True,commit_enabled)
        def _apply_world_contact_impact(self, state, speed, now):
            record(6,speed,now)
            if policy['kill']:
                state['alive'] = False
        def _invalidate_realised_motion(self, unused, yaw):
            record(7,yaw,1)
        def motion_report(self, unused, status, initial, final):
            record(9,status_names.index(status),initial,final)
        def _probe_timing_enabled(self):
            return False
        def _contact_motion_bodies(self, unused):
            return []
        def _contact_motion_body(self, state):
            return state
        def _update_vertical_motion(self, state, dt, p, yaw):
            return False
    self = Owner()
    self._turn_speeds = {1: state.get('_turn_speed', 0.)}
    self._hard_contact_grinds = {1: state.get('_hard_grinds', 0)}
    self._decision_cache = {}
    self._descriptors = {1: {}}
    self._contact_now = config['now']
    self._contact_players = ()
    self._physics_ground_probe = lambda: None
    self._combat_diagnostics = None
    self.navigator = Stub()
    self.navigator.report_blocked_step = lambda *unused: None
    self.navigator.report_hard_contact = lambda actor,p,target,yaw,now: record(8,*(list(p)+list(target)+[yaw,now]))
    collision = Stub()
    collision.translation_fraction = lambda *unused: policy['fraction']
    def slide(unused, movement, peers, first_fraction):
        record(10,*movement)
        return tuple(v*policy['fraction'] for v in movement)
    collision.slide_translation = slide
    def fraction(p,a,b,shape,peers,translation=(0.,0.)):
        record(0,*(list(p)+[a,b]+list(translation)))
        return policy['fraction']
    collision.rotation_fraction = fraction
    collision.wreck_yaw_inertia = lambda state: 1. if not state['alive'] else None
    module = dict(math=math, vehicle_physics=physics, tank_collision=collision,
        _position=position, _angle_delta=delta, _number=lambda v,default=0.: default if v is None else float(v),
        sys=Stub())
    module['sys'].stdout = Stub()
    module['sys'].stdout.write = lambda unused: None
    names = ('_hard_contact_response','_apply_tank_contact_response','_bleed_contact_push',
        '_try_wreck_swept_pose','_advance_wreck_yaw','_apply_wreck_contact_response')
    for name in names:
        start = source.index('    def '+name+'(')
        match = re.search(r'\n    (?:def |@)', source[start+1:])
        finish = start+1+match.start()+1 if match else len(source)
        exec(compile(textwrap.dedent(source[start:finish]),name,'exec'),module)
        setattr(Owner,name,module[name])
    Owner._retained_contact_speed = staticmethod(lambda speed,forced:
        max(0.,min(speed,forced)) if speed>=0. else min(0.,max(speed,forced)))
    state['_contact_motion_log_time'] = float('inf')
    state['_wreck_motion_log_time'] = float('inf')
    if value['op'] == 0:
        state['movement_dir'] = 1 if config['throttle']>.01 else -1 if config['throttle']<-.01 else 0
        state['rotation_dir'] = 1 if config['turn']>.01 else -1 if config['turn']<-.01 else 0
        state.pop('_contact_motor_turn',None)
        siege = Stub(); siege.ENABLED = 2
        siege.enabled_speed_limit = lambda unused: value.get('siege_limit')
        command = dict(brake=config['brake'],move_position=config.get('move_target'))
        scope = dict(module, self=self, state=state, step=config['dt'], now=config['now'],
            position=position(state), descriptor={}, neighbours=(), throttle=config['throttle'],
            turn=config['turn'], travel_sign=config['travel_sign'], steer_dir=state['rotation_dir'],
            path_clear=config['path_clear'],pose_frozen=config['pose_frozen'],
            motion_probe={'collision':config['probe_collision']}, exact_motion_owns_collision=config['exact_collision'],
            siege_motion_locked=config['siege_locked'],baked_shallow_escape=config['wet_escape'],
            controlled_shallow=lambda *unused: config['shallow_admitted'],controlled_shallow_commit=None,
            maximum_probe_distance=config.get('maximum_distance'),attempted_yaws={},passive_forwards={},
            command=command,navigation_grid=None,travel_yaw=state['yaw']+(math.pi if config['travel_sign']<0. else 0.),
            _motion_drive_pitch=lambda *unused: config['slope_pitch'],BOT_WATER_AVOID_DEPTH=1.,
            siege_mechanics=siege,timed_call=lambda diagnostic,name,function,*args: function(*args))
        exec(horizontal,scope)
        passive = scope['passive_forwards'].get(1,0.)
    else:
        response = value['response']
        if value['op']==1:
            self._apply_tank_contact_response(state,response,config['dt'],**value['options'])
        else:
            self._apply_wreck_contact_response(state,response,config['dt'])
        passive = 0.
    keys = ('x','y','z','yaw','speed')
    output = [state[key] for key in keys] + [self._turn_speeds[1]]
    output += [state.get(key,0.) for key in ('movement_dir','rotation_dir','_contact_forward_speed',
        '_direction_command','service_brake','last_drive_pitch','_rotation_contact_blocked','_native_rotation_refused_until')]
    output += [self._hard_contact_grinds[1]]
    output += [state.get(key,0.) for key in ('push_x','push_z','push_yaw')]
    output += [state['alive'],state.get('destructible_contact_speed'),state.get('_contact_motor_turn')]
    return dict(state=output,passive=passive,calls=calls)

for line in sys.stdin:
    if line.strip():
        print(json.dumps(reference(json.loads(line))))
