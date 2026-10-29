#pragma once

#include <array>
#include <cstdint>

// Narrow, synchronous rotation diagnostic. It records no objects or ray rows.
namespace stock_rotation_timing {
enum class Stage : std::uint8_t {
    Entry, ContextSetup, ContextTeardown, LifeGuard, BindingValidation,
    FilterSkin, Scene, Marshal, Factory, Business, Count
};
enum class Counter : std::uint8_t {
    WorldMain, WorldPrevious, ContextAttempt, ContextAccepted, ContextUnbound,
    NativeRay, NativeMiss, NativeFound, NativeUnbound, NativeFailed,
    SceneQuery, SceneHit, SceneMiss, SceneFailed, FilterCandidate, Materialize,
    GenericRay, CallbackFilter, CallbackResolve, CallbackTrace, CallbackGround,
    CallbackFlush, CallbackRaiseFailure, CallbackFailureReport, FactoryCall, Count
};
enum class Error : std::uint8_t {
    StartActive, StartNested, StopInactive, StopNested, SnapshotNested,
    WrongThread, StackOverflow, StackMismatch, ClockRegressed, Count
};
constexpr unsigned STAGE_COUNT = static_cast<unsigned>(Stage::Count);
constexpr unsigned COUNTER_COUNT = static_cast<unsigned>(Counter::Count);
constexpr unsigned ERROR_COUNT = static_cast<unsigned>(Error::Count);
constexpr unsigned MAX_DEPTH = 128;
struct Row { std::uint64_t calls=0, inclusive_ns=0, self_ns=0; };
struct Snapshot {
    std::uint64_t generation=0, root_count=0, root_ns=0, clock_calls=0;
    unsigned depth=0, max_depth=0, max_entry_depth=0;
    bool active=false;
    std::array<Row,STAGE_COUNT> stages{};
    std::array<std::uint64_t,COUNTER_COUNT> counters{};
    std::array<std::uint64_t,ERROR_COUNT> errors{};
};
// Only the capture's current thread can start/stop/read it. Snapshot may be
// read while capture is active, but never inside a live rotation scope.
bool start() noexcept;
bool stop() noexcept;
bool snapshot(Snapshot &) noexcept;
const char *name(Stage) noexcept;
const char *name(Counter) noexcept;
const char *name(Error) noexcept;

namespace detail {
extern thread_local bool capture_enabled;
extern thread_local bool entry_active;
unsigned enter(Stage,bool entry,std::uint64_t &generation) noexcept;
void leave(unsigned token,std::uint64_t generation,bool entry) noexcept;
void increment(Counter,std::uint64_t) noexcept;
}
class Scope {
    std::uint64_t generation_=0;
    unsigned token_;
public:
    explicit Scope(Stage stage) noexcept:
        token_(detail::entry_active?detail::enter(stage,false,generation_):0) {}
    void finish() noexcept {if(token_){detail::leave(token_,generation_,false);token_=0;}}
    ~Scope() noexcept {finish();}
    Scope(const Scope &)=delete;
    Scope &operator=(const Scope &)=delete;
};
class EntryScope {
    std::uint64_t generation_=0;
    unsigned token_;
public:
    EntryScope() noexcept:
        token_(detail::capture_enabled?detail::enter(Stage::Entry,true,generation_):0) {}
    ~EntryScope() noexcept {if(token_)detail::leave(token_,generation_,true);}
    EntryScope(const EntryScope &)=delete;
    EntryScope &operator=(const EntryScope &)=delete;
};
inline void count(Counter counter,std::uint64_t amount=1) noexcept {
    if(detail::entry_active)detail::increment(counter,amount);
}
struct Calibration {
    std::uint64_t iterations=0, off_ns=0, on_ns=0, clock_pair_ns=0;
    Snapshot measured;
};
// A bounded standalone empty-scope control, never run inside a capture.
bool calibrate(unsigned iterations,Calibration &) noexcept;
}
