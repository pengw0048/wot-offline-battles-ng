"""Original normal-map Flock properties and their actual attached steering owner."""
import importlib.util
import math
import json
import os
from pathlib import Path
import random
import struct
import shlex
import subprocess
import tarfile
import tempfile
import unittest
from native_test_compile_flags import native_test_compile_flags

ROOT = Path(__file__).resolve().parents[1]


def module(path):
    spec=importlib.util.spec_from_file_location(path.stem,path)
    result=importlib.util.module_from_spec(spec);spec.loader.exec_module(result);return result


class BoidsTest(unittest.TestCase):
    def test_original_motion_consumers(self):
        client=os.environ.get('WOT_0922_CLIENT')
        if not client:self.skipTest('Pinned client required')
        if not all(importlib.util.find_spec(n) for n in ('pefile','unicorn')):
            self.skipTest('Original x86 oracle dependencies required')
        from stock_boids_filter_oracle import OriginalBoids
        original=OriginalBoids(Path(client)/'WorldOfTanks.exe')
        rng=random.Random(1513);avoid=[];angles=[]
        f=lambda v:struct.unpack('<f',struct.pack('<f',v))[0]
        vector=lambda lo,hi:tuple(f(rng.uniform(lo,hi)) for unused in range(3))
        for i in range(1600):
            position=vector(-40,40);direction=vector(-1,1);centre=vector(-40,40)
            radius=rng.choice((0.,-1.,10.,100.));goal=vector(-40,40)
            others=[vector(-40,40)+vector(-1,1) for unused in range(i%4)]
            before,after=original.avoid(position,direction,centre,radius,goal,others)
            avoid.append(struct.pack('<16f',*(before+position+direction+centre+(radius,)+after)))
            direction=vector(-1,1);length=math.sqrt(sum(v*v for v in direction));direction=tuple(f(v/length) for v in direction)
            state=vector(-1,1)+(f(rng.uniform(-.1,.1)),f(rng.uniform(.1,5)))
            delta=vector(-1,1);rates=vector(.01,2);normal=f(rng.uniform(.1,5));ratio=.002
            dt=f(rng.uniform(.001,.2));landing=i%2
            expected=original.angles(state,delta,direction,rates,normal,ratio,dt,landing)
            angles.append(struct.pack('<23f',*(state+delta+direction+rates+(normal,ratio,f(dt*30.),float(landing))+expected)))
        with tempfile.TemporaryDirectory(prefix='boids-kernels-') as folder:
            root=Path(folder)
            for name,rows in [('avoid',avoid),('angles',angles)]:
                (root/name).write_bytes(struct.pack('<I',len(rows))+b''.join(rows))
            result=subprocess.run(['c++','-std=c++14','-Wall','-Wextra','-Werror','-ffp-contract=off',
                *native_test_compile_flags(),'-I',str(ROOT/'bigworld'),str(ROOT/'tests/stock_boids_filter_host.cpp'),
                '-o',str(root/'probe')],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stderr)
            result=subprocess.run([str(root/'probe'),str(root/'avoid'),str(root/'angles')],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)

    def test_original_output_initialisation_and_substeps(self):
        client=os.environ.get('WOT_0922_CLIENT')
        if not client:self.skipTest('Pinned client required')
        if not all(importlib.util.find_spec(n) for n in ('pefile','unicorn')):
            self.skipTest('Original x86 oracle dependencies required')
        from stock_boids_filter_oracle import OriginalBoids
        original=OriginalBoids(Path(client)/'WorldOfTanks.exe')
        matrix=(1,0,0,0,0,1,0,0,0,0,1,0,2,3,4,1)
        cases=[]
        for elapsed in (-1.,0.,.01,.1,.2,.25,1.,5.,19.999,20.,21.,float('nan'),float('inf')):
            values,steps=original.outer((10.,50.,30.),[matrix],elapsed)
            self.assertEqual(values[0][:3],(12.,53.,34.))
            self.assertTrue(all(goal==(10.,50.,30.) for goal,step in steps))
            cases.append(struct.pack('<fI',elapsed,len(steps))+b''.join(struct.pack('<f',v[1]) for v in steps))
        with tempfile.TemporaryDirectory(prefix='boids-substeps-') as folder:
            root=Path(folder);(root/'steps').write_bytes(struct.pack('<I',len(cases))+b''.join(cases))
            source='#include "stock_boids_filter.hpp"\n#include <cassert>\n#include <cstring>\n#include <fstream>\n#include <vector>\nint main(int argc,char** argv){assert(argc==2);std::ifstream f(argv[1],std::ios::binary);unsigned count;f.read(reinterpret_cast<char*>(&count),4);for(unsigned i=0;i<count;++i){float elapsed;unsigned expected;f.read(reinterpret_cast<char*>(&elapsed),4);f.read(reinterpret_cast<char*>(&expected),4);std::vector<float> values;wot::stockBoidSubsteps(elapsed,[&](float v){values.push_back(v);});assert(values.size()==expected);for(float actual:values){float original;f.read(reinterpret_cast<char*>(&original),4);assert(std::memcmp(&actual,&original,4)==0);}}assert(f);}'
            (root/'host.cpp').write_text(source)
            result=subprocess.run(['c++','-std=c++14','-Wall','-Wextra','-Werror',*native_test_compile_flags(),
                '-I',str(ROOT/'bigworld'),str(root/'host.cpp'),'-o',str(root/'probe')],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stderr)
            result=subprocess.run([str(root/'probe'),str(root/'steps')],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)

    def test_sdk_owner_cache_attach_and_original_flock(self):
        if not os.environ.get('WOT_0922_CLIENT'):self.skipTest('Pinned client required')
        python=Path(os.environ.get('WOT_PYTHON27','/Users/peng/.pyenv/versions/2.7.18/bin/python2.7'))
        config=python.with_name('python2.7-config')
        if not config.exists():self.skipTest('CPython 2.7 development files required')
        archive=Path(os.environ.get('WOT_PUBLIC_ENGINE_ARCHIVE','/tmp/wot-bigworld-engine-14.4.1.tar.gz'))
        if not archive.exists():self.skipTest('Pinned public SDK archive required')
        from test_stock_vector4_basic import block
        names=['client/'+n for n in ('py_boids_filter.hpp','py_boids_filter.cpp','boids_filter.hpp','boids_filter.cpp')]
        with tempfile.TemporaryDirectory(prefix='boids-owner-') as folder:
            root=Path(folder)
            with tarfile.open(archive) as tar:
                converter='';random_header='';random_source=''
                for member in tar:
                    if member.name.endswith('/programming/bigworld/lib/pyscript/script.cpp'):
                        converter=tar.extractfile(member).read().decode('utf-8-sig')
                    if member.name.endswith('/programming/bigworld/lib/cstdmf/bwrandom.hpp'):
                        random_header=tar.extractfile(member).read().decode('utf-8-sig')
                    if member.name.endswith('/programming/bigworld/lib/cstdmf/bwrandom.cpp'):
                        random_source=tar.extractfile(member).read().decode('utf-8-sig')
                    for name in names:
                        if member.name.endswith('/programming/bigworld/'+name):
                            path=root/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(tar.extractfile(member).read())
            module(ROOT/'bigworld/patch_stock_boids_filter.py').patch(root)
            values={n:(root/n).read_text() for n in names}
            classes=block(random_header,'class CSTDMF_DLL BWRandom')+';\n'
            classes+=block(values[names[2]],'class BoidsFilter : public AvatarFilter')+';\n'
            classes+=block(values[names[0]],'class PyBoidsFilter : public PyAvatarFilter')+';'
            for name in ('yprSpeed','deadZonePosition','deadZoneRadius'):
                self.assertIn('PY_ATTRIBUTE( '+name+' )',values[names[1]])
            native=values[names[3]];python_source=values[names[1]]
            bodies='const int BoidsFilter::STATE_LANDING=1;\n'
            bodies+='const uint32 mag01[2]={0u,0x9908b0dfu}; const uint32 UPPER_MASK=0x80000000u,LOWER_MASK=0x7fffffffu;\n'
            for signature in ('BWRandom::BWRandom()', 'BWRandom::BWRandom(uint32 seed)',
                              'void BWRandom::init(uint32 seed)', 'uint32 BWRandom::operator()()',
                              'uint32 BWRandom::generate()'):
                bodies+=block(random_source,signature)+'\n'
            bodies+=block(native,'static float stockBoidUnitRandom()')+'\n'
            for signature in ('BoidsFilter::BoidsFilter(', 'BoidsFilter::~BoidsFilter()',
                              'bool BoidsFilter::tryCopyState(', 'BoidsFilter::BoidData::BoidData()',
                              'void BoidsFilter::BoidData::updateModel(', 'void BoidsFilter::output('):
                bodies+=block(native,signature)+'\n'
            bodies+='BoidsFilter::BoidData::~BoidData(){}\n'
            bodies+=block(python_source,'PyBoidsFilter::PyBoidsFilter(')+'\n'
            start=python_source.index('#define GET_SET_CACHED')
            stop=python_source.index('/**\n *\tThis method returns the BoidsFilter * we created',start)
            bodies+=python_source[start:stop]+'\n'
            for signature in ('BoidsFilter * PyBoidsFilter::pAttachedFilter()',
                              'const BoidsFilter * PyBoidsFilter::pAttachedFilter() const',
                              'BoidsFilter * PyBoidsFilter::getNewFilter()',
                              'void PyBoidsFilter::onLosingAttachedFilter()'):
                bodies+=block(python_source,signature)+'\n'
            converters=''.join(block(converter,'int Script::setData( PyObject * pObject, '+name)
                               for name in ('double & rDouble,','float & rFloat,','Vector3 & rVector,'))
            host=(ROOT/'tests/stock_boids_filter_owner_host.cpp').read_text()
            host=host.replace('@CLASSES@',classes).replace('@SDK_OWNER_BODIES@',bodies).replace('@CONVERTERS@',converters)
            (root/'host.cpp').write_text(host)
            fixture=None
            if all(importlib.util.find_spec(n) for n in ('pefile','unicorn')):
                from stock_boids_filter_oracle import OriginalBoids
                original=OriginalBoids(Path(os.environ['WOT_0922_CLIENT'])/'WorldOfTanks.exe')
                random_source=random.Random(1513)
                rows=[]
                for unused in range(80):
                    yaw=random_source.uniform(-3,3);pitch=random_source.uniform(-1,1);roll=random_source.uniform(-1,1)
                    # Ordinary affine model matrices, with independent axis scales.
                    c,s0=math.cos(yaw),math.sin(yaw)
                    matrix=[c,math.sin(roll),-s0,0,math.sin(pitch),math.cos(roll),math.sin(roll),0,s0,math.sin(pitch),c,0,
                            random_source.uniform(-10,10),random_source.uniform(-10,10),random_source.uniform(-10,10),1]
                    matrix=[struct.unpack('<f',struct.pack('<f',v))[0] for v in matrix]
                    goal=(10.,-200.,30.)
                    expected,steps=original.outer(goal,[tuple(matrix)],0.)
                    self.assertEqual(steps,[])
                    rows.append(struct.pack('<30f',*(tuple(matrix)+goal+expected[0])))
                fixture=root/'outer.bin';fixture.write_bytes(struct.pack('<I',len(rows))+b''.join(rows))
            model_fixture=None;random_fixture=None
            if fixture:
                random_fixture=root/'random.bin';random_fixture.write_bytes(struct.pack('<64I',*original.random_values(1513,64)))
                model_rows=[]
                f=lambda v:struct.unpack('<f',struct.pack('<f',v))[0]
                vec=lambda lo,hi:tuple(f(random_source.uniform(lo,hi)) for unused in range(3))
                for i in range(300):
                    state=vec(-.7,.7)+(f(random_source.uniform(-.1,.1)),f(random_source.uniform(.1,2)))
                    position=vec(-10,10);direction=vec(-1,1)
                    n=math.sqrt(sum(v*v for v in direction));direction=tuple(f(v/n) for v in direction)
                    rates=vec(.01,1);centre=vec(-10,10);radius=(0.,10.,20.)[i%3]
                    normal=f(random_source.uniform(.1,2));ratio=f(.002);dt=f(random_source.uniform(.001,.1));landing=i%2
                    goal=vec(-10,10)
                    expected=original.model(state,position,direction,rates,centre,radius,normal,ratio,dt,landing,goal)
                    inputs=state+position+direction+rates+centre+(radius,normal,ratio,dt,float(landing))+goal
                    model_rows.append(struct.pack('<52f',*(inputs+expected)))
                model_fixture=root/'models.bin';model_fixture.write_bytes(struct.pack('<I',len(model_rows))+b''.join(model_rows))
            flags=shlex.split(subprocess.check_output([str(config),'--cflags'],text=True))
            libs=shlex.split(subprocess.check_output([str(config),'--ldflags'],text=True))
            result=subprocess.run(['c++','-std=c++14','-Wall','-Wextra','-Werror','-ffp-contract=off',
                *flags,*native_test_compile_flags(),'-UNDEBUG','-Wno-reorder','-Wno-missing-field-initializers',
                '-Wno-write-strings','-Wno-deprecated-declarations','-I',str(ROOT/'bigworld'),
                str(root/'host.cpp'),'-o',str(root/'probe'),*libs],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stderr)
            result=subprocess.run([str(root/'probe'),str(ROOT/'tests/stock_boids_filter_consumer.py')],
                capture_output=True,text=True,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0',WOT_BOIDS_OUTER_FIXTURE=str(fixture or ''),WOT_BOIDS_MODEL_FIXTURE=str(model_fixture or ''),WOT_BOIDS_RANDOM_FIXTURE=str(random_fixture or '')))
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            self.assertIn('actual SDK cache, attach, copy and model consumers PASS',result.stdout)

    def test_sdk_patch_is_idempotent(self):
        archive=Path(os.environ.get('WOT_PUBLIC_ENGINE_ARCHIVE','/tmp/wot-bigworld-engine-14.4.1.tar.gz'))
        if not archive.exists():self.skipTest('Pinned public SDK archive required')
        names=['client/'+n for n in ('py_boids_filter.hpp','py_boids_filter.cpp','boids_filter.hpp','boids_filter.cpp')]
        with tempfile.TemporaryDirectory(prefix='boids-sdk-') as folder:
            root=Path(folder)
            with tarfile.open(archive) as tar:
                for member in tar:
                    for name in names:
                        if member.name.endswith('/programming/bigworld/'+name):
                            path=root/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(tar.extractfile(member).read())
            patcher=module(ROOT/'bigworld/patch_stock_boids_filter.py');patcher.patch(root)
            first={n:(root/n).read_bytes() for n in names};patcher.patch(root)
            self.assertEqual(first,{n:(root/n).read_bytes() for n in names})


if __name__=='__main__':unittest.main()
