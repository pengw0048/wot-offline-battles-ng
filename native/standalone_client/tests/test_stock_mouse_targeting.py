"""Exact MouseTargetingMatrix detach, cached cursor law and stock GUI lifecycle."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'bigworld'));sys.path.insert(0,str(ROOT/'tests'))
import patch_stock_mouse_targeting as patcher
import patch_stock_entity_picker as picker
import patch_stock_hangar_presentation as hangar
from test_stock_particle_scale import compile_cpp,render
from test_stock_tps_owners import extract_method
from test_stock_math_oscillators import between


def inputs(test):
    import tarfile
    archive=Path(os.environ.get('WOT_PUBLIC_ENGINE_ARCHIVE','/tmp/wot-bigworld-engine-14.4.1.tar.gz'))
    if not archive.is_file():
        if 'WOT_PUBLIC_ENGINE_ARCHIVE' in os.environ:raise AssertionError('Invalid WOT_PUBLIC_ENGINE_ARCHIVE')
        test.skipTest('Pinned public SDK archive required')
    names={'client/entity_picker.cpp','client/entity_picker.hpp','lib/pyscript/script.hpp','lib/pyscript/script.cpp',
           'lib/pyscript/pyobject_plus.hpp','lib/pyscript/pyobject_plus.cpp'}
    names.update(hangar.FILES)
    output={}
    with tarfile.open(archive) as tar:
        for member in tar:
            if not member.isfile() or '/programming/bigworld/' not in member.name:continue
            name=member.name.split('/programming/bigworld/',1)[1]
            if name in names:output[name]=tar.extractfile(member).read().decode('utf-8-sig')
    test.assertEqual(set(output),names)
    return output


def stage(root,data):
    for name,text in data.items():
        path=root/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_text(text)


class MouseTargetingTests(unittest.TestCase):
    def test_patch_composes_with_picker_and_real_focus_owner(self):
        data=inputs(self)
        with tempfile.TemporaryDirectory(prefix='mouse-targeting-compose-') as temp:
            root=Path(temp);stage(root,data);picker.patch(root);hangar.patch(root);patcher.patch(root)
            first={p:p.read_bytes() for p in root.rglob('*') if p.is_file()}
            patcher.patch(root);picker.patch(root)
            self.assertEqual(first,{p:p.read_bytes() for p in root.rglob('*') if p.is_file()})
            self.assertIn('bool MouseCursor::s_inFocus_ = false;', (root/'lib/ashes/mouse_cursor.cpp').read_text())
            self.assertIn('s_inFocus_ = focus;', (root/'lib/ashes/mouse_cursor.cpp').read_text())
            self.assertIn(picker.REPLACEMENT,(root/'client/entity_picker.cpp').read_text())
            # Atomic validation: a broken late body does not publish the header.
            stage(root,data)
            cpp=root/'client/entity_picker.cpp';cpp.write_text(cpp.read_text().replace(patcher.POSITION_BEFORE,''))
            first={p:p.read_bytes() for p in root.rglob('*') if p.is_file()}
            with self.assertRaises(ValueError):patcher.patch(root)
            self.assertEqual(first,{p:p.read_bytes() for p in root.rglob('*') if p.is_file()})

    def test_real_sdk_binding_matrix_state_and_original_pyc(self):
        client=os.environ.get('WOT_0922_CLIENT')
        if not client:self.skipTest('Exact #1513 client required')
        self.assertTrue((Path(client)/'res/packages/scripts.pkg').is_file(),'Invalid WOT_0922_CLIENT')
        data=inputs(self)
        with tempfile.TemporaryDirectory(prefix='mouse-targeting-python-') as temp:
            root=Path(temp);stage(root,data);picker.patch(root);hangar.patch(root);patcher.patch(root)
            cpp=(root/'client/entity_picker.cpp').read_text();hpp=(root/'client/entity_picker.hpp').read_text()
            plus=data['lib/pyscript/pyobject_plus.hpp'];body=data['lib/pyscript/pyobject_plus.cpp'];script=data['lib/pyscript/script.hpp']
            script_bodies=data['lib/pyscript/script.cpp']
            support=(ROOT/'tests/stock_particle_clone_host.cpp').read_text().split('@PYOBJECT_PLUS_HEADER@')[0]
            values={
                '@PYOBJECT_SUPPORT@':support,
                '@SCRIPT_MACROS@':between(script,'// Section: Macros: Module Links','// Section: Macros: Enums',False)+between(script,'// Section: Macros: Recursion utilities','// Section: PyModuleLink',False),
                '@PYOBJECT_PLUS_HEADER@':plus[plus.index('/// This must be the first line'):plus.index('// Section: Weak-referenceable macros')],
                '@PYOBJECT_PLUS_SOURCE@':body[body.index('// Base needs to be 0.'):body.index('// Section: PyObjectPlusWithWeakReference')],
                '@SCRIPT_BODIES@':extract_method(script_bodies,'int Script::setData( PyObject * pObject, bool & rBool,')+'\n'+extract_method(script_bodies,'PyObject * Script::argCountError('),
                '@TARGET_CLASS@':hpp[hpp.index('class MouseTargetingMatrix :'):hpp.index('\n\n#ifdef CODE_INLINE',hpp.index('class MouseTargetingMatrix :'))],
                '@TARGET_CONSTRUCTOR@':extract_method(cpp,'MouseTargetingMatrix::MouseTargetingMatrix('),
                '@TARGET_MATRIX@':extract_method(cpp,'void MouseTargetingMatrix::matrix('),
                '@TARGET_DETACH@':extract_method(cpp,'void MouseTargetingMatrix::detach('),
                '@TARGET_METHODS@':cpp[cpp.index('PY_BEGIN_METHODS( MouseTargetingMatrix )'):cpp.index('PY_BEGIN_ATTRIBUTES( MouseTargetingMatrix )')],
            }
            binary=compile_cpp(self,root,render(ROOT/'tests/stock_mouse_targeting_host.cpp',values),python=True)
            result=subprocess.run([str(binary),str(ROOT/'tests/stock_mouse_targeting_consumer.py'),str(Path(client)/'res/packages/scripts.pkg')],capture_output=True,text=True,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0'))
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            self.assertIn('Production targeting owner:',result.stdout);self.assertIn('Original #1513 PYC:',result.stdout)
            print(result.stdout.strip())
            # A binding that merely stores detached_ while retaining the old
            # always-live cursor consumer must fail the same native/PYC host.
            negative=dict(values)
            negative['@TARGET_MATRIX@']=extract_method(data['client/entity_picker.cpp'],'void MouseTargetingMatrix::matrix(')
            binary=compile_cpp(self,root,render(ROOT/'tests/stock_mouse_targeting_host.cpp',negative),python=True)
            result=subprocess.run([str(binary),str(ROOT/'tests/stock_mouse_targeting_consumer.py'),str(Path(client)/'res/packages/scripts.pkg')],capture_output=True,text=True,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0'))
            self.assertNotEqual(result.returncode,0,'Old cursor consumer unexpectedly honored detach')
            self.assertIn('AssertionError',result.stderr)

    def test_original_native_detach_and_cursor_sampling_instructions(self):
        client=os.environ.get('WOT_0922_CLIENT')
        if not client:self.skipTest('Exact #1513 client required')
        try:from stock_mouse_targeting_oracle import audit
        except ImportError:self.skipTest('pefile and Unicorn required')
        count=audit(Path(client)/'WorldOfTanks.exe')
        self.assertGreater(count,20)
        print('Original MouseTargetingMatrix detach and sampling cases:',count)


if __name__=='__main__':unittest.main()
