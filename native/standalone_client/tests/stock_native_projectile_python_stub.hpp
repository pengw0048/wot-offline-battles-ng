#pragma once
#include "stock_native_projectile_damage_python.hpp"
#include "stock_native_bot_internal_geometry.hpp"
#include "stock_native_bot_turret_obstacles.hpp"
#include "offline_simulation_weapons.h"
#include <functional>
#include <cmath>
namespace BW {
struct Vector3 {float x,y,z;Vector3(float a=0,float b=0,float c=0):x(a),y(b),z(c){};Vector3 operator-(const Vector3&b)const{return {x-b.x,y-b.y,z-b.z};}Vector3 operator*(float f)const{return {x*f,y*f,z*f};}float length()const{return std::sqrt(x*x+y*y+z*z);}void normalise(){const float n=length();if(n){x/=n;y/=n;z/=n;}}};
struct BSPTree {};
struct StockBspSegmentHit {float distance=0;};
inline std::vector<StockBspSegmentHit> stockBspSegment(const BSPTree&,const Vector3&,const Vector3&){return {};}
}
namespace stock_native_bot_geometry {
using Point=std::array<double,3>;using Material=stock_native_bot_armor::Material;
struct Component {const BW::BSPTree*tree=nullptr;std::map<std::uint32_t,Material>materials;};
struct Vehicle {std::array<Component,4>components;bool ready=true,detached=false;};
struct Pose {Point position{};double yaw=0,pitch=0,roll=0,turret_yaw=0,gun_pitch=0;int siege=0;bool native_components=true,native_hydraulics=true;};
struct Hit {double distance=0,angle_cosine=1;unsigned component=1;std::uint32_t material_kind=1;Point local_point{},world_point{},world_normal{{0,0,-1}};const Material*material=nullptr;};
inline std::vector<Hit> intersections(const Vehicle&v,const Pose&pose,const Point&a,const Point&b){if(a[2]>=pose.position[2]||b[2]<pose.position[2])return {};const auto m=v.components[1].materials.find(1);if(m==v.components[1].materials.end())return {};Hit h;h.distance=pose.position[2]-a[2];h.material=&m->second;return {h};}
class Vehicles {};
}
namespace stock_native_bot {
struct TestActor {struct Motion {std::array<double,3>position{};double yaw=0,pitch=0,roll=0;}motion;struct Siege{int mode=0;}siege;int mode=0;};
struct ServiceEntity {std::array<stock_native_bot_geometry::Vehicle,2>geometry;std::array<wot::offline_bot_step::InternalLayout,2>interior;std::array<std::optional<stock_native_projectile_damage::TargetDescriptor>,2>projectile;std::array<bool,2>pitch_hull_aiming{{false,false}};std::array<std::map<int,std::array<std::optional<unsigned>,2>>,2>projectile_stickers;std::array<std::vector<stock_native_projectile_damage::LaunchDescriptor>,2>launch_shots;int mode=0;bool available=true,record_ready=true;};
class Runtime {
public:
 struct Store {struct State {struct Aim{double turret_yaw=0,gun_pitch=0;}aim;std::int64_t fire_seq=0;};std::deque<offline_simulation::weapons::LaunchRecord>launches;std::map<std::int64_t,State>states;const State&at(offline_simulation::ActorKey key)const{return states.at(key.id);}const auto&pending()const{return launches;}}store;
 std::map<std::int64_t,TestActor>rows;std::vector<std::pair<std::int64_t,std::int64_t>>acks;
 const auto&actors()const{return rows;}const Store&weapons()const{return store;}
 bool acknowledgeLaunch(std::int64_t bot,std::int64_t sequence){acks.push_back({bot,sequence});for(auto i=store.launches.begin();i!=store.launches.end();++i)if(i->actor.id==bot&&i->edge.shot_seq==sequence){store.launches.erase(i);break;}return true;}
};
class NativeServices {public:std::map<offline_simulation::ActorKey,ServiceEntity>rows;stock_native_bot_turret::Obstacles obstacles;const ServiceEntity*find(offline_simulation::ActorKey key)const{const auto i=rows.find(key);return i==rows.end()?nullptr:&i->second;}auto&turrets(){return obstacles;}};
}
namespace stock_native_world {class WorldEvents {public:bool mirrorOnly(const Destruction&){return true;}};}
namespace stock_native_projectile_damage {
class GeometryWorld:public DamageWorld {
 stock_native_bot_geometry::Vehicle v_;stock_native_bot_geometry::Pose p_;std::function<bool(const Point&,const Point&,const Point&)>visible_;
public:
 GeometryWorld(const stock_native_bot_geometry::Vehicle&v,const wot::offline_bot_step::InternalLayout&,const stock_native_bot_geometry::Pose&p,std::function<bool(const Point&,const Point&,const Point&)>f,std::array<std::optional<unsigned>,2>,std::optional<bool> detached={}):v_(v),p_(p),visible_(f){if(detached)v_.detached=*detached;}
 std::vector<BlastComponent>components(const Point&,double)override{return {};}
 Point toWorld(unsigned,const Point&p)override{return p;}
 std::vector<ImpactLayer>vehicleRay(const Point&a,const Point&b)override{std::vector<ImpactLayer>out;for(const auto&h:stock_native_bot_geometry::intersections(v_,p_,a,b)){ImpactLayer x(h.distance,h.angle_cosine,h.component,h.material);x.world_normal=h.world_normal;out.push_back(x);}return out;}
 bool visible(const Point&a,const Point&b,const Point&c)override{return visible_(a,b,c);}
 std::vector<CriticalLayer>internal(const Target&,const Point&,const Point&,bool,const std::vector<ImpactLayer>&)override{return {};}
 std::optional<std::uint64_t>sticker(const Target&,const Point&,const Point&,const std::vector<ImpactLayer>&,const std::optional<ArmorContact>&,int)override{return {};}
};
}
