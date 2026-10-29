#include "stock_rotation_stage_timing.hpp"
#include <cassert>
#include <cstdio>
#include <thread>
using namespace stock_rotation_timing;
namespace {
std::uint64_t total(const Snapshot &value) {std::uint64_t result=0;for(const auto &row:value.stages)result+=row.self_ns;return result;}
void overflow(unsigned depth) {Scope scope(Stage::Scene);if(depth)overflow(depth-1);}
void work() {volatile unsigned result=0;for(unsigned i=0;i<1000;++i)result+=i;}
}
int main() {
    Snapshot data;
    {EntryScope entry;Scope ignored(Stage::Scene);count(Counter::SceneQuery);}
    assert(snapshot(data) && data.clock_calls==0 && data.root_count==0);
    assert(start());
    {Scope ignored(Stage::Scene);count(Counter::SceneQuery);}
    assert(snapshot(data) && data.clock_calls==0 && data.counters[static_cast<unsigned>(Counter::SceneQuery)]==0);
    {
        EntryScope entry;count(Counter::WorldMain);
        {Scope setup(Stage::ContextSetup);{Scope live(Stage::LifeGuard);work();}}
        {Scope business(Stage::Business);EntryScope nested;Scope scene(Stage::Scene);work();}
        try {Scope marshal(Stage::Marshal);throw 1;}catch(int){}
    }
    assert(snapshot(data) && data.active && data.root_count==1 && data.max_entry_depth==2);
    assert(data.stages[static_cast<unsigned>(Stage::Entry)].calls==2 && total(data)==data.root_ns);
    for(auto error:data.errors)assert(!error);
    assert(stop());assert(snapshot(data) && !data.active);
    const auto clocks=data.clock_calls;
    {EntryScope entry;Scope ignored(Stage::LifeGuard);}
    assert(snapshot(data) && data.clock_calls==clocks);
    assert(start());assert(!start());
    {EntryScope entry;Snapshot ignored;assert(!snapshot(ignored));assert(!stop());assert(!start());}
    assert(stop() && snapshot(data));
    assert(data.errors[static_cast<unsigned>(Error::StartActive)]==1);
    assert(data.errors[static_cast<unsigned>(Error::SnapshotNested)]==1);
    assert(data.errors[static_cast<unsigned>(Error::StopNested)]==1);
    assert(data.errors[static_cast<unsigned>(Error::StartNested)]==1);
    assert(start());{EntryScope entry;overflow(MAX_DEPTH+2);}assert(stop() && snapshot(data));
    assert(data.depth==0 && data.max_depth==MAX_DEPTH && data.errors[static_cast<unsigned>(Error::StackOverflow)]>0);
    assert(total(data)==data.root_ns);
    assert(start());
    std::thread wrong([]{Snapshot ignored;assert(!stop());assert(!snapshot(ignored));});wrong.join();
    assert(stop() && snapshot(data));assert(data.errors[static_cast<unsigned>(Error::WrongThread)]==2);
    assert(start());
    auto *outer=new EntryScope();auto *inner=new Scope(Stage::Scene);delete outer;
    assert(snapshot(data) && data.depth==0 && data.errors[static_cast<unsigned>(Error::StackMismatch)]>0);
    // A deliberately broken old scope cannot corrupt the next generation.
    assert(start());{EntryScope entry;delete inner;work();}assert(stop() && snapshot(data));
    assert(total(data)==data.root_ns && data.root_count==1);
    for(auto error:data.errors)assert(!error);
    Calibration calibration;assert(calibrate(1000,calibration));
    assert(calibration.measured.root_count==1 && calibration.measured.clock_calls==2002);
    assert(calibration.measured.stages[static_cast<unsigned>(Stage::Marshal)].calls==1000);
    assert(total(calibration.measured)==calibration.measured.root_ns);
    std::printf("rotation timer passed: off/nesting/conservation/unwind/overflow/thread/generation; calibration off=%llu on=%llu clockpair=%llu ns\n",
        static_cast<unsigned long long>(calibration.off_ns),static_cast<unsigned long long>(calibration.on_ns),
        static_cast<unsigned long long>(calibration.clock_pair_ns));
}
