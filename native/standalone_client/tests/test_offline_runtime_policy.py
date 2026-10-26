"""Run the fixed fork's mapping scope against the actual C++ admission policy."""
import ctypes
import importlib.util
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


def module(path, name):
    spec = importlib.util.spec_from_file_location(name, path)
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


class OfflineMappingTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        source = os.environ.get('WOT_FORK_0101_SOURCE')
        if not source:
            raise unittest.SkipTest('The fixed v0.10.1 source input is required')
        cls.scope = module(Path(source) / 'src/res/scripts/client/gui/mods/offline_lan_0922/native_mapping_mask.py', 'fixed_mapping_mask')
        cls.temporary = tempfile.TemporaryDirectory(prefix='offline-mapping-policy-')
        cls.addClassCleanup(cls.temporary.cleanup)
        root = Path(cls.temporary.name)
        loading = module(ROOT / 'tests/test_stock_space_loading.py', 'stock_loading_fixture')
        loading.LoadingTests().fresh(root)
        patched = (root / 'lib/compiled_space/compiled_space.cpp').read_text()
        mapping = loading.body(patched, 'bool CompiledSpace::doAddMapping(')
        production = (ROOT / 'bigworld/stock_compiled_space_mapping.cpp').read_text()
        functions = '\n'.join(loading.body(production, signature) for signature in (
            'uint32 stockLocalGeometryMappingMask()',
            'long applyStockStandardGameplayMapping()',
            'long restoreStockStandardGameplayMapping()'))
        cpp = r'''
#include "wot/offline_mapping_policy.hpp"
#include <memory>
#include <string>
using uint32=std::uint32_t;using GeometryMappingID=unsigned;
template<class T>using SmartPointer=std::shared_ptr<T>;
struct DataSection{};struct Matrix{};
namespace BW{using string=std::string;}
namespace CompiledSpace {
wot::OfflineMappingPolicy localMappingPolicy;
''' + functions + r'''
class CompiledSpace {
public:
 uint32 captured=0;
 bool doAddMapping(GeometryMappingID,Matrix&,const BW::string&,const SmartPointer<DataSection>&);
 bool addMappingWithVisibilityMask(GeometryMappingID,Matrix&,const BW::string&,uint32 mask,const SmartPointer<DataSection>&){captured=mask;return true;}
};
''' + mapping + r'''
CompiledSpace scene;
}
extern "C" long apply(){return CompiledSpace::applyStockStandardGameplayMapping();}
extern "C" long restore(){return CompiledSpace::restoreStockStandardGameplayMapping();}
extern "C" unsigned current(){return CompiledSpace::stockLocalGeometryMappingMask();}
extern "C" unsigned admit(){Matrix matrix;CompiledSpace::scene.doAddMapping(1,matrix,"map",{});return CompiledSpace::scene.captured;}
extern "C" unsigned retained(){return CompiledSpace::scene.captured;}
'''
        (root / 'probe.cpp').write_text(cpp)
        result = subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-shared',
            '-fPIC', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT / 'include'),
            str(root / 'probe.cpp'), '-o', str(root / 'probe.dylib')],
            capture_output=True, text=True)
        if result.returncode:
            raise AssertionError(result.stderr)
        cls.library = ctypes.CDLL(str(root / 'probe.dylib'))
        for name in ('current', 'admit', 'retained'):
            getattr(cls.library, name).restype = ctypes.c_uint32
        for name in ('apply', 'restore'):
            getattr(cls.library, name).restype = ctypes.c_long
        cls.bridge = type('Bridge', (), {})()
        cls.bridge.apply_standard_gameplay_mask = cls.library.apply
        cls.bridge.restore_standard_gameplay_mask = cls.library.restore

    def tearDown(self):
        self.library.restore()

    def test_mapping_captures_mask_before_scope_restores(self):
        self.assertEqual(self.library.current(), 0xffffffff)
        self.assertEqual(self.scope.call_with_standard_gameplay_mask(
            self.library.admit, native_bridge=self.bridge), 1)
        self.assertEqual(self.library.current(), 0xffffffff)
        self.assertEqual(self.library.retained(), 1)
        self.assertEqual(self.library.admit(), 0xffffffff)
        self.assertEqual(self.library.restore(), 102)

    def test_callback_failure_restores_admission_but_keeps_mapping_identity(self):
        def callback():
            self.assertEqual(self.library.admit(), 1)
            raise ValueError('mapping operation failed')
        with self.assertRaisesRegex(ValueError, 'mapping operation failed'):
            self.scope.call_with_standard_gameplay_mask(callback, native_bridge=self.bridge)
        self.assertEqual(self.library.current(), 0xffffffff)
        self.assertEqual(self.library.retained(), 1)

    def test_nested_rejection_never_restores_the_outer_owner(self):
        def outer():
            with self.assertRaisesRegex(self.scope.NativeMappingMaskError, '101'):
                self.scope.call_with_standard_gameplay_mask(self.library.admit, native_bridge=self.bridge)
            self.assertEqual(self.library.current(), 1)
            return self.library.admit()
        self.assertEqual(self.scope.call_with_standard_gameplay_mask(outer, native_bridge=self.bridge), 1)
        self.assertEqual(self.library.current(), 0xffffffff)


if __name__ == '__main__':
    unittest.main()
