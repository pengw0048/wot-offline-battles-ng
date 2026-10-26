"""Original API boundary and production owner methods; fixtures are not a live client."""
from pathlib import Path
import importlib.util
import itertools
import os
import shlex
import subprocess
import tarfile
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
BOUNDARY = r'''
import BigWorld as B
for args,expected in (((),0),((False,),0),((True,),1),((0,),0),((17,),1),((-4,),1),
                      (('false',),0),(('FALSE',),0),(('true',),1),(('TrUe',),1),
                      (('true\0ignored',),1)):
    B._prepare()
    assert B.clearEntitiesAndSpaces(*args) is None
    assert B._state()==expected,(args,B._state())
for args in ((1,2),(True,False,0),(None,),(1L,),(0.0,),(u'true',),([],),('yes',),('',)):
    B._prepare()
    try:B.clearEntitiesAndSpaces(*args)
    except TypeError:pass
    else:raise AssertionError(args)
    assert B._state()==2
try:B.clearEntitiesAndSpaces(keepClientOnlySpaces=True)
except TypeError:pass
else:raise AssertionError('Keywords must not be accepted')
print('Actual CPython '+__import__('sys').version.split()[0]+': optional bool / 0..1 args / None / conversion failure before mutation PASS')
'''


def body(source, signature):
    start = source.index(signature)
    end = source.index('{', start)
    depth = 1
    while depth:
        end += 1
        depth += (source[end] == '{') - (source[end] == '}')
    return source[start:end + 1]


def load_patch(name):
    spec = importlib.util.spec_from_file_location(name, ROOT / 'bigworld' / (name + '.py'))
    module = importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
    return module.patch


class SpaceLifecycleTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.archive = Path(os.environ.get('WOT_PUBLIC_ENGINE_ARCHIVE', '/tmp/wot-bigworld-engine-14.4.1.tar.gz'))
        python = Path(os.environ.get('WOT_TEST_PY27', '/Users/peng/.pyenv/versions/2.7.18/bin/python2.7'))
        config = python.with_name('python2.7-config')
        if not config.exists() or not cls.archive.exists():
            raise unittest.SkipTest('CPython 2.7 development runtime and pristine engine archive required')
        cls.temporary = tempfile.TemporaryDirectory(prefix='stock-space-lifecycle-')
        cls.directory = Path(cls.temporary.name)
        with tarfile.open(cls.archive) as archive:
            cls.pristine = {}
            for name in ('client/script_bigworld.cpp', 'client/connection_control.hpp', 'client/connection_control.cpp',
                         'lib/space/space_manager.hpp', 'lib/pyscript/script.cpp', 'client/script_app.cpp', 'lib/space/client_space.cpp', 'lib/compiled_space/particle_system_loader.hpp', 'lib/compiled_space/particle_system_loader.cpp'):
                member = next(m for m in archive.getmembers() if m.name.endswith('/programming/bigworld/' + name))
                cls.pristine[name] = archive.extractfile(member).read()
        cls.engine = cls.directory / 'engine'
        cls.populate(cls.engine)
        cls.patch = staticmethod(load_patch('patch_stock_space_lifecycle'))
        cls.patch(cls.engine)
        script = cls.pristine['lib/pyscript/script.cpp'].decode('utf-8-sig')
        control = (cls.engine / 'client/connection_control.cpp').read_text()
        host = (ROOT / 'tests/stock_space_lifecycle_host.cpp').read_text()
        host = host.replace('@CONVERTER@', body(script, 'int Script::setData( PyObject * pObject, bool & rBool'))
        host = host.replace('@CLEAR_SPACES@', body(control, 'void ConnectionControl::clearSpaces()'))
        host = host.replace('@TICK@', body(control, 'void ConnectionControl::tick( float dt )'))
        original_control = cls.pristine['client/connection_control.cpp'].decode('utf-8-sig')
        host = host.replace('@LEGACY_CLEAR@', body(original_control, 'void ConnectionControl::clearSpaces()').replace('ConnectionControl::clearSpaces()', 'ConnectionControl::legacyClearSpaces()'))
        host = host.replace('@LEGACY_DESTROY@', body(control, 'void ConnectionControl::destroyMainConnection()'))
        host = host.replace('@PRODUCTION_FILE@', str(ROOT / 'bigworld/stock_space_lifecycle.cpp'))
        support=(ROOT/'tests/stock_space_sounds_host.hpp').read_text().replace(
            '#include WOT_SOUND_PRODUCTION_FILE', '#include "'+str(ROOT/'bigworld/stock_space_sounds.cpp')+'"')
        host=host.replace('@SOUND_TEST_SUPPORT@',support)
        (cls.directory / 'host.cpp').write_text(host)
        cls.probe = cls.directory / 'probe'
        flags = [flag for flag in shlex.split(subprocess.check_output([str(config), '--cflags'], text=True)) if flag != '-DNDEBUG']
        libs = shlex.split(subprocess.check_output([str(config), '--ldflags'], text=True))
        subprocess.run(['c++', '-std=c++14', '-Wno-deprecated-register', '-UNDEBUG', str(cls.directory / 'host.cpp'),
                        *flags, *libs, '-o', str(cls.probe)], check=True)
        destination = os.environ.get('WOT_SPACE_LIFECYCLE_FIXTURE_OUT')
        if destination:
            destination = Path(destination); destination.mkdir(parents=True, exist_ok=True)
            (destination / 'host.cpp').write_text(host.replace(str(ROOT / 'bigworld/stock_space_lifecycle.cpp'), 'stock_space_lifecycle.cpp').replace(str(ROOT/'bigworld/stock_space_sounds.cpp'),'stock_space_sounds.cpp'))
            (destination/'stock_space_sounds.cpp').write_bytes((ROOT/'bigworld/stock_space_sounds.cpp').read_bytes())
            (destination/'stock_space_sounds_data.hpp').write_bytes((ROOT/'bigworld/stock_space_sounds_data.hpp').read_bytes())
            (destination / 'stock_space_lifecycle.cpp').write_bytes((ROOT / 'bigworld/stock_space_lifecycle.cpp').read_bytes())
            (destination / 'case.py').write_text(BOUNDARY)

    @classmethod
    def populate(cls, directory):
        for name, data in cls.pristine.items():
            path = directory / name; path.parent.mkdir(parents=True, exist_ok=True); path.write_bytes(data)

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def execute(self, code):
        script = self.directory / 'case.py'; script.write_text(code)
        subprocess.run([str(self.probe), str(script)], check=True)

    def test_pristine_main_clear_reproduces_reentrant_replacement_destruction(self):
        subprocess.run([str(self.probe), '--legacy'], check=True)

    def test_real_python_boundary_and_production_owner_lifecycle(self):
        self.execute(BOUNDARY)

    def test_original_lobby_producer_selects_keep_from_real_hangar_guard(self):
        client = os.environ.get('WOT_0922_CLIENT')
        if not client: self.skipTest('WOT_0922_CLIENT required for original states.pyc')
        self.execute(r'''
import BigWorld as B,zipfile,marshal,types,sys
archive=zipfile.ZipFile(%r)
module=marshal.loads(archive.read('scripts/client/gui/app_loader/states.pyc')[8:])
def walk(code):
    if code.co_name=='_clearEntitiesAndSpaces':return code
    for child in code.co_consts:
        if isinstance(child,types.CodeType):
            found=walk(child)
            if found:return found
code=walk(module);assert code is not None
for name in ('gui','gui.shared','gui.shared.utils'):
    package=types.ModuleType(name);package.__path__=[];sys.modules[name]=package
hangar=types.ModuleType('gui.shared.utils.HangarSpace')
sys.modules['gui.shared.utils.HangarSpace']=hangar
run=types.FunctionType(code,{'BigWorld':B,'__builtins__':__builtins__})
class Hangar(object):
    def __init__(self,inited,loading):self.inited=inited;self.loading=loading;self.calls=0
    def spaceLoading(self):self.calls+=1;return self.loading
for value,expected in ((None,0),(Hangar(False,True),0),(Hangar(True,False),0),(Hangar(True,True),1)):
    hangar.g_hangarSpace=value;B._prepare();assert run() is None;assert B._state()==expected
    if value is not None:assert value.calls==int(value.inited)
print('Original LoginState _clearEntitiesAndSpaces PYC + actual CPython/native wrapper PASS')
''' % str(Path(client) / 'res/packages/scripts.pkg'))

    def test_actual_particle_sound_bind_and_unload_reentry(self):
        engine=self.directory/'particle-owner';self.populate(engine)
        load_patch('patch_stock_space_sounds')(engine)
        signatures=('bool ParticleSystemLoader::doBind()', 'void ParticleSystemLoader::doUnload()',
                    'void ParticleSystemLoader::addToScene()', 'void ParticleSystemLoader::removeFromScene()',
                    'void ParticleSystemLoader::freeParticleSystems()')
        source=(engine/'lib/compiled_space/particle_system_loader.cpp').read_text()
        old=self.pristine['lib/compiled_space/particle_system_loader.cpp'].decode('utf-8-sig')
        support=(ROOT/'tests/stock_space_sounds_host.hpp').read_text().replace(
            '#include WOT_SOUND_PRODUCTION_FILE','#include "'+str(ROOT/'bigworld/stock_space_sounds.cpp')+'"')
        for name,implementation in (('original',old),('patched',source)):
            host=(ROOT/'tests/stock_space_sound_particles_host.cpp').read_text()
            host=host.replace('@SOUND_SUPPORT@',support).replace('@METHODS@','\n'.join(body(implementation,v)for v in signatures))
            cpp=self.directory/(name+'-particle.cpp');cpp.write_text(host)
            binary=self.directory/(name+'-particle')
            flags=['-DLEGACY_PARTICLE_LIFECYCLE'] if name=='original' else ['-fsanitize=address,undefined']
            subprocess.run(['c++','-std=c++14','-O1','-g','-Wall','-Wextra','-Werror','-UNDEBUG',*flags,str(cpp),'-o',str(binary)],check=True)
            subprocess.run([str(binary)],check=True)

    def test_patch_composes_with_replay_and_typed_space_in_all_orders(self):
        patches = [load_patch(name) for name in ('patch_stock_space_lifecycle',
                   'patch_stock_replay_controller', 'patch_stock_space_data', 'patch_stock_space_sounds')]
        for number, order in enumerate(itertools.permutations(patches)):
            directory = self.directory / ('order-%d' % number); self.populate(directory)
            for patch in order: patch(directory)
            before = {p.relative_to(directory):p.read_bytes() for p in directory.rglob('*') if p.is_file()}
            for patch in reversed(order): patch(directory)
            self.assertEqual(before, {p.relative_to(directory):p.read_bytes() for p in directory.rglob('*') if p.is_file()})
            script = (directory / 'client/script_bigworld.cpp').read_text()
            self.assertEqual(script.count('#include "stock_space_lifecycle.hpp"'), 1)
            self.assertEqual(script.count('StockSpaceLifecycle_token |'), 1)
            space=(directory/'lib/space/client_space.cpp').read_text()
            self.assertEqual(script.count('#include "stock_space_sounds.hpp"'),1)
            self.assertEqual(space.count('#include "stock_space_sounds.hpp"'),1)
            for signature in ('ClientSpace::~ClientSpace()', 'void ClientSpace::clear()'):
                value=body(space,signature)
                self.assertEqual(value.count('retireStockSpaceSounds( this );'),1)
                self.assertLess(value.index('retireStockSpaceSounds( this );'),value.index('retireStockSpaceData( id_, this );'))


    def test_sound_patch_failure_does_not_publish_other_mutations(self):
        directory=self.directory/'bad-sound';self.populate(directory)
        path=directory/'lib/compiled_space/particle_system_loader.cpp'
        path.write_text(path.read_text().replace('\t\tsceneObj.handle( &instance );','changed_instance_seam();'))
        before={p.relative_to(directory):p.read_bytes()for p in directory.rglob('*')if p.is_file()}
        with self.assertRaises(ValueError):load_patch('patch_stock_space_sounds')(directory)
        self.assertEqual(before,{p.relative_to(directory):p.read_bytes()for p in directory.rglob('*')if p.is_file()})

    def test_pristine_patch_idempotence_and_failed_transaction(self):
        paths = ['client/script_bigworld.cpp', 'client/connection_control.hpp', 'client/connection_control.cpp',
                 'lib/space/space_manager.hpp', 'lib/stock_space_lifecycle.hpp']
        before = {p:(self.engine / p).read_bytes() for p in paths}
        self.patch(self.engine)
        self.assertEqual(before, {p:(self.engine / p).read_bytes() for p in paths})
        bad = self.directory / 'bad'; self.populate(bad)
        path = bad / 'client/connection_control.cpp'
        path.write_text(path.read_text().replace('iter->second->updateServer();', 'changed_tick_seam();'))
        before = {p:(bad / p).read_bytes() for p in paths[:-1]}
        with self.assertRaises(ValueError): self.patch(bad)
        self.assertEqual(before, {p:(bad / p).read_bytes() for p in paths[:-1]})
        self.assertFalse((bad / paths[-1]).exists())


if __name__ == '__main__':unittest.main()
