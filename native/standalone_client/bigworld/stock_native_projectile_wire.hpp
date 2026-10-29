#pragma once
#include "stock_native_projectile_owner.hpp"
struct _object;
namespace stock_native_projectile {
// Plain-data wire adapters. They never invoke Python arithmetic, descriptors,
// damage helpers or user-defined conversions during message ingress.
bool readLaunch(_object*,Launch&);
// The visible owner accepts only its player launches; the worker entry above
// remains Bot-only and rejects player input identities.
bool readPlayerLaunch(_object*,Launch&);
_object* packDispatches(const std::vector<Dispatch>&);
_object* packMetrics(const OwnerMetrics&);
}
