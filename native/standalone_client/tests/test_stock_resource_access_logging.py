"""The actual frame owner must not arm per-package file-access diagnostics."""
import importlib.util
import os
from pathlib import Path
import subprocess
import tarfile
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location(
    'resource_access_logging', ROOT / 'bigworld/patch_stock_resource_access_logging.py')
patcher = importlib.util.module_from_spec(spec)
spec.loader.exec_module(patcher)


def body(source, signature):
    start = source.index(signature)
    end = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


HOST = r'''
#include <string>
#define BW_GUARD
#define BW_GUARD_PROFILER(x)
#define WARNING_MSG(...) ++warnings
static unsigned warnings=0, presented=0, ended=0;
static bool gWorldDrawEnabled=false, g_doneLoading=false;
struct DogWatch { DogWatch(const char*) {} void start() {} void stop() {} };
struct Pipeline { void end() { ++ended; } };
struct Renderer { static Renderer& instance() { static Renderer r; return r; }
 Pipeline* pipeline() { static Pipeline p; return &p; } };
namespace Moo { struct RC { void endScene() {} void present() { ++presented; } };
 RC& rc() { static RC r; return r; } }
struct BWOpenAutomate { static const bool s_runningBenchmark=false; };
using oaFloat=float;
void oaDisplayFrame(float) {}
struct App { static App& instance() { static App a; return a; }
 float getRenderTimeFrameStart() { return 0; } };
struct StringRef { std::string to_string() const { return "resource"; } };
namespace BW { using StringRef=::StringRef; }
struct BWResource {
 static void watchAccessFromCallingThread(bool);
 static bool watchAccessFromCallingThread();
 static void checkAccessFromCallingThread(const StringRef&, const char*);
};
struct FinaleApp { void draw(); };
'''

MAIN = r'''
int main() {
 FinaleApp app;
 for (bool world : {false, true, true, false}) {
  gWorldDrawEnabled=world;
  BWResource::watchAccessFromCallingThread(false);
  app.draw();
  BWResource::checkAccessFromCallingThread(StringRef(), "probe");
  if (BWResource::watchAccessFromCallingThread() || warnings) return 51;
 }
 // An explicitly armed diagnostic still works and is not silently disabled.
 BWResource::watchAccessFromCallingThread(true);
 app.draw();
 BWResource::checkAccessFromCallingThread(StringRef(), "probe");
 if (!BWResource::watchAccessFromCallingThread() || warnings!=1) return 52;
 BWResource::watchAccessFromCallingThread(false);
 return (presented==5 && ended==5) ? 0 : 53;
}
'''


class ResourceAccessLoggingTests(unittest.TestCase):
    def test_actual_frame_owner_and_explicit_diagnostic(self):
        archive = Path(os.environ.get('WOT_PUBLIC_ENGINE_ARCHIVE',
                                     '/tmp/wot-bigworld-engine-14.4.1.tar.gz'))
        if not archive.is_file():
            self.skipTest('Pinned public SDK archive required')
        sources = {}
        with tarfile.open(archive) as tar:
            for member in tar:
                for name in ('client/finale_app.cpp', 'lib/resmgr/bwresource.cpp'):
                    if member.name.endswith('/programming/bigworld/' + name):
                        sources[name] = tar.extractfile(member).read().decode('utf-8-sig')
        resource = sources['lib/resmgr/bwresource.cpp']
        methods = '\n'.join(body(resource, signature) for signature in (
            'void BWResource::watchAccessFromCallingThread(bool watch)',
            'bool BWResource::watchAccessFromCallingThread()',
            'void BWResource::checkAccessFromCallingThread('))
        with tempfile.TemporaryDirectory(prefix='resource-access-owner-') as temp:
            root = Path(temp)
            target = root / 'client/finale_app.cpp'
            target.parent.mkdir()
            target.write_text(sources['client/finale_app.cpp'])
            before = target.read_text()
            patcher.patch(root)
            after = target.read_text()
            patcher.patch(root)
            self.assertEqual(after, target.read_text())
            for source, expected in ((before, 51), (after, 0)):
                cpp, binary = root / 'host.cpp', root / 'host'
                cpp.write_text(HOST + methods + body(source, 'void FinaleApp::draw()') + MAIN)
                result = subprocess.run(['c++', '-std=c++14', '-fsanitize=address,undefined',
                                         str(cpp), '-o', str(binary)], capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stderr)
                result = subprocess.run([str(binary)], capture_output=True, text=True)
                self.assertEqual(result.returncode, expected, result.stdout + result.stderr)
            target.write_text(before.replace(patcher.BEFORE, 'changed upstream seam'))
            broken = target.read_bytes()
            with self.assertRaises(ValueError):
                patcher.patch(root)
            self.assertEqual(broken, target.read_bytes())


if __name__ == '__main__':
    unittest.main()
