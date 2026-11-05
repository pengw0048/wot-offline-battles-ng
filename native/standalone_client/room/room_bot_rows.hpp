#pragma once
#include "room_value.hpp"
#include <utility>

namespace wot::room {
// Decode the current worker's compact checkpoint. Immutable consumable
// contracts come from this Bot's admitted round manifest, never the row.
Value decodeBotRow(const Value &row, const Value &static_state);
Value decodeBotRow(const Value &row);
// Return independently owned snapshot rows and their shared contract table.
std::pair<Value, Value> encodeSnapshotEquipment(const Value &bots);
} // namespace wot::room
