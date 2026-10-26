"""Processed-resource loading and the original #1513 storage contracts."""
import ctypes
import importlib.util
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tarfile
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[3]
spec = importlib.util.spec_from_file_location(
    'processed_patch', ROOT / 'native/standalone_client/bigworld/patch_processed_resources.py')
patcher = importlib.util.module_from_spec(spec)
spec.loader.exec_module(patcher)


class ProcessedPatchTests(unittest.TestCase):
    def fixture(self, root):
        for name, changes in patcher.patches().items():
            path = root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text('\n'.join(before for before, unused, count in changes))

    def test_distinct_native_storages_and_repeatable_patch(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            self.fixture(root)
            patcher.patch_engine(root)
            saved = {name: (root / name).read_bytes() for name in patcher.patches()}
            patcher.patch_engine(root)
            self.assertEqual(saved, {name: (root / name).read_bytes() for name in saved})
            table = (root / 'lib/moo/vertex_element.cpp').read_text()
            for name, unused, count in patcher.STORAGES:
                self.assertIn('Stor::%s, "%s"' % (name, name), table)
            self.assertNotIn('Stor::SC_REVERSE_PADDED_III_, "SC_UBYTE4', table)

    def test_unknown_pinned_source_fails_before_any_write(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            self.fixture(root)
            (root / 'lib/moo/vertex_declaration.cpp').write_text('changed source')
            saved = {name: (root / name).read_bytes() for name in patcher.patches()}
            with self.assertRaisesRegex(ValueError, 'vertex_declaration.cpp'):
                patcher.patch_engine(root)
            self.assertEqual(saved, {name: (root / name).read_bytes() for name in saved})


CPP_PREFIX = r'''
#include <cstdint>
#include <cstring>
#include <new>
#include <string>
#include <vector>
#include <set>
using uint8 = std::uint8_t;
using int32 = std::int32_t;
struct Float1 { float x; };
struct Float2 { float x, y; };
struct Ubyte2 { uint8 x, y; };
struct Ubyte3 { uint8 x, y, z; };
'''
CPP_SUFFIX = r'''
static_assert(sizeof(ElementMult3I) == 4, "float index stride");
static_assert(sizeof(ElementMult3III) == 3, "three index stride");
static_assert(sizeof(ElementMult3ReversePaddedIIIP) == 4, "packed index stride");
static_assert(sizeof(ElementReversePaddedPWWP) == 4, "packed weight stride");
template<class D, class S> void assign(void* dst, const void* src) {
    D value = *static_cast<const S*>(src);
    std::memcpy(dst, &value, sizeof(value));
}
extern "C" void convert(int op, void* dst, const void* src) {
    switch (op) {
    case 0: assign<Float1, ElementMult3I>(dst, src); break;
    case 1: assign<ElementMult3I, Float1>(dst, src); break;
    case 2: assign<Ubyte3, ElementMult3III>(dst, src); break;
    case 3: assign<ElementMult3III, Ubyte3>(dst, src); break;
    case 4: assign<Ubyte3, ElementMult3ReversePaddedIIIP>(dst, src); break;
    case 5: assign<ElementMult3ReversePaddedIIIP, Ubyte3>(dst, src); break;
    case 6: assign<ElementMult3ReversePaddedIIIP, ElementMult3III>(dst, src); break;
    case 7: assign<ElementMult3III, ElementMult3ReversePaddedIIIP>(dst, src); break;
    case 8: assign<ElementMult3III, ElementMult3I>(dst, src); break;
    case 9: assign<ElementMult3ReversePaddedIIIP, Float1>(dst, src); break;
    case 10: assign<Ubyte2, ElementReversePaddedPWWP>(dst, src); break;
    case 11: assign<ElementReversePaddedPWWP, Ubyte2>(dst, src); break;
    case 12: assign<Float2, ElementReversePaddedPWWP>(dst, src); break;
    case 13: assign<ElementReversePaddedPWWP, Float2>(dst, src); break;
    }
}
extern "C" void default_weights(void* dst) {
    new (dst) ElementReversePaddedPWWP;
}
std::set<std::string> available;
std::vector<std::string> requested;
namespace BW { using string = std::string; }
using DataSectionPtr = bool;
struct BWResource {
    static BWResource& instance() { static BWResource resource; return resource; }
    static DataSectionPtr openSection(const std::string& path) {
        requested.push_back(path);
        return available.count(path) != 0;
    }
};
extern "C" int visual(int present, bool useProcessedData) {
    available.clear(); requested.clear();
    const char* suffixes[] = {".visual_processed", ".visual.processed", ".visual"};
    for (int index = 0; index < 3; ++index)
        if (present & (1 << index)) available.insert(std::string("tank") + suffixes[index]);
    BW::string resourceBaseName("tank"), actualResourceName;
    DataSectionPtr dataSection = false;
    if (useProcessedData) {
VISUAL_BODY
    }
    if (!dataSection) dataSection = BWResource::openSection("tank.visual");
    return dataSection ? int(requested.size()) : -int(requested.size());
}
extern "C" int primitive(int present) {
    available.clear(); requested.clear();
    const char* suffixes[] = {"_processed", ".processed", ""};
    for (int index = 0; index < 3; ++index)
        if (present & (1 << index)) available.insert(std::string("tank.primitives") + suffixes[index]);
    BW::string resourceID("tank.primitives");
PRIMITIVE_BODY
    if (!pNew) pNew = BWResource::instance().openSection(resourceID);
    return pNew ? int(requested.size()) : -int(requested.size());
}
'''


class ProcessedNativeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which(os.environ.get('CXX', 'c++'))
        if not compiler or os.name == 'nt':
            raise unittest.SkipTest('A Unix C++ compiler is needed for the native storage fixture')
        cls.temporary = tempfile.TemporaryDirectory(prefix='processed-storage-')
        root = Path(cls.temporary.name)
        source = root / 'probe.cpp'
        source.write_text(CPP_PREFIX + patcher.NATIVE_STORAGE_TYPES + CPP_SUFFIX.replace(
            'VISUAL_BODY', patcher.VISUAL_AFTER).replace('PRIMITIVE_BODY', patcher.PRIMITIVE_AFTER))
        library = root / ('probe.dylib' if sys.platform == 'darwin' else 'probe.so')
        subprocess.run([compiler, '-std=c++17', '-Wall', '-Wextra', '-Werror',
                        '-ffp-contract=off', '-fPIC', '-shared', str(source), '-o', str(library)],
                       check=True, capture_output=True)
        cls.library = ctypes.CDLL(str(library))
        cls.library.convert.argtypes = [ctypes.c_int, ctypes.c_void_p, ctypes.c_void_p]
        cls.library.visual.argtypes = [ctypes.c_int, ctypes.c_bool]
        cls.library.primitive.argtypes = [ctypes.c_int]

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def convert(self, op, source, size):
        destination = ctypes.create_string_buffer(8)
        original = ctypes.create_string_buffer(source)
        self.library.convert(op, destination, original)
        return destination.raw[:size]

    def test_processed_priority_and_raw_negative_paths(self):
        self.assertEqual(self.library.visual(7, True), 1)
        self.assertEqual(self.library.visual(6, True), 2)
        self.assertEqual(self.library.visual(4, True), 3)
        self.assertEqual(self.library.visual(0, True), -3)
        self.assertEqual(self.library.visual(7, False), 1)
        self.assertEqual(self.library.visual(1, False), -1)
        for mask, expected in ((7, 1), (6, 2), (4, 3), (0, -3)):
            self.assertEqual(self.library.primitive(mask), expected)

    def test_indices_are_row_offsets_with_reverse_padding(self):
        # Captured #1513 0x009cf370/3c0/4a0/600/670 kernel results.
        self.assertEqual(self.convert(2, bytes((3, 6, 255)), 3), bytes((1, 2, 85)))
        self.assertEqual(self.convert(3, bytes((85, 86, 255)), 3), bytes((255, 2, 253)))
        self.assertEqual(self.convert(4, bytes((255, 6, 3, 177)), 3), bytes((1, 2, 85)))
        self.assertEqual(self.convert(5, bytes((1, 2, 85)), 4), bytes((255, 6, 3, 0)))
        self.assertEqual(self.convert(6, bytes((3, 6, 255)), 4), bytes((255, 6, 3, 0)))
        self.assertEqual(self.convert(7, bytes((255, 6, 3, 177)), 3), bytes((3, 6, 255)))

    def test_float_indices_and_unclamped_weight_storage(self):
        self.assertEqual(self.convert(0, struct.pack('<f', 7), 4),
                         struct.pack('<f', 2.3333334922790527))
        self.assertEqual(self.convert(1, struct.pack('<f', 7), 4), struct.pack('<f', 21))
        self.assertEqual(self.convert(8, struct.pack('<f', 7.9), 3), bytes((7, 7, 7)))
        self.assertEqual(self.convert(9, struct.pack('<f', 2.9), 3), bytes((8, 8, 8)))
        self.assertEqual(self.convert(8, struct.pack('<f', float('nan')), 3), bytes(3))
        self.assertEqual(self.convert(10, bytes((177, 10, 20, 88)), 2), bytes((20, 10)))
        self.assertEqual(self.convert(11, bytes((20, 10)), 4), bytes((0, 10, 20, 0)))
        self.assertEqual(self.convert(13, struct.pack('<ff', -.1, 1.1), 4),
                         bytes((0, 25, 231, 0)))
        self.assertEqual(self.convert(13, struct.pack('<ff', 1, 0), 4), bytes(4))
        self.assertEqual(self.convert(12, bytes((77, 128, 255, 88)), 8),
                         struct.pack('<ff', .99609375, .5))
        original = ctypes.create_string_buffer(bytes((177, 177, 177, 177)))
        self.library.default_weights(original)
        self.assertEqual(original.raw[:4], bytes((177, 0, 255, 177)))


class ProcessedStorageDispatchTests(unittest.TestCase):
    def test_original_storage_traits_reach_runtime_size_and_component_queries(self):
        archive = Path(os.environ.get('WOT_PUBLIC_ENGINE_ARCHIVE',
                                     '/tmp/wot-bigworld-engine-14.4.1.tar.gz'))
        if not archive.exists():
            self.skipTest('The pinned public engine archive is required')
        with tempfile.TemporaryDirectory(prefix='processed-dispatch-') as temporary:
            root = Path(temporary)
            with tarfile.open(archive) as source:
                names = set(patcher.patches()) | {'lib/moo/vertex_format.cpp'}
                for name in names:
                    member = next(m for m in source.getmembers()
                                  if m.name.endswith('/programming/bigworld/' + name))
                    path = root / name
                    path.parent.mkdir(parents=True, exist_ok=True)
                    path.write_bytes(source.extractfile(member).read())
            patcher.patch_engine(root)
            element = (root / 'lib/moo/vertex_element.hpp').read_text()
            storage = element[element.index('\tstruct StorageType'):]
            storage = storage[:storage.index('\n\t};') + len('\n\t};')]
            header = (root / 'lib/moo/vertex_format.hpp').read_text()
            macros = header[header.index('#define DECLARE_VERTEX_TYPE_MAPPING('):
                            header.index('// declare, but instead')]
            producer = '\n'.join(line for line in header.splitlines()
                                 if line.startswith('DECLARE_VERTEX_TYPE_MAPPING(') and
                                 any(name in line for name, unused, count in patcher.STORAGES))
            runtime = (root / 'lib/moo/vertex_format.cpp').read_text()
            runtime = runtime[runtime.index('bool fillStorageInfo('):
                              runtime.index('// Section: VertexFormat')]
            # Compile the real patched dispatch and real new storage traits.
            # Unrelated legacy traits need no fake conversion implementation.
            code = CPP_PREFIX + '\nusing uint32 = std::uint32_t;\nnamespace VertexElement {\n'
            code += storage + '\n' + patcher.NATIVE_STORAGE_TYPES
            code += '\nuint32 componentCount(StorageType::Value);\nsize_t typeSize(StorageType::Value);\n}\n'
            code += '''
template<class T> class VertexElementTypeTraits;
template<VertexElement::StorageType::Value T> class VertexElementStorageTypeTraits {
public:
    static const size_t s_size = 0, s_components = 0;
};
'''
            code += macros + producer + '\n'
            code += 'DECLARE_VERTEX_TYPE_MAPPING(float, FLOAT1, 1);\n'
            code += 'DECLARE_VERTEX_TYPE_MAPPING(Ubyte3, UBYTE3, 3);\n' + runtime
            code += '\nextern "C" unsigned metadata(unsigned storage, bool components) {\n'
            code += 'auto value = static_cast<VertexElement::StorageType::Value>(storage);\n'
            code += 'return static_cast<unsigned>(components ? VertexElement::componentCount(value) : VertexElement::typeSize(value));\n}\n'
            code += 'extern "C" unsigned kind(unsigned index) {\nconst unsigned values[] = {'
            code += ','.join('VertexElement::StorageType::' + name
                             for name in ('FLOAT1', 'UBYTE3', 'UNKNOWN') +
                             tuple(value[0] for value in patcher.STORAGES))
            code += '}; return values[index]; }\n'
            (root / 'dispatch.cpp').write_text(code)
            library = root / ('dispatch.dylib' if sys.platform == 'darwin' else 'dispatch.so')
            subprocess.run(['c++', '-std=c++14', '-Wall', '-Wextra', '-Werror',
                            '-fPIC', '-shared', str(root / 'dispatch.cpp'), '-o', str(library)],
                           check=True)
            actual = ctypes.CDLL(str(library))
            actual.metadata.argtypes = [ctypes.c_uint, ctypes.c_bool]
            for index, expected in enumerate(((4, 1), (3, 3), (0, 0),
                                               (4, 1), (3, 3), (4, 3), (4, 2))):
                with self.subTest(storage=index):
                    self.assertEqual((actual.metadata(actual.kind(index), False),
                                      actual.metadata(actual.kind(index), True)), expected)
            self.assertEqual(actual.metadata(0xffffffff, False), 0)
            self.assertEqual(actual.metadata(0xffffffff, True), 0)


if __name__ == '__main__':
    unittest.main()
