"""Exercise actual resource owner functions and original Python consumers."""
import importlib.util
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import tempfile
import unittest
import json
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def load_patch():
    spec = importlib.util.spec_from_file_location('scaleform_resources', ROOT / 'bigworld/patch_stock_scaleform_resources.py')
    patch = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(patch)
    return patch


def prepare(folder):
    root = Path(folder)
    archive = Path(os.environ.get('WOT_PUBLIC_BIGWORLD_ARCHIVE', '/tmp/wot-bigworld-engine-14.4.1.tar.gz'))
    if not archive.is_file():
        raise unittest.SkipTest('Pinned public BigWorld SDK archive unavailable: ' + str(archive))
    with tarfile.open(archive) as archive:
        prefix = archive.getmembers()[0].name.split('/')[0] + '/programming/bigworld/'
        for name in ['lib/scaleform/loader.hpp', 'lib/scaleform/loader.cpp']:
            path = root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(archive.extractfile(prefix + name).read())
    return root


def class_definition(header, name):
    start = header.index('\tclass ' + name + ' :')
    end = header.index('{', start)
    depth = 1
    while depth:
        end += 1
        depth += (header[end] == '{') - (header[end] == '}')
    return header[start:end + 2]


def host_files(root):
    patch = load_patch()
    patch.patch(root)
    header = (root / 'lib/scaleform/loader.hpp').read_text()
    declaration = '\n'.join(class_definition(header, n) for n in ['FileOpener', 'BWGMemoryFile'])
    fixture = (ROOT / 'tests/stock_scaleform_resources_host.hpp').read_text()
    fake_loader = fixture + '\nnamespace BW { namespace ScaleformBW { struct StockScaleformFileState;\n' + declaration + '\n} }\n'
    for name, content in {
        'scaleform/loader.hpp': fake_loader,
        'scaleform/manager.hpp': r'''
namespace BW { namespace ScaleformBW {
struct Loader { ::Scaleform::Ptr<FileOpener> opener; explicit Loader(FileOpener*p):opener(p){} ::Scaleform::Ptr<::Scaleform::GFx::FileOpenerBase>GetFileOpener(){return opener.GetPtr();} };
struct Manager { static Manager* current; Loader* loader=nullptr;int reloads=0;static Manager*pInstance(){return current;}Loader*pLoader(){return loader;}void needReloadResources(){++reloads;} };
} }
''',
        'resmgr/bwresource.hpp': '', 'resmgr/multi_file_system.hpp': '',
        'cstdmf/bw_util.hpp': '',
        'Kernel/SF_SysFile.h': '', 'pch.hpp': '#include <Python.h>\n#include "bridge.hpp"\n',
        'bridge.hpp': r'''
#include "scaleform/loader.hpp"
namespace BW {namespace Script {
inline int setData(PyObject*p,string&v){PyObject*utf8=nullptr;if(PyUnicode_Check(p)){utf8=PyUnicode_AsUTF8String(p);p=utf8;}if(!p||!PyString_Check(p)){Py_XDECREF(utf8);PyErr_SetString(PyExc_TypeError,"string expected");return -1;}v.assign(PyString_AS_STRING(p),PyString_GET_SIZE(p));Py_XDECREF(utf8);return 0;}
inline int setData(PyObject*p,bool&v){int n=PyObject_IsTrue(p);if(n<0)return -1;v=n!=0;return 0;}
}}
#define PY_MODULE_FUNCTION(name,module)
''',
    }.items():
        path = root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content)
    (root / 'stock_scaleform_resources.hpp').write_bytes((ROOT / 'bigworld/stock_scaleform_resources.hpp').read_bytes())


class ScaleformResourceTests(unittest.TestCase):
    def test_transactional_idempotent_patch_and_old_path_upgrade(self):
        for old_path_patch in (False, True):
            with self.subTest(old_path_patch=old_path_patch), tempfile.TemporaryDirectory() as folder:
                root = prepare(folder)
                patch = load_patch()
                if old_path_patch:
                    path = root/'lib/scaleform/loader.cpp'
                    source = path.read_text().replace('#include "resmgr/bin_section.hpp"',
                        '#include "resmgr/bin_section.hpp"\n#include "stock_scaleform_resources.hpp"')
                    source = source.replace('BinaryPtr ptr = BWResource::instance().rootSection()->readBinary( pfilename );',
                        'const std::string resourcePath = wot::stockScaleformResourcePath(pfilename);\n'
                        'BinaryPtr ptr = BWResource::instance().rootSection()->readBinary( resourcePath.c_str() );')
                    path.write_text(source)
                patch.patch(root)
                before = {p.relative_to(root): p.read_bytes() for p in root.rglob('*') if p.is_file()}
                patch.patch(root)
                self.assertEqual(before, {p.relative_to(root): p.read_bytes() for p in root.rglob('*') if p.is_file()})
                (root / 'lib/scaleform/loader.hpp').write_text('changed source')
                before = {p.relative_to(root): p.read_bytes() for p in root.rglob('*') if p.is_file()}
                with self.assertRaises(ValueError): patch.patch(root)
                self.assertEqual(before, {p.relative_to(root): p.read_bytes() for p in root.rglob('*') if p.is_file()})

    def test_general_relative_paths_and_actual_original_archive_images(self):
        if not shutil.which(os.environ.get('CXX', 'c++')):
            raise unittest.SkipTest('C++ compiler unavailable: ' + os.environ.get('CXX', 'c++'))
        cases = {
            'gui/flash/../maps/login/__login_bg.png': 'gui/maps/login/__login_bg.png',
            'gui/flash/../maps/loading_award.png': 'gui/maps/loading_award.png',
            'other/a/b/../../images/test.png': 'other/images/test.png',
            'gui/./a//../maps/test.png': 'gui/maps/test.png',
            '../res/gui/maps/test.png': 'gui/maps/test.png',
            '../../unresolved/test.png': '../../unresolved/test.png',
            'a/../../../test.png': '../../test.png',
            'a/../': './',
            'C:/folder/../test.png': 'C:/folder/../test.png',
            'img://folder/../test.png': 'img://folder/../test.png',
            '/folder/../test.png': '/folder/../test.png',
            'gui/maps/日本語.png': 'gui/maps/日本語.png',
        }
        source = '#include "stock_scaleform_resources.hpp"\n#include <cassert>\nint main(){\n'
        for path, expected in cases.items():
            source += 'assert(wot::stockScaleformFallbackResourcePath(%s)==%s);\n' % (json.dumps(path, ensure_ascii=False), json.dumps(expected, ensure_ascii=False))
        source += 'assert(wot::stockScaleformInitialResourcePath("C:/SDK/GUI/IME.SWF")=="gui/ime.swf");\n'
        source += 'assert(wot::stockScaleformInitialResourcePath("GUI\\\\MAPS/Test.PNG")=="gui/maps/test.png");\n}\n'
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            (root/'probe.cpp').write_text(source)
            command = [os.environ.get('CXX','c++'), '-std=c++14', '-Wall', '-Wextra', '-Werror', '-I'+str(ROOT/'bigworld')]
            if sys.platform != 'win32': command += ['-fsanitize=address,undefined']
            result = subprocess.run(command+[str(root/'probe.cpp'),'-o',str(root/'probe')],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stderr)
            result = subprocess.run([str(root/'probe')],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stderr)
        client = Path(os.environ.get('WOT_0922_CLIENT','/Users/peng/Downloads/World_of_Tanks_0.09.22.00.01_CH_1513_HD'))
        if not client.is_dir(): self.skipTest('Original archive unavailable')
        with zipfile.ZipFile(client/'res/packages/gui.pkg') as package:
            for path in list(cases)[:2]:
                self.assertNotIn(path,package.namelist())
                self.assertEqual(package.read(cases[path])[:8],b'\x89PNG\r\n\x1a\n')

    def test_actual_production_owner_and_original_python2_consumers(self):
        if not shutil.which('c++'):
            raise unittest.SkipTest('C++ compiler unavailable: c++')
        python = Path.home() / '.pyenv/versions/2.7.18/bin/python2.7'
        if sys.platform != 'darwin' or not python.is_file():
            self.skipTest('Pinned CPython 2.7 extension linker required')
        with tempfile.TemporaryDirectory(prefix='scaleform-resource-owner-') as folder:
            root = prepare(folder)
            host_files(root)
            source = (ROOT / 'bigworld/stock_scaleform_resources.cpp').read_text()
            source += (ROOT / 'tests/stock_scaleform_resources_host.cpp').read_text()
            (root / 'host.cpp').write_text(source)
            prefix = python.parents[1]
            command = ['c++', '-std=c++14', '-Wall', '-Wextra', '-Werror', '-Wno-deprecated-register', '-fPIC', '-shared', '-undefined', 'dynamic_lookup', '-I'+str(prefix/'include/python2.7'), '-I'+str(root), str(root/'host.cpp'), '-o', str(root/'resource_owner.so')]
            result = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            script = (ROOT / 'tests/stock_scaleform_resources_consumer.py').read_text()
            result = subprocess.run([str(python), '-c', script], cwd=root, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == '__main__':
    unittest.main()
