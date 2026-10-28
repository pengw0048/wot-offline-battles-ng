"""Exact #1513 TrajectoryDrawer data, owner and production Python bindings.

The x86 oracle proves numeric/state contracts; the guarded native adapter
host proves lifetime, callbacks and draw arguments. Windows pixels and device
reset are still native acceptance boundaries.
"""
from pathlib import Path
import ctypes
import importlib.util
import os
import random
import re
import shlex
import struct
import subprocess
import sys
import tarfile
import tempfile
import unittest
import zipfile
import io

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'bigworld'))
import patch_stock_trajectory_drawer as patcher


class StockTrajectoryDrawerTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory=tempfile.TemporaryDirectory(prefix='trajectory-drawer-')
        cls.output=Path(cls.directory.name)
        cls.binary=cls.output/'core.so'
        subprocess.run(['c++','-std=c++14','-ffp-contract=off','-shared','-fPIC','-I',str(ROOT/'include'),
            str(ROOT/'src/stock_trajectory_drawer.cpp'),str(ROOT/'src/gun_ballistics.cpp'),
            str(ROOT/'tests/stock_trajectory_drawer_core.cpp'),'-o',str(cls.binary)],check=True)
        cls.core=ctypes.CDLL(str(cls.binary))
        pointer=ctypes.POINTER(ctypes.c_float)
        cls.core.trajectory_colour.argtypes=[pointer];cls.core.trajectory_colour.restype=ctypes.c_uint
        cls.core.trajectory_bounds.argtypes=[pointer,pointer,pointer,pointer]
        cls.core.trajectory_calculate.argtypes=[pointer,pointer]
        cls.core.trajectory_advance.argtypes=[pointer,pointer]
        cls.core.trajectory_simulate.argtypes=[pointer,pointer]

    @classmethod
    def tearDownClass(cls):cls.directory.cleanup()

    def floats(self,values):return (ctypes.c_float*len(values))(*values)
    def original(self):
        client=os.environ.get('WOT_0922_CLIENT')
        if not client:self.skipTest('WOT_0922_CLIENT is required for #1513 instructions')
        if not all(importlib.util.find_spec(name) for name in ('pefile','unicorn')):
            self.skipTest('pefile and unicorn are required for #1513 instructions')
        sys.path.insert(0,str(ROOT/'tests'))
        from stock_trajectory_drawer_oracle import Original
        return Original(Path(client)/'WorldOfTanks.exe')

    def test_colours_match_original_integer_conversion(self):
        original=self.original()
        cases=[[0,255,0,255],[255,0,0,255],[128,128,128,255],[-1,256,2.9,100.1],
            [float('nan'),float('inf'),-float('inf'),2147483648.]]
        rng=random.Random(1513)
        cases += [[rng.uniform(-500,500) for _ in range(4)] for _ in range(80)]
        for case in cases:
            self.assertEqual(self.core.trajectory_colour(self.floats(case)),original.colour(case))

    def test_boundary_matches_original(self):
        original=self.original();rng=random.Random(6713)
        bounds=[-500,-100,-400,600,100,700]
        cases=[([0,0,0],[100,0,0]),([0,0,0],[800,0,0]),([-800,0,0],[0,0,0]),
            ([600,0,0],[900,0,0]),([0,0,0],[0,0,0]),([0,0,0],[900,1000,800])]
        cases += [([rng.uniform(-900,900) for _ in range(3)],
                   [rng.uniform(-900,900) for _ in range(3)]) for _ in range(60)]
        for a,b in cases:
            a,b=list(self.floats(a)),list(self.floats(b));out=self.floats([0,0,0])
            expected,point=original.leave_bounds(bounds[:3],bounds[3:],a,b)
            actual=self.core.trajectory_bounds(self.floats(bounds),self.floats(a),self.floats(b),out)
            self.assertEqual(actual,expected)
            if actual:
                for left,right in zip(out,point):self.assertAlmostEqual(left,right,delta=0.0003)

    def test_collision_fractions_match_original_subdivision(self):
        original=self.original();rng=random.Random(7130)
        from stock_trajectory_drawer_oracle import f
        for case in range(45):
            target=[rng.uniform(-100,100),rng.uniform(-20,30),rng.uniform(100,900)]
            start=[rng.uniform(-5,5),rng.uniform(5,30),rng.uniform(-5,5)]
            velocity=[rng.uniform(-20,20),rng.uniform(-20,120),rng.uniform(20,250)]
            gravity=[0,rng.choice([-9.81,-20,-30]),0]
            maximum=rng.choice([100.,500.,1500.]);ground=rng.choice([0.,-10.,15.])
            if case%9==0:target[0]=start[0];target[2]=start[2]
            values=list(self.floats(target+start+velocity+gravity+[maximum,ground,-1000,-100,-1000,1000,100,1000]))
            def scene(a,b):
                if (a[1]>ground and b[1]<=ground) or (a[1]<ground and b[1]>=ground):
                    t=f(f(a[1]-ground)/f(a[1]-b[1]))
                    return [f(a[i]+f(f(b[i]-a[i])*t)) for i in range(3)]
            expected=original.calculate(values[:3],values[3:6],values[6:9],values[9:12],maximum,scene,values[14:])
            out=self.floats([0]*4);self.core.trajectory_calculate(self.floats(values),out)
            for actual,reference in zip(out,expected):
                self.assertAlmostEqual(actual,reference,delta=max(0.0004,abs(reference)*0.00005),msg=str((case,values,list(out),expected)))

    def test_per_frame_projection_matches_original(self):
        original=self.original();rng=random.Random(690)
        for case in range(60):
            values=[case%4!=0,case%5!=0,case%2,0.01,10.]+[rng.uniform(0,1) for _ in range(8)]+[
                rng.uniform(-100,100) for _ in range(6)]+[40.,10.,500.,rng.choice([0.,.03,.1,.2]),case%7!=0]
            values=list(self.floats(values))
            expected=original.advance(values);out=self.floats([0]*19)
            self.core.trajectory_advance(self.floats(values),out)
            for actual,reference in zip(out,expected):self.assertAlmostEqual(actual,reference,delta=.0001)

    def test_projectile_simulation_matches_original(self):
        original=self.original();rng=random.Random(0xf5db50)
        for case in range(48):
            start=[rng.uniform(-10,10),rng.uniform(5,20),rng.uniform(-10,10)]
            velocity=[rng.uniform(-100,100),rng.uniform(-20,40),rng.uniform(-100,100)]
            if case%4==0:velocity[0]=0.
            if case%8==0:velocity[2]=0.
            values=list(self.floats(start+velocity+[0,rng.choice([-10,-20,-30]),0,rng.choice([.1,.03,.2]),.03,0.]))
            expected=original.simulate(values[:3],values[3:6],values[6:9],values[9],values[10])
            out=self.floats([0]*7);yes=self.core.trajectory_simulate(self.floats(values),out)
            self.assertEqual(bool(yes),expected is not None)
            if expected is not None:
                for actual,reference in zip(out,[expected[0]]+expected[1]+expected[2]):
                    self.assertAlmostEqual(actual,reference,delta=max(.0003,abs(reference)*.00002))
            self.assertTrue(all(mask==(128,0) for mask in original.mask_calls))
        self.assertIsNone(original.simulate([0,10,0],[0,10,100],[0,-10,0],.1,.03,0x10080,0x10001))
        self.assertTrue(all(mask==(128,1) for mask in original.mask_calls))

    def test_python_owner_and_original_producer(self):
        if os.environ.get('WOT_0922_CLIENT'):
            original=self.original()
            # The initializer sets PyObjectPlus as base and installs its
            # rejecting tp_new; the on-disk type record has both slots zero.
            name=struct.unpack('<I',original.u.mem_read(0x1a0e99c,4))[0]
            self.assertEqual(bytes(original.u.mem_read(name,19)),b'WGTrajectoryDrawer\0')
            self.assertEqual(bytes(original.u.mem_read(0x5a6351,10)),
                             b'\xc7\x05\x10\xea\xa0\x01\x00\xc6\xa0\x01')
            self.assertEqual(bytes(original.u.mem_read(0x5a6397,10)),
                             b'\xc7\x05\x2c\xea\xa0\x01\x50\x85\x6a\x00')
            base_name=struct.unpack('<I',original.u.mem_read(0x1a0c60c,4))[0]
            self.assertEqual(bytes(original.u.mem_read(base_name,13)),b'PyObjectPlus\0')
            forbidden=b'%s() Cannot directly construct objects of this type\0'
            self.assertEqual(bytes(original.u.mem_read(0x14baab8,len(forbidden))),forbidden)
        python=Path(os.environ.get('WOT_PYTHON27','/Users/peng/.pyenv/versions/2.7.18/bin/python2.7'))
        config=python.with_name('python2.7-config')
        if not config.exists():self.skipTest('CPython 2.7 development files required')
        flags=shlex.split(subprocess.check_output([str(config),'--cflags'],text=True))
        libs=shlex.split(subprocess.check_output([str(config),'--ldflags'],text=True))
        archive=Path(os.environ.get('WOT_PUBLIC_ENGINE_ARCHIVE','/tmp/wot-bigworld-engine-14.4.1.tar.gz'))
        if not archive.is_file():self.skipTest('Pinned public engine archive required')
        sources={}
        with tarfile.open(archive) as source:
            for member in source:
                for name in ('pyobject_plus.hpp','pyobject_plus.cpp'):
                    if member.name.endswith('/programming/bigworld/lib/pyscript/'+name):
                        sources[name]=source.extractfile(member).read().decode('utf-8-sig')
        self.assertEqual(set(sources),{'pyobject_plus.hpp','pyobject_plus.cpp'})
        plus=sources['pyobject_plus.hpp'];body=sources['pyobject_plus.cpp']
        header=plus[plus.index('/// This must be the first line'):plus.index('// Section: Weak-referenceable macros')]
        definitions=body[body.index('// Base needs to be 0.'):body.index('// Section: PyObjectPlusWithWeakReference')]
        host=(ROOT/'tests/stock_trajectory_drawer_host.cpp').read_text().replace(
            '@PYOBJECT_PLUS_HEADER@',header).replace('@PYOBJECT_PLUS_SOURCE@',definitions)
        source=self.output/'host.cpp';source.write_text(host)
        binary=self.output/'host'
        subprocess.run(['c++','-std=c++14','-ffp-contract=off','-Wno-deprecated-register','-Wno-writable-strings',
            '-I',str(ROOT/'include'),'-I',str(ROOT/'bigworld'),*flags,'-UNDEBUG',
            str(source),str(ROOT/'src/stock_trajectory_drawer.cpp'),
            str(ROOT/'src/gun_ballistics.cpp'),*libs,'-o',str(binary)],check=True)
        result=subprocess.run([str(binary),str(ROOT/'tests/stock_trajectory_drawer_consumer.py')],capture_output=True,text=True)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        self.assertIn('TrajectoryDrawer CPython consumer passed',result.stdout)
        if os.environ.get('WOT_0922_CLIENT'):
            self.assertIn('Original control_modes.pyc shot producer passed',result.stdout)
            self.assertIn('Original ArtyAimingSystem.pyc simulation consumer passed',result.stdout)

    def test_original_geometrics_effect_contract(self):
        client=os.environ.get('WOT_0922_CLIENT')
        if not client:self.skipTest('WOT_0922_CLIENT is required for original shader resources')
        binary=self.output/'effect'
        subprocess.run(['c++','-std=c++17','-I',str(ROOT/'include'),
            str(ROOT/'tests/stock_trajectory_drawer_effect.cpp'),str(ROOT/'src/stock_shader_binary.cpp'),
            str(ROOT/'src/stock_effect_reflection.cpp'),'-o',str(binary)],check=True)
        with zipfile.ZipFile(Path(client)/'res/packages/misc.pkg') as package:
            for model in (10,11):
                resource='system/materials/geometrics.%d.dx9.fxo'%model
                with zipfile.ZipFile(io.BytesIO(package.read(resource))) as shader:
                    effect=self.output/('geometrics%d.effect'%model)
                    effect.write_bytes(shader.read('effect'))
                result=subprocess.run([str(binary),str(effect)],capture_output=True,text=True)
                self.assertEqual(result.returncode,0,result.stdout+result.stderr)
                self.assertIn('geometrics shader contract passed',result.stdout)

    def test_gui_owner_patch_is_anchored_idempotent_and_ordered(self):
        archive=Path(os.environ.get('WOT_PUBLIC_ENGINE_ARCHIVE','/tmp/wot-bigworld-engine-14.4.1.tar.gz'))
        if not archive.is_file():self.skipTest('Pinned public engine source archive required')
        required={'client/gui_app.cpp','lib/romp/flora.cpp'}
        sources={}
        with tarfile.open(archive) as tar:
            for member in tar:
                for relative in required:
                    if member.name.endswith('/programming/bigworld/'+relative):
                        sources[relative]=tar.extractfile(member).read().decode()
                if len(sources)==len(required):break
        self.assertEqual(set(sources),required)
        # Execute the real patch CLIs in the builder's declared order. Both
        # patches insert after the same GUI anchor, so order is observable only
        # after applying them; checking their script positions alone misses it.
        scripts={'patch_stock_graphics_settings_consumers.py','patch_stock_trajectory_drawer.py'}
        build_script=(ROOT.parents[1]/'tools/build_bigworld_client.ps1').read_text()
        declared=re.findall(r'Invoke-Checked "python" @\(\(Join-Path \$StandaloneRoot "([^"\n]+\.py)"\)',build_script)
        ordered=[name for name in declared if name in scripts]
        self.assertCountEqual(ordered,scripts)
        with tempfile.TemporaryDirectory(prefix='trajectory-gui-') as directory:
            directory=Path(directory)
            for relative,source in sources.items():
                destination=directory/relative
                destination.parent.mkdir(parents=True,exist_ok=True)
                destination.write_text(source)
            snapshots=None
            for unused in range(2):
                for script in ordered:
                    subprocess.run([sys.executable,str(ROOT/'bigworld'/script),str(directory)],check=True)
                current={relative:(directory/relative).read_text() for relative in required}
                if snapshots is not None:self.assertEqual(snapshots,current)
                snapshots=current
                gui=current['client/gui_app.cpp']
                calls=['rp().beginGUIDraw();','drawStockTrajectoryDrawer()',
                       'drawStockBinoculars()','SimpleGUI::instance().draw(']
                for call in calls:self.assertEqual(gui.count(call),1)
                for before,after in zip(calls,calls[1:]):
                    self.assertLess(gui.index(before),gui.index(after))
                for lifecycle in ('initialiseStockTrajectoryDrawer()',
                                  'finaliseStockTrajectoryDrawer()',
                                  'tickStockTrajectoryDrawer(dRenderTime)'):
                    self.assertEqual(gui.count(lifecycle),1)
            path=directory/'client/gui_app.cpp'
            path.write_text(sources['client/gui_app.cpp'].replace('    rp().beginGUIDraw();',''))
            with self.assertRaises(RuntimeError):patcher.patch(directory)
