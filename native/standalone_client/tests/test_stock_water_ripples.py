"""Run water impulses through the real SDK cell and shader draw consumers."""
from pathlib import Path
import io
import json
import importlib.util
import random
import struct
import os
import shlex
import subprocess
import sys
import tarfile
import tempfile
import unittest
import zipfile

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'bigworld'))
import patch_stock_water_ripples as patcher


def body(source,signature):
    start=source.index(signature);end=source.index('{',start);depth=1
    while depth:
        end+=1;depth+=(source[end]=='{')-(source[end]=='}')
    return source[start:end+1]


def sources(test):
    archive=Path(os.environ.get('WOT_PUBLIC_ENGINE_ARCHIVE','/tmp/wot-bigworld-engine-14.4.1.tar.gz'))
    if not archive.is_file():test.skipTest('Pinned public engine source archive required')
    needed={'lib/romp/water.cpp','lib/romp/water.hpp','lib/romp/water_simulation.cpp',
            'lib/romp/water_simulation.ipp','lib/scene/scene_draw_context.hpp',
            'lib/romp/effect_parameter_cache.hpp','lib/romp/effect_parameter_cache.cpp',
            'lib/moo/managed_effect.cpp','lib/moo/effect_material.cpp','client/world_app.cpp'}
    result={}
    with tarfile.open(archive) as archive:
        for member in archive:
            for relative in needed:
                if member.name.endswith('/programming/bigworld/'+relative):
                    result[relative]=archive.extractfile(member).read().decode()
            if set(result)==needed:break
    test.assertEqual(set(result),needed)
    return result


def put_sources(directory,original):
    for relative,text in original.items():
        path=directory/relative;path.parent.mkdir(parents=True,exist_ok=True);path.write_text(text)


def build_host(test,directory,original):
    python=Path(os.environ.get('WOT_PYTHON27','/Users/peng/.pyenv/versions/2.7.18/bin/python2.7'))
    config=python.with_name('python2.7-config')
    if not config.exists():test.skipTest('CPython 2.7 development files required')
    engine=directory/'engine';put_sources(engine,original);patcher.patch(engine)
    water=(engine/'lib/romp/water.cpp').read_text()
    simulation=(engine/'lib/romp/water_simulation.cpp').read_text()
    inline=original['lib/romp/water_simulation.ipp']
    generated=(ROOT/'tests/stock_water_ripples_host.cpp').read_text()
    replacements={
        '@WATER_ADD@':body(water,'bool Water::addMovement('),
        '@WATERS_ADD@':body(water,'void Waters::addMovement('),
        '@DRAW_MOVEMENTS@':body(simulation,'void SimulationManager::drawMovements('),
        '@SIMULATE_BLOCK@':body(simulation,'void SimulationManager::simulateBlock('),
        '@SIMULATE_CELL@':body(simulation,'void SimulationCell::simulate('),
        '@SIMULATION_METHODS@':'\n'.join(body(inline,signature) for signature in (
            'INLINE void Simulation::addMovement(', 'INLINE void Simulation::updateMovements(',
            'INLINE const Movements& Simulation::movements()', 'INLINE void Simulation::resetIdleTimer()',
            'INLINE int Simulation::size()', 'INLINE void Simulation::clearMovements()'))}
    for key,value in replacements.items():generated=generated.replace(key,value)
    generated=generated.replace('const char*=""','const char* = ""').replace('struct Waters;','using ID3DXEffect=Effect;\nstruct Waters;')
    source=directory/'host.cpp';source.write_text(generated)
    flags=shlex.split(subprocess.check_output([str(config),'--cflags'],text=True))
    libs=shlex.split(subprocess.check_output([str(config),'--ldflags'],text=True))
    binary=directory/'host'
    result=subprocess.run(['c++','-std=c++14','-ffp-contract=off','-Wno-deprecated-register','-Wno-writable-strings',
        '-I',str(ROOT/'bigworld'),*flags,'-UNDEBUG',str(source),*libs,'-o',str(binary)],capture_output=True,text=True)
    test.assertEqual(result.returncode,0,result.stdout+result.stderr)
    return binary


class WaterRippleTests(unittest.TestCase):
    def test_real_setup_sim_with_original_material_uniform_storage(self):
        client=os.environ.get('WOT_0922_CLIENT')
        if not client:self.skipTest('WOT_0922_CLIENT required for original simulation resources')
        sys.path.insert(0,str(ROOT.parents[1]/'tools'))
        from prepare_native_client import OriginalResources
        resources=OriginalResources(client)
        with tempfile.TemporaryDirectory(prefix='water-simulation-storage-') as directory:
            directory=Path(directory);engine=directory/'engine';put_sources(engine,sources(self));patcher.patch(engine)
            setup=body((engine/'lib/romp/water_simulation.cpp').read_text(),'void SimulationManager::setupSim(')
            generated=(ROOT/'tests/stock_water_simulation_setup_host.cpp').read_text().replace('@SETUP@',setup)
            source=directory/'setup.cpp';source.write_text(generated);binary=directory/'setup'
            result=subprocess.run(['c++','-std=c++17','-I',str(ROOT/'include'),str(source),
                str(ROOT/'src/stock_shader_binary.cpp'),str(ROOT/'src/stock_effect_reflection.cpp'),
                str(ROOT/'src/stock_effect_binding.cpp'),'-o',str(binary)],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            for model in (10,11):
                with zipfile.ZipFile(io.BytesIO(resources.read('shaders/water/simulation.%d.dx9.fxo'%model))) as shader:
                    effect=directory/('simulation%d.effect'%model);effect.write_bytes(shader.read('effect'))
                result=subprocess.run([str(binary),str(effect)],capture_output=True,text=True)
                self.assertEqual(result.returncode,0,result.stdout+result.stderr)

    def test_real_surface_draw_material_cache_and_reset(self):
        import patch_stock_effect_owners as owners
        original=sources(self)
        with tempfile.TemporaryDirectory(prefix='water-surface-binding-') as directory:
            directory=Path(directory);engine=directory/'engine';put_sources(engine,original)
            patcher.patch(engine)
            water=(engine/'lib/romp/water.cpp').read_text()
            cache=original['lib/romp/effect_parameter_cache.cpp']
            cache=cache[cache.index('EffectParameterCache::~'):cache.index('BW_END_NAMESPACE')]
            managed=owners.patch_managed_lifecycle(original['lib/moo/managed_effect.cpp'])
            material=owners.replace_once(original['lib/moo/effect_material.cpp'],
                'ID3DXEffect*\t\t\t\tpEffect = pManagedEffect_->pEffect();',
                'if (!properties_.select(*pManagedEffect_)) return;\n'
                '\tID3DXEffect*\t\t\t\tpEffect = pManagedEffect_->pEffect();',
                'immediate material owner')
            generated=(ROOT/'tests/stock_water_surface_host.cpp').read_text()
            replacements={
                '@CACHE@':original['lib/romp/effect_parameter_cache.hpp']+'\n'+cache,
                '@PROPERTIES@':owners.PROPERTIES,
                '@MANAGED@':'\n'.join(body(managed,signature) for signature in (
                    'bool ManagedEffect::selectMaterialBinding(',
                    'bool ManagedEffect::begin( bool setAutoConstants )',
                    'bool ManagedEffect::beginPass( uint32 pass )',
                    'bool ManagedEffect::endPass()', 'bool ManagedEffect::end()',
                    'bool ManagedEffect::commitChanges()')),
                '@MATERIAL@':'\n'.join(body(material,signature)
                    for signature in ('bool EffectMaterial::begin() const',
                        'void EffectMaterial::setProperties() const',
                        'bool EffectMaterial::beginPass( uint32 pass ) const',
                        'bool EffectMaterial::endPass() const', 'bool EffectMaterial::end() const')),
                '@RELEASE@':body(water,'static void releaseStockWaterRenderTargets('),
                '@DELETE@':body(water,'void Water::deleteUnmanagedObjects('),
                '@DRAW@':body(water,'void Water::draw( Waters & group, float dTime )')}
            for key,value in replacements.items():generated=generated.replace(key,value)
            source=directory/'surface.cpp';source.write_text(generated);binary=directory/'surface'
            result=subprocess.run(['c++','-std=c++14',str(source),'-o',str(binary)],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            result=subprocess.run([str(binary)],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            self.assertIn('SDK Water::draw and cache/material/reset consumer passed',result.stdout)
            # Failing control: the previous patch never selected/rebound the
            # surface cache. Its actual SDK draw writes the retained Definition.
            selection='''    Waters::s_effect_->checkEffectRecompiled();
    if (!Waters::s_effect_->properties().select(*Waters::s_effect_->pEffect()))
        return;
    paramCache_->effect(Waters::s_effect_->pEffect()->pEffect());'''
            self.assertIn(selection,generated)
            source.write_text(generated.replace(selection,''))
            result=subprocess.run(['c++','-std=c++14',str(source),'-o',str(binary)],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            result=subprocess.run([str(binary)],capture_output=True,text=True)
            self.assertNotEqual(result.returncode,0)

    def test_real_water_cell_and_shader_consumers(self):
        with tempfile.TemporaryDirectory(prefix='water-ripples-') as directory:
            binary=build_host(self,Path(directory),sources(self))
            result=subprocess.run([str(binary),str(ROOT/'tests/stock_water_ripples_consumer.py')],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            self.assertIn('Water ripple cell and shader consumer passed',result.stdout)
            if os.environ.get('WOT_0922_CLIENT'):
                self.assertIn('Original ProjectileMover.pyc six-ripple callback producer passed',result.stdout)

    def test_original_wrapper_forwarding_and_failures(self):
        client=os.environ.get('WOT_0922_CLIENT')
        if not client:self.skipTest('WOT_0922_CLIENT required for original wrapper')
        if not all(importlib.util.find_spec(name) for name in ('pefile','unicorn')):
            self.skipTest('pefile and unicorn required for original wrapper')
        sys.path.insert(0,str(ROOT/'tests'))
        from stock_water_ripples_oracle import Original,WATERS
        original=Original(Path(client)/'WorldOfTanks.exe');rng=random.Random(0xf625e0)
        f=lambda value:struct.unpack('<f',struct.pack('<f',value))[0]
        for count in (0,1,3):
            for unused in range(20):
                position=[f(rng.uniform(-200,200)) for _ in range(3)]
                diameter=f(rng.uniform(-2,20))
                result,calls,movements=original.run(position,diameter,count)
                self.assertEqual(result,'None')
                self.assertEqual([call[1] for call in calls],['Vector3','float'])
                self.assertEqual(movements,[[WATERS+0x200+0x400*i,
                    [position[0],f(position[1]+5.),position[2]],
                    [position[0],f(position[1]-5.),position[2]],diameter] for i in range(count)])
        for argc in (0,1,3,4):
            result,calls,movements=original.run([0,0,0],2,argc=argc)
            self.assertEqual(result,'NULL');self.assertEqual(movements,[])
            self.assertEqual(calls[0][:4],['argCountError','wg_addWaterRipples',2,2])
        for failure in (0,1):
            result,calls,movements=original.run([0,0,0],2,fail_at=failure)
            self.assertEqual(result,'NULL');self.assertEqual(movements,[])

    def test_original_water_guards_and_cell_impulses(self):
        client=os.environ.get('WOT_0922_CLIENT')
        if not client:self.skipTest('WOT_0922_CLIENT required for original Water instructions')
        if not all(importlib.util.find_spec(name) for name in ('pefile','unicorn')):
            self.skipTest('pefile and unicorn required for original Water instructions')
        sys.path.insert(0,str(ROOT/'tests'))
        from stock_water_ripples_oracle import OriginalWater
        original=OriginalWater(Path(client)/'WorldOfTanks.exe');rng=random.Random(0xa279d0)
        cases=[];expected=[]
        for index in range(75):
            position=[rng.uniform(-55,55),rng.choice([0.,5.,10.,15.,20.,22.,25.,30.,300.]),rng.uniform(-45,45)]
            diameter=rng.uniform(-2,30);mode=index%5 if index%5<3 else 0
            position=[struct.unpack('<f',struct.pack('<f',v))[0] for v in position]
            diameter=struct.unpack('<f',struct.pack('<f',diameter))[0]
            accepted,resets,impulses=original.add(position,diameter,mode!=1,mode!=2)
            self.assertEqual(resets,0 if mode==1 else 4)
            if impulses:self.assertEqual(accepted,1)
            cases.append([mode,position,diameter]);expected.append(impulses)
        with tempfile.TemporaryDirectory(prefix='water-ripples-owner-') as directory:
            directory=Path(directory);binary=build_host(self,directory,sources(self))
            script=directory/'native_owner.py'
            script.write_text('import BigWorld as B,json\nB.fixture("one")\nresult=[]\n'+
                'for mode,position,diameter in '+repr(cases)+':\n'+
                ' B.fixture("clear");B.fixture("mode",mode);B.wg_addWaterRipples(position,diameter)\n'+
                ' result.append(B.fixture("details"))\nprint(json.dumps(result))\n')
            result=subprocess.run([str(binary),str(script)],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            actual=json.loads(result.stdout)
            for case,(left,right) in enumerate(zip(actual,expected)):
                self.assertEqual(len(left),len(right),str(cases[case]))
                for produced,reference in zip(left,right):
                    self.assertEqual(produced[0],reference[0])
                    for a,b in zip(produced[1:],reference[1:]):self.assertAlmostEqual(a,b,delta=.00002)

    def test_original_simulation_and_surface_shader_resources(self):
        client=os.environ.get('WOT_0922_CLIENT')
        if not client:self.skipTest('WOT_0922_CLIENT required for original resources')
        sys.path.insert(0,str(ROOT.parents[1]/'tools'))
        from prepare_native_client import OriginalResources
        resources=OriginalResources(client)
        self.assertTrue(resources.read('system/maps/water_cookie.dds').startswith(b'DDS '))
        self.assertTrue(resources.read('system/maps/null_sim.dds').startswith(b'DDS '))
        with tempfile.TemporaryDirectory(prefix='water-ripples-effect-') as directory:
            directory=Path(directory);binary=directory/'probe'
            subprocess.run(['c++','-std=c++17','-I',str(ROOT/'include'),
                str(ROOT/'tests/stock_water_ripples_effect.cpp'),str(ROOT/'src/stock_shader_binary.cpp'),
                str(ROOT/'src/stock_effect_reflection.cpp'),'-o',str(binary)],check=True)
            for model in (10,11):
                for name in ('simulation','water'):
                    resource='shaders/water/%s.%d.dx9.fxo'%(name,model)
                    with zipfile.ZipFile(io.BytesIO(resources.read(resource))) as shader:
                        effect=directory/('%s%d.effect'%(name,model));effect.write_bytes(shader.read('effect'))
                    kind=name if name=='simulation' else ('water_deferred' if model==10 else 'water_forward')
                    result=subprocess.run([str(binary),str(effect),kind],capture_output=True,text=True)
                    self.assertEqual(result.returncode,0,resource+result.stdout+result.stderr)

    def test_patcher_is_anchored_idempotent_and_composes(self):
        original=sources(self)
        import patch_stock_space_water
        import patch_stock_control_mode_rendering
        with tempfile.TemporaryDirectory(prefix='water-ripples-patch-') as directory:
            directory=Path(directory);put_sources(directory,original)
            # Existing water patch owns fields, transforms, resource parameters
            # and draw-list lifetime. The control-mode patch owns a scene flag.
            patch_stock_space_water.patch(directory)
            patch_stock_control_mode_rendering.patch(directory)
            patcher.patch(directory)
            first={relative:(directory/relative).read_text() for relative in original}
            patcher.patch(directory)
            self.assertEqual(first,{relative:(directory/relative).read_text() for relative in original})
            simulation=first['lib/romp/water_simulation.cpp']
            # Reusing an engine preparation from the previous ripple patch
            # must upgrade its rain selector instead of rejecting the cache.
            rain=('\t// Rain has no cell neighbours; clear a prior edge cell\'s flags.\n'+
                ''.join('\tpEffect->SetBool("%s", false);\n' % name
                    for name in ('Y0_enabled','X0_enabled','Y2_enabled','X2_enabled')).rstrip())
            self.assertIn(rain,simulation)
            simulation_path=directory/'lib/romp/water_simulation.cpp'
            simulation_path.write_text(simulation.replace(rain,
                '\t// #1513 selects edge behaviour through neighbour booleans and techniques.'))
            patcher.patch(directory)
            self.assertEqual(simulation_path.read_text(),simulation)
            self.assertNotIn('SetInt( "perturb"',simulation)
            self.assertNotIn('SetInt("perturb"',simulation)
            self.assertNotIn('SetInt( "edge"',simulation)
            self.assertIn('hTechnique("WaterSimulationPerturb")',simulation)
            self.assertIn('hTechnique("WaterSimulation")',simulation)
            self.assertNotIn('"water_simulation"',first['lib/romp/water.cpp'])
            (directory/'lib/romp/water_simulation.cpp').write_text(original['lib/romp/water_simulation.cpp'].replace('"perturb", 1','"changed", 1'))
            with self.assertRaises(RuntimeError):patcher.patch(directory)
