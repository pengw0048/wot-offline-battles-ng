#include "stock_native_client_drive_geometry.hpp"
#include "stock_native_bot_python.hpp"
#include <algorithm>
#include <limits>

namespace stock_native_client_drive {
namespace {
namespace p=stock_native_bot_python;
namespace world=stock_native_world;
using Key=world::Key;
int cell(double value){const double out=std::floor(value/8.);if(!std::isfinite(out)||out<std::numeric_limits<int>::min()||out>std::numeric_limits<int>::max())throw p::Error("Native player support region is invalid");return int(out);}
}
bool LocalGeometry::sync(stock_native_world::Registry&registry,stock_native_world::Scene&scene,const std::array<Point,2>&region){
    Stamp next;next.region={cell(std::min(region[0][0],region[1][0])),cell(std::max(region[0][0],region[1][0])),
        cell(std::min(region[0][2],region[1][2])),cell(std::max(region[0][2],region[1][2]))};
    next.stream=scene.streamRevision();
    if(installed_&&next==stamp_){++reuses_;return false;}
    std::set<Key> current;
    for(int x=next.region[0];;++x){for(int z=next.region[2];;++z){
        const auto baked=registry.skin.baked_shot_bins.find({x,z});if(baked!=registry.skin.baked_shot_bins.end())current.insert(baked->second.begin(),baked->second.end());
        if(z==next.region[3])break;}if(x==next.region[1])break;}
    auto affected=current;affected.insert(members_.begin(),members_.end());
    for(const auto&id:affected){
        if(registry.isolated(id)){registry.remove(id);registry.skin.tree_instances.erase(id);continue;}
        // Admission belongs to this read-only filter store; it does not authorize native destruction.
        if(scene.refresh(id,registry)!=world::Admission::Admitted)registry.remove(id);
    }
    members_=std::move(current);stamp_=std::move(next);installed_=true;++refreshes_;return true;
}
}
