"""Keep a retained script output writer safe across Python finalization.

The Windows b730f9b7 exit dump has Py_IsInitialized()==0 and a writer with
reference count 2 while PythonConsole::~PythonConsole calls delHook. This
host compiles the SDK hook, writer and console methods and uses real CPython
2.7 to exercise the same boundary; it does not claim GUI exit acceptance.
"""
from pathlib import Path
import os
import shlex
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'bigworld'))
import patch_stock_console_lifecycle

FILES = ('lib/romp/console.hpp', 'lib/romp/console.cpp', 'client/canvas_app.cpp',
         'lib/script/script_output_hook.cpp', 'lib/script/py_script_output_writer.cpp',
         'client/debug_app.cpp', 'lib/romp/engine_statistics.hpp')


def function(source, signature):
    begin = source.index(signature)
    cursor = source.index('{', begin)
    depth = 1
    while depth:
        cursor += 1
        depth += (source[cursor] == '{') - (source[cursor] == '}')
    return source[begin:cursor + 1]


HOST = r'''
#include <Python.h>
#include <algorithm>
#include <cassert>
#include <list>
#define BW_GUARD
#define MF_ASSERT(expression) assert(expression)
#define ERROR_MSG(...) assert(false)
#define ENABLE_CONSOLES 1
namespace BW {
struct ScriptOutputWriter;
struct ScriptOutputHook {
 virtual ~ScriptOutputHook() {}
 virtual void onOutputWriterDestroyed(ScriptOutputWriter*)=0;
 static ScriptOutputWriter* hookScriptOutput(ScriptOutputHook*);
 static void unhookScriptOutput(ScriptOutputWriter*,ScriptOutputHook*);
};
struct ScriptOutputWriter {
 typedef std::list<ScriptOutputHook*> Hooks;
 Hooks hooks_;
 ScriptOutputHook* pCurrentHook_=NULL;
 bool shouldDeleteCurrentHook_=false;
 static ScriptOutputWriter* current;
 static ScriptOutputWriter* getCurrentStdio() { assert(Py_IsInitialized()); return current; }
 ~ScriptOutputWriter();
 void addHook(ScriptOutputHook*);
 void delHook(ScriptOutputHook*);
};
ScriptOutputWriter* ScriptOutputWriter::current=NULL;
struct PythonConsole:ScriptOutputHook {
 bool isMultiline_;
 int autoCompIndex_;
 ScriptOutputWriter* pHookedWriter_;
 PythonConsole();
 ~PythonConsole();
 void finiScriptOutput();
 void onOutputWriterDestroyed(ScriptOutputWriter*);
 void start() { pHookedWriter_=hookScriptOutput(this); }
};
struct ConsoleManager {
 PythonConsole* console=NULL;
 static ConsoleManager& instance() { static ConsoleManager manager; return manager; }
 PythonConsole* find(const char*) { return console; }
};
struct EnviroMinder { static void fini() {} };
template<class T> void bw_safe_delete(T*& pointer) { delete pointer; pointer=NULL; }
struct CanvasApp { int* distortion_=NULL; void fini(); };
@METHODS@
}
int main(int argc,char**) {
 using namespace BW;
 Py_Initialize();
 // Partial startup has no Python console yet.
 CanvasApp canvas;canvas.fini();
 // The writer may retire before the console, clearing its borrowed pointer.
 {
  ScriptOutputWriter writer;
  ScriptOutputWriter::current=&writer;
  PythonConsole console;console.start();
  assert(writer.hooks_.size()==1);
  console.finiScriptOutput();console.finiScriptOutput();
  assert(writer.hooks_.empty() && !console.pHookedWriter_);
 }
 PythonConsole* detached=new PythonConsole;
 {
  ScriptOutputWriter writer;
  ScriptOutputWriter::current=&writer;detached->start();
 }
 assert(!detached->pHookedWriter_);delete detached;
 // A real retained writer outlives Py_Finalize, as in the captured dump.
 ScriptOutputWriter writer;
 ScriptOutputWriter::current=&writer;
 PythonConsole* console=new PythonConsole;console->start();
 ConsoleManager::instance().console=console;
 assert(writer.hooks_.size()==1);
 if(argc==1) {
  canvas.fini();
  assert(Py_IsInitialized() && writer.hooks_.empty());
 }
 Py_Finalize();
 assert(!Py_IsInitialized());
 delete console;
 ConsoleManager::instance().console=NULL;
 assert(writer.hooks_.empty());
}
'''


class StockConsoleLifecycleTest(unittest.TestCase):
    def sources(self):
        root = Path(os.environ.get('WOT_BIGWORLD_ROOT',
            '/tmp/wot-bigworld-engine-14.4.1/programming/bigworld'))
        if not all((root / name).exists() for name in FILES):
            self.skipTest('Pinned public engine source required')
        return {name: (root / name).read_text() for name in FILES}

    def test_patch_is_atomic_idempotent_and_rejects_drift(self):
        sources = self.sources()
        with tempfile.TemporaryDirectory(prefix='console-lifecycle-patch-') as directory:
            root = Path(directory)
            for name, text in sources.items():
                (root / name).parent.mkdir(parents=True, exist_ok=True)
                (root / name).write_text(text)
            patch_stock_console_lifecycle.patch(root)
            patched = {name: (root / name).read_text() for name in FILES}
            patch_stock_console_lifecycle.patch(root)
            self.assertEqual(patched, {name: (root / name).read_text() for name in FILES})
            for name, text in sources.items():
                (root / name).write_text(text)
            path = root / 'client/canvas_app.cpp'
            path.write_text(path.read_text().replace('void CanvasApp::fini()', 'void CanvasApp::retire()'))
            before = {name: (root / name).read_text() for name in FILES}
            with self.assertRaisesRegex(RuntimeError, 'canvas_app'):
                patch_stock_console_lifecycle.patch(root)
            self.assertEqual(before, {name: (root / name).read_text() for name in FILES})
            for name, text in sources.items():
                (root / name).write_text(text)
            path = root / 'client/debug_app.cpp'
            path.write_text(path.read_text().replace('"Debug/Log slow frames"',
                                                    '"Debug/Unexpected slow frames"'))
            before = {name: (root / name).read_text() for name in FILES}
            with self.assertRaisesRegex(RuntimeError, 'debug_app'):
                patch_stock_console_lifecycle.patch(root)
            self.assertEqual(before, {name: (root / name).read_text() for name in FILES})

    def test_console_watcher_compiles_with_watchers_and_both_console_modes(self):
        sources = self.sources()
        with tempfile.TemporaryDirectory(prefix='console-watcher-host-') as directory:
            root = Path(directory)
            for name, text in sources.items():
                (root / name).parent.mkdir(parents=True, exist_ok=True)
                (root / name).write_text(text)
            patch_stock_console_lifecycle.patch(root)
            original = sources['client/debug_app.cpp']
            patched = (root / 'client/debug_app.cpp').read_text()
            before = patch_stock_console_lifecycle.SLOW_FRAME_WATCHER
            after = patch_stock_console_lifecycle.GUARDED_SLOW_FRAME_WATCHER
            self.assertEqual(patched.replace(after, before), original)
            self.assertEqual(patched.count('"Debug/Log slow frames"'), 1)
            # Include neighboring registrations so disabling consoles cannot
            # silently disable otherwise available watchers.
            begin = patched.index('    MF_WATCH( "Debug/Break on critical"')
            end = patched.index('\n#if ENABLE_CONSOLES\n    // 6. Consoles', begin)
            registrations = patched[begin:end]
            # Use the real SDK header: EngineStatistics does not exist at all
            # with ENABLE_CONSOLES=0, as in the failing Consumer build.
            console = root / 'lib/romp/console.hpp'
            console.write_text('struct XConsole {};\nstruct StatisticsConsole { '
                               'struct Handler {}; };\n')
            host = r'''
#include <cassert>
#include <string>
#include <vector>
#define BW_BEGIN_NAMESPACE
#define BW_END_NAMESPACE
#include "romp/engine_statistics.hpp"
#if ENABLE_CONSOLES
bool EngineStatistics::logSlowFrames_ = false;
#endif
struct Watcher { enum { WT_READ_WRITE }; };
std::vector<std::string> registered;
void registerWatcher(const char* name, bool&, int, const char*) {
    registered.push_back(name);
}
#if ENABLE_WATCHERS
#define MF_WATCH(...) registerWatcher(__VA_ARGS__)
#else
#error This regression requires enabled watchers
#endif
int main() {
    bool shouldBreakOnCritical_=false, shouldAddTimePrefix_=false;
@REGISTRATIONS@
    assert(registered.size() == (ENABLE_CONSOLES ? 3u : 2u));
    assert(registered[0] == "Debug/Break on critical");
    assert(registered[1] == "Debug/Add time prefix");
#if ENABLE_CONSOLES
    assert(registered[2] == "Debug/Log slow frames");
#endif
}
'''
            source = root / 'watcher.cpp'
            source.write_text(host.replace('@REGISTRATIONS@', registrations))
            for enabled in (0, 1):
                with self.subTest(consoles=enabled):
                    flags = ['c++', '-std=c++14', '-DENABLE_WATCHERS=1',
                             '-DENABLE_CONSOLES=%d' % enabled, '-I', str(root / 'lib')]
                    preprocessed = subprocess.check_output(
                        flags + ['-E', '-P', str(source)], text=True)
                    if not enabled:
                        self.assertNotIn('EngineStatistics', preprocessed)
                    subprocess.run(flags + [str(source), '-o', str(root / 'watcher')],
                                   check=True)
                    subprocess.run([str(root / 'watcher')], check=True)
            # The exact unguarded registration must still reproduce the
            # missing-type failure, rather than being swallowed by a fake.
            source.write_text(host.replace('@REGISTRATIONS@',
                                           registrations.replace(after, before)))
            negative = subprocess.run(['c++', '-std=c++14', '-DENABLE_WATCHERS=1',
                '-DENABLE_CONSOLES=0', '-I', str(root / 'lib'), '-fsyntax-only',
                str(source)], capture_output=True, text=True)
            self.assertNotEqual(negative.returncode, 0)
            self.assertIn('EngineStatistics', negative.stderr)

    def test_sdk_hooks_detach_before_real_python_finalization(self):
        sources = self.sources()
        python = Path(os.environ.get('WOT_PYTHON27', '/Users/peng/.pyenv/versions/2.7.18/bin/python2.7'))
        config = python.with_name('python2.7-config')
        if not config.exists():
            self.skipTest('CPython 2.7 development files required')
        with tempfile.TemporaryDirectory(prefix='console-lifecycle-host-') as directory:
            root = Path(directory)
            for name, text in sources.items():
                (root / name).parent.mkdir(parents=True, exist_ok=True)
                (root / name).write_text(text)
            patch_stock_console_lifecycle.patch(root)
            methods = []
            for name, signatures in {
                'lib/romp/console.cpp': ('PythonConsole::PythonConsole()', 'PythonConsole::~PythonConsole()',
                    'void PythonConsole::finiScriptOutput()', 'void PythonConsole::onOutputWriterDestroyed('),
                'lib/script/script_output_hook.cpp': ('ScriptOutputWriter * ScriptOutputHook::hookScriptOutput(',
                    'void ScriptOutputHook::unhookScriptOutput('),
                'lib/script/py_script_output_writer.cpp': ('ScriptOutputWriter::~ScriptOutputWriter()',
                    'void ScriptOutputWriter::addHook(', 'void ScriptOutputWriter::delHook('),
                'client/canvas_app.cpp': ('void CanvasApp::fini()',),
            }.items():
                source = (root / name).read_text()
                methods.extend(function(source, signature) for signature in signatures)
            (root / 'host.cpp').write_text(HOST.replace('@METHODS@', '\n'.join(methods)))
            flags = [flag for flag in shlex.split(subprocess.check_output([str(config), '--cflags'], text=True))
                     if flag != '-DNDEBUG']
            libs = shlex.split(subprocess.check_output([str(config), '--ldflags'], text=True))
            subprocess.run(['c++', '-std=c++14', '-UNDEBUG', '-Wno-register', *flags,
                str(root / 'host.cpp'), *libs, '-o', str(root / 'host')], check=True)
            subprocess.run([str(root / 'host')], check=True)
            old = subprocess.run([str(root / 'host'), 'omit-canvas-retirement'], capture_output=True, text=True)
            self.assertNotEqual(old.returncode, 0)
            self.assertIn('Py_IsInitialized()', old.stderr)


if __name__ == '__main__':
    unittest.main()
