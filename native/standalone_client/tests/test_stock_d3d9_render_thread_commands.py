"""Execute the real command admission and render worker on a portable ring host."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from native_test_compile_flags import native_test_compile_flags

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'bigworld/stock_d3d9_render_thread.cpp'


def function(source, signature):
    start = source.index(signature)
    brace = source.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


def host_source(mode):
    source = SOURCE.read_text()
    ring = source[source.index('struct Rec {'):source.index('template<class T> struct WithData')]
    command = function(source, 'void post_command(')
    worker = function(source, 'static DWORD WINAPI renderThread(')
    barrier = function(source, 'void drain_commands(')
    # Preserve the worker-owned counter across deterministic pump invocations.
    # The real worker runs once for its entire lifetime; the host yields at idle.
    assert worker.count('LONG64 completed = 0;') == 1
    worker = worker.replace('LONG64 completed = 0;', 'LONG64& completed = workerCompleted;')
    worker = worker.replace('        if (tail != head) {',
                            '        hostAfterCommandCheck();\n        if (tail != head) {')
    if mode == 'synchronous':
        start = command.index('    // Any SDK producer')
        command = command[:start] + '    run(fn, ctx);\n}'
    elif mode == 'main-ring':
        start = command.index('    // Any SDK producer')
        command = command[:start] + ('    if (rt::onMain()) { rt::post([fn, ctx] { fn(ctx); }); return; }\n') + command[start:]
    elif mode == 'overtake':
        worker = worker.replace('completed >= rt::g_foreignCommands.front().fence',
                                '(completed >= rt::g_foreignCommands.front().fence || true)')
    elif mode == 'capture':
        start = command.index('    // Any SDK producer')
        command = command[:start] + '    ensureShadows();\n' + command[start:]
    elif mode == 'stale-ack':
        start = barrier.index('    for (unsigned waits = 0;')
        barrier = barrier[:start] + '    WaitForSingleObject(completed.event, 5);\n}'
    elif mode == 'sync-barrier':
        barrier = 'void drain_commands() { run([](void*) {}, nullptr); }'
    elif mode == 'late-published':
        ring = ring.replace('    InterlockedIncrement64(&g_published);\n', '')
        ring = ring.replace('InterlockedExchange(&g_head, g_head + size);',
                            'InterlockedExchange(&g_head, g_head + size); InterlockedIncrement64(&g_published);')
    elif mode == 'late-pending':
        command = command.replace('    InterlockedExchange(&rt::g_commandsPending, 1);\n', '')
        command = command.replace('    rt::g_foreignCommands.push_back(',
                                  '    InterlockedExchange(&rt::g_commandsPending, 1);\n    rt::g_foreignCommands.push_back(')
    elif mode == 'late-snapshot':
        snapshot = '        const uint32_t tail = rt::g_tail, head = rt::g_head;\n        MemoryBarrier();\n'
        assert worker.count(snapshot) == 1
        worker = worker.replace(snapshot, '')
        worker = worker.replace('        hostAfterCommandCheck();\n',
                                '        hostAfterCommandCheck();\n' + snapshot)
    sync = '\n'.join(function(source, signature) + suffix for signature, suffix in (
        ('static void runTask(', ''),
        ('static HANDLE threadEvent(', ''), ('template<class F> auto sync(', '')))
    return (HOST.replace('@RING@', ring)
            .replace('@ASYNC@', function(source, 'struct AsyncCommand ') + ';')
            .replace('@SYNC_TASK@', function(source, 'struct SyncTask ') + ';')
            .replace('@SYNC@', sync).replace('@DRAIN@', barrier)
            .replace('@COMMAND@', command).replace('@WORKER@', worker))


HOST = r'''
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <deque>
#include <new>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>
#define WINAPI
using DWORD = uint32_t;
using LONG = int32_t;
using LONG64 = int64_t;
using HANDLE = int;
struct CRITICAL_SECTION { bool held = false; };
constexpr int THREAD_PRIORITY_ABOVE_NORMAL = 1;
constexpr int FALSE = 0;
constexpr DWORD WAIT_OBJECT_0 = 0;
static DWORD WINAPI renderThread(void*);
struct Event { bool signaled = false, closed = false; };
std::unordered_map<HANDLE, Event> events;
HANDLE nextEvent = 1;
std::vector<int> lastAckTrace;
int closedEvents = 0;
DWORD currentThread = 1;
bool sdkContextLocked = false;
std::function<void()> afterCommandCheck, afterHeadPublish;
volatile LONG* publishedHead = nullptr;
volatile LONG* pendingFlag = nullptr;
LONG64 workerCompleted = 0;
std::vector<int> trace;
int wakeCount = 0, synchronousCalls = 0, bindingCaptures = 0;
struct Idle {};
void require(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}
DWORD GetCurrentThreadId() { return currentThread; }
HANDLE GetCurrentThread() { return 0; }
void SetThreadPriority(HANDLE, int) {}
LONG InterlockedExchange(volatile LONG* target, LONG value) {
    LONG old = *target; *target = value;
    if (target == publishedHead && afterHeadPublish) {
        auto hook = std::move(afterHeadPublish); afterHeadPublish = nullptr; hook();
    }
    return old;
}
LONG InterlockedCompareExchange(volatile LONG* target, LONG value, LONG expected) {
    LONG old = *target; if (old == expected) *target = value; return old;
}
LONG64 InterlockedIncrement64(volatile LONG64* target) { return ++*target; }
LONG64 InterlockedCompareExchange64(volatile LONG64* target, LONG64 value, LONG64 expected) {
    if (sdkContextLocked)
        require(pendingFlag && *pendingFlag, "command fence read before pending publication");
    LONG64 old = *target; if (old == expected) *target = value; return old;
}
void EnterCriticalSection(CRITICAL_SECTION* lock) {
    require(!lock->held, "recursive foreign queue lock"); lock->held = true;
}
void LeaveCriticalSection(CRITICAL_SECTION* lock) {
    require(lock->held, "unbalanced foreign queue lock"); lock->held = false;
}
HANDLE CreateEventA(void*, int, int, void*) {
    const HANDLE event = nextEvent++; events[event] = Event(); return event;
}
void SetEvent(HANDLE event) {
    ++wakeCount;
    if (!event) return;
    require(events.count(event) && !events[event].closed, "signal used invalid event");
    events[event].signaled = true; lastAckTrace = trace;
}
void CloseHandle(HANDLE event) {
    require(events.count(event) && !events[event].closed, "close used invalid event");
    events[event].closed = true; ++closedEvents;
}
DWORD WaitForSingleObject(HANDLE, int);
void SwitchToThread() {
    require(!sdkContextLocked, "ring capacity wait while SDK Context lock held");
    throw std::runtime_error("unexpected ring capacity wait");
}
void YieldProcessor() {}
void MemoryBarrier() {}
void hostAfterCommandCheck() {
    if (afterCommandCheck) {
        auto hook = std::move(afterCommandCheck); afterCommandCheck = nullptr; hook();
    }
}
namespace rt {
DWORD g_main = 1, g_renderId = 2;
bool g_started = true, g_parked = false;
const uint32_t RING = 512;
alignas(8) uint8_t storage[RING];
uint8_t* g_ring = storage;
volatile LONG g_head = 0, g_tail = 0, g_sleeping = 0, g_foreign = 0;
alignas(8) volatile LONG64 g_published = 0;
HANDLE g_work = 0, g_idle = 0;
uint32_t g_sinceWake = 0;
long long g_bytes = 0;
bool onMain() { return g_started && GetCurrentThreadId() == g_main; }
bool onRender() { return GetCurrentThreadId() == g_renderId; }
bool mainOwnsDevice() { return g_parked && onMain(); }
@RING@
@ASYNC@
std::deque<AsyncCommand> g_foreignCommands;
volatile LONG g_commandsPending = 0;
CRITICAL_SECTION g_foreignLock;
long long g_syncCount = 0, g_syncTicks = 0;
std::unordered_map<const char*, long> g_syncBy;
long long now() { return 0; }
void stallReport(const char*) { throw std::runtime_error("unexpected command drain stall"); }
@SYNC_TASK@
std::vector<SyncTask*> g_foreignTasks;
@SYNC@
void runDeferred() {}
}
std::vector<int> g_qpoll;
void pollQueries() {}
DWORD render_thread_id() { return rt::g_started ? rt::g_renderId : 0; }
void ensureShadows() {
    ++bindingCaptures;
    require(!sdkContextLocked, "device binding capture while SDK Context lock held");
}
void run(void (*fn)(void*), void* ctx) {
    ++synchronousCalls;
    require(!sdkContextLocked, "synchronous foreign admission while SDK Context lock held");
    rt::sync([&]() -> int { fn(ctx); return 0; }, "host runSync");
}
@COMMAND@
@DRAIN@
@WORKER@
DWORD WaitForSingleObject(HANDLE event, int) {
    if (!event) throw Idle();
    require(!sdkContextLocked, "wait retained SDK Context lock");
    require(!rt::g_foreignLock.held, "wait retained foreign command queue lock");
    require(events.count(event) && !events[event].closed, "wait used invalid event");
    if (!events[event].signaled) {
        const DWORD saved = currentThread; currentThread = 2;
        try { renderThread(nullptr); } catch (const Idle&) {}
        currentThread = saved;
    }
    require(events[event].signaled, "wait returned without acknowledgement");
    events[event].signaled = false;
    return WAIT_OBJECT_0;
}
void reset(uint32_t offset = 0) {
    require(rt::g_head == rt::g_tail && rt::g_foreignCommands.empty(), "undrained prior case");
    rt::g_head = rt::g_tail = offset;
    rt::g_published = workerCompleted = 0;
    publishedHead = &rt::g_head; pendingFlag = &rt::g_commandsPending;
    rt::g_sleeping = 1; rt::g_commandsPending = 0;
    rt::g_sinceWake = 0; rt::g_bytes = 0;
    currentThread = 1; sdkContextLocked = false;
    trace.clear(); lastAckTrace.clear(); afterCommandCheck = afterHeadPublish = nullptr;
    wakeCount = synchronousCalls = bindingCaptures = 0;
}
void display(int value) {
    DWORD saved = currentThread; currentThread = 1;
    rt::post([value] { require(currentThread == 2, "Display ran outside render owner"); trace.push_back(value); });
    currentThread = saved;
}
void command(void* value) {
    require(!sdkContextLocked, "command executed before SDK Context lock release");
    trace.push_back(*static_cast<int*>(value));
}
void submit(int& value, DWORD thread) {
    currentThread = thread; sdkContextLocked = true;
    post_command(command, &value);
    require(synchronousCalls == 0, "command admission used synchronous dispatch");
    require(bindingCaptures == 0, "command admission captured device bindings");
    sdkContextLocked = false;
}
void drain() {
    currentThread = 2;
    try { renderThread(nullptr); } catch (const Idle&) {}
    require(rt::g_head == rt::g_tail && !rt::g_commandsPending && rt::g_foreignCommands.empty(),
            "worker slept before draining admitted commands");
}
void expect(std::initializer_list<int> values, const char* message) {
    require(trace == std::vector<int>(values), message);
}
int main() {
    try {
        int a = 2, b = 3, c = 7;
        // Fill the actual ring until another record would wait. Both producer
        // classes must still admit commands while holding the SDK Context lock.
        reset();
        std::vector<int> fullExpected;
        const uint32_t need = (sizeof(rt::Rec) + sizeof(int) + 7) & ~7u;
        for (int id = 100; uint32_t(rt::g_head) + need + 64 < rt::RING; ++id) {
            display(id); fullExpected.push_back(id);
        }
        const LONG fullHead = rt::g_head;
        submit(a, 3); submit(b, 1);
        require(rt::g_head == fullHead, "command admission consumed full-ring capacity");
        fullExpected.push_back(2); fullExpected.push_back(3);
        drain(); require(trace == fullExpected, "full-ring commands lost mixed-producer FIFO");

        reset(); display(1); submit(a, 1); display(3);
        require(trace.empty(), "main command ran synchronously");
        require(wakeCount > 0, "main command did not wake sleeping worker");
        drain(); expect({1, 2, 3}, "main Display/command FIFO changed");

        reset(); display(1); submit(a, 3); submit(b, 1); display(4);
        require(trace.empty(), "foreign command ran synchronously");
        drain(); expect({1, 2, 3, 4}, "mixed producer commands crossed admission fences");

        // The marker is not a real record and must not advance completed.
        reset(rt::RING - 32); display(1); submit(a, 3); display(3);
        require(rt::g_tail > rt::g_head, "fixture did not exercise wrap marker");
        drain(); expect({1, 2, 3}, "wrapped ring lost command sequence fence");

        // A newly admitted command wins over a record published after its fence,
        // even when both arrive just after this worker's readiness check.
        reset();
        afterCommandCheck = [&a] {
            submit(a, 3); display(1); currentThread = 2;
        };
        drain(); expect({2, 1}, "late snapshot let future record overtake admitted command");

        // Suspend the producer immediately after head becomes visible. The
        // record's sequence must already be visible to command admission.
        reset();
        afterHeadPublish = [&a] {
            submit(a, 3); drain(); currentThread = 1;
        };
        display(1);
        expect({1, 2}, "head publication preceded record sequence publication");

        // A command submitted during a record callback must follow older queue
        // entries, but must precede records published after its own admission.
        reset(); currentThread = 1;
        rt::post([&c] { trace.push_back(1); submit(c, 3); display(6); currentThread = 2; });
        submit(a, 3); drain();
        expect({1, 2, 7, 6}, "new producer command crossed its admission fence");

        // Exercise sequence values beyond the 32-bit ring offsets.
        reset(); rt::g_published = workerCompleted = LONG64(1) << 33;
        display(1); submit(a, 3); display(3);
        drain(); expect({1, 2, 3}, "command fence truncated long-lived record sequence");

        // Force the first private sync ACK to compete with older foreign
        // commands, then prove a stale reusable event is not a new ACK.
        for (DWORD caller : {DWORD(3), DWORD(1)}) {
            reset(); display(1); submit(a, 4); submit(b, 5); display(4);
            currentThread = caller;
            const HANDLE reusable = rt::threadEvent();
            const HANDLE eventsBeforeDrain = nextEvent;
            SetEvent(reusable);
            drain_commands();
            require(lastAckTrace == std::vector<int>({1, 2, 3, 4}),
                    "drain acknowledgement overtook pending foreign commands");
            expect({1, 2, 3, 4}, "command drain returned before pending commands executed");
            require(nextEvent == eventsBeforeDrain && !events[reusable].closed,
                    "command drain did not reuse its live per-thread event");
            int finalDestroy = 9;
            currentThread = 1; run(command, &finalDestroy);
            expect({1, 2, 3, 4, 9}, "finalDestroy overtook acknowledged commands");
        }

        // Direct execution remains valid when no worker or another device owner exists.
        reset(); rt::g_started = false; currentThread = 3;
        post_command(command, &a); rt::g_started = true;
        currentThread = 2; post_command(command, &b);
        rt::g_parked = true; currentThread = 1; post_command(command, &c); rt::g_parked = false;
        expect({2, 3, 7}, "direct device-owner command behavior changed");
        std::puts("Actual render command FIFO and lock-safe admission passed");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what()); return 1;
    }
}
'''


class RenderThreadCommandTests(unittest.TestCase):
    def test_real_command_producer_and_worker_fences(self):
        compiler = shutil.which(os.environ.get('CXX', 'c++'))
        if not compiler:
            self.skipTest('Host C++ compiler is required')
        with tempfile.TemporaryDirectory(prefix='d3d9-command-fifo-') as directory:
            root = Path(directory)
            for mode, failure in (
                    ('after', None),
                    ('main-ring', 'ring capacity wait while SDK Context lock held'),
                    ('synchronous', 'synchronous foreign admission while SDK Context lock held'),
                    ('overtake', 'full-ring commands lost mixed-producer FIFO'),
                    ('capture', 'device binding capture while SDK Context lock held'),
                    ('late-published', 'head publication preceded record sequence publication'),
                    ('late-pending', 'command fence read before pending publication'),
                    ('late-snapshot', 'late snapshot let future record overtake admitted command'),
                    ('sync-barrier', 'drain acknowledgement overtook pending foreign commands'),
                    ('stale-ack', 'drain acknowledgement overtook pending foreign commands')):
                with self.subTest(mode=mode):
                    source, binary = root / (mode + '.cpp'), root / (mode + '.exe')
                    source.write_text(host_source(mode))
                    build = subprocess.run([compiler, '-std=c++14', '-Wall', '-Wextra', '-Werror',
                                            '-UNDEBUG', *native_test_compile_flags(compiler),
                                            str(source), '-o', str(binary)],
                                           capture_output=True, text=True, timeout=60)
                    self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
                    result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=10)
                    if failure is None:
                        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                        self.assertIn('Actual render command FIFO and lock-safe admission passed', result.stdout)
                    else:
                        self.assertNotEqual(result.returncode, 0, mode + ' negative control passed')
                        self.assertIn(failure, result.stderr)


if __name__ == '__main__':
    unittest.main()
