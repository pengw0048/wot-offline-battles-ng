"""Exact PyMetaParticleSystem clone binding, native deep-copy owner and PYC."""
from pathlib import Path
import itertools
import os
import shlex
import subprocess
import sys
import tarfile
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tests'))
sys.path.insert(0,str(ROOT/'bigworld'))
import patch_stock_particle_clone as patcher
import patch_stock_tps as tps
from test_stock_tps_owners import extract_method

FILES=('lib/particle/py_meta_particle_system.cpp','lib/particle/py_meta_particle_system.hpp',
       'lib/pyscript/pyobject_plus.cpp','lib/pyscript/pyobject_plus.hpp',
       'lib/particle/meta_particle_system.cpp','lib/particle/meta_particle_system.hpp',
       'lib/particle/particle_system.cpp','lib/particle/actions/particle_system_action.hpp',
       'lib/particle/actions/particle_system_action.cpp','client/world_app.cpp',
       'client/facade_app.cpp','client/script_app.cpp')


def sources(test):
    archive=Path(os.environ.get('WOT_PUBLIC_ENGINE_ARCHIVE','/tmp/wot-bigworld-engine-14.4.1.tar.gz'))
    if not archive.is_file():test.skipTest('Pinned public SDK archive required')
    result={}
    with tarfile.open(archive) as tar:
        for member in tar:
            for name in FILES:
                if member.name.endswith('/programming/bigworld/'+name):
                    result[name]=tar.extractfile(member).read().decode('utf-8-sig')
    test.assertEqual(set(result),set(FILES))
    return result


def stage(directory,source):
    for name,text in source.items():
        path=directory/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_text(text)


def host(test,directory,source,background=None):
    stage(directory,source);tps.patch(directory);patcher.patch(directory)
    if background:background['patch'](directory)
    plus=source['lib/pyscript/pyobject_plus.hpp'];body=source['lib/pyscript/pyobject_plus.cpp']
    particle=source['lib/particle/particle_system.cpp']
    meta=(directory/'lib/particle/meta_particle_system.cpp').read_text()
    binding=(directory/'lib/particle/py_meta_particle_system.cpp').read_text()
    header=(directory/'lib/particle/py_meta_particle_system.hpp').read_text()
    declaration='PY_METHOD_DECLARE( py_clone )';registration='PY_METHOD( clone )'
    test.assertIn(declaration,header);test.assertIn(registration,binding)
    substitutions={
        '@PYOBJECT_PLUS_HEADER@':plus[plus.index('/// This must be the first line'):plus.index('// Section: Weak-referenceable macros')],
        '@PYOBJECT_PLUS_SOURCE@':body[body.index('// Base needs to be 0.'):body.index('// Section: PyObjectPlusWithWeakReference')],
        '@PARTICLE_CLONE@':extract_method(particle,'ParticleSystem * ParticleSystem::clone() const'),
        '@META_CLONE@':extract_method(meta,'MetaParticleSystemPtr MetaParticleSystem::clone() const'),
        '@CLONE_DECLARATION@':declaration,
        '@CLONE_REGISTRATION@':registration,
        '@CLONE_BINDING@':extract_method(binding,'PyObject * PyMetaParticleSystem::py_clone('),
        '@FORCE_BINDING@':extract_method(binding,'PyObject * PyMetaParticleSystem::py_force('),
        '@CLEAR_BINDING@':extract_method(binding,'PyObject * PyMetaParticleSystem::py_clear('),
        '@BACKGROUND_LOAD_DECLARATION@':'',
        '@BACKGROUND_FACTORY_DECLARATION@':'',
        '@BACKGROUND_SUPPORT@':'',
        '@BACKGROUND_METHODS@':'',
    }
    if background:substitutions.update(background['substitutions'](directory,source))
    text=(ROOT/'tests/stock_particle_clone_host.cpp').read_text()
    for token,value in substitutions.items():
        test.assertEqual(text.count(token),1,token);text=text.replace(token,value)
    path=directory/'host.cpp';path.write_text(text)
    python=Path(os.environ.get('WOT_PYTHON27','/Users/peng/.pyenv/versions/2.7.18/bin/python2.7'))
    config=python.with_name('python2.7-config')
    if not config.exists():test.skipTest('CPython 2.7 development files required')
    flags=[f for f in shlex.split(subprocess.check_output([str(config),'--cflags'],text=True)) if f!='-DNDEBUG']
    libs=shlex.split(subprocess.check_output([str(config),'--ldflags'],text=True))
    binary=directory/'host'
    subprocess.run(['c++','-std=c++14','-UNDEBUG',*flags,'-Wno-deprecated-register','-Wno-writable-strings',
                    '-fsanitize=address,undefined',str(path),*libs,'-o',str(binary)],check=True)
    return binary


class StockParticleCloneTest(unittest.TestCase):
    def test_production_binding_native_deep_copy_and_original_pyc(self):
        client=os.environ.get('WOT_0922_CLIENT')
        if not client:self.skipTest('Exact #1513 client required')
        with tempfile.TemporaryDirectory(prefix='particle-clone-cpython-') as name:
            binary=host(self,Path(name),sources(self))
            result=subprocess.run([str(binary),str(ROOT/'tests/stock_particle_clone_consumer.py'),
                                   str(Path(client)/'res/packages/scripts.pkg')],capture_output=True,text=True,
                                  env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0'))
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        self.assertIn('Production particle clone:',result.stdout)
        self.assertIn('Original EffectsList/PixieBG PYC:',result.stdout)
        print(result.stdout.strip())

    def test_exact_original_wrapper_guards_and_ignored_arguments(self):
        client=os.environ.get('WOT_0922_CLIENT')
        if not client:self.skipTest('Exact #1513 client required')
        try:from stock_particle_clone_oracle import audit
        except ImportError:self.skipTest('pefile and Unicorn required')
        count=audit(Path(client)/'WorldOfTanks.exe')
        self.assertEqual(count,32)
        print('Original PyMetaParticleSystem.clone wrapper: %d guard/argument cases passed'%count)

    def test_patch_composes_with_tps_guard_and_is_idempotent(self):
        source=sources(self);expected=None
        for order in itertools.permutations((patcher.patch,tps.patch)):
            with tempfile.TemporaryDirectory(prefix='particle-clone-patches-') as name:
                directory=Path(name);stage(directory,source)
                for operation in order:operation(directory)
                first={n:(directory/n).read_text()for n in source}
                for operation in order:operation(directory)
                self.assertEqual(first,{n:(directory/n).read_text()for n in source})
                if expected is None:expected=first
                else:self.assertEqual(expected,first)
                self.assertEqual(first['lib/particle/py_meta_particle_system.cpp'].count('PY_METHOD( clone )'),1)
                self.assertIn('if (stockTPSGroupDisabled( inverseRenderGroups_ )) return NULL;',
                              first['lib/particle/meta_particle_system.cpp'])
                header=directory/'lib/particle/py_meta_particle_system.hpp'
                header.write_text(source['lib/particle/py_meta_particle_system.hpp'])
                cpp=directory/'lib/particle/py_meta_particle_system.cpp'
                cpp.write_text(source['lib/particle/py_meta_particle_system.cpp'].replace('PY_METHOD( clear )',''))
                before=header.read_text()
                with self.assertRaises(ValueError):patcher.patch(directory)
                self.assertEqual(header.read_text(),before)


if __name__=='__main__':unittest.main()
