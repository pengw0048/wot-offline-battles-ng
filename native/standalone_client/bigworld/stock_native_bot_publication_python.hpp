#pragma once
#include "stock_native_bot_runtime.hpp"
struct _object;
namespace stock_native_bot {
// Pure native-to-Python value packing; never invokes user or mod code.
_object* packNativeOutputs(const std::vector<Output>&,std::int64_t horizon);
_object* packNativeActor(const Actor&,const offline_simulation::weapons::ActorState&,
    const contacts::Actor*,_object* previous,double equipment_time);
// An unchanged server combat base only acknowledges a ledger receipt and
// reanchors stun time. Preserve the existing motion/equipment mirror.
_object* packNativeSnapshotActor(const Actor&,const offline_simulation::weapons::ActorState&,
    const contacts::Actor*,_object* previous,double equipment_time,bool changed);
}
