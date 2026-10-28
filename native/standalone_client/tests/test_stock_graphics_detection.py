"""Exact cached hardware classification, names and signed wide score contract."""
import base64
import json
import os
import shlex
import sys
import tarfile
import zipfile
from pathlib import Path
import random
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
DEFAULTS = [
    {'minimum': [800, 80, 3000, 3000, 5000], 'pixelShader': 3., 'vertexShader': 3.},
    {'minimum': [600, 60, 2000, 3000, 3000], 'pixelShader': 3., 'vertexShader': 3.},
    {'minimum': [450, 40, 2000, 2000, 2000], 'pixelShader': 3., 'vertexShader': 3.},
    {'minimum': [250, 20, 1000, 1000, 1500], 'pixelShader': 2., 'vertexShader': 2.},
    {'minimum': [250, 0, 1000, 1000, 1500], 'pixelShader': 2., 'vertexShader': 2.},
]


class GraphicsDetectionTests(unittest.TestCase):
    def test_production_cached_bindings_preferences_and_first_lobby_consumer(self):
        client = os.environ.get('WOT_0922_CLIENT')
        if not client:
            self.skipTest('Exact #1513 client is required')
        python = Path(os.environ.get('WOT_PYTHON27', '/Users/peng/.pyenv/versions/2.7.18/bin/python2.7'))
        config = python.with_name('python2.7-config')
        if not config.exists():
            self.skipTest('Pinned CPython2.7 development files unavailable')
        compiler = shutil.which(os.environ.get('CXX', 'c++'))
        if not compiler:
            self.skipTest('C++ compiler unavailable')
        from test_stock_runtime_services import between, engine_sources
        engine = engine_sources(self)
        header, source = engine['lib/pyscript/script.hpp'], engine['lib/pyscript/script.cpp']
        support = (ROOT / 'tests/stock_display_callbacks_host.hpp').read_text()
        support = support.replace('SmartPointer(T* value=nullptr):value_(value){Py_XINCREF(value_);}',
            'static const bool STEAL_REFERENCE=true; SmartPointer(T* value=nullptr,bool steal=false):value_(value){if(!steal)Py_XINCREF(value_);}')
        support = support.replace('T* get()const{return value_;}',
            'SmartPointer& operator=(const SmartPointer& other){return operator=(other.value_);}'
            'T* get()const{return value_;}')
        support = support.replace('namespace Script {',
            'namespace Script {\nint setData(PyObject*,int&,const char* = "");\n'
            'int setData(PyObject*,BW::string&,const char* = "");\nPyObject* getData(const int64);\n' +
            between(header, 'template <class T>\n\tclass IsValidRetData',
                    'return Script::IsValidRetData<T>::getData( data );\n\t}'))
        bodies = between(source, 'PyObject * Script::argCountError(', '\n}\n')
        for marker in ('int Script::setData( PyObject * pObject, SmartPointer<PyObject> & rPyObject,',
                       'int Script::setData( PyObject * pObject, int & rInt,',
                       'int Script::setData( PyObject * pObject, BW::string & rString,',
                       'PyObject * Script::getData( const int64 data )'):
            bodies += between(source, marker, '\n}\n')
        support = support.replace('@SCRIPT_CONVERTERS@', bodies).replace('@AUTO_MACROS@',
            between(header, '// Section: Macros: Module Links', '// Section: Macros: Enums', False) +
            between(header, '// Section: Macros: Recursion utilities', '// Section: PyModuleLink', False))
        sys.path.insert(0, str(ROOT.parents[1]))
        from tools.packed_xml import read_packed_xml, TYPE_ELEMENT, TYPE_COMPRESSED_STRING
        with zipfile.ZipFile(Path(client) / 'res/packages/misc.pkg') as archive:
            resource = read_packed_xml(archive.read('system/data/auto_config_settings.xml'))
        def scalar(value):
            if value.value_type == TYPE_COMPRESSED_STRING:
                return base64.b64encode(value.value).decode('ascii')
            return value.value.decode() if isinstance(value.value, bytes) else value.value
        profiles = []
        for name, value in resource.children:
            if name == b'graphicsPreferences' and value.value_type == TYPE_ELEMENT:
                item = {'name': scalar(value.value.value)}
                item.update((name.decode(), scalar(field)) for name, field in value.value.children
                            if field.value_type != TYPE_ELEMENT)
                for key in ('PixelShaderVersion', 'VertexShaderVersion'):
                    if key in item:
                        item[key] = float(item[key])
                profiles.append(item)
        with tempfile.TemporaryDirectory(prefix='graphics-detection-bindings-') as folder:
            path = Path(folder)
            for name in ('pch.hpp', 'resmgr/datasection.hpp', 'moo/graphics_settings_picker.hpp',
                         'moo/init.hpp', 'moo/render_context.hpp', 'pyscript/script.hpp'):
                destination = path / 'include' / name
                destination.parent.mkdir(parents=True, exist_ok=True)
                destination.write_text('#pragma once\n')
            (path / 'host.cpp').write_text((ROOT / 'tests/stock_graphics_detection_host.cpp').read_text()
                                          .replace('@SDK_SUPPORT@', support))
            flags = shlex.split(subprocess.check_output([str(config), '--cflags'], text=True))
            libs = shlex.split(subprocess.check_output([str(config), '--ldflags'], text=True))
            result = subprocess.run([compiler, '-std=c++14', *flags, '-UNDEBUG', '-Wno-deprecated-register',
                                     '-I', str(path / 'include'), '-I', str(ROOT / 'include'),
                                     '-I', str(ROOT / 'bigworld'), str(path / 'host.cpp'), *libs,
                                     '-o', str(path / 'host')], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            result = subprocess.run([str(path / 'host'), str(ROOT / 'tests/stock_graphics_detection_consumer.py'),
                                     json.dumps(profiles), str(Path(client) / 'res/packages/scripts.pkg')],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_profile_patch_composition_and_atomic_failure(self):
        from test_stock_graphics_settings import fresh_engine, load_patch
        detector = load_patch('patch_stock_graphics_detection')
        graphics = load_patch('patch_stock_graphics_settings')
        archive = Path(os.environ.get('WOT_PUBLIC_BIGWORLD_ARCHIVE', '/tmp/wot-bigworld-engine-14.4.1.tar.gz'))
        if not archive.is_file():
            self.skipTest('Pinned public SDK archive unavailable')
        with tarfile.open(archive) as source:
            member = next(item for item in source if item.name.endswith('/programming/bigworld/lib/moo/graphics_settings_picker.cpp'))
            picker = source.extractfile(member).read()
        for order in ((detector.patch, graphics.patch), (graphics.patch, detector.patch)):
            with self.subTest(order=[patch.__module__ for patch in order]), tempfile.TemporaryDirectory() as folder:
                root = Path(folder)
                fresh_engine(root)
                path = root / 'lib/moo/graphics_settings_picker.cpp'
                path.write_bytes(picker)
                for patch in order:
                    patch(root)
                before = {path.relative_to(root): path.read_bytes() for path in root.rglob('*') if path.is_file()}
                for patch in order:
                    patch(root)
                self.assertEqual(before, {path.relative_to(root): path.read_bytes() for path in root.rglob('*') if path.is_file()})
                # Return to pristine picker but invalidate its publication anchor;
                # even the new include must not be written after this failure.
                path.write_bytes(picker.replace(b'initConfig();', b'initConfigChanged();'))
                snapshot = path.read_bytes()
                with self.assertRaisesRegex(ValueError, 'profile publication'):
                    detector.patch(root)
                self.assertEqual(path.read_bytes(), snapshot)

    def test_original_cache_classification_and_wide_queries(self):
        client = os.environ.get('WOT_0922_CLIENT')
        if not client:
            self.skipTest('Exact #1513 client is required')
        compiler = shutil.which(os.environ.get('CXX', 'c++'))
        if not compiler:
            self.skipTest('C++ compiler unavailable')
        try:
            from stock_graphics_detection_oracle import OriginalGraphicsDetection
        except ImportError:
            self.skipTest('pefile and Unicorn are required')
        original = OriginalGraphicsDetection(Path(client) / 'WorldOfTanks.exe')
        self.assertEqual(original.default_profiles(), DEFAULTS)
        original.configure(DEFAULTS)
        generator = random.Random(1513)
        cases = [([0] * 7, shader) for shader in (0, 0x100, 0x200, 0x300, 0x500)]
        for profile in DEFAULTS:
            for index in range(5):
                for delta in (-1, 0, 1):
                    values = [10000] * 7
                    values[index] = profile['minimum'][index] + delta
                    cases.append((values, 0x300))
        for unused in range(240):
            values = [generator.choice((-1, 0, 20, 100, 1000, 3000, 6000, 2 ** 40)) for unused in range(7)]
            cases.append((values, generator.choice((0x100, 0x200, 0x201, 0x300, 0x400))))
        expected = [original.classify(values, shader) for values, shader in cases]
        scores = [-(2 ** 63), -1, 0, 2 ** 31, 2 ** 40, 7, 2 ** 63 - 1]
        original.store(scores)
        self.assertEqual([original.score(index) for index in range(-2, 10)], [0, 0] + scores + [0, 0, 0])
        self.assertEqual([original.name(name) for name in ('max', 'high', 'medium', 'low', 'min', '', ' max ', 'unknown')],
                         [0, 1, 2, 3, 4, 5, 5, 5])
        source = '''#include "wot/stock_graphics_detection.hpp"
#include <cassert>
#include <cstdio>
int main(){wot::StockGraphicsDetection detector;
@CASES@
const std::int64_t values[]={INT64_MIN,-1,0,INT64_C(2147483648),INT64_C(1099511627776),7,INT64_MAX};
for(int i=0;i<7;++i)detector.store(i,values[i]);
for(int i=0;i<7;++i)assert(detector.score(i)==values[i]);
assert(detector.score(-1)==0&&detector.score(7)==0);
assert(detector.presetId("MAX")==0&&detector.presetId("hIgH")==1&&detector.presetId(" min ")==5);
}'''
        commands = []
        for values, shader in cases:
            commands.append('detector.scores={{%s}};std::printf("%%d\\n",detector.classify(%d));' %
                            (','.join(str(value) + 'LL' for value in values), shader))
        source = source.replace('@CASES@', '\n'.join(commands))
        with tempfile.TemporaryDirectory(prefix='graphics-detection-cache-') as folder:
            path = Path(folder)
            (path / 'probe.cpp').write_text(source)
            result = subprocess.run([compiler, '-std=c++14', '-Wall', '-Wextra', '-Werror', '-UNDEBUG',
                                     '-I', str(ROOT / 'include'), str(path / 'probe.cpp'), '-o', str(path / 'probe')],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            result = subprocess.run([str(path / 'probe')], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual([int(value) for value in result.stdout.split()], expected)
