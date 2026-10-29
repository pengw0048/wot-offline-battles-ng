#pragma once
#include "stock_native_projectile_damage.hpp"
#include "stock_native_bot_vehicle_geometry.hpp"
#include "stock_native_bot_internal_geometry.hpp"
#include <functional>
namespace stock_native_projectile_damage {
// One frozen target frame for a direct hit or one splash victim. The caller
// supplies the native scene/turret visibility proof at the impact clock.
class GeometryWorld final:public DamageWorld {
 stock_native_bot_geometry::Vehicle vehicle_;
 const wot::offline_bot_step::InternalLayout& layout_;
 stock_native_bot_geometry::Pose pose_;
 std::function<bool(const Point&,const Point&,const Point&)> visible_;
 std::array<std::optional<unsigned>,2> stickers_;
public:
 GeometryWorld(const stock_native_bot_geometry::Vehicle&,
  const wot::offline_bot_step::InternalLayout&,const stock_native_bot_geometry::Pose&,
  std::function<bool(const Point&,const Point&,const Point&)>,
  std::array<std::optional<unsigned>,2> stickers={},std::optional<bool> detached={});
 std::vector<BlastComponent> components(const Point&,double)override;
 Point toWorld(unsigned,const Point&)override;
 std::vector<ImpactLayer> vehicleRay(const Point&,const Point&)override;
 bool visible(const Point&,const Point&,const Point&)override;
 std::vector<CriticalLayer> internal(const Target&,const Point&,const Point&,bool,
  const std::vector<ImpactLayer>&)override;
 std::optional<std::uint64_t> sticker(const Target&,const Point&,const Point&,
  const std::vector<ImpactLayer>&,const std::optional<ArmorContact>&,int)override;
};
}
