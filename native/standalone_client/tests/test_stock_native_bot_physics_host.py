"""Native Host transaction guards and fixed-release slope state parity."""
import ast,json,math,os,random,struct,subprocess,tempfile,types,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
class PhysicsHost(unittest.TestCase):
    def test_pose_rollback_landing_and_slope(self):
        source=os.environ.get('WOT_FORK_0101_SOURCE')
        if not source:self.skipTest('fixed gameplay source is required')
        base=Path(source);big=ROOT/'bigworld';native=base/'native'
        with tempfile.TemporaryDirectory(prefix='physics-host-') as directory:
            folder=Path(directory);binary=folder/'physics'
            law=(native/'offline_release_drive_python.inc').read_text().split('PyObject *WOT_CDECL release_drive(')[0]
            (folder/'offline_release_drive_law.hpp').write_text('#pragma once\n#include <cmath>\n#include <algorithm>\n'+law)
            sources=['stock_native_bot_physics_host','stock_native_bot_physics_world','stock_native_bot_motion','stock_native_bot_contacts','stock_native_bot_step_state',
                'stock_native_bot_candidate','stock_native_bot_navigation','stock_native_bot_navigator','stock_native_bot_motion_plan','stock_native_bot_planner',
                'stock_native_bot_turret_obstacles','stock_native_world_pipeline','stock_motion_rotation_batch','stock_offline_query_skin']
            command=['c++','-std=c++17','-O1','-ffp-contract=off','-Wall','-Wextra','-Werror','-pthread','-I'+str(folder),'-I'+str(native),'-I'+str(big)]
            command += [str(big/(s+'.cpp')) for s in sources]
            command += [str(native/(s+'.cpp')) for s in ['offline_math_batch_geometry','offline_contact_roster','offline_destructible_geometry','offline_navigation','offline_async_worker','world_stage']]
            command += [str(ROOT/'tests/stock_native_bot_physics_host_fixture.cpp'),'-o',str(binary)]
            built=subprocess.run(command,capture_output=True,text=True);self.assertEqual(built.returncode,0,built.stderr)
            self.assertEqual(subprocess.check_output([str(binary),'scenarios'],text=True).strip(),'ok')
            text=(base/'src/res/scripts/client/gui/mods/offline_lan_0922/bot_runtime.py').read_text();tree=ast.parse(text)
            slope=next(n for n in tree.body if isinstance(n,ast.FunctionDef) and n.name=='slope_pose')
            update=next(m for n in tree.body if isinstance(n,ast.ClassDef) for m in n.body if isinstance(m,ast.FunctionDef) and m.name=='_update_slope_pose');update.decorator_list=[]
            env=dict(math=math,SLOPE_SAMPLE_METRES=(.35,1.5,4.),SLOPE_SAMPLE_RADIANS=(.05,.15,.40),_value=lambda o,k,d=None:getattr(o,k,d))
            exec(compile(ast.Module(body=[slope,update],type_ignores=[]),'fixed-slope','exec'),env)
            f32=lambda x:struct.unpack('f',struct.pack('f',x))[0]
            owner=types.SimpleNamespace(_suspension_params={},_descriptors={},_turret_motion_probe=None)
            owner._ground_probe_at=lambda x,z,y:f32(.05*f32(x)+.1*f32(z))
            owner._terrain_pitch=lambda s:s['terrain_pitch'];rng=random.Random(1513);cases=[];expected=[]
            for i in range(150):
                row=[rng.uniform(-30.,30.),10.,rng.uniform(-30.,30.),rng.uniform(-math.pi,math.pi),rng.uniform(-.5,.5),rng.uniform(-.5,.5),rng.uniform(-.2,.2),rng.uniform(2.,5.),rng.uniform(.8,2.5),i%3,int(i%2==0),int(i%4==0),rng.uniform(-.5,.5),rng.uniform(-.5,.5)]
                x,y,z,yaw,pitch,roll,hydro,length,width,tier,exact,marker,a,b=row
                state=dict(id=7,x=x,y=y,z=z,yaw=yaw,terrain_pitch=pitch,pitch=pitch+hydro,roll=roll,suspension_pitch=hydro,half_length=length,half_width=width,grounded_once=True)
                if marker:state.update(pose_sample=(x,z,yaw),_slope_pose_target=(a,b))
                owner._detail_tier=lambda s,tier=tier:tier;owner._descriptors={7:types.SimpleNamespace(isPitchHullAimingAvailable=not exact)}
                changed=env['_update_slope_pose'](owner,state)
                expected.append([int(changed),state['terrain_pitch'],state['pitch'],state['roll'],state.get('_slope_pose_target'),state.get('pose_sample')]);cases.append(row)
            actual=[json.loads(s) for s in subprocess.check_output([str(binary)],input='\n'.join(' '.join(map(str,r)) for r in cases)+'\n',text=True).splitlines()]
        def compare(a,b):
            if isinstance(b,(list,tuple)):
                self.assertEqual(len(a),len(b))
                for x,y in zip(a,b):compare(x,y)
            elif isinstance(b,(int,float)):self.assertTrue(math.isclose(a,b,rel_tol=3.e-12,abs_tol=3.e-12),(a,b))
            else:self.assertEqual(a,b)
        for a,b in zip(actual,expected):compare(a,b)
if __name__=='__main__':unittest.main()
