#pragma once
#include "stock_native_bot_contacts.hpp"
#include "stock_native_bot_seed.hpp"

namespace stock_native_bot {
struct MotionSnapshot {
    stock_native_bot_motion::State state;
    stock_native_bot_motion::Descriptor descriptor;
    wot::offline_bot_step::SuspensionDescriptor suspension;
    wot::offline_bot_step::VerticalMemory memory;
    stock_native_bot_contacts::Profile ram;
    std::map<std::int64_t,stock_native_bot_contacts::Checkpoint> push_acknowledgements;
    std::map<std::int64_t,stock_native_bot_contacts::RamMotion> ram_motion;
    std::int64_t ram_sequence=0;
    int team=0,slot=0;
    bool detailed_suspension=false;
};
struct MotionModeSnapshot {
    stock_native_bot_motion::Descriptor descriptor;
    wot::offline_bot_step::SuspensionDescriptor suspension;
    stock_native_bot_contacts::Profile ram;
    bool detailed_suspension=false;
};
// Called only at actor installation, before the native clock starts. Exact
// descriptor readers may run here; all resulting values are copied by value.
// New reference; derive_params drive fields without crew factors. The result
// contains mass, powerW, nativePowerRatio, speedFwd, speedBwd,
// specificFriction, brakeDecel, rotSpd, trackCenter, and terrainResist.
_object* readUnmodifiedDrive(_object* descriptor);
bool readMotionSnapshot(const BotSeed & bot,_object * descriptor,MotionSnapshot & result);
bool readMotionMode(const BotSeed & bot,_object * descriptor,MotionModeSnapshot & result);
}
