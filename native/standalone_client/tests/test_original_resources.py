"""Original package ordering is retained without importing original mods."""

import importlib.util
from pathlib import Path
import os
import shutil
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'tools'))
from prepare_native_client import original_resource_paths


MOUNT_HOST = r'''
#include <cassert>
#include <algorithm>
#include <cctype>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>
namespace BW { using string = std::string; }
int bw_stricmp(const char* left, const char* right) {
    std::string a(left), b(right);
    for (char& c: a) c = std::tolower(static_cast<unsigned char>(c));
    for (char& c: b) c = std::tolower(static_cast<unsigned char>(c));
    return a.compare(b);
}
template<class T> struct Pointer {
    std::shared_ptr<T> value;
    Pointer() = default;
    Pointer(T* object):value(object) {}
    T* operator->() const { return value.get(); }
    explicit operator bool() const { return bool(value); }
};
struct ZipFileSystem;
using ZipFileSystemPtr = Pointer<ZipFileSystem>;
using FileSystemPtr = ZipFileSystemPtr;
std::map<std::string, std::set<std::string>> contents;
int liveArchives = 0, errors = 0;
struct ZipFileSystem {
    ZipFileSystemPtr parentZip_;
    ZipFileSystem* parentSystem_ = nullptr;
    std::string path, prefix, tag_;
    explicit ZipFileSystem(const std::string& file):path(file) { ++liveArchives; }
    ZipFileSystem(ZipFileSystemPtr archive, const std::string& directory):
        parentZip_(archive), path(archive->path), prefix(directory + "/"), tag_(directory) { ++liveArchives; }
    ~ZipFileSystem() { --liveArchives; }
    bool dirTest(const std::string& name) const {
        for (const auto& key: contents.at(path))
            if (key.compare(0, name.size() + 1, name + "/") == 0) return true;
        return false;
    }
    bool hasFile(const std::string& name) const { return contents.at(path).count(prefix + name); }
    bool internalChildNode() const { return bool(parentZip_); }
    std::string streamFile(const std::string& inPath) {
@STREAM@
        return hasFile(inPath) ? inPath : "";
    }
    std::string posixFileOpen(const std::string& path, const char* mode) {
@FILE@
        return hasFile(path) ? path + ":" + mode : "";
    }
    std::set<std::string> keys(const std::string& directory) const {
        std::set<std::string> result;
        const std::string start = prefix + directory + "/";
        for (const auto& key: contents.at(path)) {
            if (key.compare(0, start.size(), start) == 0)
                result.insert(key.substr(start.size()).substr(0, key.find('/', start.size()) - start.size()));
        }
        return result;
    }
};
struct BWResource {
    struct Extension { std::string value; std::string to_string() const { return value; } };
    static Extension getExtension(const std::string& path) { return {path.substr(path.rfind('.') + 1)}; }
    static std::vector<std::string> roots;
    static int getPathNum() { return static_cast<int>(roots.size()); }
    static std::string getPath(int i) { return roots.at(i); }
};
std::vector<std::string> BWResource::roots;
struct PyImportPaths {
    std::vector<std::string> paths;
    void addNonResPath(const std::string& path) { paths.push_back(path); }
    void addResPath(const BW::string& path) {
        int numPaths = BWResource::getPathNum();
        for (int i = 0; i < numPaths; ++i) {
@IMPORT@
        }
    }
};
#define CRITICAL_MSG(...) ++errors
FileSystemPtr mount(const std::string& pakPath) {
    FileSystemPtr baseFileSystem;
@DETECT@
@MOUNT@
    return baseFileSystem;
}
int main() {
    for (const std::string path: {"stock.pkg", "legacy.zip"}) {
        contents[path] = {"scripts/client/gui/mods/__init__.pyc", "gui/flash/stock.swf"};
        auto root = mount(path);
        assert(root && liveArchives == 1);
        assert(root->hasFile("gui/flash/stock.swf"));
        assert(root->keys("scripts/client/gui/mods") == std::set<std::string>{"__init__.pyc"});
    }
    assert(liveArchives == 0);
    for (const std::string path: {"ui.wotmod", "ui.WOTMOD"}) {
        contents[path] = {"meta.xml", "res/gui/flash/ui.swf", "res/scripts/client/gui/mods/mod_ui.pyc"};
        auto root = mount(path);
        assert(root && liveArchives == 2 && root->parentZip_);
        assert(root->hasFile("gui/flash/ui.swf"));
        assert(!root->hasFile("res/gui/flash/ui.swf") && !root->hasFile("meta.xml"));
        assert(root->keys("scripts/client/gui/mods") == std::set<std::string>{"mod_ui.pyc"});
        assert(root->parentZip_->hasFile("meta.xml"));
        assert(root->streamFile("gui/flash/ui.swf") == "res/gui/flash/ui.swf");
        assert(root->posixFileOpen("gui/flash/ui.swf", "rb") == "res/gui/flash/ui.swf:rb");
        assert(root->streamFile("missing.swf").empty());
        assert(root->posixFileOpen("missing.swf", "rb").empty());
    }
    assert(liveArchives == 0 && errors == 0);
    contents["missing-res.wotmod"] = {"meta.xml", "scripts/client/gui/mods/mod_wrong.pyc"};
    assert(!mount("missing-res.wotmod"));
    assert(liveArchives == 0 && errors == 1);
    BWResource::roots = {"private-res", "loose-ui", "ui.WOTMOD", "stock.pkg", "engine-res"};
    for (const std::string script: {"scripts/client", "scripts/common", "scripts/common/Lib"}) {
        PyImportPaths imports;
        imports.addResPath(script);
        const std::vector<std::string> expected = {"private-res/" + script,
            "loose-ui/" + script, "ui.WOTMOD/res/" + script,
            "stock.pkg/" + script, "engine-res/" + script};
        assert(imports.paths == expected);
    }
}
'''


class OriginalResourcesTests(unittest.TestCase):
    def package_patcher(self):
        source = ROOT / 'native/standalone_client/bigworld/patch_resource_packages.py'
        spec = importlib.util.spec_from_file_location('resource_patch', source)
        patcher = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(patcher)
        return patcher

    def read_fixtures(self, engine, patcher):
        path = engine / 'lib/resmgr/zip_file_system.cpp'
        path.write_text(patcher.STREAM_ORIGINAL + '\n' + patcher.FILE_ORIGINAL)
        import_path = engine / 'lib/pyscript/py_import_paths.cpp'
        import_path.parent.mkdir(parents=True)
        import_path.write_text('#include "py_import_paths.hpp"\n' + patcher.IMPORT_ORIGINAL)
        return path

    def test_hd_packages_keep_authored_precedence_and_original_root(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            packages = root / 'res/packages'
            packages.mkdir(parents=True)
            for name in ('shared_content.pkg', 'shared_content_hd.pkg', 'scripts.pkg'):
                (packages / name).touch()
            (root / 'paths.xml').write_text('''<root><Paths>
              <Path>./res_mods/0.9.22.0.1</Path>
              <Path mode="recursive" mask="*.wotmod">./mods/0.9.22.0.1</Path>
              <Path type="sd,hd">./res/packages/shared_content.pkg</Path>
              <Path type="hd">./res/packages/shared_content_hd.pkg</Path>
              <Path type="sandbox">./res/packages/uninstalled_sandbox.pkg</Path>
              <Path type="sandbox,sd,hd">./res/packages/scripts.pkg</Path>
              <Path>./res</Path>
            </Paths></root>''')
            self.assertEqual(original_resource_paths(root), [
                packages / 'shared_content.pkg', packages / 'shared_content_hd.pkg',
                packages / 'scripts.pkg', root / 'res'])

    def test_missing_authored_package_is_not_silently_dropped(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            (root / 'res/packages').mkdir(parents=True)
            (root / 'paths.xml').write_text('''<root><Paths>
              <Path type="hd">./res/packages/scripts.pkg</Path>
            </Paths></root>''')
            with self.assertRaisesRegex(ValueError, 'Missing authored HD package'):
                original_resource_paths(root)

    def test_scripts_from_an_unrelated_directory_are_not_original_content(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            (root / 'other').mkdir()
            (root / 'other/scripts.pkg').touch()
            (root / 'paths.xml').write_text('''<root><Paths>
              <Path>./other/scripts.pkg</Path>
            </Paths></root>''')
            with self.assertRaisesRegex(ValueError, 'do not contain scripts.pkg'):
                original_resource_paths(root)

    def test_native_package_patch_preserves_zip_path_and_is_repeatable(self):
        patcher = self.package_patcher()
        with tempfile.TemporaryDirectory() as temporary:
            engine = Path(temporary)
            target = engine / 'lib/resmgr/bwresource.cpp'
            target.parent.mkdir(parents=True)
            target.write_text('before\n\t\t' + patcher.ORIGINAL +
                              patcher.MOUNT_ORIGINAL + '\nafter\n')
            zip_target = self.read_fixtures(engine, patcher)
            patcher.patch(engine)
            expected = target.read_text()
            expected_zip = zip_target.read_text()
            patcher.patch(engine)
            self.assertEqual(target.read_text(), expected)
            self.assertEqual(zip_target.read_text(), expected_zip)
            self.assertIn('== PACKED_EXTENSION ||', expected)
            self.assertIn('== "pkg" ||', expected)
            self.assertIn('isWotMod) &&', expected)
            self.assertTrue(expected.endswith('after\n'))

    def test_native_package_patch_upgrades_previous_pkg_support(self):
        patcher = self.package_patcher()
        with tempfile.TemporaryDirectory() as temporary:
            target = Path(temporary) / 'lib/resmgr/bwresource.cpp'
            target.parent.mkdir(parents=True)
            target.write_text(patcher.PKG_REPLACEMENT + patcher.MOUNT_ORIGINAL)
            self.read_fixtures(Path(temporary), patcher)
            patcher.patch(Path(temporary))
            self.assertIn(patcher.REPLACEMENT, target.read_text())
            self.assertIn(patcher.MOUNT_REPLACEMENT, target.read_text())

    def test_native_package_patch_refuses_changed_mount_without_partial_write(self):
        patcher = self.package_patcher()
        with tempfile.TemporaryDirectory() as temporary:
            target = Path(temporary) / 'lib/resmgr/bwresource.cpp'
            target.parent.mkdir(parents=True)
            target.write_text(patcher.ORIGINAL + 'a changed archive mount')
            original = target.read_text()
            with self.assertRaisesRegex(ValueError, 'package dispatch has changed'):
                patcher.patch(Path(temporary))
            self.assertEqual(target.read_text(), original)

    def test_native_package_patch_refuses_changed_child_reads_without_partial_write(self):
        patcher = self.package_patcher()
        with tempfile.TemporaryDirectory() as temporary:
            engine = Path(temporary)
            target = engine / 'lib/resmgr/bwresource.cpp'
            target.parent.mkdir(parents=True)
            target.write_text(patcher.ORIGINAL + patcher.MOUNT_ORIGINAL)
            original = target.read_text()
            zip_target = self.read_fixtures(engine, patcher)
            zip_target.write_text(patcher.STREAM_ORIGINAL + '\na changed file read')
            original_zip = zip_target.read_text()
            with self.assertRaisesRegex(ValueError, 'child read contract has changed'):
                patcher.patch(engine)
            self.assertEqual(target.read_text(), original)
            self.assertEqual(zip_target.read_text(), original_zip)

    def test_wotmod_child_mount_retains_parent_and_exposes_res_resources(self):
        # Execute the patched mount against a reference-counted filesystem host.
        # This verifies dispatch and ownership, not BigWorld's ZIP implementation.
        compiler = shutil.which('c++')
        if not compiler:
            self.skipTest('a C++ compiler is required')
        patcher = self.package_patcher()
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            source = folder / 'mount.cpp'
            source.write_text(MOUNT_HOST.replace('@MOUNT@', patcher.MOUNT_REPLACEMENT)
                              .replace('@DETECT@', patcher.WOTMOD_DECLARATION)
                              .replace('@STREAM@', patcher.STREAM_REPLACEMENT)
                              .replace('@FILE@', patcher.FILE_REPLACEMENT)
                              .replace('@IMPORT@', patcher.IMPORT_REPLACEMENT))
            result = subprocess.run([compiler, '-std=c++14', str(source), '-o', str(folder / 'mount')],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            subprocess.run([str(folder / 'mount')], check=True)

    def test_pinned_engine_accepts_complete_package_patch(self):
        engine = os.environ.get('WOT_BIGWORLD_SOURCE')
        if not engine:
            self.skipTest('WOT_BIGWORLD_SOURCE is required')
        patcher = self.package_patcher()
        with tempfile.TemporaryDirectory() as temporary:
            target = Path(temporary) / 'lib/resmgr/bwresource.cpp'
            target.parent.mkdir(parents=True)
            shutil.copyfile(Path(engine) / 'lib/resmgr/bwresource.cpp', target)
            zip_target = target.with_name('zip_file_system.cpp')
            shutil.copyfile(Path(engine) / 'lib/resmgr/zip_file_system.cpp', zip_target)
            import_target = Path(temporary) / 'lib/pyscript/py_import_paths.cpp'
            import_target.parent.mkdir()
            shutil.copyfile(Path(engine) / 'lib/pyscript/py_import_paths.cpp', import_target)
            patcher.patch(Path(temporary))
            first = target.read_text()
            first_zip = zip_target.read_text()
            first_import = import_target.read_text()
            patcher.patch(Path(temporary))
            self.assertEqual(target.read_text(), first)
            self.assertEqual(zip_target.read_text(), first_zip)
            self.assertEqual(import_target.read_text(), first_import)
            self.assertIn(patcher.MOUNT_REPLACEMENT, first)
            self.assertIn(patcher.STREAM_REPLACEMENT, first_zip)
            self.assertIn(patcher.FILE_REPLACEMENT, first_zip)
            self.assertIn(patcher.IMPORT_REPLACEMENT, first_import)
            self.assertEqual(first_import.count('#include "cstdmf/string_utils.hpp"'), 1)


if __name__ == '__main__':
    unittest.main()
