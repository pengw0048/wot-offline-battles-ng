"""Execute actual scene owners and both Reset branches on a portable device host."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from native_test_compile_flags import native_test_compile_flags
from test_stock_d3d9_render_thread_commands import function

SOURCE = Path(__file__).resolve().parents[1] / 'bigworld/stock_d3d9_render_thread.cpp'


def host_source(mode):
    source = SOURCE.read_text()
    wrappers = '\n'.join(function(source, signature) for signature in (
        'HRESULT WDevice::BeginScene(', 'HRESULT WDevice::EndScene(',
        'HRESULT WDevice::Reset(', 'HRESULT WDevice::ResetEx('))
    helpers = '\n'.join(function(source, signature) for signature in (
        'HRESULT begin_command_scene(', 'HRESULT end_command_scene('))
    reset = function(source, 'template<class F> static HRESULT resetOnWindowThread(')
    if mode == 'missing-world-state':
        wrappers = wrappers.replace('if (SUCCEEDED(result)) rt::g_sceneOpen = true;', '')
    elif mode == 'invalidcall-open':
        helpers = helpers.replace('opened = SUCCEEDED(result);',
                                  'opened = SUCCEEDED(result) || result == D3DERR_INVALIDCALL;')
    elif mode == 'failed-end':
        helpers = helpers.replace('if (SUCCEEDED(result)) rt::g_sceneOpen = false;',
                                  'rt::g_sceneOpen = false;')
    elif mode == 'missing-reset-state':
        reset = reset.replace('if (SUCCEEDED(h)) rt::g_sceneOpen = false;', '')
    elif mode == 'failed-reset':
        reset = reset.replace('if (SUCCEEDED(h)) rt::g_sceneOpen = false;', 'rt::g_sceneOpen = false;')
    macro = next(line for line in source.splitlines() if line.startswith('#define ASYNC('))
    return (HOST.replace('@ASYNC@', function(source, 'template<class F> HRESULT async('))
            .replace('@MACRO@', macro).replace('@WRAPPERS@', wrappers)
            .replace('@HELPERS@', helpers).replace('@RESET@', reset)
            .replace('@BEFORE_RESET@', function(source, 'static void beforeReset(')))


HOST = r'''
#include <cstdint>
#include <cstdio>
#include <deque>
#include <functional>
#include <stdexcept>
#include <unordered_map>
#include <utility>
using HRESULT = int32_t;
using HANDLE = int;
const HRESULT D3D_OK = 0, S_FALSE = 1;
const HRESULT D3DERR_INVALIDCALL = -2005530516, D3DERR_DEVICELOST = -2005530520;
const int FALSE = 0, INFINITE = -1;
#define SUCCEEDED(value) ((value) >= 0)
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
int currentThread = 1;
struct Parked {};
std::unordered_map<HANDLE, bool> events;
HANDLE nextEvent = 1;
bool workerParked = false;
HANDLE suspendedOn = 0;
int shadowClears = 0, deferredRuns = 0, shadowRefreshes = 0;
HANDLE CreateEventA(void*, int, int, void*) { int event = nextEvent++; events[event] = false; return event; }
void SetEvent(HANDLE event) {
    events[event] = true;
    // Resuming the parked lambda consumes its auto-reset event. There is no
    // code after its wait, so no simulated continuation is needed.
    if (suspendedOn == event) { events[event] = false; suspendedOn = 0; }
}
void WaitForSingleObject(HANDLE event, int);
namespace rt {
bool g_sceneOpen = false, g_parked = false, g_inlineRetire = false;
int g_sinceWake = 0;
std::deque<std::function<void()>> queue;
bool onMain() { return currentThread == 1; }
bool mainOwnsDevice() { return onMain() && g_parked; }
void wake() {}
template<class F> void post(F&& call) { queue.emplace_back(std::forward<F>(call)); }
template<class F> auto sync(F&& call, const char*) -> decltype(call()) {
    const int saved = currentThread;
    if (!mainOwnsDevice()) currentThread = 2;
    auto result = call(); currentThread = saved; return result;
}
HRESULT reportResult(const char*, HRESULT result) { return result; }
@ASYNC@
void step() {
    require(!queue.empty(), "no queued device operation");
    auto call = std::move(queue.front()); queue.pop_front();
    const int saved = currentThread; currentThread = 2;
    try { call(); } catch (const Parked&) { workerParked = true; }
    currentThread = saved;
}
void runDeferred() { ++deferredRuns; }
}
void WaitForSingleObject(HANDLE event, int) {
    if (!events[event] && currentThread == 2) { suspendedOn = event; throw Parked(); }
    if (!events[event]) rt::step();
    require(events[event], "window Reset waited without park acknowledgement");
    events[event] = false;
}
namespace sh { void clearAll() { ++shadowClears; } }
struct D3DPRESENT_PARAMETERS {};
struct D3DDISPLAYMODEEX {};
struct IDirect3DDevice9 {
    bool nativeScene = false;
    int begins = 0, ends = 0, resets = 0;
    HRESULT beginResult = D3D_OK, endResult = D3D_OK, resetResult = D3D_OK;
    HRESULT BeginScene() {
        require(currentThread == 2 || rt::mainOwnsDevice(), "BeginScene outside device owner");
        ++begins;
        if (!SUCCEEDED(beginResult)) return beginResult;
        if (nativeScene) return D3DERR_INVALIDCALL;
        nativeScene = true; return beginResult;
    }
    HRESULT EndScene() {
        require(currentThread == 2 || rt::mainOwnsDevice(), "EndScene outside device owner");
        ++ends;
        if (!SUCCEEDED(endResult)) return endResult;
        if (!nativeScene) return D3DERR_INVALIDCALL;
        nativeScene = false; return endResult;
    }
    HRESULT Reset(D3DPRESENT_PARAMETERS*) {
        require(rt::mainOwnsDevice(), "window Reset did not acquire device ownership");
        ++resets;
        if (SUCCEEDED(resetResult)) nativeScene = false;
        return resetResult;
    }
    HRESULT ResetEx(D3DPRESENT_PARAMETERS* p, D3DDISPLAYMODEEX*) { return Reset(p); }
};
using IDirect3DDevice9Ex = IDirect3DDevice9;
void refreshShadows(IDirect3DDevice9*) { ++shadowRefreshes; }
struct WDevice {
    IDirect3DDevice9Ex* real;
    HRESULT BeginScene(); HRESULT EndScene();
    HRESULT Reset(D3DPRESENT_PARAMETERS*);
    HRESULT ResetEx(D3DPRESENT_PARAMETERS*, D3DDISPLAYMODEEX*);
};
#define REAL (this->real)
@BEFORE_RESET@
@RESET@
@MACRO@
@WRAPPERS@
@HELPERS@
void fresh(IDirect3DDevice9& device) {
    require(rt::queue.empty(), "previous case left queued work");
    device = IDirect3DDevice9(); rt::g_sceneOpen = false; rt::g_parked = false;
    rt::g_inlineRetire = false; currentThread = 1;
    shadowClears = deferredRuns = shadowRefreshes = 0;
    workerParked = false;
}
int main() {
    try {
        IDirect3DDevice9 device; WDevice wrapper{&device};
        fresh(device);
        require(wrapper.BeginScene() == D3D_OK, "world BeginScene admission failed");
        require(!rt::g_sceneOpen && !device.nativeScene && device.begins == 0,
                "world scene changed before render execution");
        rt::step();
        require(rt::g_sceneOpen && device.nativeScene, "world BeginScene did not publish actual open state");
        currentThread = 2;
        bool opened = true;
        require(begin_command_scene(&device, opened) == D3D_OK && !opened && device.begins == 1,
                "command nested in world scene opened a second scene");
        require(end_command_scene(&device, opened) == D3D_OK && device.ends == 0 && rt::g_sceneOpen,
                "command closed world-owned scene");
        currentThread = 1;
        require(wrapper.EndScene() == D3D_OK && rt::g_sceneOpen && device.ends == 0,
                "world EndScene changed state on admission");
        rt::step();
        require(!rt::g_sceneOpen && !device.nativeScene && device.ends == 1,
                "world EndScene did not publish actual closed state");

        fresh(device); currentThread = 2;
        bool outer = false, inner = true;
        require(begin_command_scene(&device, outer) == D3D_OK && outer && rt::g_sceneOpen,
                "outer command failed to own scene");
        require(begin_command_scene(&device, inner) == D3D_OK && !inner && device.begins == 1,
                "nested command opened duplicate scene");
        require(end_command_scene(&device, inner) == D3D_OK && device.ends == 0,
                "nested command ended outer scene");
        require(end_command_scene(&device, outer) == D3D_OK && !rt::g_sceneOpen && device.ends == 1,
                "outer command did not close owned scene");

        for (HRESULT failure : {D3DERR_INVALIDCALL, D3DERR_DEVICELOST}) {
            fresh(device); device.beginResult = failure;
            require(wrapper.BeginScene() == D3D_OK, "failed world BeginScene was not admitted");
            rt::step();
            require(!rt::g_sceneOpen && !device.nativeScene, "failed world BeginScene fabricated open state");
            currentThread = 2; opened = true;
            require(begin_command_scene(&device, opened) == failure && !opened && !rt::g_sceneOpen,
                    "failed command BeginScene fabricated scene ownership");
            const int ends = device.ends;
            require(end_command_scene(&device, opened) == D3D_OK && device.ends == ends,
                    "failed command BeginScene was followed by EndScene");
        }
        // INVALIDCALL from an unexpectedly open native device is still a failure.
        fresh(device); currentThread = 2; device.nativeScene = true;
        require(begin_command_scene(&device, opened) == D3DERR_INVALIDCALL && !opened && !rt::g_sceneOpen,
                "INVALIDCALL was mistaken for known scene ownership");

        for (HRESULT failure : {D3DERR_INVALIDCALL, D3DERR_DEVICELOST}) {
            fresh(device); currentThread = 2;
            require(begin_command_scene(&device, opened) == D3D_OK && opened, "command BeginScene failed");
            device.endResult = failure;
            require(end_command_scene(&device, opened) == failure && rt::g_sceneOpen && device.nativeScene,
                    "failed command EndScene fabricated closed state");
            currentThread = 1; wrapper.EndScene(); rt::step();
            require(rt::g_sceneOpen && device.nativeScene, "failed world EndScene fabricated closed state");
        }
        fresh(device); currentThread = 2; opened = true;
        require(begin_command_scene(nullptr, opened) == D3D_OK && !opened && !rt::g_sceneOpen,
                "null command device acquired scene ownership");
        require(end_command_scene(nullptr, true) == D3D_OK, "null command device EndScene failed");

        // Execute both the parked-window and already-owned inline Reset branches.
        for (bool parked : {false, true}) for (bool extended : {false, true})
        for (HRESULT result : {D3D_OK, S_FALSE, D3DERR_INVALIDCALL, D3DERR_DEVICELOST}) {
            fresh(device); rt::g_sceneOpen = device.nativeScene = true;
            rt::g_parked = parked; device.resetResult = result;
            D3DPRESENT_PARAMETERS p; D3DDISPLAYMODEEX mode;
            const HRESULT actual = extended ? wrapper.ResetEx(&p, &mode) : wrapper.Reset(&p);
            require(actual == result && device.resets == 1, "Reset result or call count changed");
            if (SUCCEEDED(result))
                require(!rt::g_sceneOpen && !device.nativeScene, "successful Reset retained open scene state");
            else
                require(rt::g_sceneOpen && device.nativeScene, "failed Reset fabricated closed scene state");
            require(shadowClears == 1 && deferredRuns == 1 && shadowRefreshes == 1 && !rt::g_inlineRetire,
                    "Reset skipped existing shadow or retirement lifecycle");
            if (!parked) require(!rt::g_parked && workerParked,
                                "window Reset did not park and release actual owner");
        }
        std::puts("Actual scene ownership and Reset state transitions passed");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what()); return 1;
    }
}
'''


class RenderThreadSceneTests(unittest.TestCase):
    def test_actual_scene_owners_and_reset_outcomes(self):
        compiler = shutil.which(os.environ.get('CXX', 'c++'))
        if not compiler:
            self.skipTest('Host C++ compiler is required')
        with tempfile.TemporaryDirectory(prefix='d3d9-scene-owner-') as directory:
            root = Path(directory)
            for mode, failure in (
                    ('after', None),
                    ('missing-world-state', 'world BeginScene did not publish actual open state'),
                    ('invalidcall-open', 'failed command BeginScene fabricated scene ownership'),
                    ('failed-end', 'failed command EndScene fabricated closed state'),
                    ('missing-reset-state', 'successful Reset retained open scene state'),
                    ('failed-reset', 'failed Reset fabricated closed scene state')):
                with self.subTest(mode=mode):
                    source, binary = root / (mode + '.cpp'), root / (mode + '.exe')
                    source.write_text(host_source(mode))
                    built = subprocess.run([compiler, '-std=c++14', '-Wall', '-Wextra', '-Werror',
                                            '-UNDEBUG', *native_test_compile_flags(compiler),
                                            str(source), '-o', str(binary)],
                                           capture_output=True, text=True, timeout=60)
                    self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
                    result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=10)
                    if failure is None:
                        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                        self.assertIn('Actual scene ownership and Reset state transitions passed', result.stdout)
                    else:
                        self.assertNotEqual(result.returncode, 0, mode + ' negative control passed')
                        self.assertIn(failure, result.stderr)


if __name__ == '__main__':
    unittest.main()
