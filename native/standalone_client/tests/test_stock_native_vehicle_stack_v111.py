"""Vertical contact, descent and cumulative pressure against pinned v1.1.1."""
import ast
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

ROOT=Path(__file__).resolve().parents[1]

class VehicleStackV111(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        source=os.environ.get('WOT_FORK_SOURCE')
        if not source:raise unittest.SkipTest('fixed gameplay source is required')
        cls.folder=tempfile.TemporaryDirectory(prefix='stack-v111-');cls.addClassCleanup(cls.folder.cleanup)
        binary=Path(cls.folder.name)/'stack.so'
        subprocess.run(['c++','-std=c++17','-O1','-shared','-fPIC','-ffp-contract=off','-Wall','-Wextra','-Werror','-I'+str(ROOT/'bigworld'),str(ROOT/'tests/stock_native_vehicle_stack_v111_host.cpp'),'-o',str(binary)],check=True)
        cls.lib=ctypes.CDLL(str(binary));ptr=ctypes.POINTER(ctypes.c_double)
        cls.lib.stack_ledger.argtypes=[ptr,ctypes.c_int,ptr];cls.lib.stack_descent.argtypes=[ptr,ctypes.c_int,ptr]
        cls.lib.stack_match.argtypes=[ptr,ctypes.c_int,ptr,ctypes.c_int,ptr];cls.lib.stack_impact.argtypes=[ctypes.c_int,ctypes.c_double,ctypes.c_double,ctypes.c_double];cls.lib.stack_impact.restype=ctypes.c_int
        base=Path(source)/'src/res/scripts/client/gui/mods/offline_lan_0922'
        diagnostic=types.ModuleType('gui.mods.offline_lan_0922.worker_diagnostics');diagnostic.observed=lambda unused:lambda function:function
        package=types.ModuleType('stack_v111');package.__path__=[str(base)]
        previous=dict(sys.modules);sys.modules[diagnostic.__name__]=diagnostic;sys.modules[package.__name__]=package
        spec=importlib.util.spec_from_file_location('stack_v111.vehicle_physics',base/'vehicle_physics.py');cls.physics=importlib.util.module_from_spec(spec);sys.modules[spec.name]=cls.physics;spec.loader.exec_module(cls.physics)
        spec=importlib.util.spec_from_file_location('stack_v111.vehicle_stack',base/'vehicle_stack.py');cls.oracle=importlib.util.module_from_spec(spec);sys.modules[spec.name]=cls.oracle;spec.loader.exec_module(cls.oracle)
        cls.addClassCleanup(lambda:(sys.modules.clear(),sys.modules.update(previous)))

    def array(self,rows):return (ctypes.c_double*sum(len(row) for row in rows))(*(value for row in rows for value in row))

    def test_pressure_live_dead_and_fractional_carry(self):
        rng=random.Random(111)
        for unused in range(60):
            rows=[];ledger=self.oracle.Ledger()
            for i in range(24):
                row=[rng.choice([0.,.016,.05,.1,-.05]),int(i%3!=0),int(i%5!=0),rng.uniform(1000.,80000.),rng.uniform(1000.,80000.),rng.randrange(0,50),rng.randrange(0,20),0.]
                ledger.advance(('bot',1),('player',2),row[0],bool(row[1]),bool(row[2]),row[3],row[4]);ledger.strike(('bot',1),('player',2),row[5],row[6]);rows.append(row)
            out=(ctypes.c_double*5)();self.lib.stack_ledger(self.array(rows),len(rows),out);expected=ledger.publication()[0]
            for value,key in zip(out,('upper_total','lower_total','lower_environment_total','upper_impact_total','contact_seconds')):self.assertAlmostEqual(value,expected[key],places=6)

    def test_descent_deduplicates_support_flicker_and_zero_dt(self):
        rows=[[1.,-18.,0.,-20.,.05,0.,1.,20.],[1.,-15.,1.,-19.,.05,0.,1.,20.],
              [1.,-15.,1.,-19.,.05,1.,2.,20.],[0.,0.,0.,99999.,.05,0.,1.,0.],
              [1.,-25.,1.,-26.,0.,0.,1.,26.],[1.,-25.,0.,-26.,.05,0.,1.,26.]]
        expected=[];descent=self.oracle.Descent()
        for row in rows:
            solved=dict(airborne=bool(row[2]),impact_speed=None if row[3]==99999. else row[3]);carrier=('player' if row[5] else 'bot',int(row[6]))
            descent.observe(bool(row[0]),row[1],solved,row[4]);admit=descent.verdict(carrier,row[7],True,row[4])=='admit';expected.extend((admit,descent.active,descent.sequence))
            if admit:descent.commit(carrier)
        out=(ctypes.c_double*(len(rows)*3))();self.lib.stack_descent(self.array(rows),len(rows),out);self.assertEqual(list(out),expected)

    def test_final_contacts_select_carrier_and_vertical_closing(self):
        rng=random.Random(1513)
        for unused in range(100):
            points=[[float(i),0.,0.] for i in range(5)];hits=[]
            for i in range(10):
                point=points[i%5];hits.append([i%2,1+i%3,point[0]+rng.choice([0.,.04,.08]),rng.choice([0.,.02,.05]),0.,0.,.8,.6])
            # Match the source's final-contact loop with real spring keys.
            params=dict(springs=[dict(x=p[0],y=0.,z=p[2],static_compression=.2,side='left') for p in points],pseudo_contacts=[])
            source_hits=[(('player' if h[0] else 'bot',int(h[1])),{},tuple(h[2:5])) for h in hits]
            carrier=self.oracle.settled_carrier(params,[0.]*5,[],dict(height=0.,pitch=0.,roll=0.),(0.,0.,0.),0.,0.,0.,source_hits)
            out=(ctypes.c_double*4)();self.lib.stack_match(self.array(points),len(points),self.array(hits),len(hits),out)
            self.assertEqual(bool(out[0]),carrier is not None)
            if carrier:self.assertEqual([bool(out[1]),int(out[2])],[carrier[0]=='player',carrier[1]]);self.assertAlmostEqual(out[3],16.)

    def test_struck_vehicle_mass_ratio(self):
        rng=random.Random(922)
        for i in range(200):
            health=rng.randint(100,3000);speed=rng.uniform(0.,50.);upper,lower=rng.uniform(1000.,120000.),rng.uniform(1000.,120000.)
            self.assertEqual(self.lib.stack_impact(health,speed,upper,lower),self.oracle.impact_damage(health,speed,upper,lower))
    def test_impact_credit_uses_largest_vertical_closing(self):
        # The majority of resting contacts can carry on a tilted roof while a
        # different flat roof receives the largest impact of this descent.
        surfaces=[dict(carrier=['bot',1],point=(0.,0.,0.),normal=(0.,.6,.8)),dict(carrier=['bot',1],point=(1.,0.,0.),normal=(0.,.6,.8)),dict(carrier=['player',2],point=(2.,0.,0.),normal=(0.,1.,0.))]
        impact=self.oracle.contact_impact(surfaces,(500.,-20.,500.));self.assertEqual(impact['carrier'],['player',2]);self.assertEqual(impact['speed'],20.)
        out=(ctypes.c_double*5)();self.lib.stack_struck_carrier(out);self.assertEqual(list(out),[0.,1.,1.,2.,20.])

if __name__=='__main__':unittest.main()
