#include "pch.hpp"
#include "stock_native_client_projectile_world.hpp"
#include "stock_native_client_projectile.hpp"
#include "stock_native_client_projectile_state.hpp"
#include "stock_native_projectile_scene.hpp"
#include "stock_native_projectile_geometry.hpp"
#include "stock_native_projectile_random.hpp"
#include "stock_native_bot_world_adapter.hpp"
#include "stock_native_bot_python.hpp"
#include "stock_native_bot_vehicle_geometry.hpp"
#include <algorithm>
#include <array>
#include <map>
#include <set>
namespace stock_native_client_projectile {
namespace p=stock_native_bot_python;namespace cs=stock_native_client_state;namespace geo=stock_native_bot_geometry;namespace world=stock_native_world;namespace shot=stock_native_projectile;
namespace {
using Pose=shot::Pose;
Point rounded(Point point){for(auto&value:point)value=float(value);return point;}
BW::Vector3 vector(const Point&p){return BW::Vector3{float(p[0]),float(p[1]),float(p[2])};}
Pose pose(const cs::Actor&a){Pose out;out.position={a.current[0],a.current[1],a.current[2]};out.yaw=a.current[3];out.pitch=a.current[4];out.roll=a.current[5];out.turret_yaw=shot::angle_delta(out.yaw,a.current[6]);out.gun_pitch=a.current[7];out.siege=int(a.siege_state);return out;}
geo::Pose geometryPose(const Pose&p,bool historical){geo::Pose out;out.position=p.position;out.yaw=p.yaw;out.pitch=p.pitch;out.roll=p.roll;out.turret_yaw=p.turret_yaw;out.gun_pitch=p.gun_pitch;out.siege=p.siege;out.native_components=!historical;out.native_hydraulics=!historical;return out;}
struct Resources {
    std::shared_ptr<cs::Actor> actor;std::shared_ptr<const void> query_owner;
    std::array<geo::Vehicle,2> geometry;
    std::array<damage::TargetDescriptor,2> descriptor;
    std::array<wot::offline_bot_step::InternalLayout,2> interior;
    std::array<bool,2> pitch_hull{};
    std::array<damage::Target,2> targets;
    cs::Value effective;
    std::uint64_t descriptor_revision=0,state_revision=0;
};
}
struct World::State {
    std::shared_ptr<cs::State> battle;
    stock_native_client_drive::SceneAccess access;
    std::shared_ptr<stock_native_client_query::ProjectileTurrets> turrets;
    shot::ShotScene scene;
    shot::Random random;
    std::unique_ptr<p::Ref> weak_battle;
    shot::Owner* owner=nullptr;
    std::map<std::string,Resources> resources;
    std::vector<std::string> order;
    std::set<std::string> actor_keys;
    std::vector<world::Destruction> destructions;
    std::map<std::string,double> anchors;
    std::map<std::string,std::array<std::optional<unsigned>,2>> stickers;
    damage::StunConfig stun;
    std::uint64_t epoch;
    std::int64_t battle_end_ms=0;
    double now=0.;
    State(std::shared_ptr<cs::State>b,stock_native_client_drive::SceneAccess a,std::shared_ptr<stock_native_client_query::ProjectileTurrets>t):battle(std::move(b)),access(std::move(a)),turrets(std::move(t)),scene(*access.registry,*access.scene),epoch(battle->epoch){}
    bool current()const{return !battle->retired&&battle->epoch==epoch&&access.scene&&access.scene->current();}
    PyObject* python(){if(!weak_battle)throw p::Error("Native player projectile lifecycle was not installed");auto*value=PyWeakref_GetObject(weak_battle->get());if(value==Py_None)throw p::Error("Native player projectile battle was released");return value;}
    Resources* find(const std::string&key){const auto at=resources.find(key);return at==resources.end()?nullptr:&at->second;}
    const Resources* find(const std::string&key)const{const auto at=resources.find(key);return at==resources.end()?nullptr:&at->second;}
    std::int64_t server(double at)const{return owner?owner->serverTime(at).value_or(0):0;}
    bool visible(const Point&start,const Point&contact,const Point&burst,double at){const auto a=rounded(start),b=rounded(contact),origin=rounded(burst);if(double((vector(b)-vector(origin)).length())<=.001)return true;const double distance=double((vector(b)-vector(a)).length());
        if(turrets){const auto hit=turrets->block(a,b,double(server(at)));if(hit&&*hit+.001<distance)return false;}world::Hit hit;const auto status=access.pipeline->horizontal(a,b,hit);if(status==world::QueryStatus::Failed)throw p::Error("Native player projectile blast visibility failed");return status!=world::QueryStatus::Found||double((vector(hit.point)-vector(a)).length())+.001>=distance;
    }
    damage::GeometryWorld geometry(const Resources&r,const Pose&p,bool historical,double at,const std::string&projectile){const auto mode=p.siege==2?1:0;return damage::GeometryWorld(r.geometry[mode],r.interior[mode],geometryPose(p,historical),[this,at](const Point&a,const Point&b,const Point&c){return visible(a,b,c,at);},stickers[projectile],r.actor->detached);}
};
World::World(std::shared_ptr<cs::State>state,stock_native_client_drive::SceneAccess scene,std::shared_ptr<stock_native_client_query::ProjectileTurrets>turrets){if(!scene.registry||!scene.scene||!scene.pipeline)throw p::Error("Native player projectile scene is unavailable");state_=std::make_unique<State>(std::move(state),std::move(scene),std::move(turrets));}
World::~World()=default;
bool World::current()const{return state_->current();}
bool World::install(PyObject*battle){try{auto&s=*state_;if(!s.current())throw p::Error("Native player projectile scene is stale");s.weak_battle=std::make_unique<p::Ref>(PyWeakref_NewRef(battle,nullptr));p::Ref module(PyImport_ImportModule("random"));auto instance=p::value(module.get(),"_inst");if(!s.random.install(instance.get()))return false;p::Ref stun(PyImport_ImportModule("items.stun"));auto config=p::value(stun.get(),"g_cfg");if(!damage::readStunConfig(config.get(),s.stun))return false;
    auto raw=p::value(battle,"_config");const double prebattle=std::max(0.,p::numeric(raw.get(),"prebattleCountdownSeconds",10.)),duration=std::max(1.,p::numeric(raw.get(),"battleDurationSeconds",900.));s.battle_end_ms=std::int64_t(std::round((prebattle+duration)*1000.));return true;
}catch(const std::exception&e){if(!PyErr_Occurred())PyErr_SetString(PyExc_ValueError,e.what());return false;}}
bool World::ingress(PyObject*battle,double now){try{auto&s=*state_;if(!s.current()||s.python()!=battle)throw p::Error("Native player projectile ingress is stale");s.now=now;bool changed=false;std::set<std::string> seen;
    for(const auto&entry:s.battle->actors){auto actor=entry.second;if(actor->missing||actor->tombstone||!actor->ready||!actor->has_current)continue;seen.insert(entry.first);try{const auto*geometry=stock_native_client_query::geometry(*actor);auto*descriptor=stock_native_client_query::descriptor(*actor);if(!geometry||!descriptor){changed=s.resources.erase(entry.first)>0||changed;continue;}auto&cached=s.resources[entry.first];
        if(cached.actor!=actor||cached.query_owner!=actor->geometry||cached.descriptor_revision!=actor->descriptor_revision||(!(cached.effective.reference&&cached.effective.reference==actor->state.get("effective_params").reference)&&cached.effective!=actor->state.get("effective_params"))){Resources fresh;fresh.actor=actor;fresh.query_owner=actor->geometry;fresh.descriptor_revision=actor->descriptor_revision;fresh.effective=actor->state.get("effective_params");
            auto default_descriptor=p::value(descriptor,"defaultVehicleDescr"),siege_descriptor=p::value(descriptor,"siegeVehicleDescr");std::array<PyObject*,2> modes={default_descriptor.get()==Py_None?descriptor:default_descriptor.get(),siege_descriptor.get()==Py_None?descriptor:siege_descriptor.get()};
            p::Ref profile(cs::write(actor->state.get("effective_params").get("critical")));for(unsigned mode=0;mode<2;++mode){auto*exact=modes[mode];if(exact==descriptor)fresh.geometry[mode]=*geometry;else if(!geo::readVehicle(exact,Py_None,true,actor->descriptor_revision,fresh.geometry[mode]))throw p::Error("Native player projectile alternate mode geometry is unavailable");
                if(!damage::readTargetDescriptor(exact,exact,fresh.descriptor[mode],profile.get())||!wot::offline_bot_step::load_internal_layout(exact,fresh.interior[mode]))throw p::Error("Native player projectile target installation failed");fresh.geometry[mode].ready=actor->ready;fresh.geometry[mode].native_body=geometry->native_body;fresh.geometry[mode].native_ground=geometry->native_ground;fresh.pitch_hull[mode]=p::truth(exact,"isPitchHullAimingAvailable");}
            cached=std::move(fresh);changed=true;
        }
        for(auto&geometry:cached.geometry)geometry.detached=actor->detached;
        if(cached.state_revision!=actor->revision){for(unsigned mode=0;mode<2;++mode)cached.targets[mode]=targetState(*actor,cached.descriptor[mode],s.stun);cached.state_revision=actor->revision;}
        }catch(const std::exception&error){changed=s.resources.erase(entry.first)>0||changed;PyErr_Clear();PySys_WriteStdout("[Offline LAN 0.9.22] Native player projectile resource %s: %s\n",entry.first.c_str(),error.what());}
    }
    for(auto at=s.resources.begin();at!=s.resources.end();)if(!seen.count(at->first)){changed=true;at=s.resources.erase(at);}else ++at;
    changed=changed||seen!=s.actor_keys;s.actor_keys=std::move(seen);
    if(changed||s.order.empty()){s.order.clear();auto records=p::value(battle,"_records");if(!PyDict_Check(records.get()))throw p::Error("Native player projectile lifecycle records are unavailable");Py_ssize_t cursor=0;PyObject*key,*value;while(PyDict_Next(records.get(),&cursor,&key,&value)){const auto name=p::string(key);if(s.battle->actors.count(name))s.order.push_back(name);}}
    return true;
}catch(const std::exception&e){if(!PyErr_Occurred())PyErr_SetString(PyExc_ValueError,e.what());return false;}}
void World::clock(shot::Owner*owner,double now){state_->owner=owner;state_->now=now;if(owner){for(auto at=state_->anchors.begin();at!=state_->anchors.end();)if(!owner->flights().count(at->first)){state_->stickers.erase(at->first);at=state_->anchors.erase(at);}else ++at;}}
std::vector<shot::Record> World::records(){auto&s=*state_;if(!s.current())throw p::Error("Native player projectile records are stale");std::vector<shot::Record> rows;for(const auto&key:s.order){const auto*actor=s.battle->find(key);if(!actor)continue;const auto&a=*actor;if(a.missing||a.tombstone||!a.ready||!a.has_current)continue;const auto*r=s.find(key);shot::Record row;row.key=key;row.id=a.id;row.engine_id=a.engine_id;row.human=a.kind=="player";row.local=a.local;row.ready=a.ready;row.tombstone=a.tombstone;row.started=a.ready;row.alive=a.alive;row.detached=a.detached;row.pitch_hull_aiming=r&&r->pitch_hull[a.siege_state==2?1:0];row.current=pose(a);rows.push_back(std::move(row));}return rows;}

bool World::liveGeometry(const shot::Record&r,const Pose&p)const{const auto*resource=state_->find(r.key);return r.detached||(resource&&resource->pitch_hull[p.siege==2?1:0]);}
shot::CollisionQuery World::collide(const shot::Record&r,const Point&a,const Point&b,const std::optional<Pose>&historic){auto&s=*state_;shot::CollisionQuery out;out.start=rounded(a);out.end=rounded(b);out.length=double((vector(out.end)-vector(out.start)).length());const auto*resource=s.find(r.key);if(!s.current()||!resource){out.valid=false;return out;}const auto&p=historic?*historic:r.current;const auto mode=p.siege==2?1:0;const auto&geometry=resource->geometry[mode];if(!geometry.ready){out.valid=false;return out;}
    for(const auto&hit:geo::intersections(geometry,geometryPose(p,bool(historic)),out.start,out.end)){shot::Collision collision;collision.distance=hit.distance;collision.cosine=hit.angle_cosine;collision.component=hit.component;collision.material_kind=hit.material_kind;collision.local_point=hit.local_point;collision.world_point=hit.world_point;collision.world_normal=hit.world_normal;const auto&materials=resource->geometry[mode].components[hit.component].materials;const auto found=materials.find(hit.material_kind);collision.material=found==materials.end()?nullptr:&found->second;out.collisions.push_back(collision);}return out;}
std::optional<double> World::turretBlock(const shot::Chord&chord){auto&s=*state_;return s.turrets?s.turrets->block(rounded(chord.start),rounded(chord.end),double(s.server(chord.end_time)),double(s.server(chord.start_time))):std::nullopt;}
shot::SceneResult World::scenery(const shot::Cursor&,shot::FlightMeta&meta,const Point&a,const Point&b,const Point&direction){auto&s=*state_;if(!s.current())throw p::Error("Native player projectile world is stale");return s.scene.trace(meta,a,b,direction,s.now,s.random,s.destructions);}
std::vector<world::Destruction> World::takeDestructions(){auto&s=*state_;std::vector<world::Destruction> result;result.swap(s.destructions);for(const auto&event:result)stock_native_client_drive::mirrorDestruction(s.python(),event);return result;}
damage::Resolution World::resolve(shot::Flight&flight,const shot::Cursor&cursor,const shot::TerminalData&data){auto&s=*state_;damage::Resolution result;if(!s.current())throw p::Error("Native player projectile damage owner is stale");const double elapsed=std::max(0.,cursor.cursor_time-cursor.launch_time);Point incoming;for(unsigned i=0;i<3;++i)incoming[i]=cursor.velocity[i]+cursor.gravity[i]*elapsed;const auto server=s.server(cursor.cursor_time),checked=std::max(flight.canonical.checked_ms,flight.canonical.segment_ms+std::int64_t(std::round(elapsed*1000.)));
    if(data.target){const auto*r=s.find(*data.target);if(r&&r->actor->alive){const auto&p=data.collision_pose?*data.collision_pose:pose(*r->actor);auto target=r->targets[p.siege==2?1:0];target.position=p.position;if(target.critical_target_base_revision<0||target.critical_target_ack_sequence<0)throw p::Error("Native player projectile critical frontier is unavailable");auto geometry=s.geometry(*r,p,bool(data.collision_pose),cursor.cursor_time,flight.canonical.id);damage::DirectEvidence evidence;evidence.impact=data.impact;evidence.query_start=data.query.start;evidence.query_end=data.query.end;evidence.incoming=incoming;evidence.trajectory_start=data.trajectory_query.start;evidence.trajectory_end=data.trajectory_query.end;evidence.trajectory_start_time=data.trajectory_query.start_time;evidence.trajectory_end_time=data.trajectory_query.end_time;evidence.has_trajectory_query=true;evidence.range_distance=shot::length(flight.meta.range_origin,data.impact);evidence.piercing_loss=data.piercing_loss;evidence.checked_distance=std::max({cursor.distance,flight.canonical.checked_distance,flight.acked_distance,flight.queued_distance});evidence.elapsed_ms=checked;evidence.impact_server_ms=server;evidence.battle_end_ms=s.battle_end_ms;
        for(const auto&hit:data.query.collisions){damage::ImpactLayer layer(hit.distance,hit.cosine,hit.component,hit.material,hit.material?hit.material->use_lining:true);layer.local_point=hit.local_point;layer.world_normal=hit.world_normal;evidence.collisions.push_back(layer);}if(!data.query.collisions.empty())evidence.world_normal=std::min_element(data.query.collisions.begin(),data.query.collisions.end(),[](const auto&a,const auto&b){return a.distance<b.distance;})->world_normal;result.direct=damage::direct(flight.meta.shot,target,evidence,geometry,s.random,result.ricochet);}}
    if(flight.canonical.he&&!(result.direct&&result.direct->result==2)){const double magnitude=std::sqrt(incoming[0]*incoming[0]+incoming[1]*incoming[1]+incoming[2]*incoming[2]);Point origin=rounded(data.impact);if(magnitude>1.e-9){auto burst=vector(origin),direction=vector(incoming);direction.normalise();const auto offset=burst-direction*.002f;origin={offset.x,offset.y,offset.z};}
        for(const auto&row:records()){if(result.splash.size()>=30)break;if((data.target&&row.key==*data.target)||!row.alive||!row.started||row.tombstone)continue;try{const auto*r=s.find(row.key);if(!r)continue;const auto historical=s.owner->historic(row.key,cursor.cursor_time);if(!historical)throw p::Error("Native player projectile splash pose is unavailable");const bool frozen=!liveGeometry(row,*historical);const auto&p=frozen?*historical:row.current;auto target=r->targets[p.siege==2?1:0];target.position=p.position;auto geometry=s.geometry(*r,p,frozen,cursor.cursor_time,flight.canonical.id);auto effect=damage::splash(flight.meta.shot,target,data.impact,origin,data.impact,server,s.battle_end_ms,geometry,s.random);if(effect)result.splash.push_back(std::move(*effect));}catch(const std::exception&error){PyErr_Clear();PySys_WriteStdout("[Offline LAN 0.9.22] Native player projectile splash victim %s: %s\n",row.key.c_str(),error.what());}}}
    return result;
}
std::string World::shooterKey(const shot::Launch&launch)const{return "player:"+std::to_string(launch.shooter);}
std::optional<double> World::localLaunchTime(const shot::Launch&launch,double)const{const auto found=state_->anchors.find(launch.id);return found==state_->anchors.end()?std::nullopt:std::optional<double>(found->second);}
double World::wireValue(double value)const{return wireRound(value);}
void World::frozen(const Frozen&frozen){state_->anchors[frozen.flight.id]=frozen.local_time;state_->stickers[frozen.flight.id]=frozen.stickers;}
damage::Random& World::random(){return state_->random;}
}
