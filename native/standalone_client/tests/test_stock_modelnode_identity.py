"""Run the patched SDK getters and one-shot diagnostic without any providers."""
import importlib.util
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location(
    'modelnode_identity_patch', ROOT / 'bigworld/patch_stock_modelnode_identity.py')
patcher = importlib.util.module_from_spec(spec)
spec.loader.exec_module(patcher)


def method(text, name):
    start = text.index(name)
    opening = text.index('{', start)
    level = 1
    end = opening + 1
    while level:
        level += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]


HOST = r'''
#include <cassert>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>
#define ENABLE_TRANSFORM_VALIDATION 1
#define BW_GUARD
using uint32 = uint32_t;
namespace BW { using string = std::string; }
struct Matrix { int value = 73; };
static std::vector<std::string> records;
static std::function<void()> onIdentity;
static void warning(const char* format, ...) {
    char text[2048];
    va_list args; va_start(args, format);
    vsnprintf(text, sizeof(text), format, args); va_end(args);
    records.emplace_back(text);
    if (records.back().find("staleIdentity:") != std::string::npos && onIdentity) {
        auto callback = onIdentity; onIdentity = nullptr; callback();
    }
}
#define WARNING_MSG warning
namespace Moo {
struct Context { uint32 stamp = 0; uint32 frameTimestamp() const { return stamp; } };
static Context context;
static Context& rc() { return context; }
}
struct PyModel {
    mutable unsigned reads = 0;
    BW::string resource = "vehicles/test.model";
    bool inWorld = true, attached = true;
    BW::string name() const { ++reads; return resource; }
    bool isInWorld() const { return inWorld; }
    bool isAttached() const { return attached; }
    // Deliberately no worldTransform()/matrix() API: diagnostics cannot sample it.
};
struct Node { BW::string identifier() const { return "HP_gunFire"; } };
struct NodePtr {
    Node* value;
    Node* operator->() const { return value; }
    Node* getObject() const { return value; }
};
struct PyModelNode {
    PyModel *pOwner_;
    NodePtr pNode_;
    bool lodded_ = true;
    Matrix lastWorldTransform_;
@FIELDS@
    static bool disabled;
    static bool disableFrameBehindWarning() { return disabled; }
    PyModelNode(PyModel* owner, Node* node) : pOwner_(owner), pNode_{node},
        frameTimestamp_(0), lastLatchTimestamp_(0), reportedStaleIdentity_(false) {}
    void matrix(Matrix&) const;
    const Matrix& getMatrix() const;
};
bool PyModelNode::disabled = false;
@HELPER@
@MATRIX@
@GETMATRIX@
static unsigned count(const char* key) {
    unsigned value = 0;
    for (const auto& text : records) value += text.find(key) != std::string::npos;
    return value;
}
int main() {
    PyModel owner; Node node; PyModelNode modelNode(&owner, &node); Matrix output;
    const auto read = [&]() { modelNode.matrix(output); modelNode.getMatrix(); };
    read(); assert(records.empty());
    Moo::context.stamp = 1; read();
    assert(count("staleIdentity:") == 0 && records.size() == 2 && owner.reads == 0);
    Moo::context.stamp = 2;
    onIdentity = [&]() { modelNode.getMatrix(); };
    read(); read();
    assert(count("staleIdentity:") == 1 && count("frames behind") == 7);
    assert(owner.reads == 1 && output.value == 73);
    assert(modelNode.lastLatchTimestamp_ == 0 && modelNode.frameTimestamp_ == 0);
    assert(modelNode.lastWorldTransform_.value == 73);
    assert(records[2].find("current=2 lastLatch=0 lastAnimation=0 age=2") != std::string::npos);
    assert(records[2].find("model=\"vehicles/test.model\"") != std::string::npos);
    assert(records[2].find("name=\"HP_gunFire\"") != std::string::npos);

    // A retained detached node has no live owner to dereference, even if once latched.
    PyModelNode detached(nullptr, &node);
    detached.lastLatchTimestamp_ = 900; detached.frameTimestamp_ = 900;
    Moo::context.stamp = 4000; detached.getMatrix(); detached.matrix(output);
    assert(count("staleIdentity:") == 2 && owner.reads == 1);
    assert(records[8].find("model=\"<detached>\"") != std::string::npos);
    assert(records[8].find("lastLatch=900 lastAnimation=900 age=3100") != std::string::npos);
    assert(records[8].find("inWorld=0 attached=0") != std::string::npos);

    // Unsigned frame wrap retains the SDK's modular stamp difference.
    PyModelNode wrapped(&owner, &node);
    wrapped.lastLatchTimestamp_ = UINT32_MAX;
    Moo::context.stamp = 0; wrapped.getMatrix();
    assert(count("staleIdentity:") == 2);
    Moo::context.stamp = 1; wrapped.matrix(output);
    assert(count("staleIdentity:") == 3);
    assert(records[12].find("lastLatch=4294967295 lastAnimation=0 age=2") != std::string::npos);

    PyModelNode guarded(&owner, &node);
    PyModelNode::disabled = true; Moo::context.stamp = 9000;
    guarded.getMatrix(); guarded.matrix(output);
    assert(!guarded.reportedStaleIdentity_);
    PyModelNode::disabled = false;
    owner.resource.clear(); owner.inWorld = false; owner.attached = false;
    guarded.getMatrix(); assert(count("staleIdentity:") == 4);
    // A newly constructed instance is diagnosed independently, without a global pointer set.
    { PyModelNode fresh(nullptr, &node); fresh.matrix(output); }
    { PyModelNode fresh(nullptr, &node); fresh.getMatrix(); }
    assert(count("staleIdentity:") == 6);
}
'''


class ModelNodeIdentityTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        engine = Path(os.environ.get('WOT_BW_ENGINE_SOURCE',
            '/tmp/wot-bigworld-engine-14.4.1/programming/bigworld'))
        cls.original = {}
        cls.patched = {}
        for name in (patcher.HEADER, patcher.SOURCE):
            path = engine / name
            if not path.is_file():
                if 'WOT_BW_ENGINE_SOURCE' in os.environ:
                    raise AssertionError('Configured SDK source missing: ' + name)
                raise unittest.SkipTest('Pinned SDK source required')
            cls.original[name] = path.read_text(encoding='utf-8-sig')
            cls.patched[name] = patcher.patch_text(cls.original[name], name)

    def test_actual_getters_and_diagnostic_lifetime_wrap_and_reentry(self):
        compiler = shlex.split(os.environ.get('CXX', 'c++'))
        if not compiler or not shutil.which(compiler[0]):
            self.skipTest('Requires a C++ compiler')
        source = self.patched[patcher.SOURCE]
        host = HOST.replace('@FIELDS@', patcher.NEW_FIELDS)
        for token, name in [('@HELPER@', 'void PyModelNode::reportStaleIdentity('),
                            ('@MATRIX@', 'void PyModelNode::matrix('),
                            ('@GETMATRIX@', 'const Matrix & PyModelNode::getMatrix(')]:
            host = host.replace(token, method(source, name))
        with tempfile.TemporaryDirectory() as directory:
            cpp, binary = Path(directory) / 'host.cpp', Path(directory) / 'host'
            cpp.write_text(host)
            result = subprocess.run(compiler + ['-std=c++11', '-Wall', '-Wextra', '-Werror',
                '-fsanitize=address,undefined', str(cpp), '-o', str(binary)],
                capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)

    def test_original_warnings_and_stamps_preserved_and_patch_idempotent(self):
        for name, original in self.original.items():
            patched = self.patched[name]
            self.assertEqual(patcher.patch_text(patched, name), patched)
            if name == patcher.HEADER:
                self.assertEqual(patched.replace(patcher.NEW_FIELDS, patcher.FIELDS), original)
            else:
                restored = patched.replace(patcher.NEW_INIT, patcher.INIT).replace(patcher.HELPER, '')
                for reader in ('matrix', 'getMatrix'):
                    restored = restored.replace('\t\tthis->reportStaleIdentity( "%s", Moo::rc().frameTimestamp() );\n' % reader, '')
                self.assertEqual(restored, original)
                helper = method(patched, 'void PyModelNode::reportStaleIdentity(')
                self.assertNotIn('worldTransform(', helper)
                self.assertNotIn('->matrix(', helper)
                self.assertNotIn('lastLatchTimestamp_ =', helper)
                self.assertNotIn('frameTimestamp_ =', helper)

    def test_partial_or_drifted_diagnostics_rejected_and_guard_composes(self):
        mutations = [
            (patcher.HEADER, self.patched[patcher.HEADER].replace('mutable bool reportedStaleIdentity_;', 'mutable int reportedStaleIdentity_;')),
            (patcher.SOURCE, self.patched[patcher.SOURCE].replace('reportedStaleIdentity_( false )', 'reportedStaleIdentity_( true )')),
            (patcher.SOURCE, self.patched[patcher.SOURCE].replace('age <= 1', 'age <= 3')),
            (patcher.SOURCE, self.patched[patcher.SOURCE].replace('reportStaleIdentity( "matrix", Moo', 'reportStaleIdentity( "matrix", Changed')),
            (patcher.SOURCE, self.original[patcher.SOURCE] + self.original[patcher.SOURCE])]
        for name, text in mutations:
            with self.subTest(name=name, text=text[-80:]), self.assertRaises(ValueError):
                patcher.patch_text(text, name)
        guard_spec = importlib.util.spec_from_file_location('nested_guard_patch',
            ROOT / 'bigworld/patch_stock_frame_behind_guard.py')
        guard = importlib.util.module_from_spec(guard_spec)
        guard_spec.loader.exec_module(guard)
        self.assertEqual(guard.patch_text(self.patched[patcher.HEADER]),
            patcher.patch_text(guard.patch_text(self.original[patcher.HEADER]), patcher.HEADER))

    def test_file_encoding_and_validation_before_any_write(self):
        with tempfile.TemporaryDirectory() as directory:
            paths = {}
            for name, text in self.original.items():
                path = Path(directory) / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(b'\xef\xbb\xbf' + text.replace('\n', '\r\n').encode())
                path.chmod(0o640)
                paths[name] = path
            source = paths[patcher.SOURCE]
            source.write_bytes(source.read_bytes().replace(b'lastLatchTimestamp_( 0 )', b'lastLatchTimestamp_( 1 )'))
            before = {name: path.read_bytes() for name, path in paths.items()}
            with self.assertRaises(ValueError):
                patcher.patch(directory)
            self.assertEqual({name: path.read_bytes() for name, path in paths.items()}, before)
            source.write_bytes(b'\xef\xbb\xbf' + self.original[patcher.SOURCE].replace('\n', '\r\n').encode())
            self.assertTrue(patcher.patch(directory))
            self.assertFalse(patcher.patch(directory))
            for name, path in paths.items():
                self.assertEqual(path.read_bytes(), b'\xef\xbb\xbf' + self.patched[name].replace('\n', '\r\n').encode())
                if os.name != 'nt':
                    self.assertEqual(path.stat().st_mode & 0o777, 0o640)


if __name__ == '__main__':
    unittest.main()
