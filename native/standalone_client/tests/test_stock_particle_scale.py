"""Exact #1513 scale wrappers, original PYC lifecycle and actual CPU draw bodies."""
from pathlib import Path
import os
import re
import shlex
import shutil
import subprocess
import sys
import tarfile
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'bigworld'))
sys.path.insert(0,str(ROOT/'tests'))
import patch_stock_particle_scale as scale
import patch_stock_tps as tps
import patch_stock_particle_clone as clone
import patch_stock_particle_background as background
import patch_stock_battleground_elements as battleground
from test_stock_tps_owners import extract_method
from native_test_compile_flags import native_test_compile_flags

EXTRA=('lib/pyscript/pyobject_plus.hpp','lib/pyscript/pyobject_plus.cpp','lib/pyscript/script.cpp',
       'lib/moo/draw_context.cpp','client/world_app.cpp','client/facade_app.cpp','client/script_app.cpp','client/canvas_app.cpp')


def sources(test):
    archive=Path(os.environ.get('WOT_PUBLIC_ENGINE_ARCHIVE','/tmp/wot-bigworld-engine-14.4.1.tar.gz'))
    if not archive.is_file():
        if 'WOT_PUBLIC_ENGINE_ARCHIVE' in os.environ:raise AssertionError('Invalid explicit WOT_PUBLIC_ENGINE_ARCHIVE')
        test.skipTest('Pinned public SDK archive required')
    result={}
    with tarfile.open(archive) as tar:
        for member in tar:
            if not member.isfile() or '/programming/bigworld/' not in member.name:continue
            name=member.name.split('/programming/bigworld/',1)[1]
            if name in EXTRA or (name.startswith('lib/particle/') and name.endswith(('.cpp','.hpp'))):
                result[name]=tar.extractfile(member).read().decode('utf-8-sig')
    test.assertTrue(set(EXTRA).issubset(result))
    return result


def prepare(directory,source):
    for name,text in source.items():
        path=directory/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_text(text)
    for operation in (tps.patch,clone.patch,background.patch,battleground.patch,scale.patch):operation(directory)


def render(template,substitutions):
    text=template.read_text()
    for token,value in substitutions.items():
        assert text.count(token)==1,token
        text=text.replace(token,value)
    assert not re.search(r'@[A-Z_]+@',text)
    return text


def compile_cpp(test,directory,text,python=False):
    compiler=os.environ.get('CXX') or shutil.which('c++')
    if not compiler:
        if 'CXX' in os.environ:raise AssertionError('Invalid explicit CXX')
        test.skipTest('C++ compiler required')
    flags=[];libs=[]
    if python:
        interpreter=Path(os.environ.get('WOT_PYTHON27','/Users/peng/.pyenv/versions/2.7.18/bin/python2.7'))
        config=interpreter.with_name('python2.7-config')
        if not config.is_file():
            if 'WOT_PYTHON27' in os.environ:raise AssertionError('Invalid explicit WOT_PYTHON27 development files')
            test.skipTest('CPython 2.7 development files required')
        flags=[v for v in shlex.split(subprocess.check_output([str(config),'--cflags'],text=True)) if v!='-DNDEBUG']
        libs=shlex.split(subprocess.check_output([str(config),'--ldflags'],text=True))
    path=directory/'host.cpp';path.write_text(text);binary=directory/'host'
    subprocess.run([compiler,'-std=c++14','-UNDEBUG',*flags,'-Wno-deprecated-register','-Wno-writable-strings',
                    *native_test_compile_flags(compiler),str(path),*libs,'-o',str(binary)],check=True)
    return binary


class StockParticleScaleTest(unittest.TestCase):
    def test_patch_composition_all_renderers_defaults_and_atomic_failure(self):
        source=sources(self)
        with tempfile.TemporaryDirectory(prefix='particle-scale-compose-') as temp:
            root=Path(temp);prepare(root,source)
            first={str(p.relative_to(root)):p.read_bytes() for p in root.rglob('*') if p.is_file()}
            scale.patch(root)
            self.assertEqual(first,{str(p.relative_to(root)):p.read_bytes() for p in root.rglob('*') if p.is_file()})
            # The actual runner order has to finish the splitter before adding
            # the new explicit parameter. Every existing patch runs unchanged.
            runner=(ROOT.parents[1]/'tools/build_bigworld_client.ps1').read_text()
            if 'patch_stock_particle_scale.py' in runner:
                self.assertLess(runner.index('patch_stock_battleground_elements.py'),runner.index('patch_stock_particle_scale.py'))
            for name in ('sprite','point_sprite','mesh','visual','amp','blur','trail'):
                header=(root/('lib/particle/renderers/'+name+'_particle_renderer.hpp')).read_text()
                self.assertEqual(header.count('float scale = 1.f'),2,name)
            sprite=(root/'lib/particle/renderers/sprite_particle_renderer.cpp').read_text()
            self.assertIn('const float size = p.size() * scale_;',sprite)
            self.assertIn('job->scale_ = scale;',sprite)
            self.assertIn('sortedDrawItem_.set( this, worldTransform, beg, end, scale );',sprite)
            for name in ('point_sprite','amp','blur','trail'):
                cpp=(root/('lib/particle/renderers/'+name+'_particle_renderer.cpp')).read_text()
                self.assertIn('sortedDrawItem_.set( this, worldTransform, beg, end, scale );',cpp)
                self.assertIn('float /*scale*/',cpp)
            # A late mismatch cannot publish an early partially patched wrapper.
            for name,text in source.items():(root/name).write_text(text)
            for op in (tps.patch,clone.patch,background.patch,battleground.patch):op(root)
            late=root/'lib/particle/renderers/particle_system_draw_item.cpp'
            late.write_text(late.read_text().replace('void ParticleSystemDrawItem::draw()','void changedDraw()'))
            snapshot={p:p.read_bytes() for p in root.rglob('*') if p.is_file()}
            with self.assertRaises(ValueError):scale.patch(root)
            self.assertEqual(snapshot,{p:p.read_bytes() for p in root.rglob('*') if p.is_file()})

    def test_real_sdk_python_properties_and_original_effect_lifecycle(self):
        client=os.environ.get('WOT_0922_CLIENT')
        if not client:self.skipTest('Exact #1513 client required')
        self.assertTrue((Path(client)/'res/packages/scripts.pkg').is_file(),'Invalid explicit WOT_0922_CLIENT')
        source=sources(self)
        with tempfile.TemporaryDirectory(prefix='particle-scale-python-') as temp:
            root=Path(temp);prepare(root,source)
            def read(name):return (root/('lib/particle/'+name)).read_text()
            plus=source['lib/pyscript/pyobject_plus.hpp'];body=source['lib/pyscript/pyobject_plus.cpp']
            script=source['lib/pyscript/script.cpp']
            support=(ROOT/'tests/stock_particle_clone_host.cpp').read_text().split('@PYOBJECT_PLUS_HEADER@')[0]
            conversion='\n'.join(extract_method(script,sig) for sig in (
                'int Script::setData( PyObject * pObject, double & rDouble,',
                'int Script::setData( PyObject * pObject, float & rFloat,',
                'PyObject * Script::getData( const float data )'))
            values={'@PYOBJECT_SUPPORT@':support,
                '@PYOBJECT_PLUS_HEADER@':plus[plus.index('/// This must be the first line'):plus.index('// Section: Weak-referenceable macros')],
                '@PYOBJECT_PLUS_SOURCE@':body[body.index('// Base needs to be 0.'):body.index('// Section: PyObjectPlusWithWeakReference')],
                '@FLOAT_CONVERSION@':conversion}
            for stem,kind,cls in (('py_meta_particle_system','META','PyMetaParticleSystem'),('py_particle_system','PARTICLE','PyParticleSystem')):
                header=read(stem+'.hpp');cpp=read(stem+'.cpp')
                declaration='PY_RW_ATTRIBUTE_DECLARE( scale_, scale )';registration='PY_ATTRIBUTE( scale )'
                self.assertIn(declaration,header);self.assertIn(registration,cpp)
                values['@'+kind+'_SCALE_DECLARATION@']=declaration
                values['@'+kind+'_SCALE_REGISTRATION@']=registration
                values['@'+kind+'_CONSTRUCTOR@']=extract_method(cpp,cls+'::'+cls+'(')
            for name in ('clone','force','clear'):
                values['@'+name.upper()+'_BINDING@']=extract_method(read('py_meta_particle_system.cpp'),'PyObject * PyMetaParticleSystem::py_'+name+'(')
            binary=compile_cpp(self,root,render(ROOT/'tests/stock_particle_scale_host.cpp',values),python=True)
            result=subprocess.run([str(binary),str(ROOT/'tests/stock_particle_scale_consumer.py'),str(Path(client)/'res/packages/scripts.pkg')],
                capture_output=True,text=True,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0'))
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            self.assertIn('Production SDK property:',result.stdout)
            self.assertIn('BG and cached scale->force->attach->delete',result.stdout)
            print(result.stdout.strip())

    def test_actual_cpu_draw_chain_deferred_buckets_and_sprite_vertices(self):
        source=sources(self)
        with tempfile.TemporaryDirectory(prefix='particle-scale-render-') as temp:
            root=Path(temp);prepare(root,source)
            def read(name):return (root/('lib/particle/'+name)).read_text()
            sprite=read('renderers/sprite_particle_renderer.cpp');item=read('renderers/particle_system_draw_item.cpp')
            item_header=read('renderers/particle_system_draw_item.hpp')
            job=sprite[sprite.index('class SpriteParticleJob\n'):sprite.index('void SpriteParticleJob::execute()')].replace('private:','public:')
            stock=(ROOT/'bigworld/stock_battleground_elements.cpp').read_text()
            storage=stock[stock.index('struct DeferredCloudParticles'):stock.index('\n}\n\nbool drawStockBattlegroundParticles')]
            helper=(ROOT/'bigworld/stock_particle_scale.hpp').read_text()
            values={
                '@SCALE_HELPER@':helper[helper.index('BW_BEGIN_NAMESPACE'):],
                '@SPRITE_JOB@':job,'@SPRITE_EXECUTE@':extract_method(sprite,'void SpriteParticleJob::execute()'),
                '@PARTICLE_DRAW@':extract_method(read('particle_system.cpp'),'void ParticleSystem::draw('),
                '@META_DRAW@':extract_method(read('meta_particle_system.cpp'),'void MetaParticleSystem::draw('),
                '@META_WRAPPER_DRAW@':extract_method(read('py_meta_particle_system.cpp'),'void PyMetaParticleSystem::draw('),
                '@PARTICLE_WRAPPER_DRAW@':extract_method(read('py_particle_system.cpp'),'void PyParticleSystem::draw('),
                '@DRAW_ITEM_HEADER@':item_header[item_header.index('class ParticleSystemDrawItem :'):item_header.index('BW_END_NAMESPACE')],
                '@DRAW_ITEM_SET@':extract_method(item,'void ParticleSystemDrawItem::set('),
                '@DRAW_ITEM_DRAW@':extract_method(item,'void ParticleSystemDrawItem::draw()'),
                '@DRAW_ITEM_SPLIT@':extract_method(item,'void ParticleSystemDrawItem::drawBattlegroundSplit()'),
                '@DEFERRED_STORAGE@':storage,
                '@BATTLEGROUND_DRAW@':extract_method(stock,'bool drawStockBattlegroundParticles('),
                '@BATTLEGROUND_FINISH@':extract_method(stock,'void finishStockBattlegroundParticles()'),
            }
            binary=compile_cpp(self,root,render(ROOT/'tests/stock_particle_scale_render_host.cpp',values))
            result=subprocess.run([str(binary)],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            print(result.stdout.strip())
            # Old sprite behavior still reaches the actual job, but cannot
            # satisfy the non-unit vertex assertion merely by exposing a field.
            negative=dict(values)
            negative['@SPRITE_EXECUTE@']=values['@SPRITE_EXECUTE@'].replace('p.size() * scale_', 'p.size()')
            binary=compile_cpp(self,root,render(ROOT/'tests/stock_particle_scale_render_host.cpp',negative))
            result=subprocess.run([str(binary)],capture_output=True,text=True)
            self.assertNotEqual(result.returncode,0,'Old unscaled sprite job unexpectedly passed')

    def test_exact_original_wrapper_and_sprite_instruction_contract(self):
        client=os.environ.get('WOT_0922_CLIENT')
        if not client:self.skipTest('Exact #1513 client required')
        try:from stock_particle_scale_oracle import audit
        except ImportError:self.skipTest('pefile and Unicorn required')
        count=audit(Path(client)/'WorldOfTanks.exe')
        self.assertGreater(count,20)
        print('Original scale wrapper/transform/deferred/sprite instruction cases:',count)


if __name__=='__main__':unittest.main()
