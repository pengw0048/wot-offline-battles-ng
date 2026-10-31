#pragma once
#include "stock_native_projectile_owner.hpp"
#include "stock_native_client_state.hpp"
#include <string>
#include <vector>
struct _object;
namespace stock_native_projectile {
// Plain-data wire adapters over decoded protocol Values. False either when the
// row is not this owner's kind or, with `error` set, when it is malformed.
bool readLaunch(const stock_native_client_state::Value&,Launch&,std::string* error=nullptr);
// The visible owner accepts only its player launches; the worker entry above
// remains Bot-only and rejects player input identities.
bool readPlayerLaunch(const stock_native_client_state::Value&,Launch&,std::string* error=nullptr);
// One send_projectile_<kind> payload (its parameters by name) under its owner serial.
struct WireDispatch {std::uint64_t serial=0;std::string kind;stock_native_client_state::Value wire;};
std::vector<WireDispatch> packDispatches(const std::vector<Dispatch>&);
_object* packMetrics(const OwnerMetrics&);
}
