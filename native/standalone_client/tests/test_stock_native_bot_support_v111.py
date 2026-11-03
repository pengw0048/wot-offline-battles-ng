"""Five-point support admission against the complete v1.1.1 source reader."""
import ast
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

ROOT=Path(__file__).resolve().parents[1]

class SupportV111(unittest.TestCase):
    def test_native_footprint_against_v111(self):
        source=os.environ.get('WOT_FORK_SOURCE')
        if not source:self.skipTest('fixed gameplay source is required')
        base=Path(source)/'src/res/scripts/client/gui/mods/offline_lan_0922'
        diagnostic=types.ModuleType('gui.mods.offline_lan_0922.worker_diagnostics')
        diagnostic.observed=lambda unused:lambda function:function
        original=sys.modules.get(diagnostic.__name__);sys.modules[diagnostic.__name__]=diagnostic
        spec=importlib.util.spec_from_file_location('support_physics',base/'vehicle_physics.py')
        physics=importlib.util.module_from_spec(spec);spec.loader.exec_module(physics)
        if original is None:sys.modules.pop(diagnostic.__name__)
        else:sys.modules[diagnostic.__name__]=original
        tree=ast.parse((base/'bot_runtime.py').read_text())
        names={'_basic_support_status','_basic_support_rise_is_continuous','_release_detailed_support','_revalidate_basic_support_datum'}
        methods=[m for n in tree.body if isinstance(n,ast.ClassDef) for m in n.body if isinstance(m,ast.FunctionDef) and m.name in names]
        env=dict(math=math,vehicle_physics=physics,BOT_FULL_SUPPORT_ENABLED=False,_number=lambda x,d=0.:d if x is None else float(x),_position=lambda s:(s['x'],s['y'],s['z']))
        exec(compile(ast.fix_missing_locations(ast.Module(body=[ast.ClassDef(name='Harness',bases=[],keywords=[],body=methods,decorator_list=[])],type_ignores=[])),'v111-support','exec'),env)
        owner=env['Harness']();rng=random.Random(111);cases=[];expected=[]
        for i in range(180):
            mode=i%4;gx,gz=(rng.uniform(-.2,.2),rng.uniform(-.3,.3));x,z=rng.uniform(-2.,2.),rng.uniform(-2.,2.)
            y=gx*x+gz*z+rng.choice([0.,.2,.75,1.2]);yaw=rng.uniform(-math.pi,math.pi);pitch,roll=rng.uniform(-.5,.5),rng.uniform(-.4,.4)
            def ground(px,pz,hint,maximum=None,evidence=None):
                h=gx*px+gz*pz+(3. if mode==1 and px>1. else 0.)
                if mode==2 and pz>1. or mode==3 and px< -1. or maximum is not None and h>maximum or h>hint+2.:return None
                return h
            owner._ground_probe_at=ground
            state=dict(x=x,y=y,z=z,yaw=yaw,terrain_pitch=pitch,roll=roll,half_length=3.,half_width=1.5,grounded_once=True)
            expected.append(owner._basic_support_status(state));cases.append((mode,gx,gz,x,y,z,yaw,pitch,roll))
        with tempfile.TemporaryDirectory(prefix='bot-support-v111-') as folder:
            binary=Path(folder)/'support'
            subprocess.run(['c++','-std=c++17','-O1','-ffp-contract=off','-Wall','-Wextra','-Werror','-I'+str(ROOT/'bigworld'),str(ROOT/'bigworld/stock_native_bot_step_state.cpp'),str(ROOT/'tests/stock_native_bot_support_v111_host.cpp'),'-o',str(binary)],check=True)
            actual=subprocess.check_output([str(binary)],input='\n'.join(' '.join(map(str,c)) for c in cases)+'\n',text=True).splitlines()
            lifecycle=subprocess.check_output([str(binary),'lifecycle'],text=True).splitlines()
        for line,rows in zip(actual,expected):
            values=list(map(float,line.split()))
            for i,row in enumerate(rows):
                got=values[i*6:i*6+6]
                want=[row['x'],row['expected'],row['z'],int(row['valid']),int(row['continuous_rise']),99999. if row['ground'] is None else row['ground']]
                for a,b in zip(got,want):self.assertTrue(math.isclose(a,b,rel_tol=2.e-12,abs_tol=2.e-12),(a,b,cases[actual.index(line)]))
            self.assertEqual(bool(values[30]),any(not row['valid'] for row in rows))
            self.assertFalse(bool(values[31]))
        # Run the original handoff/datum guards, including a raised side point
        # that would leave reach if the raw basic root consumed the offset.
        owner._suspension_params={7:object()};owner._report_support_mode=lambda *unused:None
        state=dict(id=7,x=0.,y=.3,z=0.,yaw=0.,terrain_pitch=0.,pitch=0.,roll=0.,half_length=3.,half_width=1.5,grounded_once=True,_edge_support_active=True)
        mode=0
        owner._ground_probe_at=lambda x,z,hint,maximum=None,evidence=None:None if mode==2 and z>1. else .8 if mode==4 and x>1. else 0.
        self.assertTrue(owner._release_detailed_support(state));self.assertEqual(state['_basic_support_datum'],.3)
        self.assertEqual(list(map(float,lifecycle[0].split())),[1.,0.,.3,0.,.3,.2,.1])
        owner._revalidate_basic_support_datum(state,0.,0.);self.assertEqual(list(map(float,lifecycle[1].split())),[state['y'],0.,state['_basic_support_datum'],.2,.1])
        mode=4;owner._revalidate_basic_support_datum(state,0.,.1);self.assertEqual(list(map(float,lifecycle[2].split())),[state['y'],0.,state['_basic_support_datum'],.2,.1])
        mode=0;owner._revalidate_basic_support_datum(state,0.,.1);self.assertNotIn('_basic_support_datum',state)
        self.assertEqual(list(map(float,lifecycle[3].split())),[state['y'],0.,99999.,.2,.1])
        self.assertEqual(list(map(float,lifecycle[4].split())),[1.,0.,1.,99999.,.2,.1])
        self.assertEqual(list(map(float,lifecycle[5].split())),[0.,0.,1.,99999.,.2,.1])

if __name__=='__main__':unittest.main()
