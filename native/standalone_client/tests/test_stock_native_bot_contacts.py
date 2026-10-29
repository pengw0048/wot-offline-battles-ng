"""Contact owner differential checks against the actual pinned Python module."""
import json
import math
import os
from pathlib import Path
import random
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
SOURCE=os.environ.get('WOT_FORK_0101_SOURCE')

@unittest.skipUnless(SOURCE,'set WOT_FORK_0101_SOURCE to the pinned gameplay checkout')
class ContactDifferential(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.folder=tempfile.TemporaryDirectory(prefix='wot-contacts-');path=Path(cls.folder.name)
        native=Path(SOURCE)/'native'
        law=(native/'offline_release_drive_python.inc').read_text().split('PyObject *WOT_CDECL release_drive(PyObject *,PyObject *args) {')[0]
        (path/'offline_release_drive_law.hpp').write_text('#pragma once\n#include <algorithm>\n#include <cmath>\n'+law)
        cls.binary=path/'contacts'
        subprocess.run(['c++','-std=c++17','-O2','-ffp-contract=off','-Wall','-Wextra','-Werror',
            '-I',str(native),'-I',str(path),'-I',str(ROOT/'bigworld'),
            str(ROOT/'bigworld/stock_native_bot_contacts.cpp'),str(ROOT/'bigworld/stock_native_bot_motion.cpp'),
            str(native/'offline_contact_roster.cpp'),str(native/'offline_math_batch_geometry.cpp'),
            str(ROOT/'tests/stock_native_bot_contacts_host.cpp'),'-o',str(cls.binary)],check=True)
        cls.py27=subprocess.check_output(['pyenv','which','python2.7'],env=dict(os.environ,PYENV_VERSION='2.7.18'),text=True).strip()
    @classmethod
    def tearDownClass(cls):cls.folder.cleanup()
    def test_native_contact_lifecycle(self):
        self.assertEqual(subprocess.check_output([str(self.binary),'scenarios'],text=True).strip(),'ok')
    def test_damage_geometry_and_roster_order(self):
        rng=random.Random(1513);cases=[]
        for n in range(600):
            if n%3==0:
                cases.append([0,rng.uniform(-30,30),rng.uniform(1000,150000),rng.uniform(1000,150000),
                    rng.uniform(0,300),rng.uniform(0,300),rng.uniform(1,1.5),rng.uniform(1,1.5),
                    rng.uniform(0,.15),rng.uniform(0,.15),n%2==0,n%4==0])
            elif n%3==1:
                def body(i):return [i,rng.uniform(-4,4),rng.uniform(-2,2),rng.uniform(-5,5),
                    rng.uniform(-math.pi,math.pi),rng.uniform(-1,1),rng.uniform(-1,1),
                    rng.uniform(.5,2.5),rng.uniform(1.5,5),-.8,rng.uniform(1,3)]
                cases.append([1,body(1),body(2),[rng.uniform(-5,5) for _ in range(3)]])
            else:cases.append([2]+[rng.choice([rng.randrange(1,31),1000000+rng.randrange(1,30),rng.randrange(1,2147483647)]) for _ in range(n%40)])
        def flatten(row):
            if row[0]==1:values=[1]+row[1]+row[2]+row[3]
            elif row[0]==2:values=[2,len(row)-1]+row[1:]
            else:values=row
            return ' '.join(str(int(v)) if isinstance(v,bool) else str(v) for v in values)
        actual=[json.loads(row) for row in subprocess.check_output([str(self.binary)],input='\n'.join(map(flatten,cases))+'\n',text=True).splitlines()]
        expected=[json.loads(row) for row in subprocess.check_output([self.py27,str(ROOT/'tests/stock_native_bot_contacts_oracle.py'),SOURCE],
            input='\n'.join(map(json.dumps,cases))+'\n',text=True,env=dict(os.environ,PYTHONDONTWRITEBYTECODE='1')).splitlines()]
        def equal(a,b,path):
            if isinstance(b,list):
                self.assertEqual(len(a),len(b),path)
                for i,(x,y) in enumerate(zip(a,b)):equal(x,y,path+'/'+str(i))
            elif b is None:self.assertIsNone(a,path)
            elif isinstance(b,(float,int)):self.assertTrue(math.isclose(a,b,rel_tol=2e-12,abs_tol=2e-12),(path,a,b))
            else:self.assertEqual(a,b,path)
        self.assertEqual(len(actual),len(expected))
        for i,(a,b) in enumerate(zip(actual,expected)):equal(a,b,str(i))

if __name__=='__main__':unittest.main()
