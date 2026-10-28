"""Production particle background weak callbacks, failures and exact PYC."""
from pathlib import Path
import itertools
import os
import subprocess
import sys
import tarfile
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tests'))
sys.path.insert(0,str(ROOT/'bigworld'))
import patch_stock_particle_background as patcher
import patch_stock_particle_clone as clone
import patch_stock_tps as tps
import test_stock_particle_clone as shared
from test_stock_tps_owners import extract_method


def sources(test):
    result=shared.sources(test)
    archive=Path(os.environ.get('WOT_PUBLIC_ENGINE_ARCHIVE','/tmp/wot-bigworld-engine-14.4.1.tar.gz'))
    with tarfile.open(archive) as tar:
        for member in tar:
            for name in ('lib/pyscript/script.cpp','lib/pyscript/script.ipp'):
                if member.name.endswith('/programming/bigworld/'+name):
                    result[name]=tar.extractfile(member).read().decode('utf-8-sig')
    test.assertIn('lib/pyscript/script.cpp',result);test.assertIn('lib/pyscript/script.ipp',result)
    return result


def support(directory,source):
    cpp=(directory/'lib/particle/py_meta_particle_system.cpp').read_text()
    header=source['lib/particle/py_meta_particle_system.hpp']
    # PyObjectPtr is precisely the SDK's SmartPointer<PyObject> alias. Adapt
    # only this host's pointer spelling; real CPython owns the weak-reference.
    declaration=extract_method(header,'class MPSBackgroundTask : public BackgroundTask').replace(
        'SmartPointer<PyObject>','PyObjectPtr')+';'
    body=(ROOT/'tests/stock_particle_background_host.inc').read_text()
    replacements={
        '@SCRIPT_ASK@':extract_method(source['lib/pyscript/script.cpp'],'PyObject * Script::ask('),
        '@SCRIPT_CALL@':extract_method(source['lib/pyscript/script.ipp'],'INLINE bool call(').replace('INLINE ','inline ',1),
        '@BACKGROUND_CLASS@':declaration,
        '@BACKGROUND_CONSTRUCTOR@':extract_method(cpp,'MPSBackgroundTask::MPSBackgroundTask('),
        '@BACKGROUND_LOAD@':extract_method(cpp,'void MPSBackgroundTask::doBackgroundTask('),
        '@BACKGROUND_COMPLETE@':extract_method(cpp,'void MPSBackgroundTask::doMainThreadTask('),
        '@BACKGROUND_FACTORY@':extract_method(cpp,'PyObject* PyMetaParticleSystem::createBG('),
    }
    for name,text in replacements.items():
        assert body.count(name)==1,name;body=body.replace(name,text)
    return {
        '@BACKGROUND_LOAD_DECLARATION@':'bool load(const BW::string&,const char*,StringBuilder*);',
        '@BACKGROUND_FACTORY_DECLARATION@':'static PyObject* createBG(const BW::string&,PyObjectPtr);',
        '@BACKGROUND_SUPPORT@':body,
        '@BACKGROUND_METHODS@':'''{"submit",submitBackground,METH_VARARGS,NULL},
        {"background",loadBackground,METH_NOARGS,NULL},{"complete",completeBackground,METH_NOARGS,NULL},
        {"cancel",cancelBackground,METH_NOARGS,NULL},{"backgroundStatus",backgroundStatus,METH_NOARGS,NULL},''',
    }


class StockParticleBackgroundTest(unittest.TestCase):
    def test_original_constructor_completion_and_destructor(self):
        client=os.environ.get('WOT_0922_CLIENT')
        if not client:self.skipTest('Exact #1513 client required')
        try:from stock_particle_background_oracle import audit
        except ImportError:self.skipTest('pefile and Unicorn required')
        self.assertEqual(audit(Path(client)/'WorldOfTanks.exe'),16)

    def test_production_task_sdk_script_call_and_original_pyc(self):
        client=os.environ.get('WOT_0922_CLIENT')
        if not client:self.skipTest('Exact #1513 client required')
        with tempfile.TemporaryDirectory(prefix='particle-background-cpython-') as name:
            binary=shared.host(self,Path(name),sources(self),
                {'patch':patcher.patch,'substitutions':support})
            result=subprocess.run([str(binary),str(ROOT/'tests/stock_particle_background_consumer.py'),
                str(Path(client)/'res/packages/scripts.pkg')],capture_output=True,text=True,
                env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0'))
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        self.assertIn('Production particle background:',result.stdout)
        self.assertIn('Original PixieBG PYC:',result.stdout)
        self.assertNotIn("NoneType' object is not callable",result.stderr)
        print(result.stdout.strip())

    def test_patch_composes_with_clone_and_tps_and_is_atomic(self):
        source=sources(self);expected=None
        for order in itertools.permutations((patcher.patch,clone.patch,tps.patch)):
            with tempfile.TemporaryDirectory(prefix='particle-background-patches-') as name:
                directory=Path(name);shared.stage(directory,source)
                for operation in order:operation(directory)
                first={n:(directory/n).read_text()for n in source}
                for operation in order:operation(directory)
                self.assertEqual(first,{n:(directory/n).read_text()for n in source})
                if expected is None:expected=first
                else:self.assertEqual(expected,first)
                path=directory/'lib/particle/py_meta_particle_system.cpp'
                path.write_text(source['lib/particle/py_meta_particle_system.cpp'].replace(
                    '\tpSystem_->load( filename_, "", &errorMsg );',''))
                before=path.read_text()
                with self.assertRaises(ValueError):patcher.patch(directory)
                self.assertEqual(path.read_text(),before)


if __name__=='__main__':unittest.main()
