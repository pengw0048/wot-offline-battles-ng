"""Exact #1513 shader selection with real SDK load/save/clone and deferred update."""
from pathlib import Path
import importlib.util
import io
import os
import shutil
import subprocess
import sys
import tarfile
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tests'))
from native_test_compile_flags import native_test_compile_flags
from test_stock_tps_owners import extract_method
spec = importlib.util.spec_from_file_location('sprite_shimmer', ROOT / 'bigworld/patch_stock_sprite_shimmer.py')
patch = importlib.util.module_from_spec(spec)
spec.loader.exec_module(patch)


def sdk(test):
    archive = Path(os.environ.get('WOT_PUBLIC_ENGINE_ARCHIVE', '/tmp/wot-bigworld-engine-14.4.1.tar.gz'))
    if not archive.is_file():
        if 'WOT_PUBLIC_ENGINE_ARCHIVE' in os.environ:
            raise AssertionError('Invalid explicit WOT_PUBLIC_ENGINE_ARCHIVE')
        test.skipTest('Pinned public SDK archive required')
    names = ('lib/particle/renderers/sprite_particle_renderer.cpp',
             'lib/particle/renderers/sprite_particle_renderer.ipp',
             'lib/particle/particle_serialisation.hpp',
             'client/world_app.cpp')
    with tarfile.open(archive) as tar:
        return {name: tar.extractfile(next(m for m in tar if m.name.endswith('/'+name))).read().decode('utf-8-sig') for name in names}


def client(test):
    value = os.environ.get('WOT_0922_CLIENT')
    if not value:
        test.skipTest('WOT_0922_CLIENT required for exact client evidence')
    result = Path(value)
    if not (result / 'WorldOfTanks.exe').is_file() or not (result / 'res/packages/particles.pkg').is_file():
        raise AssertionError('Invalid explicit WOT_0922_CLIENT')
    return result


def prepared(folder, source):
    for name, text in source.items():
        path = folder/name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text)
    patch.patch(folder)
    return (folder/'lib/particle/renderers/sprite_particle_renderer.cpp').read_text()


def host(source, original_ipp):
    methods = [extract_method(source, 'void SpriteParticleRenderer::materialFX(')]
    for signature in ('void SpriteParticleRenderer::updateMaterial()',
                      'void SpriteParticleRenderer::setMaterial(',
                      'void SpriteParticleRenderer::setMaterialProperties(',
                      'void SpriteParticleRenderer::setMaterialFX()',
                      'void SpriteParticleRenderer::loadInternal(',
                      'void SpriteParticleRenderer::saveInternal(',
                      'SpriteParticleRenderer * SpriteParticleRenderer::clone() const'):
        methods.append(extract_method(source, signature))
    begin = source.index('template < typename Serialiser >')
    end = source.index('\n\nvoid SpriteParticleRenderer::loadInternal', begin)
    methods.append(source[begin:end])
    methods.append(extract_method(original_ipp, 'INLINE void SpriteParticleRenderer::effectName(').replace('INLINE ', ''))
    begin = source.index('void SpriteParticleRenderer::draw(')
    begin = source.index('{', begin)+1
    end = source.index('    if ( beg != end )', begin)
    methods.append('void SpriteParticleRenderer::drawPrefix(){'+source[begin:end]+'}')
    text = (ROOT/'tests/stock_sprite_shimmer_host.hpp').read_text().replace('@METHODS@', '\n'.join(methods))
    # Adapt only the public SDK SmartPointer raw-new assignment to std::shared_ptr.
    return text.replace('material = new Moo::EffectMaterial();', 'material.reset(new Moo::EffectMaterial());')


class SpriteShimmerTests(unittest.TestCase):
    def test_sdk_shader_owner_load_clone_serialise_and_negative_controls(self):
        source = sdk(self)
        compiler = os.environ.get('CXX') or shutil.which('c++')
        if not compiler:
            if 'CXX' in os.environ:
                raise AssertionError('Invalid explicit CXX')
            self.skipTest('C++ compiler required')
        with tempfile.TemporaryDirectory(prefix='sprite-shimmer-') as temp:
            folder = Path(temp)
            cpp = prepared(folder, source)
            first = {str(p.relative_to(folder)):p.read_bytes() for p in folder.rglob('*') if p.is_file()}
            patch.patch(folder)
            self.assertEqual(first, {str(p.relative_to(folder)):p.read_bytes() for p in folder.rglob('*') if p.is_file()})
            ipp = source['lib/particle/renderers/sprite_particle_renderer.ipp']
            generated = host(cpp, ipp)
            (folder/'particle_serialisation.hpp').write_text(source['lib/particle/particle_serialisation.hpp'])
            for name, text in [('production', generated),
                               ('old-selection', generated.replace(patch.SETTER, 'void SpriteParticleRenderer::materialFX(MaterialFX v){materialFX_=v;materialSettingsChanged_=true;}')),
                               ('old-reload', generated.replace('        updateMaterial();\n', '        setMaterialProperties(true);\n')),
                               ('old-properties', generated.replace(
                                   extract_method(cpp, 'void SpriteParticleRenderer::setMaterialProperties('),
                                   extract_method(source['lib/particle/renderers/sprite_particle_renderer.cpp'],
                                                  'void SpriteParticleRenderer::setMaterialProperties('))),
                               ('old-blending', generated.replace(
                                   extract_method(cpp, 'void SpriteParticleRenderer::setMaterialFX()'),
                                   extract_method(source['lib/particle/renderers/sprite_particle_renderer.cpp'],
                                                  'void SpriteParticleRenderer::setMaterialFX()')))]:
                path=folder/(name+'.cpp');path.write_text(text);binary=folder/(name+'.exe')
                subprocess.run([compiler,'-std=c++17','-UNDEBUG',*native_test_compile_flags(compiler),str(path),'-o',str(binary)],check=True)
                result=subprocess.run([str(binary)],capture_output=True,text=True)
                self.assertEqual(result.returncode==0,name=='production',(name,result.stdout,result.stderr))
            # Invalid second seam must leave the first source file untouched.
            bad=folder/'lib/particle/renderers/sprite_particle_renderer.ipp'
            bad.write_text('changed SDK seam')
            before=(folder/'lib/particle/renderers/sprite_particle_renderer.cpp').read_bytes()
            with self.assertRaises(ValueError):patch.patch(folder)
            self.assertEqual(before,(folder/'lib/particle/renderers/sprite_particle_renderer.cpp').read_bytes())

    def test_composition_after_existing_particle_scale(self):
        from test_stock_particle_scale import sources, prepare
        source=sources(self)
        source.update(sdk(self))
        with tempfile.TemporaryDirectory(prefix='sprite-shimmer-chain-') as temp:
            folder=Path(temp)
            prepare(folder,source)
            patch.patch(folder)
            first={str(p.relative_to(folder)):p.read_bytes() for p in folder.rglob('*') if p.is_file()}
            patch.patch(folder)
            self.assertEqual(first,{str(p.relative_to(folder)):p.read_bytes() for p in folder.rglob('*') if p.is_file()})
            cpp=(folder/'lib/particle/renderers/sprite_particle_renderer.cpp').read_text()
            self.assertIn('job->scale_ = scale;',cpp)
            self.assertIn('s_shimmerEffectFileName',cpp)
            world=(folder/'client/world_app.cpp').read_text()
            self.assertNotIn('ALL_CHANNELS_MASK',world)
            self.assertEqual(world.count('Moo::DrawContext::TRANSPARENT_CHANNEL_MASK );'),2)

    def test_original_native_selection(self):
        original=client(self)
        try:
            import pefile, unicorn
        except ImportError:
            self.skipTest('Optional original PE execution dependencies required')
        from stock_sprite_shimmer_oracle import audit, audit_properties
        self.assertEqual(audit(original/'WorldOfTanks.exe'),28)
        self.assertEqual(audit_properties(original/'WorldOfTanks.exe'),128)

    def test_original_shot_producer_and_shader_resources(self):
        original=client(self)
        sys.path.insert(0,str(ROOT.parents[1]/'tools'))
        from packed_xml import read_packed_xml
        def children(element):return {name.decode():value for name,value in element.children}
        resources=children(read_packed_xml((original/'res/resources.xml').read_bytes()))
        sprites=children(children(resources['system'].value)['spriteParticles'].value)
        self.assertEqual(sprites['default'].value,b'shaders/std_effects/sprite_particle.fx')
        self.assertEqual(sprites['shimmer'].value,b'shaders/std_effects/shimmer_sprite_particle.fx')
        with zipfile.ZipFile(original/'res/packages/particles.pkg') as z:
            root=children(read_packed_xml(z.read('particles/Tank/shots/shot_small_01.xml')))
            renderer=children(children(root['shimmer_wave'].value)['Renderer'].value)['SpriteParticleRenderer']
            settings=children(renderer.value)
            self.assertEqual(settings['materialFX_'].value,6)
            self.assertEqual(settings['textureName_'].value,b'particles/maps/animtex/shimmer_anim.dds')
        compiler=os.environ.get('CXX') or shutil.which('c++')
        if not compiler:
            if 'CXX' in os.environ:raise AssertionError('Invalid explicit CXX')
            self.skipTest('C++ compiler required for original shader consumer')
        with tempfile.TemporaryDirectory(prefix='sprite-shader-') as temp:
            folder=Path(temp);binary=folder/'probe.exe'
            subprocess.run([compiler,'-std=c++17',*native_test_compile_flags(compiler),'-I'+str(ROOT/'include'),
                str(ROOT/'tests/stock_sprite_shimmer_effect.cpp'),
                *[str(ROOT/'src'/name) for name in ('stock_shader_binary.cpp','stock_effect_binding.cpp','stock_shader_runtime.cpp')],
                '-o',str(binary)],check=True)
            with zipfile.ZipFile(original/'res/packages/shaders.pkg') as z:
                for infix in ('10','11'):
                    files=[]
                    for shader in ('sprite_particle','shimmer_sprite_particle'):
                        member='shaders/std_effects/%s.%s.dx9.fxo'%(shader,infix)
                        output=folder/(shader+'.effect')
                        output.write_bytes(zipfile.ZipFile(io.BytesIO(z.read(member))).read('effect'))
                        files.append(str(output))
                    subprocess.run([str(binary),*files],check=True)

    def test_original_dx9_shaders_never_sample_the_distortion_target(self):
        original=client(self)
        compiler=os.environ.get('CXX') or shutil.which('c++')
        if not compiler:
            if 'CXX' in os.environ:raise AssertionError('Invalid explicit CXX')
            self.skipTest('C++ compiler required for original shader consumers')
        with tempfile.TemporaryDirectory(prefix='distortion-consumers-') as temp:
            folder=Path(temp);binary=folder/'probe.exe'
            subprocess.run([compiler,'-std=c++17',*native_test_compile_flags(compiler),'-I'+str(ROOT/'include'),
                str(ROOT/'tests/stock_distortion_consumers.cpp'),str(ROOT/'src/stock_shader_binary.cpp'),
                '-o',str(binary)],check=True)
            files=[]
            with zipfile.ZipFile(original/'res/packages/shaders.pkg') as z:
                for member in z.namelist():
                    if member.endswith(('.10.dx9.fxo','.11.dx9.fxo')):
                        output=folder/('%d.effect'%len(files))
                        output.write_bytes(zipfile.ZipFile(io.BytesIO(z.read(member))).read('effect'))
                        files.append(str(output))
            self.assertEqual(len(files),464)
            subprocess.run([str(binary),*files],check=True)



if __name__=='__main__':unittest.main()
