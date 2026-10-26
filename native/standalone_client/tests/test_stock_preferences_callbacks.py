"""Exercise production preference events, save ordering and original consumers."""
import importlib.util
import os
from pathlib import Path
import subprocess
import sys
import tarfile
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]


def load_patch():
    spec=importlib.util.spec_from_file_location('preferences_callbacks_patch',ROOT/'bigworld/patch_stock_preferences_callbacks.py')
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module);return module


def fresh(root):
    with tarfile.open(os.environ.get('WOT_PUBLIC_BIGWORLD_ARCHIVE','/tmp/wot-bigworld-engine-14.4.1.tar.gz')) as archive:
        prefix=archive.getmembers()[0].name.split('/')[0]+'/programming/bigworld/'
        path=root/'client/device_app.cpp';path.parent.mkdir(parents=True)
        path.write_bytes(archive.extractfile(prefix+'client/device_app.cpp').read())
        (root/'client/script_bigworld.cpp').write_bytes(archive.extractfile(prefix+'client/script_bigworld.cpp').read())


def function(source,marker):
    start=source.index(marker);end=source.index('{',start);depth=1
    while depth:
        end+=1;depth+=(source[end]=='{')-(source[end]=='}')
    return source[start:end+1]


class PreferencesCallbacksTests(unittest.TestCase):
    def test_patch_idempotent_atomic_and_real_save_boundary(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);fresh(root);(root/'lib').mkdir();patch=load_patch();patch.patch(root)
            source=(root/'client/device_app.cpp').read_text()
            save=function(source,'bool DeviceApp::savePreferences()')
            self.assertLess(save.index('scrptPref->copy'),save.index('stockNotifySavePreferences(root)'))
            self.assertLess(save.index('stockNotifySavePreferences(root)'),save.index('dataRes.save()'))
            self.assertLess(source.index('stockRecordPreferencesLoaded(preferences)'),source.index('preferences->openSection("appPreferences")'))
            before={p.relative_to(root):p.read_bytes()for p in root.rglob('*')if p.is_file()};patch.patch(root)
            self.assertEqual(before,{p.relative_to(root):p.read_bytes()for p in root.rglob('*')if p.is_file()})
            (root/'client/device_app.cpp').write_text(source.replace('// save it','// changed'))
            before={p.relative_to(root):p.read_bytes()for p in root.rglob('*')if p.is_file()}
            # Remove the complete already-patched seam to simulate source drift.
            (root/'client/device_app.cpp').write_text('unsupported source')
            before={p.relative_to(root):p.read_bytes()for p in root.rglob('*')if p.is_file()}
            with self.assertRaises(ValueError):patch.patch(root)
            self.assertEqual(before,{p.relative_to(root):p.read_bytes()for p in root.rglob('*')if p.is_file()})

    def test_actual_owner_save_producer_and_original_python2_consumers(self):
        python=Path(os.environ.get('WOT_PY27',str(Path.home()/'.pyenv/versions/2.7.18/bin/python2.7')))
        if sys.platform!='darwin' or not python.exists():self.skipTest('Pinned Python 2.7 extension linker unavailable')
        with tempfile.TemporaryDirectory(prefix='preferences-callbacks-') as folder:
            root=Path(folder);fresh(root);(root/'lib').mkdir();load_patch().patch(root)
            for name in ['pch.hpp','pyscript/script.hpp','pyscript/py_data_section.hpp','resmgr/datasection.hpp']:
                p=root/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_text('#include "fixture.hpp"\n')
            (root/'fixture.hpp').write_bytes((ROOT/'tests/stock_preferences_callbacks_host.hpp').read_bytes())
            (root/'stock_preferences_callbacks.hpp').write_bytes((ROOT/'bigworld/stock_preferences_callbacks.hpp').read_bytes())
            source=(ROOT/'bigworld/stock_preferences_callbacks.cpp').read_text()
            public=(root/'client/device_app.cpp').read_text()
            source+='\nnamespace BW {\n'+function(public,'bool DeviceApp::savePreferences()')+'\n'
            source+=function((root/'client/script_bigworld.cpp').read_text(),'static bool savePreferences()')+'\n}\n'
            source+=(ROOT/'tests/stock_preferences_callbacks_host.cpp').read_text()
            (root/'host.cpp').write_text(source)
            command=['c++','-std=c++14','-Wall','-Wextra','-Werror','-Wno-deprecated-register','-Wno-missing-field-initializers','-fPIC','-shared','-undefined','dynamic_lookup','-I'+str(python.parents[1]/'include/python2.7'),'-I'+str(root),str(root/'host.cpp'),'-o',str(root/'preferences_owner.so')]
            result=subprocess.run(command,capture_output=True,text=True);self.assertEqual(result.returncode,0,result.stderr)
            script=(ROOT/'tests/stock_preferences_callbacks_consumer.py').read_text()
            result=subprocess.run([str(python),'-c',script],cwd=root,capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)


if __name__=='__main__':unittest.main()
