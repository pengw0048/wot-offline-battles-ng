"""Actual sweep identity and unresolved motor ownership against v1.1.1."""
import ast
import math
import os
from pathlib import Path
import random
import subprocess
import tempfile
import types
import unittest

ROOT=Path(__file__).resolve().parents[1]
class TranslationReceiptV111(unittest.TestCase):
    def test_exact_remote_identity_and_motor_settlement(self):
        source=os.environ.get('WOT_FORK_SOURCE')
        if not source:self.skipTest('fixed gameplay source is required')
        base=Path(source);native=base/'native';big=ROOT/'bigworld';tree=ast.parse((base/'src/res/scripts/client/gui/mods/offline_lan_0922/bot_runtime.py').read_text())
        names={'_settle_unresolved_drive_sweep','_retained_contact_speed'}
        methods=[m for n in tree.body if isinstance(n,ast.ClassDef) for m in n.body if isinstance(m,ast.FunctionDef) and m.name in names]
        env=dict(math=math,HUMAN_TARGET_ID_BASE=1000000)
        exec(compile(ast.fix_missing_locations(ast.Module(body=[ast.ClassDef(name='Harness',bases=[],keywords=[],body=methods,decorator_list=[])],type_ignores=[])),'v111-translation','exec'),env)
        owner=env['Harness']();rng=random.Random(111);cases=[];expected=[]
        outcomes={name:i for i,name in enumerate(('pending_contact_solver','unrestricted','velocity_owned_by_remote_player','velocity_owned_by_contact_solver','missing_physics_step','not_forward_motor_displacement','unresolved_motor_sweep_settled'))}
        for i in range(250):
            x,z,yaw=rng.uniform(-3.,3.),rng.uniform(-3.,3.),rng.uniform(-math.pi,math.pi);speed=rng.uniform(-10.,15.);forced=rng.uniform(-6.,6.);sx,sz=rng.uniform(-2.,2.),rng.uniform(-2.,2.);rx,rz=rng.uniform(-3.,3.),rng.uniform(-3.,3.);fraction=rng.choice([0.,.4,1.]);dt=rng.choice([0.,.02,.1]);before=speed if i%3 else speed+1.;remote=i%4==0
            receipt=dict(outcome='pending_contact_solver',fraction=fraction,step=dt,speed_before=before,start=(sx,0.,sz),requested=(rx,rz),contacts=[(1000001 if remote else 2,fraction)])
            state=dict(x=x,z=z,yaw=yaw,speed=speed,_contact_forward_speed=forced,_translation_motion_receipt=receipt);owner._settle_unresolved_drive_sweep(state)
            cases.append((x,z,yaw,speed,forced,sx,sz,rx,rz,fraction,dt,before,int(remote)));expected.append((state['speed'],outcomes[receipt['outcome']]))
        with tempfile.TemporaryDirectory(prefix='translation-v111-') as folder:
            folder=Path(folder);law=(native/'offline_release_drive_python.inc').read_text().split('PyObject *WOT_CDECL release_drive(')[0];(folder/'offline_release_drive_law.hpp').write_text('#pragma once\n#include <cmath>\n#include <algorithm>\n'+law)
            binary=folder/'receipt';command=['c++','-std=c++17','-O1','-ffp-contract=off','-Wall','-Wextra','-Werror','-I'+str(folder),'-I'+str(big),'-I'+str(native)]
            command += [str(big/(name+'.cpp')) for name in ('stock_native_bot_motion','stock_native_bot_contacts','stock_native_client_drive_roster')]+[str(native/'offline_math_batch_geometry.cpp'),str(ROOT/'tests/stock_native_translation_receipt_v111_host.cpp'),'-o',str(binary)]
            subprocess.run(command,check=True);self.assertEqual(subprocess.check_output([str(binary),'producer'],text=True).strip(),'ok')
            actual=subprocess.check_output([str(binary)],input='\n'.join(' '.join(map(str,row)) for row in cases)+'\n',text=True).splitlines()
        for line,want in zip(actual,expected):
            speed,outcome=map(float,line.split());self.assertAlmostEqual(speed,want[0],places=12);self.assertEqual(int(outcome),want[1])
if __name__=='__main__':unittest.main()
