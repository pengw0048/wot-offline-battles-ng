#if !defined(WOT_CLIENT_IMPACT_HOST_BUILD)
#include "pch.hpp"
#endif
#include "stock_native_client_drive_impact.hpp"
#ifdef hypot
#undef hypot
#endif
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace stock_native_client_drive {
namespace {
using Value=stock_native_client_state::Value;namespace geo=stock_native_bot_geometry;
BW::Vector3 native(const Point&p){return BW::Vector3(float(p[0]),float(p[1]),float(p[2]));}
Point point(const BW::Vector3&p){return {p.x,p.y,p.z};}
Value number(double n){Value out;out.kind=Value::Kind::Number;out.number=n;return out;}
}
PlayerImpact::PlayerImpact(PublicationLedger&publication,ImpactConfig config,step::InternalLayout interior):interior_(std::move(interior)),publication_(publication),config_(config){
    if(config.maximum_health<1||config.safe_speed<0.||!std::isfinite(config.safe_speed)||config.maximum_speed<config.safe_speed||!std::isfinite(config.maximum_speed)||!config.maximum_pending)throw std::invalid_argument("Native player impact metadata is invalid");
}
void PlayerImpact::geometry(const geo::Vehicle&vehicle,geo::Pose pose){vehicle_=&vehicle;pose_=std::move(pose);}
std::vector<step::ImpactHit>PlayerImpact::material_segment(const Point&a,const Point&b){
    if(!vehicle_||!vehicle_->ready)throw std::runtime_error("Native player impact external geometry is unavailable");std::vector<step::ImpactHit>out;
    // #1513 constructs both endpoints as Vector3 before either external or
    // adopted internal hit testing. Preserve that one float32 crossing.
    const auto start=point(native(a)),end=point(native(b));
    for(const auto&hit:geo::intersections(*vehicle_,pose_,start,end))if(hit.material&&!hit.material->device.empty())out.push_back({hit.distance,hit.material->device});return out;
}
std::vector<step::ImpactHit>PlayerImpact::interior_segment(const Point&a,const Point&b){
    if(!vehicle_||!vehicle_->ready||!interior_.valid)throw std::runtime_error("Native player impact internal geometry is unavailable");
    const auto frames=geo::frames(*vehicle_,pose_);std::array<step::InternalSegment,4>segments{};const auto start=native(a),end=native(b);
    for(unsigned i=0;i<4;++i){if(i>=2&&vehicle_->detached)continue;const auto&root=i==0?frames.chassis_inverse:frames.body_inverse;
        segments[i]={point(frames.component_inverse[i].applyPoint(root.applyPoint(start))),point(frames.component_inverse[i].applyPoint(root.applyPoint(end))),true};}
    const auto delta=end-start;return step::internal_hits(interior_,segments,delta.length());
}
int PlayerImpact::observe(double speed,const std::vector<Point>&witnesses,const Point&normal){
    const int damage=int(config_.maximum_health*std::max(0.,std::abs(speed)-config_.safe_speed)*.03);if(damage<=0)return 0;
    speed=std::min(config_.maximum_speed,std::abs(speed));Value observation=number(speed);
    if(!witnesses.empty()){const auto contacts=step::collect_impact_contacts(witnesses,normal,speed,*this);if(!contacts.empty()){
        observation.kind=Value::Kind::Object;observation.object["impact_speed"]=number(speed);auto&rows=observation.object["module_contacts"];rows.kind=Value::Kind::Array;
        for(const auto&contact:contacts){Value row;row.kind=Value::Kind::Object;auto&name=row.object["name"];name.kind=Value::Kind::Text;name.text=contact.name;row.object["weight"]=number(contact.weight);rows.array.push_back(std::move(row));}}}
    publication_.queueLanding(std::move(observation),config_.maximum_pending);return damage;
}
int PlayerImpact::landing(PlayerState&s,double speed,bool normal_impact,const std::vector<Point>&witnesses,const Point&normal,std::optional<Point>position){
    const double lateral=std::sqrt(s.air_lateral_x*s.air_lateral_x+s.air_lateral_z*s.air_lateral_z);if(lateral>.01)s.slide_speed=std::max(s.slide_speed,lateral);s.air_lateral_x=s.air_lateral_z=0.;
    if(!normal_impact)speed=std::sqrt(speed*speed+lateral*lateral);const auto saved=pose_;if(position)pose_.position=*position;
    try{const int damage=observe(speed,witnesses,normal);pose_=saved;return damage;}catch(...){pose_=saved;throw;}
}
double PlayerImpact::world(const PlayerState&s,const ContactTrace&trace,double speed,double yaw,double now){
    if(!s.airborne||!trace.point)return 0.;double impact=0.;if(trace.normal){const auto&normal=*trace.normal;const double length=std::sqrt(normal[0]*normal[0]+normal[1]*normal[1]+normal[2]*normal[2]);
        if(std::isfinite(length)&&length>1.e-8)impact=std::max(0.,-(std::sin(yaw)*speed*normal[0]+std::cos(yaw)*speed*normal[2])/length);}
    if(impact>config_.safe_speed&&now-last_world_impact_>=.25){if(observe(impact,{*trace.point},trace.normal.value_or(Point{})))last_world_impact_=now;}return impact;
}
}
