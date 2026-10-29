#pragma once
#include "stock_native_client_state.hpp"
#include "stock_native_projectile_damage_python.hpp"
namespace stock_native_client_projectile {
// Accepted actor/critical/equipment data is projected once at ingress. Damage
// calculation never reads a Python entity or calls a live gameplay adapter.
stock_native_projectile_damage::Target targetState(
    const stock_native_client_state::Actor&,
    const stock_native_projectile_damage::TargetDescriptor&,
    const stock_native_projectile_damage::StunConfig&);
}
