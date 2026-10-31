#pragma once
#include "stock_native_bot_runtime.hpp"
#include "stock_native_bot_seed.hpp"
namespace stock_native_bot {
// Cold installation only, from the native round start's seed, the accepted server orders and the
// round's refresh debt. Exact descriptor factories may run here; the owner retains no descriptor.
bool readNativeSnapshot(const RoundSeed& seed,const ServerOrders& orders,double refresh_bank,Snapshot&);
bool readNativeServerSnapshot(_object* row,_object* tick,_object* server_time,
    const Actor&,const offline_simulation::weapons::ActorState&,ServerSnapshot&);
}
