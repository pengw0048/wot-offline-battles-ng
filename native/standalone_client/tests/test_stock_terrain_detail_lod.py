"""Exercise authored terrain height LOD through the real SDK resource methods."""
import importlib.util
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import tarfile
import tempfile
import unittest
import zipfile

from native_test_compile_flags import native_test_compile_flags


ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('terrain_detail_patch', ROOT / 'bigworld/patch_stock_terrain_detail_lod.py')
patch = importlib.util.module_from_spec(spec)
spec.loader.exec_module(patch)


def body(source, signature):
    start = source.index(signature)
    cursor = source.index('{', start)
    end, depth = cursor, 1
    while depth:
        end += 1
        depth += (source[end] == '{') - (source[end] == '}')
    return source[cursor:end + 1]


def host(block, height, resource):
    load_start = block.index('#if !defined(EDITOR_ENABLED)\n\tconst uint32 detailedHeightMapLod =')
    load_end = block.index('\n\t// In editor we load', load_start)
    load = block[load_start:load_end]
    load = load[:load.index('\n\t// set up detail')]
    if 'forcedHeightMapLod_ = forcedLod;' in block:
        load = 'forcedHeightMapLod_ = forcedLod;\n' + load
    call_start = block.index('pDetailHeightMapResource_->evaluate(')
    call_end = block.index('// Vertex evaluation', call_start)
    call = block[call_start:call_end]
    return r'''
#include <cassert>
#include <cstdint>
#include <set>
#include <string>
using uint32 = uint32_t;
enum ResourceRequired { RR_No, RR_Yes };
enum ResourceState { RS_Loaded, RS_Unloaded };
enum ResourceStreamType { RST_Asyncronous, RST_Synchronous };
#define MF_ASSERT assert
#define BW_GUARD
static int warnings = 0;
#define WARNING_MSG(...) (++warnings)
struct Ptr {
    bool value = false;
    bool exists() const { return value; }
    explicit operator bool() const { return value; }
    bool operator==(std::nullptr_t) const { return !value; }
    Ptr& operator=(std::nullptr_t) { value = false; return *this; }
    template<class T> Ptr& operator=(T* p) { value = p != nullptr; delete p; return *this; }
};
template<class O> struct Resource {
    virtual ~Resource() = default;
    ResourceRequired required_ = RR_No;
    Ptr object_;
    ResourceStreamType streamType_ = RST_Synchronous;
    ResourceState getState() const { return object_ ? RS_Loaded : RS_Unloaded; }
    virtual bool load() = 0;
    void unload() { object_ = nullptr; }
    void startAsyncTask() { load(); }
    void stream(ResourceStreamType streamType) @STREAM@
};
struct DataSectionPtr {
    bool present;
    explicit operator bool() const { return present; }
};
namespace BW { using string = std::string; }
struct BWResource {
    std::set<std::string> available;
    static BWResource& instance() { static BWResource value; return value; }
    DataSectionPtr openSection(const std::string& name) { return {available.count(name) != 0}; }
};
struct TerrainHeightMap2 {
    TerrainHeightMap2(float, int, uint32) {}
    bool load(DataSectionPtr, std::string*) { return true; }
    static std::string getHeightSectionName(const std::string& name, uint32 lod) {
        return name + (lod ? std::to_string(lod) : "");
    }
};
template<class T> void bw_safe_delete(T* p) { delete p; }
struct HeightMapResource : Resource<TerrainHeightMap2> {
    std::string terrainSectionName_ = "block";
    uint32 lodLevel_;
    float blockSize_ = 100;
    explicit HeightMapResource(uint32 lod) : lodLevel_(lod) {}
    ResourceRequired evaluate(uint32 requiredVertexGridSize, uint32 standardHeightMapSize,
        float detailHeightMapDistance, float blockDistance, uint32 topLodLevel) @EVALUATE@
    bool load() override @LOAD@
};
struct Settings { uint32 top = 0; uint32 topVertexLod() const { return top; } };
struct Block {
    int forcedHeightMapLod_ = -1;
    Settings owned;
    Settings* settings_ = &owned;
    HeightMapResource* pDetailHeightMapResource_ = nullptr;
    struct { float minDistanceToCamera_ = 0; } distanceInfo_;
    void load(int forcedLod) {
        @BLOCK_LOAD@
        delete pDetailHeightMapResource_;
        pDetailHeightMapResource_ = new HeightMapResource(detailedHeightMapLod);
    }
    void evaluate(uint32 requiredVertexGridSize = 16, uint32 standard = 16, float distance = 0) {
        float detailDistance = 100;
        distanceInfo_.minDistanceToCamera_ = distance;
        struct Height { uint32 size; uint32 blocksWidth() const { return size; } };
        auto heightMap = [standard]() { return Height{standard}; };
        @CALL@
        pDetailHeightMapResource_->stream(RST_Synchronous);
    }
    ~Block() { delete pDetailHeightMapResource_; }
};
int main() {
    auto& available = BWResource::instance().available;
#ifdef EDITOR_ENABLED
    available = {"block/heights2"};
    Block editor;
    editor.owned.top = 2;
    editor.load(3);
    editor.evaluate(); editor.evaluate();
    assert(editor.pDetailHeightMapResource_->lodLevel_ == 2 && warnings == 0);
    return 0;
#else
    available = {"block/heights3"};
    Block b;
    b.load(3);
    b.evaluate();
    b.evaluate();
    b.evaluate();
    if (!b.pDetailHeightMapResource_->object_.exists() || warnings) return 2;
    b.owned.top = 1;
    b.evaluate();
    assert(b.pDetailHeightMapResource_->lodLevel_ == 3);
    b.evaluate(16, 16, 200);
    assert(!b.pDetailHeightMapResource_->object_.exists());
    b.evaluate();
    assert(b.pDetailHeightMapResource_->object_.exists());
    b.evaluate(32, 16, 200);
    assert(b.pDetailHeightMapResource_->object_.exists());
    // A real missing authored resource still fails and is retried; no negative cache.
    available.clear();
    b.load(3);
    b.evaluate(); b.evaluate();
    assert(!b.pDetailHeightMapResource_->object_.exists() && warnings == 2);
    available = {"block/heights", "block/heights1", "block/heights2"};
    warnings = 0;
    b.owned.top = 2;
    b.load(0); b.evaluate(); b.evaluate();
    assert(b.pDetailHeightMapResource_->lodLevel_ == 0 && warnings == 0);
    // Reloading an unforced block clears the previous authored override.
    b.owned.top = 0;
    b.load(-1); b.evaluate();
    b.owned.top = 1; b.evaluate();
    assert(b.pDetailHeightMapResource_->lodLevel_ == 1 && warnings == 0);
    b.owned.top = 2; b.evaluate();
    assert(b.pDetailHeightMapResource_->lodLevel_ == 2 && warnings == 0);
    return 0;
#endif
}
'''.replace('@STREAM@', body(resource, 'inline void Resource<O>::stream(')).replace(
        '@EVALUATE@', body(height, 'inline ResourceRequired HeightMapResource::evaluate(')).replace(
        '@LOAD@', body(height, 'bool HeightMapResource::load()')).replace(
        '@BLOCK_LOAD@', load).replace('@CALL@', call)


class TerrainDetailLodTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        configured = os.environ.get('WOT_PUBLIC_ENGINE_ARCHIVE') or os.environ.get('WOT_BIGWORLD_SOURCE_ARCHIVE')
        archive = Path(configured or '/tmp/wot-bigworld-engine-14.4.1.tar.gz')
        if not archive.is_file():
            if configured:
                raise AssertionError('Configured engine archive missing: ' + str(archive))
            raise unittest.SkipTest('Pinned public engine archive required')
        configured_compiler = os.environ.get('CXX')
        cls.compiler = shutil.which(configured_compiler or 'c++')
        if not cls.compiler:
            if configured_compiler:
                raise AssertionError('Configured compiler missing: ' + configured_compiler)
            raise unittest.SkipTest('C++ compiler required')
        cls.temporary = tempfile.TemporaryDirectory(prefix='terrain-detail-lod-')
        cls.addClassCleanup(cls.temporary.cleanup)
        cls.root = Path(cls.temporary.name)
        cls.original = {}
        files = (*patch.FILES, 'lib/terrain/terrain2/terrain_height_map2.hpp',
                 'lib/terrain/terrain2/terrain_height_map2.cpp', 'lib/terrain/terrain2/resource.hpp')
        with tarfile.open(archive) as source:
            for name in files:
                matches = [m for m in source.getmembers() if m.name.endswith('/programming/bigworld/' + name)]
                if len(matches) != 1:
                    raise AssertionError('Pinned SDK member not unique: ' + name)
                raw = source.extractfile(matches[0]).read()
                cls.original[name] = raw.decode('utf-8-sig')
                target = cls.root / name
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(raw)

    def test_real_sdk_resource_lifecycle_and_negative_control(self):
        patch.patch(self.root)
        snapshot = {name: (self.root / name).read_bytes() for name in patch.FILES}
        patch.patch(self.root)
        self.assertEqual(snapshot, {name: (self.root / name).read_bytes() for name in patch.FILES})
        height = self.original['lib/terrain/terrain2/terrain_height_map2.hpp'] + self.original['lib/terrain/terrain2/terrain_height_map2.cpp']
        resource = self.original['lib/terrain/terrain2/resource.hpp']
        for label, block, expected, defines in (
            ('before', self.original[patch.FILES[0]], 2, []),
            ('after', (self.root / patch.FILES[0]).read_text(), 0, []),
            ('editor', (self.root / patch.FILES[0]).read_text(), 0, ['-DEDITOR_ENABLED']),
        ):
            source = self.root / (label + '.cpp')
            source.write_text(host(block, height, resource))
            binary = self.root / (label + ('.exe' if os.name == 'nt' else ''))
            result = subprocess.run([self.compiler, *native_test_compile_flags(self.compiler), '-std=c++17',
                                     '-Wall', '-Wextra', '-Werror', '-UNDEBUG', *defines, str(source), '-o', str(binary)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True)
            self.assertEqual(result.returncode, expected, result.stdout + result.stderr)

    def test_patch_is_transactional(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for name in patch.FILES:
                target = root / name
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_text(self.original[name])
            bad = root / patch.FILES[1]
            bad.write_text(bad.read_text().replace('forcedLod_;', 'changedLod_;'))
            before = {name: (root / name).read_bytes() for name in patch.FILES}
            with self.assertRaises(ValueError):
                patch.patch(root)
            self.assertEqual(before, {name: (root / name).read_bytes() for name in patch.FILES})

    def test_exact_metadata_and_native_lod_owner(self):
        configured = os.environ.get('WOT_0922_CLIENT')
        if not configured:
            self.skipTest('WOT_0922_CLIENT required for exact #1513 evidence')
        client = Path(configured)
        self.assertTrue((client / 'WorldOfTanks.exe').is_file(), 'Configured client executable missing')
        self.assertTrue((client / 'res/packages/hangar_v2.pkg').is_file(), 'Configured hangar package missing')
        try:
            import pefile
            from unicorn import Uc, UC_ARCH_X86, UC_MODE_32
            from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_EDI, UC_X86_REG_EBP, UC_X86_REG_ESP
        except ImportError as error:
            self.skipTest('Optional exact PE oracle dependency missing: ' + str(error))
        with zipfile.ZipFile(client / 'res/packages/hangar_v2.pkg') as package:
            blocks = [name for name in package.namelist() if name.endswith('.cdata_processed')]
            self.assertEqual(len(blocks), 4)
            for name in blocks:
                import io
                with zipfile.ZipFile(io.BytesIO(package.read(name))) as terrain:
                    heights = sorted(n for n in terrain.namelist() if re.fullmatch(r'terrain2/heights\d*', n))
                    self.assertEqual(heights, ['terrain2/heights3'])
                    self.assertRegex(terrain.read('terrain2/TerrainBlockMetaData').decode(), r'<ForcedLod>\s*3\s*</ForcedLod>')
        pe = pefile.PE(str(client / 'WorldOfTanks.exe'))
        self.assertEqual(pe.FILE_HEADER.Machine, 0x14c)
        image, base = pe.get_memory_mapped_image(), pe.OPTIONAL_HEADER.ImageBase
        self.assertEqual(image[0xad1d12-base:0xad1d19-base], bytes.fromhex('8b d0 89 55 90 85 d2'))
        for forced in (-1, 0, 1, 3, 5):
            for default in (0, 2, 3):
                machine = Uc(UC_ARCH_X86, UC_MODE_32)
                machine.mem_map(base, (len(image) + 4095) & ~4095)
                machine.mem_write(base, image)
                block, settings, frame = 0x30000000, 0x30001000, 0x30002000
                machine.mem_map(block, 0x3000)
                machine.mem_write(block + 0xc, struct.pack('<I', settings))
                machine.mem_write(settings + 0x9c, struct.pack('<I', default))
                machine.reg_write(UC_X86_REG_EDI, block)
                machine.reg_write(UC_X86_REG_EBP, frame + 0x100)
                machine.reg_write(UC_X86_REG_ESP, frame + 0x800)
                machine.reg_write(UC_X86_REG_EAX, forced & 0xffffffff)
                machine.emu_start(0xad1d12, 0xad1d34, count=30)
                result = struct.unpack('<I', machine.mem_read(block + 0xc4, 4))[0]
                self.assertEqual(result, default if forced < 0 else forced)


if __name__ == '__main__':
    unittest.main()
