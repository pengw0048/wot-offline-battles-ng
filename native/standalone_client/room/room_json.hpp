#pragma once
#include "room_value.hpp"
#include <string>

namespace wot::room {
// Cold configuration and durable receipts only; battle transport stays binary.
Value parseJson(const std::string&);
std::string writeJson(const Value&);
}
