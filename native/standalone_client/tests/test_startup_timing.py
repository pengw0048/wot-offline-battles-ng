"""Startup timing retains early evidence without changing task failure semantics."""

import importlib.util
import os
from pathlib import Path
import subprocess
import tarfile
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
ARCHIVE = Path(os.environ.get('WOT_PUBLIC_ENGINE_ARCHIVE', '/tmp/wot-bigworld-engine-14.4.1.tar.gz'))


def load(name):
    spec = importlib.util.spec_from_file_location(name, ROOT / 'bigworld' / (name + '.py'))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def method(source, signature):
    start = source.index(signature)
    end = source.index('{', start)
    depth = 1
    while depth:
        end += 1
        depth += (source[end] == '{') - (source[end] == '}')
    return source[start:end + 1]


class StartupTimingTests(unittest.TestCase):
    def test_pinned_patch_and_native_nested_failure_accounting(self):
        if not ARCHIVE.exists():
            self.skipTest('Pinned public engine source archive is required')
        with tempfile.TemporaryDirectory(prefix='startup-timing-') as directory:
            root = Path(directory)
            relatives = ('client/app.cpp', 'client/finale_app.cpp', 'lib/cstdmf/main_loop_task.cpp',
                         'lib/moo/effect_compiler.cpp', 'lib/moo/effect_compiler.hpp',
                         'lib/moo/effect_manager.cpp', 'lib/moo/managed_effect.cpp',
                         'lib/moo/effect_helpers.cpp')
            with tarfile.open(ARCHIVE) as archive:
                names = archive.getnames()
                for relative in relatives:
                    member = next(name for name in names if name.endswith('/programming/bigworld/' + relative))
                    path = root / relative
                    path.parent.mkdir(parents=True, exist_ok=True)
                    path.write_bytes(archive.extractfile(member).read())
            load('patch_shader_compiler').patch_engine(root)
            load('patch_stock_effect_owners').patch_resource_names(root)
            load('patch_startup_timing').patch(root)
            app = (root / 'client/app.cpp').read_text()
            self.assertLess(app.index('if (!initLoggers())'), app.index('wot_startup::finish()'))
            self.assertLess(app.index('wot_startup::finish()'), app.index('RecreateDeviceCallback::createInstance();'))
            compiler = (root / 'lib/moo/effect_compiler.cpp').read_text()
            self.assertIn('"fx.source_compile"', compiler)
            self.assertIn('"fx.original_read"', compiler)
            self.assertIn('"fx.eager_cache"', (root / 'client/finale_app.cpp').read_text())

            # The host runs the patched production init body, including nested
            # groups, continuation after a failed task and additions during init.
            init = method((root / 'lib/cstdmf/main_loop_task.cpp').read_text(), 'bool MainLoopTasks::init()')
            (root / 'lib/cstdmf/cstdmf_windows.hpp').write_text(r'''
#pragma once
#include <cstdint>
struct FILETIME { uint32_t dwLowDateTime, dwHighDateTime; };
union ULARGE_INTEGER { struct { uint32_t LowPart, HighPart; }; uint64_t QuadPart; };
inline void* GetCurrentProcess() { return nullptr; }
inline bool GetProcessTimes(void*, FILETIME* c, FILETIME* e, FILETIME* k, FILETIME* u)
{ *c = *e = *k = *u = FILETIME{0, 0}; return true; }
inline void GetSystemTimeAsFileTime(FILETIME* now) { *now = FILETIME{100000, 0}; }
''')
            (root / 'other.cpp').write_text(r'''
#include "cstdmf/stock_startup_timing.hpp"
void other() { wot_startup::Scope scope("driver.create", true); }
''')
            (root / 'host.cpp').write_text(r'''
#include "cstdmf/stock_startup_timing.hpp"
#include <cassert>
#include <functional>
#include <thread>
#include <utility>
using uint = unsigned;
namespace BW { template<class T> using vector = std::vector<T>; }
struct MainLoopTask { virtual ~MainLoopTask() {} virtual bool init() = 0; };
struct MainLoopTasks : MainLoopTask {
    using OrderList = std::vector<int>;
    std::vector<std::pair<const char*, MainLoopTask*>> tasks_;
    OrderList order_;
    bool initted_ = false;
    bool init() override;
    void add(const char* name, MainLoopTask* task) {
        order_.push_back(int(tasks_.size())); tasks_.push_back({name, task});
    }
};
@INIT@
void other();
struct Task : MainLoopTask {
    std::function<bool()> work;
    explicit Task(std::function<bool()> value) : work(value) {}
    bool init() override { return work(); }
};
int main() {
    assert(wot_startup::file_time_ms(FILETIME{10000, 0}) == 1.);
    assert(wot_startup::file_time_ms(FILETIME{0, 1}) == double(uint64_t(1) << 32) / 10000.);
    MainLoopTasks root, nested;
    unsigned failed = 0, continued = 0, appended = 0;
    Task late([&] { ++appended; return true; });
    Task bad([&] { ++failed; root.add("late", &late); other(); return false; });
    Task good([&] { ++continued; return true; });
    nested.add("App", &bad);
    root.add("Device", &nested); root.add("Script", &good);
    assert(!root.init());
    assert(failed == 1 && continued == 1 && appended == 0);
    assert(root.initted_ && nested.initted_);
    std::thread thread([] { for (int i = 0; i < 100; ++i) other(); });
    for (int i = 0; i < 100; ++i) other();
    thread.join();
    const auto records = wot_startup::finish();
    assert(records.size() == 5);
    bool child = false, parent = false, script = false, aggregate = false, process = false;
    for (const auto& record : records) {
        assert(record.wall_ms >= 0 && record.worst_ms >= 0 && record.cpu_ms == 0);
        if (record.stage == "Device/App") child = record.calls == 1;
        if (record.stage == "Device") parent = record.calls == 1;
        if (record.stage == "Script") script = record.calls == 1;
        if (record.stage == "driver.create") aggregate = record.calls == 201;
        if (record.stage == "process.logger_ready") process = record.calls == 1 && record.wall_ms == 10.;
    }
    assert(child && parent && script && aggregate && process);
    { wot_startup::Scope ignored("after.startup"); other(); }
    assert(wot_startup::finish().empty());
}
'''.replace('@INIT@', init))
            command = ['c++', '-std=c++14', '-Wall', '-Wextra', '-Werror', '-pthread', '-UNDEBUG',
                       '-I', str(root / 'lib'), str(root / 'host.cpp'), str(root / 'other.cpp'),
                       '-o', str(root / 'host')]
            built = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stderr)
            result = subprocess.run([str(root / 'host')], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == '__main__':
    unittest.main()
