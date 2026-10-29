#include "pch.hpp"
#include "stock_native_bot_weapon_world.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace stock_native_bot {
namespace {
std::vector<stock_native_bot_armor::Layer> layers(const std::vector<geometry::Hit>& hits)
{
    std::vector<stock_native_bot_armor::Layer> result;result.reserve(hits.size());
    for(const auto& h:hits)result.push_back({h.distance,h.angle_cosine,h.component,h.material});
    return result;
}
geometry::Pose pose(geometry::Pose p,const stock_native_bot_armor::Body& b)
{p.position={{b.x,b.y,b.z}};p.yaw=b.yaw;p.pitch=b.pitch;p.roll=b.roll;return p;}
}
const GeometryRow* NativeWeaponWorld::row(std::int64_t id)const
{const auto it=context_.rows.find(id);return it==context_.rows.end()?nullptr:&it->second;}
std::vector<geometry::Hit> NativeWeaponWorld::intersections(const GeometryRow& row,const geometry::Pose& pose,
    const step::Point& start,const step::Point& end)const
{
    if(!row.available)return {};
    return context_.vehicles.intersections(row.key,pose,start,end);
}
void NativeWeaponWorld::bind(std::int64_t source,std::int64_t target,geometry::Pose observed,const stock_native_bot_armor::Shell* shell)
{source_=source;target_=target;target_pose_=observed;target_pose_.native_components=false;target_pose_.native_hydraulics=false;shell_=shell;}
void NativeWeaponWorld::freezeWrecks()
{
    context_.wrecks.clear();
    for(const auto& pair:context_.rows)if(pair.second.available&&!pair.second.alive)context_.wrecks.push_back(pair.first);
}
bool NativeWeaponWorld::turret_clear(const step::AimPose& before,const step::AimPose& after)
{
    stock_native_world::TurretSweep sweep;sweep.actor=source_;
    auto convert=[](const step::AimPose&p){stock_native_world::TurretPose result;result.position=p.position;result.yaw=p.yaw;result.pitch=p.pitch;result.roll=p.roll;return result;};
    sweep.start=convert(before);sweep.end=convert(after);
    return !context_.physics.turret_blocks(sweep);
}
bool NativeWeaponWorld::barrel_under_water(const step::Point& start)
{
    auto end=start;end[1]+=.1;double distance=-1.;
    const auto result=context_.scene.water(start,end,distance);
    return result==stock_native_world::QueryStatus::Failed||distance>0.;
}
bool NativeWeaponWorld::vehicle_segment(const step::LaneBody& body,const step::Point& start,const step::Point& end)
{
    const auto actor=row(body.id);if(!actor||!actor->available)return false;
    return !intersections(*actor,actor->pose,start,end).empty();
}
bool NativeWeaponWorld::debris_segment(const step::Point& start,const step::Point& end)
{return bool(context_.turrets.block_distance(start,end,context_.server_time_ms,*this));}
bool NativeWeaponWorld::static_segment_blocked(const step::Point& start,const step::Point& end)
{
    stock_native_world::Hit hit;
    return context_.scene.segment(start,end,128,{},hit)!=stock_native_world::QueryStatus::Miss;
}
bool NativeWeaponWorld::wreck_segment_blocked(const step::Point& start,const step::Point& end)
{
    for(const auto id:context_.wrecks) {
        if(id==source_||id==target_)continue;
        const auto actor=row(id);if(actor&&!intersections(*actor,actor->pose,start,end).empty())return true;
    }
    return debris_segment(start,end);
}
std::optional<double> NativeWeaponWorld::nominal_damage(const step::Point& start,const step::Point& end)
{
    const auto target=row(target_);if(!target||!shell_)return {};
    try {
        const auto hits=intersections(*target,target_pose_,start,end);
        const double length=std::sqrt(std::pow(start[0]-end[0],2)+std::pow(start[1]-end[1],2)+std::pow(start[2]-end[2],2));
        return stock_native_bot_armor::nominal_damage(*shell_,length,layers(hits));
    } catch(const std::exception&) {return {};}
}
step::ArcQuery NativeWeaponWorld::segment(const step::Point& start,const step::Point& end,step::Point& point)
{
    stock_native_world::Hit hit;
    const auto result=context_.scene.segment(start,end,128,{},hit);
    if(result==stock_native_world::QueryStatus::Failed)return step::ArcQuery::failed;
    const auto finish=result==stock_native_world::QueryStatus::Found?hit.point:end;
    // Arc work has no source/target exclusion: the original queue shares its
    // chord service across all guns and tests the complete retained wreck set.
    for(const auto id:context_.wrecks) {
        const auto actor=row(id);if(actor&&!intersections(*actor,actor->pose,start,finish).empty())return step::ArcQuery::failed;
    }
    if(debris_segment(start,finish))return step::ArcQuery::failed;
    point=finish;return result==stock_native_world::QueryStatus::Found?step::ArcQuery::hit:step::ArcQuery::clear;
}
bool NativeWeaponWorld::ready(const stock_native_bot_armor::Body& body)
{
    const auto actor=row(body.id);if(!actor||!actor->available)return false;
    const auto vehicle=context_.vehicles.find(actor->key);return vehicle&&vehicle->ready;
}
stock_native_bot_armor::Body NativeWeaponWorld::physical_body(const stock_native_bot_armor::Body&body)
{
    const auto actor=row(body.id);if(!actor)throw std::runtime_error("Native ram body record is unavailable");
    const auto vehicle=context_.vehicles.find(actor->key);if(!vehicle||!vehicle->ready)throw std::runtime_error("Native ram body geometry is unavailable");
    const auto frame=geometry::frames(*vehicle,pose(actor->pose,body));const auto origin=frame.body.applyToOrigin();
    auto result=body;result.x=origin.x;result.y=origin.y;result.z=origin.z;
    result.yaw=frame.body.yaw();result.pitch=frame.body.pitch();result.roll=frame.body.roll();
    const auto&chassis=vehicle->components[0];const auto&hull=vehicle->components[1];
    result.shape={{std::max({std::abs(chassis.minimum[0]),std::abs(chassis.maximum[0]),.8}),
        std::max({std::abs(chassis.minimum[2]),std::abs(chassis.maximum[2]),1.}),chassis.minimum[1],
        std::max(chassis.maximum[1],vehicle->hull_offset[1]+hull.maximum[1])}};
    return result;
}
step::Point NativeWeaponWorld::native_point(const step::Point&point)
{return {{float(point[0]),float(point[1]),float(point[2])}};}
std::optional<stock_native_bot_armor::Hull> NativeWeaponWorld::hull(const stock_native_bot_armor::Body& body)
{
    const auto actor=row(body.id);if(!actor)return {};
    const auto vehicle=context_.vehicles.find(actor->key);if(!vehicle)return {};
    const auto& part=vehicle->components[1];return stock_native_bot_armor::Hull{part.minimum,part.maximum,vehicle->hull_offset};
}
std::vector<stock_native_bot_armor::Layer> NativeWeaponWorld::segment(const stock_native_bot_armor::Body& body,const step::Point& start,const step::Point& end)
{
    const auto actor=row(body.id);if(!actor)return {};
    return layers(intersections(*actor,pose(actor->pose,body),start,end));
}
std::vector<double> NativeWeaponWorld::segment(stock_native_bot_turret::Owner owner,bool gun,const step::Point& start,const step::Point& end)
{
    const geometry::Key key{owner.human?offline_simulation::ActorKind::Human:offline_simulation::ActorKind::Bot,owner.id};
    const auto vehicle=context_.vehicles.find(key);if(!vehicle)return {};
    const auto& component=vehicle->components[gun?3:2];if(!component.tree)return {};
    const BW::Vector3 a{float(start[0]),float(start[1]),float(start[2])},b{float(end[0]),float(end[1]),float(end[2])};
    std::vector<double> result;
    for(const auto& hit:BW::stockBspSegment(*component.tree,a,b))result.push_back(hit.distance);
    return result;
}
}
