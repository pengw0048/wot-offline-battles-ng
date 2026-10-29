"""Differential checks against the pinned visible-player support chain."""
import ast
import copy
import ctypes
import importlib.util
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
TUNING = '''GRAVITY CONTACT_PENETRATION AIRBORNE_ANGULAR_DAMPING AIRBORNE_ANGULAR_SPEED_LIMIT
FREEZE_ACCEL_EPSILON FREEZE_VEL_EPSILON FREEZE_ANG_ACCEL_EPSILON FREEZE_ANG_VEL_EPSILON SERVER_PHYSICS_MAX_SUBSTEPS'''.split()


def params(physics):
    out = dict(mass=10000., pitch_inertia=30000., roll_inertia=10000., fixed_step=1./60., constraint_iterations=10,
               width=2.5, clearance=.3, rest_length=.5, contact_memory_distance=.5, footprint_half_width=.15,
               springs=[], pseudo_contacts=[], rigid_contacts=[], turret_origin=(0., 1., 0.), turret_contacts=[])
    for side in (-1, 1):
        for index in range(5):
            out['springs'].append(dict(x=side, y=0., z=index-2., side='left' if side<0 else 'right',
                stiffness=10000.*physics.GRAVITY/2., damping=2000., static_compression=.2, max_compression=.4,
                max_force=100000., rest_length=.5, footprint_front=.5, footprint_rear=.5))
        for index in range(4):
            out['pseudo_contacts'].append(dict(x=side, y=0., z=index-1.5, kind='track',
                side='left' if side<0 else 'right', penetration=.04))
    for index in range(4):
        out['pseudo_contacts'].append(dict(x=0., y=.3, z=index-1.5, side=None, kind='body', penetration=.1))
    for x in (-1.25, 1.25):
        for y in (.3, 2.):
            for z in (-2.5, 2.5):
                out['rigid_contacts'].append(dict(x=x, y=y, z=z, side=None, kind='rigid', penetration=.1))
    for x in (-.5, .5):
        for y in (0., 1.):
            for z in (-.5, .5):
                out['turret_contacts'].append((x,y,z))
    for name in ('springs','pseudo_contacts','rigid_contacts','turret_contacts'):
        out[name]=tuple(out[name])
    return out


@unittest.skipUnless(SOURCE, 'set WOT_FORK_0101_SOURCE to the fixed gameplay checkout')
class ClientDriveDifferential(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.folder = tempfile.TemporaryDirectory(prefix='native-client-drive-')
        folder = Path(cls.folder.name)
        binary = folder/'drive.so'
        subprocess.run(['c++', '-std=c++17', '-O0', '-shared', '-fPIC', '-ffp-contract=off', '-Wall', '-Wextra', '-Werror',
            '-I'+str(ROOT/'bigworld'), '-I'+str(Path(SOURCE)/'native'),
            str(ROOT/'bigworld/stock_native_client_drive.cpp'), str(ROOT/'bigworld/stock_native_bot_step_state.cpp'),
            str(ROOT/'tests/stock_native_client_drive_host.cpp'), '-o', str(binary)], check=True)
        cls.native = ctypes.CDLL(str(binary)).client_drive_fixture
        cls.native.argtypes = [ctypes.POINTER(ctypes.c_double), ctypes.POINTER(ctypes.c_double)]
        cls.native.restype = ctypes.c_int
        base = Path(SOURCE)/'src/res/scripts/client/gui/mods/offline_lan_0922'
        # Run the original complete local method bodies, with only native scene
        # IO replaced by an analytical surface. None of the migrated policy is mocked.
        diagnostic = types.ModuleType('gui.mods.offline_lan_0922.worker_diagnostics')
        diagnostic.observed = lambda unused: lambda function: function
        cls.previous_modules = dict(sys.modules)
        sys.modules[diagnostic.__name__] = diagnostic
        spec = importlib.util.spec_from_file_location('client_drive_physics',base/'vehicle_physics.py')
        cls.physics = importlib.util.module_from_spec(spec);spec.loader.exec_module(cls.physics)
        physics = cls.physics
        tree = ast.parse((base/'battle_runtime.py').read_text())
        wanted = '''_local_suspension_ground_samples _local_suspension_pseudo_ground_samples
_local_suspension_predicted_probe_height _local_suspension_path_is_supported _local_suspension_rise_is_continuous
_commit_local_suspension_metadata _update_suspension_vertical_motion'''.split()
        methods = [node for outer in tree.body if isinstance(outer,ast.ClassDef) for node in outer.body
                   if isinstance(node,ast.FunctionDef) and node.name in wanted]
        harness = ast.Module(body=[ast.ClassDef(name='Harness',bases=[],keywords=[],body=methods,decorator_list=[])],type_ignores=[])
        env = dict(math=math, vehicle_physics=physics, GROUND_PLANE_EPSILON=.35,
                   tank_collision=types.SimpleNamespace(support_rise_is_obstacle=lambda a,b,c:b-a>c+.02))
        exec(compile(ast.fix_missing_locations(harness),'pinned-local-support','exec'),env)
        cls.Harness = env['Harness']
        for name in ('gui','gui.mods','gui.mods.offline_lan_0922'):
            if name not in sys.modules:sys.modules[name]=types.ModuleType(name)
        package=sys.modules['gui.mods.offline_lan_0922'];package.vehicle_physics=physics
        spec=importlib.util.spec_from_file_location('gui.mods.offline_lan_0922.environment_modules',base/'environment_modules.py')
        module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
        package.environment_modules=module;sys.modules[module.__name__]=module

    @classmethod
    def tearDownClass(cls):
        cls.folder.cleanup()
        for name in set(sys.modules)-set(cls.previous_modules):
            if name.startswith('gui'):del sys.modules[name]
        for name,value in cls.previous_modules.items():
            if name.startswith('gui'):sys.modules[name]=value

    def compare(self, value):
        h=self.Harness();h._local_suspension_params=params(self.physics)
        names='''position yaw pitch roll vertical_speed suspension_pitch_velocity suspension_roll_velocity
suspension_support_vertical_speed speed turn_speed airborne fall_armed left_flying right_flying'''.split()
        state=value['state']
        for name,raw in zip(names,state):setattr(h,'_local_'+name,copy.deepcopy(raw))
        previous=value.get('previous');h._local_ground_plane=None
        if previous:
            gx,gz,cy=previous;length=math.sqrt(1.+gx*gx+gz*gz)
            h._local_ground_plane=dict(center_x=0.,center_z=0.,center_y=cy,gradient_x=gx,gradient_z=gz,normal=(-gx/length,1./length,-gz/length))
        h._local_suspension_support_gradient=tuple(previous[:2]) if previous and value.get('gradient') else None
        h._local_spring_ground_memory=h._local_pseudo_ground_memory=None
        h._local_support_tick_pose=value['tick'];h._local_support_motion_pose=value.get('motion')
        h._local_frame_stages=None;h._local_drive_turn=1.;h._local_drive_throttle=1.
        h._local_direction_command=1.;h._local_service_brake=True
        h._prepared_ground_filter=lambda unused:None;h._suspension_ground_probe_layers=()
        gx,gz,cy,holes,missing=value['world']
        def ground(x,z,low,high,flat_maximum_y=None,prepared_filter=None):
            if missing or (holes and abs(math.remainder(z,2.))<.08):return None
            height=cy+gx*x+gz*z;up=1./math.sqrt(1.+gx*gx+gz*gz)
            return height if low-.01<=height<=high+.01 and up>.5 and (flat_maximum_y is None or height<=flat_maximum_y+.01 or up<.995) else None
        h._suspension_ground_y=ground;h._ground_y=lambda x,z,hint,allow_wide:None if missing else cy+gx*x+gz*z
        h._entity_turret_yaw=lambda unused:value['turret'];h.impacts=[]
        h._apply_landing_impact=lambda entity,impact,normal_impact:h.impacts.append(impact)
        expected=h._update_suspension_vertical_motion(None,state[0],state[1],value['dt'],value['height_delta'],value['speed_delta'])
        raw=[getattr(self.physics,name) for name in TUNING]+list(state[0])+list(state[1:])+list(value['tick'])
        raw += [value.get('motion') is not None]+list(value.get('motion') or (0.,0.,0.))
        raw += [value['dt'],value['turret'],value['height_delta'],value['speed_delta']]+value['world']
        raw += [previous is not None]+list(previous or (0.,0.,0.))+[bool(value.get('gradient'))]
        actual=(ctypes.c_double*400)();count=self.native((ctypes.c_double*len(raw))(*raw),actual)
        self.assertGreater(count,0)
        target=list(expected)+[getattr(h,'_local_'+name) for name in names[1:]]
        for index,(a,e) in enumerate(zip(actual,target)):
            self.assertTrue(math.isclose(a,e,rel_tol=3e-10,abs_tol=3e-10),(index,a,e,value))
        k=len(target);self.assertEqual(bool(actual[k]),h._local_support_rise_blocked)
        k+=2;self.assertEqual(bool(actual[k]),bool(h.impacts))
        if h.impacts:self.assertAlmostEqual(actual[k+1],h.impacts[0],places=9)
        k+=2;self.assertEqual(bool(actual[k]),h._local_ground_plane is not None)
        if h._local_ground_plane:
            for index,key in enumerate(('gradient_x','gradient_z','center_y')):
                self.assertAlmostEqual(actual[k+1+index],h._local_ground_plane[key],places=9)

    def test_pinned_local_ground_vertical_rules(self):
        rng=random.Random(1513)
        for index in range(160):
            gx,gz=rng.uniform(-.3,.3),rng.uniform(-.3,.3)
            x,z=rng.uniform(-2.,2.),rng.uniform(-2.,2.)
            state=[(x,gx*x+gz*z+rng.uniform(-.05,.15),z),rng.uniform(-math.pi,math.pi),rng.uniform(-.2,.2),rng.uniform(-.2,.2),
                   rng.uniform(-1.,.5),rng.uniform(-.3,.3),rng.uniform(-.3,.3),rng.uniform(-.2,.2),4.,.2,False,True,False,False]
            if index%7==0:state[4]=-8.;state[10]=True;state[0]=(x,gx*x+gz*z+.2,z)
            if index%11==0:state[2]=1.6;state[3]=.4
            if index%13==0:state[11]=False;state[0]=(x,100.,z)
            value=dict(state=state,tick=state[0],motion=(x-.02,state[0][1],z-.05),dt=0. if index%5==0 else rng.uniform(.001,.1),
                turret=rng.uniform(-2.,2.),height_delta=.03 if index%5==0 else 0.,speed_delta=.02 if index%17==0 else 0.,
                world=[gx,gz,0.,index%9==0,index%23==0],previous=[gx,gz,0.] if index%3 else None,gradient=index%4==0)
            self.compare(value)


class ClientDriveLifecycle(unittest.TestCase):
    def facade(self):
        calls=[]
        native=types.ModuleType('offline_client_drive_native')
        native.create=lambda battle:calls.append(('create',battle)) or object()
        native.close=lambda owner:calls.append(('close',owner))
        native.restore=lambda owner,battle:calls.append(('restore',owner,battle))
        native.frame=lambda owner,battle,now:calls.append(('frame',owner,battle,now))
        native.ingress=lambda owner,battle,event,now:calls.append(('ingress',owner,battle,event,now)) or True
        native.advance=lambda *args:((1.,2.,3.),4.,[(1.,0.,3.)],(0.,1.,0.))
        previous=sys.modules.get(native.__name__);sys.modules[native.__name__]=native
        try:
            spec=importlib.util.spec_from_file_location('native_client_drive_fixture',ROOT/'bigworld/stock_native_client_drive_facade.py')
            module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
        finally:
            if previous is None:del sys.modules[native.__name__]
            else:sys.modules[native.__name__]=previous
        return module,calls

    def test_one_owner_and_ordered_event_cleanup(self):
        module,calls=self.facade();impacts=[]
        battle=types.SimpleNamespace(_worker_mode=False,_clock=lambda:8.,_local_suspension_params={},
            _entity_turret_yaw=lambda entity:.3,_apply_landing_impact=lambda *a,**kw:impacts.append((a,kw)))
        owner=module.prepare(battle);self.assertIs(owner,module.prepare(battle));self.assertEqual(len(calls),1)
        module.frame(battle,7.);self.assertTrue(module.ingress(battle,{'event':1}));module.restore(battle)
        self.assertEqual([row[0] for row in calls],['create','frame','ingress','restore'])
        result=module.advance(battle,None,(0.,0.,0.),0.,.1,0.,0.)
        self.assertEqual(result,(1.,2.,3.));self.assertEqual(battle._landing_contact_witnesses,[(1.,0.,3.)])
        self.assertEqual(len(impacts),1)
        module.retire(battle);module.retire(battle)
        self.assertEqual([row[0] for row in calls].count('close'),1)
        self.assertIsNone(battle._native_client_drive_owner)

    def test_worker_remains_separate(self):
        module,calls=self.facade();battle=types.SimpleNamespace(_worker_mode=True)
        self.assertIsNone(module.prepare(battle));self.assertEqual(calls,[])

    @unittest.skipUnless(SOURCE,'fixed gameplay source is required')
    def test_exact_preparation_seams_and_complete_method_boundary(self):
        spec=importlib.util.spec_from_file_location('client_drive_bindings_fixture',ROOT/'bigworld/stock_native_client_drive_bindings.py')
        module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
        original=(Path(SOURCE)/'src/res/scripts/client/gui/mods/offline_lan_0922/battle_runtime.py').read_text()
        prepared=module.battle(original)
        self.assertEqual(prepared.count('return native_client_drive.advance('),1)
        self.assertEqual(prepared.count('native_client_drive.ingress(self, event)'),1)
        self.assertIn('result = self._apply_destructible_event_visible(event)',prepared)
        self.assertIn('native_client_drive.restore(self)',prepared)
        self.assertIn('native_client_drive.retire(self)',prepared)
        with self.assertRaises(ValueError):module.battle(prepared)


@unittest.skipUnless(SOURCE, 'fixed gameplay source is required')
class ClientDriveScene(unittest.TestCase):
    def test_native_layer_filter_and_terminal_failures(self):
        from native_test_compile_flags import native_test_compile_flags
        with tempfile.TemporaryDirectory(prefix='native-client-drive-scene-') as folder:
            binary=Path(folder)/'scene'
            big=ROOT/'bigworld';native=Path(SOURCE)/'native'
            command=['c++','-std=c++17','-O0','-Wall','-Wextra','-Werror',
                     *native_test_compile_flags('c++'),'-I'+str(big),'-I'+str(native),
                     str(ROOT/'tests/stock_native_client_drive_scene_host.cpp')]
            command += [str(big/(name+'.cpp')) for name in ('stock_native_client_drive_scene',
                'stock_native_world_pipeline','stock_motion_rotation_batch','stock_offline_query_skin')]
            command += [str(native/(name+'.cpp')) for name in ('offline_destructible_geometry','world_stage')]
            subprocess.run(command+['-o',str(binary)],check=True,capture_output=True,text=True)
            result=subprocess.run([str(binary)],check=True,capture_output=True,text=True)
            self.assertIn('Native client support scene tests passed',result.stdout)


@unittest.skipUnless(SOURCE, 'fixed gameplay source is required')
class ClientDriveGeometryLifecycle(unittest.TestCase):
    def test_changed_local_cells_streaming_and_falling_geometry(self):
        from native_test_compile_flags import native_test_compile_flags
        prefix=Path(subprocess.check_output(['pyenv','prefix'],env=dict(os.environ,PYENV_VERSION='2.7.18'),text=True).strip())
        with tempfile.TemporaryDirectory(prefix='native-client-drive-geometry-') as folder:
            binary=Path(folder)/'geometry';big=ROOT/'bigworld';native=Path(SOURCE)/'native'
            command=['c++','-std=c++17','-O0','-Wall','-Wextra','-Werror',*native_test_compile_flags('c++'),
                '-isystem',str(prefix/'include/python2.7'),'-I'+str(big),'-I'+str(native),
                str(ROOT/'tests/stock_native_client_drive_geometry_host.cpp')]
            command += [str(big/(name+'.cpp')) for name in ('stock_native_client_drive_geometry',
                'stock_native_client_drive_scene','stock_native_world_pipeline','stock_motion_rotation_batch','stock_offline_query_skin')]
            command += [str(native/(name+'.cpp')) for name in ('offline_destructible_geometry','world_stage')]
            command += ['-L'+str(prefix/'lib'),'-lpython2.7','-o',str(binary)]
            subprocess.run(command,check=True,capture_output=True,text=True)
            result=subprocess.run([str(binary)],check=True,capture_output=True,text=True)
            self.assertIn('Native player local geometry lifecycle tests passed',result.stdout)

    def test_mutation_revisions_cover_actual_pinned_writers(self):
        spec=importlib.util.spec_from_file_location('client_drive_ledger_bindings_fixture',ROOT/'bigworld/stock_native_client_drive_bindings.py')
        bindings=importlib.util.module_from_spec(spec);spec.loader.exec_module(bindings)
        base=Path(SOURCE)/'src/res/scripts/client/gui/mods/offline_lan_0922'
        sensor=bindings.sensor((base/'destructibles_sensor.py').read_text())
        authority=bindings.authority((base/'destructibles_authority.py').read_text())
        self.assertEqual(sensor.count("globals()['_native_client_drive_ledger_revision'] ="),11)
        self.assertEqual(authority.count("globals()['_native_client_drive_ledger_revision'] ="),3)
        wanted={'begin_local_prediction','clear_local_prediction'}
        methods=[node for node in ast.parse(sensor).body if isinstance(node,ast.FunctionDef) and node.name in wanted]
        env={'g_offh_destr_instances':{(1,7):{'kind':'fragile'}},'_native_client_drive_ledger_revision':0}
        exec(compile(ast.Module(body=methods,type_ignores=[]),'fixed-ledger-mutations','exec'),env)
        self.assertTrue(env['begin_local_prediction']([(1,7,None)]));first=env['_native_client_drive_ledger_revision']
        self.assertGreater(first,0)
        self.assertTrue(env['clear_local_prediction']([(1,7,None)]));self.assertGreater(env['_native_client_drive_ledger_revision'],first)
        self.assertEqual(len(env['g_offh_destr_speculative']),0)
        wanted={'reset','_ensure_shape','_reset_if_new_space','_chunk','accept_tree_without_presentation'}
        methods=[node for node in ast.parse(authority).body if isinstance(node,ast.FunctionDef) and node.name in wanted]
        env={'_state':{},'_applies_reported':[0],'_chunks_reported':[0],'_native_client_drive_ledger_revision':0}
        exec(compile(ast.Module(body=methods,type_ignores=[]),'fixed-authority-mutations','exec'),env)
        self.assertTrue(env['accept_tree_without_presentation'](4,1,7));accepted=env['_native_client_drive_ledger_revision']
        self.assertGreaterEqual(accepted,2);self.assertFalse(env['accept_tree_without_presentation'](4,1,7))
        self.assertEqual(env['_native_client_drive_ledger_revision'],accepted)
        env['reset'](4);self.assertGreater(env['_native_client_drive_ledger_revision'],accepted)


if __name__ == "__main__":
    unittest.main()
