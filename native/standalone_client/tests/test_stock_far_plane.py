"""Exercise the original per-space far-plane reader and live quality callback."""
import importlib.util
import os
from pathlib import Path
import shutil
import subprocess
import tarfile
import tempfile
import unittest


PATCH_PATH = Path(__file__).parents[1] / 'bigworld/patch_stock_effect_owners.py'
SPEC = importlib.util.spec_from_file_location('stock_far_plane_patch', PATCH_PATH)
PATCH = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PATCH)


class StockFarPlaneTests(unittest.TestCase):
    def test_no_sky_dome_occluder_is_created(self):
        archive = Path(os.environ.get('WOT_PUBLIC_ENGINE_ARCHIVE', '/tmp/wot-bigworld-engine-14.4.1.tar.gz'))
        if not archive.is_file():
            self.skipTest('Pinned public source archive is required')
        with tempfile.TemporaryDirectory(prefix='stock-photon-occluders-') as temporary:
            root = Path(temporary)
            name = 'lib/romp/enviro_minder.cpp'
            with tarfile.open(archive) as original:
                member = next(m for m in original.getnames() if m.endswith('/programming/bigworld/' + name))
                (root / name).parent.mkdir(parents=True)
                (root / name).write_bytes(original.extractfile(member).read())
            PATCH.patch_photon_occluders(root)
            cpp = (root / name).read_text()
            PATCH.patch_photon_occluders(root)
            self.assertEqual(cpp, (root / name).read_text())
            # The #1513 executable has ZBufferOccluder but no SkyDomeOccluder RTTI.
            self.assertNotIn('new SkyDomeOccluder', cpp)
            self.assertIn('zBufferOccluder_ = new ZBufferOccluder();', cpp)

    def test_original_space_distances_replace_the_global_ratio(self):
        archive = Path(os.environ.get('WOT_PUBLIC_ENGINE_ARCHIVE', '/tmp/wot-bigworld-engine-14.4.1.tar.gz'))
        compiler = shutil.which('c++')
        if not archive.is_file() or not compiler:
            self.skipTest('Pinned public source archive and host C++ compiler are required')
        with tempfile.TemporaryDirectory(prefix='stock-far-plane-') as temporary:
            root = Path(temporary)
            files = ('lib/romp/enviro_minder.cpp', 'lib/romp/enviro_minder.hpp')
            with tarfile.open(archive) as original:
                for name in files:
                    member = next(m for m in original.getnames()
                                  if m.endswith('/programming/bigworld/' + name))
                    path = root / name
                    path.parent.mkdir(parents=True, exist_ok=True)
                    path.write_bytes(original.extractfile(member).read())
            PATCH.patch_far_plane(root)
            generated = {name: (root / name).read_text() for name in files}
            PATCH.patch_far_plane(root)
            self.assertEqual(generated, {name: (root / name).read_text() for name in files})
            cpp = generated[files[0]]
            header = generated[files[1]]
            self.assertIn('stockFarPlanesAvailable_ = false;', cpp)
            self.assertIn('setStockFarPlaneOption(optionIndex, ratio);', cpp)
            self.assertIn('void setStockFarPlaneOption(int optionIndex, float ratio);', header)
            source = r'''
#include <cassert>
#include <map>
#include <string>
#define MF_ASSERT assert
struct DataSection {
    std::map<std::string, float> fields;
    DataSection* table = nullptr;
    DataSection* openSection(const char*) { return table; }
    unsigned countChildren() const { return unsigned(fields.size()); }
    float readFloat(const char* key, float fallback) const {
        const auto it = fields.find(key); return it == fields.end() ? fallback : it->second;
    }
};
using DataSectionPtr = DataSection*;
struct EnviroMinder {
    float stockFarPlanes_[4] = {1000,800,600,600};
    bool stockFarPlanesAvailable_ = false;
    float farPlaneBaseLine_ = 500, farPlane_ = 500, cameraClip = 500;
    void load(DataSectionPtr pDS) {
        farPlaneBaseLine_ = 500; stockFarPlanesAvailable_ = false;
        if (pDS) {
READ
        }
    }
    void setFarPlane(float value) { cameraClip = farPlane_ = value; }
    void setStockFarPlaneOption(int optionIndex, float ratio);
};
SELECT
int main() {
    EnviroMinder owner;
    // Exact #1513 Karelia space.settings: MIN is 600m, not 500m * 0.5.
    DataSection table{{{"max",1450},{"high",1100},{"medium",800},{"low",600}}};
    DataSection space; space.table = &table;
    owner.load(&space);
    const float expected[] = {1450,1100,800,600};
    for (int option = 0; option != 4; ++option) {
        owner.setStockFarPlaneOption(option, .5f);
        assert(owner.cameraClip == expected[option]);
        assert(owner.farPlane_ == 1450);
    }
    // Exact Fjords values from this playtest and live quality changes.
    table.fields = {{"max",1300},{"high",1000},{"medium",800},{"low",600}};
    owner.load(&space);
    owner.setStockFarPlaneOption(3, .1f);
    assert(owner.cameraClip == 600 && owner.farPlane_ == 1300);
    owner.setStockFarPlaneOption(0, .1f);
    assert(owner.cameraClip == 1300);
    // a35bf0..a35c7e supplies each missing child's own constructor default.
    table.fields = {{"max",1800}};
    owner.load(&space);
    owner.setStockFarPlaneOption(1, .1f);
    assert(owner.cameraClip == 800 && owner.farPlane_ == 1800);
    owner.setStockFarPlaneOption(2, .1f); assert(owner.cameraClip == 600);
    owner.setStockFarPlaneOption(3, .1f); assert(owner.cameraClip == 600);
    table.fields = {{"max",0},{"low",-.5f}};
    owner.load(&space); owner.setStockFarPlaneOption(3, .5f);
    assert(owner.cameraClip == 1 && owner.farPlane_ == 1);
    // Scalar environments, absent settings and empty children do not retain
    // the previous space's table; their original multiplier remains active.
    space.table = nullptr; space.fields = {{"farPlane",1200}};
    owner.load(&space); owner.setStockFarPlaneOption(3, .5f);
    assert(owner.cameraClip == 600 && owner.farPlane_ == 600);
    table.fields.clear(); space.table = &table;
    owner.load(&space); owner.setStockFarPlaneOption(2, .75f);
    assert(owner.cameraClip == 900);
    owner.load(nullptr); owner.setStockFarPlaneOption(0, .5f);
    assert(owner.cameraClip == 250);
    owner.setStockFarPlaneOption(0, -1.f); assert(owner.cameraClip == 1);
}
'''.replace('READ', PATCH.STOCK_FAR_PLANE_READ).replace('SELECT', PATCH.STOCK_FAR_PLANE_SELECT)
            path = root / 'probe.cpp'
            path.write_text(source)
            binary = root / 'probe'
            subprocess.run([compiler, '-std=c++14', '-Wall', '-Wextra', '-Werror',
                            str(path), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)


if __name__ == '__main__':
    unittest.main()
