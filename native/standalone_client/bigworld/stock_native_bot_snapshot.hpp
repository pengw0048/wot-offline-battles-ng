#pragma once
#include "stock_native_bot_runtime.hpp"
struct _object;
namespace stock_native_bot {
// Cold installation only. Exact descriptor factories may run here; the
// resulting owner retains no Python callback or descriptor object.
bool readNativeSnapshot(_object* bot_runtime,_object* battle_runtime,Snapshot&);
bool readNativeServerSnapshot(_object* row,_object* tick,_object* server_time,
    const Actor&,const offline_simulation::weapons::ActorState&,ServerSnapshot&);
}
