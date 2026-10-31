#pragma once
#include "stock_native_bot_runtime.hpp"
#include "stock_native_client_state.hpp"
namespace stock_native_bot {
// Pure native value packing of one update's outgoing authority messages (bot_state, bot_ram, bot_observation).
stock_native_client_state::Value packNativeOutputs(const std::vector<Output>&,std::int64_t horizon);
}
