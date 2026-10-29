#include "stock_rotation_stage_timing.hpp"

#include <atomic>
#include <chrono>

namespace stock_rotation_timing {
namespace {
struct Frame {Stage stage;std::uint64_t start,children;bool entry;};
struct State {
    Snapshot data;
    std::array<Frame,MAX_DEPTH> frames{};
    unsigned entry_depth=0;
    std::uint64_t foreign_baseline=0;
};
thread_local State state;
std::atomic<std::uintptr_t> capture_owner{0},last_owner{0};
std::atomic<std::uint64_t> generations{0},foreign_calls{0};
std::uintptr_t token() noexcept {return reinterpret_cast<std::uintptr_t>(&state);}
std::uint64_t now() noexcept {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}
void error(Error code) noexcept {++state.data.errors[static_cast<unsigned>(code)];}
bool correctThread(bool starting=false) noexcept {
    auto owner=capture_owner.load(std::memory_order_relaxed);
    if(!owner && !starting)owner=last_owner.load(std::memory_order_relaxed);
    if(owner && owner!=token()){++foreign_calls;return false;}
    return true;
}
#if defined(_MSC_VER)
__declspec(noinline)
#else
__attribute__((noinline))
#endif
void emptyScope() noexcept {Scope scope(Stage::Marshal);}
volatile std::uint64_t calibration_sink=0;
}
namespace detail {
thread_local bool capture_enabled=false;
thread_local bool entry_active=false;
unsigned enter(Stage stage,bool entry,std::uint64_t &generation) noexcept {
    if(!capture_enabled || (!entry && !entry_active))return 0;
    auto &data=state.data;generation=data.generation;
    if(data.depth>=MAX_DEPTH){error(Error::StackOverflow);return 0;}
    if(entry){++state.entry_depth;if(state.entry_depth>data.max_entry_depth)data.max_entry_depth=state.entry_depth;}
    auto &frame=state.frames[data.depth];
    frame={stage,now(),0,entry};++data.clock_calls;
    ++data.depth;if(data.depth>data.max_depth)data.max_depth=data.depth;
    ++data.stages[static_cast<unsigned>(stage)].calls;
    entry_active=true;return data.depth;
}
void leave(unsigned expected,std::uint64_t generation,bool entry) noexcept {
    auto &data=state.data;
    if(generation!=data.generation)return;
    if(!data.depth || data.depth!=expected || state.frames[data.depth-1].entry!=entry) {
        error(Error::StackMismatch);capture_enabled=false;entry_active=false;
        data.depth=0;state.entry_depth=0;capture_owner.store(0);return;
    }
    const auto end=now();++data.clock_calls;
    const auto frame=state.frames[--data.depth];
    std::uint64_t elapsed=0;
    if(end>=frame.start)elapsed=end-frame.start;else error(Error::ClockRegressed);
    auto &row=data.stages[static_cast<unsigned>(frame.stage)];row.inclusive_ns+=elapsed;
    if(elapsed>=frame.children)row.self_ns+=elapsed-frame.children;else error(Error::ClockRegressed);
    if(data.depth)state.frames[data.depth-1].children+=elapsed;
    else {++data.root_count;data.root_ns+=elapsed;}
    if(entry && state.entry_depth)--state.entry_depth;
    entry_active=state.entry_depth!=0;
}
void increment(Counter counter,std::uint64_t amount) noexcept {
    state.data.counters[static_cast<unsigned>(counter)]+=amount;
}
}
bool start() noexcept {
    if(!correctThread(true))return false;
    if(state.data.depth){error(Error::StartNested);return false;}
    if(detail::capture_enabled){error(Error::StartActive);return false;}
    std::uintptr_t empty=0;
    if(!capture_owner.compare_exchange_strong(empty,token())){++foreign_calls;return false;}
    last_owner.store(token());state=State{};state.data.generation=++generations;
    state.foreign_baseline=foreign_calls.load();
    detail::capture_enabled=true;detail::entry_active=false;return true;
}
bool stop() noexcept {
    if(!correctThread())return false;
    if(state.data.depth){error(Error::StopNested);return false;}
    if(!detail::capture_enabled){error(Error::StopInactive);return false;}
    detail::capture_enabled=false;detail::entry_active=false;
    capture_owner.store(0);return true;
}
bool snapshot(Snapshot &out) noexcept {
    if(!correctThread())return false;
    if(state.data.depth){error(Error::SnapshotNested);return false;}
    out=state.data;out.active=detail::capture_enabled;
    out.errors[static_cast<unsigned>(Error::WrongThread)]+=foreign_calls.load()-state.foreign_baseline;
    return true;
}
const char *name(Stage value) noexcept {
    static const char *names[]={"entry","context_setup","context_teardown","life_guard","binding_validation",
        "filter_skin","scene","marshal","factory","business"};
    return names[static_cast<unsigned>(value)];
}
const char *name(Counter value) noexcept {
    static const char *names[]={"world_main","world_previous","context_attempt","context_accepted","context_unbound",
        "native_ray","native_miss","native_found","native_unbound","native_failed","scene_query","scene_hit",
        "scene_miss","scene_failed","filter_candidate","materialize","generic_ray","callback_filter",
        "callback_resolve","callback_trace","callback_ground","callback_flush","callback_raise_failure",
        "callback_failure_report","factory_call"};
    return names[static_cast<unsigned>(value)];
}
const char *name(Error value) noexcept {
    static const char *names[]={"start_active","start_nested","stop_inactive","stop_nested","snapshot_nested",
        "wrong_thread","stack_overflow","stack_mismatch","clock_regressed"};
    return names[static_cast<unsigned>(value)];
}
bool calibrate(unsigned iterations,Calibration &out) noexcept {
    if(!iterations || iterations>100000 || !correctThread() || detail::capture_enabled || state.data.depth)return false;
    out=Calibration{};out.iterations=iterations;
    auto before=now();for(unsigned i=0;i<iterations;++i)emptyScope();out.off_ns=now()-before;
    if(!start())return false;
    before=now();{EntryScope entry;for(unsigned i=0;i<iterations;++i)emptyScope();}out.on_ns=now()-before;
    if(!stop() || !snapshot(out.measured))return false;
    before=now();for(unsigned i=0;i<iterations;++i){const auto first=now();calibration_sink=now()-first;}out.clock_pair_ns=now()-before;
    return true;
}
}
