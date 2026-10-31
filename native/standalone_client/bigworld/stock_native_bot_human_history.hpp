#pragma once
#include "stock_native_client_state.hpp"
#include <cstdint>
#include <map>
namespace native_battle {struct Battle;}
namespace stock_native_bot {
// Returns the filtered, decorated player rows. `profiles` maps a Bot id to its static ram profile
// (mass, collision_shape, ram_profile, vehicle, team). A receipt naming a past snapshot revision
// takes its Bot body from the world's ram history (world::ramBotState).
stock_native_client_state::Value prepareHumanInput(const stock_native_client_state::Value& players,
    const native_battle::Battle& battle,const std::map<std::int64_t,stock_native_client_state::Value>& profiles);
}
