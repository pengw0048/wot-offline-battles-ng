#pragma once
#include "stock_native_bot_internal_geometry.hpp"
#include <array>
#include <set>
namespace stock_native_projectile_damage {
// Each segment is apex -> apex + normalized direction * axial depth, in the
// corresponding component frame. Results are stable logical-device entries.
std::vector<wot::offline_bot_step::ImpactHit> internalCone(
 const wot::offline_bot_step::InternalLayout&,
 const std::array<wot::offline_bot_step::InternalSegment,4>&,
 double depth,const std::set<std::string>& covered);
}
