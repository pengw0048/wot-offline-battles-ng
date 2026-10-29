"""Differential traces against the pinned release's actual planner/traffic laws."""
import copy
import importlib
import inspect
import json
import math
import os
from pathlib import Path
import random
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


def body(actor=1, x=0., z=0., yaw=0., speed=0., alive=True, team=1):
    return dict(id=actor, team=team, alive=alive, position=(x, 0., z), yaw=yaw,
                velocity=(math.sin(yaw)*speed, 0., math.cos(yaw)*speed),
                half_length=3.5, half_width=1.7, shape=(1.7, 3.5, -.8, 2.),
                pitch=0., roll=0.)


def case(**changes):
    result = dict(reset=True, body=body(), slot=0, speed=0., dt=.2, now=0.,
                  decision_horizon=.4, stopping_distance=None, turn_speed_limit=1.,
                  native_rotation_refused=False, navigation_recovery_allowed=False,
                  order=dict(move_position=(0., 0., 30.), combat_mode='route'),
                  nav=None, nav_status='ready', nav_stop=None, nav_progress=None,
                  policy=0, traffic=0, peers=[])
    result.update(changes)
    return result


def encode(value):
    def flag(v):
        return str(int(bool(v)))
    def optional(v):
        return [flag(v is not None), str(0 if v is None else v)]
    def optional_point(v):
        return [flag(v is not None)] + list(map(str, (0., 0., 0.) if v is None else v))
    def body_row(b):
        return [str(b['id']), str(b['team']), flag(b['alive'])] + list(map(str,
            list(b['position'])+[b['yaw']]+list(b['velocity'])+
            [b['half_length'], b['half_width'], b['shape'][2], b['shape'][3], b['pitch'], b['roll']]))
    order = value['order']
    parts = [flag(value['reset'])] + body_row(value['body'])
    parts += list(map(str, [value['slot'], value['speed'], value['dt'], value['now'], value['decision_horizon']]))
    parts += optional(value['stopping_distance']) + optional(value['turn_speed_limit'])
    parts += [flag(value['native_rotation_refused']), flag(value['navigation_recovery_allowed'])]
    for key in ('move_position', 'aim_position', 'face_position'):
        parts += optional_point(order.get(key))
    for key in ('throttle_override', 'arrival_radius', 'target_id'):
        parts += optional(order.get(key))
    parts += [str(order.get('fire_range', 0.)), json.dumps(order.get('combat_mode', 'route')),
              json.dumps(order.get('parking_phase', ''))] + optional(order.get('parking_heading'))
    destination = order.get('move_position', order.get('aim_position', value['body']['position']))
    parts += list(map(str, value['nav'] if value['nav'] is not None else destination))
    parts += [json.dumps(value['nav_status'])] + [flag(value['nav_stop'] is not None), flag(value['nav_stop'])]
    parts += optional_point(value['nav_progress'])
    parts += list(map(str, [value['policy'], value['traffic'], len(value['peers'])]))
    for peer in value['peers']:
        parts += body_row(peer)
    return ' '.join(parts)


def reference(cases, modules):
    adapter_module, traffic_module = modules
    output = []
    for value in cases:
        if value['reset']:
            # Macro orders are supplied by the authoritative server; the
            # local adapter's director is not consulted by decide_with_order.
            adapter = adapter_module.BotAdapter.__new__(adapter_module.BotAdapter)
            adapter.driver = adapter_module.LocalDriver()
            adapter.navigation_target = None
            for key in ('_contact_peers', '_contact_attempts', '_contact_escapes',
                        '_wreck_attempts', '_wreck_history', '_withdrawal_attempts'):
                setattr(adapter, key, {})
            traffic = traffic_module.TrafficCoordinator()
        own = copy.deepcopy(value['body'])
        state = dict(own, slot=value['slot'], speed=value['speed'], dt=value['dt'],
                     now=value['now'], decision_horizon=value['decision_horizon'],
                     stopping_distance=value['stopping_distance'], turn_speed_limit=value['turn_speed_limit'],
                     native_rotation_refused=value['native_rotation_refused'],
                     navigation_recovery_allowed=value['navigation_recovery_allowed'],
                     collision_shape=own['shape'], neighbours=copy.deepcopy(value['peers']))
        probes = []
        def direction(yaw, maximum_distance=None, drive_direction=1.):
            probes.append([0, yaw, -1. if maximum_distance is None else maximum_distance,
                           drive_direction, state.get('navigation_probe_distance', -1.)])
            heading = math.atan2(math.sin(yaw), math.cos(yaw))
            if value['policy'] == 1:
                return False
            if value['policy'] == 2:
                return abs(heading) < .25
            if value['policy'] == 3:
                return abs(heading) > 2.8
            if value['policy'] == 4:
                return maximum_distance is not None and maximum_distance <= 2.
            return True
        def pose(yaw, push=False):
            probes.append([2 if push else 1, yaw, 0., 0., 0.])
            return False if value['policy'] == 5 else abs(yaw) < .2 if value['policy'] == 6 else True
        state['pose_clear'] = pose
        state['push_pose_clear'] = lambda yaw: pose(yaw, True)
        own['pose_clear'] = pose
        def navigate(actor, position, destination, order, current):
            current['navigation_status'] = value['nav_status']
            if value['nav_stop'] is not None:
                current['navigation_stop_at_target'] = value['nav_stop']
            if value['nav_progress'] is not None:
                current['navigation_progress_target'] = value['nav_progress']
            return value['nav'] if value['nav'] is not None else destination
        adapter.navigation_target = navigate
        command = adapter.decide_with_order(state, value['order'], direction)
        if value['traffic'] & 1:
            command = traffic.adjust(own['id'], own, command, value['peers'], value['now'], direction)
        if value['traffic'] & 2:
            command = traffic.safe_controls(own, command, value['peers'], value['now'],
                                           lambda: value['stopping_distance'] or 0., value['dt'])
        driver = adapter.driver.states.get(own['id'], {})
        command['driver'] = [driver.get(key, 0.) for key in
                             ('clock', 'stuck_time', 'recovery_time', 'recovery_count', 'recovery_side')]
        command['probes'] = probes
        output.append(command)
    return output


class NativePlannerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        source = os.environ.get('WOT_FORK_0101_SOURCE')
        if not source:
            raise unittest.SkipTest('Fixed v0.10.1 source input is required')
        cls.temporary = tempfile.TemporaryDirectory(prefix='native-bot-planner-')
        cls.binary = Path(cls.temporary.name)/'planner'
        command = ['c++', '-std=c++17', '-O1', '-Wall', '-Wextra', '-Werror',
                   '-I'+str(ROOT/'bigworld'), str(ROOT/'bigworld/stock_native_bot_planner.cpp'),
                   str(ROOT/'tests/stock_native_bot_planner_host.cpp'), '-o', str(cls.binary)]
        subprocess.run(command, check=True, capture_output=True, text=True)
        sys.path.insert(0, str(Path(source)/'src/res/scripts/client'))
        prefix = 'gui.mods.offline_lan_0922.ai.'
        cls.modules = (importlib.import_module(prefix+'adapter'), importlib.import_module(prefix+'traffic'))
        cls.python27 = os.environ.get('WOT_PYTHON27')
        if not cls.python27:
            try:
                cls.python27 = subprocess.check_output(['pyenv', 'which', 'python2.7'],
                    env=dict(os.environ, PYENV_VERSION='2.7.18'), text=True).strip()
            except (OSError, subprocess.CalledProcessError):
                cls.python27 = None
        cls.oracle = Path(cls.temporary.name)/'oracle.py'
        cls.oracle.write_text('import copy, json, math, os, sys, types\n'
            'sys.path.insert(0, sys.argv[1])\n'
            'for name in ("gui", "gui.mods"):\n'
            '    package=types.ModuleType(name)\n'
            '    package.__path__=[os.path.join(sys.argv[1], *name.split("."))]\n'
            '    sys.modules[name]=package\n'
            'from gui.mods.offline_lan_0922.ai import adapter, traffic\n' + inspect.getsource(reference) +
            '\nprint(json.dumps(reference(json.load(sys.stdin), (adapter, traffic))))\n')
        cls.source = source

    @classmethod
    def tearDownClass(cls):
        if hasattr(cls, 'temporary'):
            cls.temporary.cleanup()

    def compare(self, cases):
        process = subprocess.run([str(self.binary)], input='\n'.join(map(encode, cases))+'\n',
                                 text=True, capture_output=True)
        self.assertEqual(process.returncode, 0, process.stderr)
        actual = [json.loads(line) for line in process.stdout.splitlines()]
        if self.python27:
            oracle = subprocess.run([self.python27, '-B', str(self.oracle),
                str(Path(self.source)/'src/res/scripts/client')], input=json.dumps(cases),
                text=True, capture_output=True)
            self.assertEqual(oracle.returncode, 0, oracle.stderr)
            expected = json.loads(oracle.stdout)
        else:
            expected = reference(cases, self.modules)
        self.assertEqual(len(actual), len(expected))
        def close(a, b, key):
            if isinstance(a, (list, tuple)):
                self.assertEqual(len(a), len(b), key)
                for index, (first, second) in enumerate(zip(a, b)):
                    close(first, second, key+'[%d]' % index)
            elif isinstance(a, (int, float)) and not isinstance(a, bool):
                self.assertAlmostEqual(a, b, delta=1e-10, msg=key)
            else:
                self.assertEqual(a, b, key)
        defaults = dict(navigation_replan=False, navigation_recovery=False, withdrawal_aim=False,
                        traffic_mode='', forward_blocked_by=None, reverse_blocked_by=None,
                        recovery_probe_distance=None, brake=False)
        for index, (native, original) in enumerate(zip(actual, expected)):
            for key, result in native.items():
                close(result, original.get(key, defaults.get(key)), 'case %d %s %r' % (index, key, cases[index]))

    def test_route_and_tactical_orders(self):
        cases = []
        for yaw in (0., .7, -1.2, math.pi):
            for mode in ('route', 'engage', 'withdraw', 'low_health_retreat'):
                for policy in range(7):
                    cases.append(case(body=body(yaw=yaw), policy=policy, order=dict(
                        move_position=(0., 0., -12. if mode=='withdraw' else 30.),
                        aim_position=(0., 0., 50.), target_id=2, fire_range=100., combat_mode=mode)))
        cases += [case(order=dict(move_position=(0.,0.,0.), face_position=(10.,0.,0.), combat_mode='engage', throttle_override=0.)),
                  case(order=dict(move_position=(0.,0.,.1), parking_phase='waiting', parking_heading=90.)),
                  case(order={})]
        self.compare(cases)

    def test_persistent_recovery_and_navigation_wait(self):
        cases = []
        for policy in range(7):
            for tick in range(110):
                cases.append(case(reset=tick==0, now=tick*.2, policy=policy, native_rotation_refused=True))
        for policy in range(7):
            for tick in range(40):
                cases.append(case(reset=tick==0, now=tick*.2, policy=policy,
                                  nav=(0.,0.,0.), nav_status='pending', navigation_recovery_allowed=True))
        self.compare(cases)

    def test_hull_contacts_wrecks_and_braking(self):
        cases = []
        for alive in (True, False):
            for x, z in ((0.,6.5),(3.3,0.),(-3.3,2.),(0.,-6.),(0.,12.)):
                for policy in (0,1,4,5):
                    for tick in range(80):
                        cases.append(case(reset=tick==0, now=tick*.2, policy=policy,
                                          peers=[body(actor=2,x=x,z=z,alive=alive)], traffic=3))
        self.compare(cases)

    def test_seeded_moving_and_angled_rosters(self):
        rng = random.Random(1513)
        cases = []
        for index in range(200):
            yaw=rng.uniform(-math.pi,math.pi);speed=rng.uniform(-3.,10.)
            own=body(yaw=yaw,speed=speed)
            peers=[body(actor=2+j,x=rng.uniform(-12.,12.),z=rng.uniform(-12.,12.),
                        yaw=rng.uniform(-math.pi,math.pi),speed=rng.uniform(0.,5.),alive=bool(j%2)) for j in range(4)]
            cases.append(case(body=own,speed=speed,peers=peers,traffic=3,stopping_distance=rng.uniform(0.,8.),
                              order=dict(move_position=(rng.uniform(-30.,30.),rng.uniform(-15.,15.),rng.uniform(-30.,30.)),combat_mode='route')))
        self.compare(cases)

    def test_crossing_head_on_and_parked_clearance_leases(self):
        cases=[]
        for policy in (0,1,5):
            for tick in range(55):
                first=body(actor=1,z=-4.,yaw=0.)
                second=body(actor=2,z=4.,yaw=math.pi)
                for actor,peer,target in ((first,second,(0.,0.,30.)),(second,first,(0.,0.,-30.))):
                    cases.append(case(reset=tick==0 and actor['id']==1,body=actor,peers=[peer],
                                      now=tick*.2,policy=policy,traffic=3,
                                      order=dict(move_position=target,combat_mode='route')))
        for tick in range(55):
            first=body(actor=1,z=-7.,speed=.1)
            second=body(actor=2,z=0.)
            cases.append(case(reset=tick==0,body=first,speed=.1,peers=[second],now=tick*.2,
                              traffic=3,stopping_distance=1.))
            cases.append(case(reset=False,body=second,peers=[first],now=tick*.2,traffic=3,
                              order=dict(move_position=(0.,0.,0.),aim_position=(0.,0.,100.),
                                         combat_mode='engage',throttle_override=0.)))
        for tick in range(25):
            first=body(actor=1,x=-4.,yaw=math.pi/2.,speed=3.)
            second=body(actor=2,z=-4.,speed=3.)
            for actor,peer,target in ((first,second,(30.,0.,0.)),(second,first,(0.,0.,30.))):
                cases.append(case(reset=tick==0 and actor['id']==1,body=actor,speed=3.,peers=[peer],
                                  now=tick*.2,traffic=3,stopping_distance=2.,
                                  order=dict(move_position=target,combat_mode='route')))
        self.compare(cases)

    def test_changing_targets_and_realised_pose_history(self):
        rng=random.Random(144619)
        cases=[]
        for run in range(20):
            x=z=yaw=0.
            for tick in range(75):
                if tick%5:
                    x+=rng.uniform(-.2,.25);z+=rng.uniform(-.2,.25);yaw+=rng.uniform(-.1,.15)
                speed=rng.uniform(-.2,.8)
                own=body(x=x,z=z,yaw=yaw,speed=speed)
                own['pitch']=rng.uniform(-.4,.4);own['roll']=rng.uniform(-.4,.4)
                target=(2.*(tick//20),rng.uniform(-.1,.1),25.)
                peers=[body(actor=2,x=3.4,z=2.),body(actor=3,x=-.5,z=-7.,alive=False)]
                cases.append(case(reset=tick==0,body=own,peers=peers,speed=speed,now=tick*.17,dt=.17,
                                  traffic=3,policy=(run+tick//15)%7,native_rotation_refused=tick%4==0,
                                  stopping_distance=.5,order=dict(move_position=target,combat_mode='route')))
        self.compare(cases)

    def test_python27_decimal_brake_key_is_not_rounded_product(self):
        if not self.python27:
            self.skipTest('The exact CPython 2.7 rounding oracle is required')
        cases=[]
        for first,second in ((2.675,2.67),(-2.675,-2.67),(2.685,2.69),(.125,.13),(-.125,-.13)):
            for tick,target in enumerate((first,second)):
                own=body(yaw=0. if target>0. else math.pi,speed=1.)
                cases.append(case(reset=tick==0,body=own,speed=1.,stopping_distance=100. if tick==0 else 0.,
                    now=tick*.2,decision_horizon=0.,order=dict(move_position=(0.,0.,target),combat_mode='engage',arrival_radius=.1)))
        self.compare(cases)


if __name__ == '__main__':
    unittest.main()
