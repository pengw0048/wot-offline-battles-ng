#include "stock_native_projectile_python.hpp"
#include "stock_native_projectile_wire.hpp"
#include "stock_native_projectile_scene.hpp"
#include "stock_native_projectile_random.hpp"
#include "stock_native_projectile_geometry.hpp"
#include "stock_native_bot_services.hpp"
#include "stock_native_bot_resource_lifecycle.hpp"
#include "stock_native_world_events.hpp"
#include "stock_native_bot_python.hpp"
#include "stock_native_battle_projectiles.hpp"
#include "stock_native_battle_startup.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <memory>
#include <tuple>

namespace stock_native_projectile {
namespace p=stock_native_bot_python;
namespace bots=stock_native_bot;
namespace geo=stock_native_bot_geometry;
namespace world=stock_native_world;
namespace {
using ActorKey=offline_simulation::ActorKey;
using ActorKind=offline_simulation::ActorKind;
using LaunchRecord=offline_simulation::weapons::LaunchRecord;
ActorKey actor(const Record&r){return {r.human?ActorKind::Human:ActorKind::Bot,r.id};}
Point rounded(Point p){for(auto&v:p)v=float(v);return p;}
BW::Vector3 vector(const Point&p){return BW::Vector3(float(p[0]),float(p[1]),float(p[2]));}
Point point(const BW::Vector3&p){return {p.x,p.y,p.z};}
Point xyz(PyObject*raw){if(PyList_Check(raw)||PyTuple_Check(raw))return p::numbers<3>(raw);return {p::numeric(raw,"x"),p::numeric(raw,"y"),p::numeric(raw,"z")};}
geo::Pose geometryPose(const Pose&p,bool frozen){geo::Pose out;out.position=p.position;out.yaw=p.yaw;out.pitch=p.pitch;out.roll=p.roll;out.turret_yaw=p.turret_yaw;out.gun_pitch=p.gun_pitch;out.siege=p.siege;out.native_components=!frozen;out.native_hydraulics=!frozen;return out;}
Pose plainPose(const stock_native_client_state::Value&state,const Point&position){Pose out;out.position=position;const auto&s=state.resolved();
    if(s.kind==stock_native_client_state::Value::Kind::Object){if(s.object.count("x")&&s.object.count("y")&&s.object.count("z"))out.position={s.get("x").numeric(),s.get("y").numeric(),s.get("z").numeric()};out.yaw=s.get("yaw").numeric();out.pitch=s.get("pitch").numeric();out.roll=s.get("roll").numeric();out.turret_yaw=s.get("turret_yaw").numeric(angle_delta(out.yaw,s.get("aim_yaw").numeric(out.yaw)));out.gun_pitch=s.get("gun_pitch").numeric();out.siege=int(s.get("siege_state").numeric());}
    return out;
}
p::Ref shotWire(const damage::LaunchDescriptor&d){
    const auto&s=d.shot;auto out=p::dict(),shell=p::dict();p::set(out.get(),"speed",p::number(d.speed));p::set(out.get(),"gravity",p::number(d.gravity));p::set(out.get(),"maxDistance",p::number(d.maximum));p::set(out.get(),"piercingPower",p::numbers(std::array<double,2>{s.shell.piercing_near,s.shell.piercing_far}));p::set(out.get(),"deadeye",p::boolean(s.deadeye));p::set(shell.get(),"kind",p::text(s.shell.kind));p::set(shell.get(),"caliber",p::number(s.shell.caliber));p::set(shell.get(),"damage",p::numbers(std::array<double,2>{s.shell.damage,s.device_damage_mean.value_or(0.)}));p::set(shell.get(),"explosionRadius",p::number(d.raw_radius));
    if(d.explosion_factors){p::set(shell.get(),"explosionDamageFactor",p::number(s.shell.explosion_damage_factor));p::set(shell.get(),"explosionDamageAbsorptionFactor",p::number(s.shell.explosion_absorption_factor));p::set(shell.get(),"explosionEdgeDamageFactor",p::number(s.edge_damage_factor));}
    if(s.stun){const auto&v=*s.stun;auto stun=p::dict();p::set(stun.get(),"stunRadius",p::number(v.radius));p::set(stun.get(),"stunDuration",p::number(v.duration));p::set(stun.get(),"stunFactor",p::number(v.factor));p::set(stun.get(),"guaranteedStunDuration",p::number(v.guaranteed_duration));p::set(stun.get(),"damageDurationCoeff",p::number(v.damage_duration));p::set(stun.get(),"guaranteedStunEffect",p::number(v.guaranteed_effect));p::set(stun.get(),"damageEffectCoeff",p::number(v.damage_effect));p::set(shell.get(),"stun",stun);}
    p::set(out.get(),"shell",shell);return out;
}
std::optional<std::int64_t> optionalInteger(PyObject*raw,const char*key){auto value=p::value(raw,key);if(value.get()==Py_None)return {};return p::wide(value.get());}
}
struct NativeProjectiles::State final:OwnerWorld,stock_native_bot_turret::DetachedHits {
    struct CachedRecord {Record record;std::shared_ptr<const stock_native_client_state::Actor> actor;std::unique_ptr<p::Ref> entity;};
    struct FrozenLaunch {LaunchRecord source;damage::LaunchDescriptor descriptor;Point velocity{};double factor=1.;std::optional<std::int64_t> epoch;std::array<std::optional<unsigned>,2> stickers;};
    world::Registry& registry;
    world::Scene& scene;
    geo::Vehicles& vehicles;
    world::WorldEvents& events;
    bots::Runtime& runtime;
    bots::NativeServices& services;
    ShotScene shot_scene;
    world::Pipeline passive_scene;
    Random random;
    Owner owner;
    // The round this owner serves; refresh and launch read its actor table and projectile clock.
    native_battle::Battle* battle=nullptr;
    std::uint64_t generation=0;
    std::unique_ptr<p::Ref> client,process_clock;
    std::uint64_t registry_epoch=0,rejected=0,record_failures=0,effect_failures=0;
    std::string round;
    damage::StunConfig stun;
    std::int64_t battle_end_ms=0;
    double now=0.;
    std::map<std::string,CachedRecord> record_cache;
    std::vector<Record> record_order;
    std::vector<world::Destruction> destructions;
    std::map<std::int64_t,std::int64_t> confirmed_sequences;
    std::map<std::int64_t,std::set<std::int64_t>> confirmations;
    std::map<std::pair<std::int64_t,std::int64_t>,FrozenLaunch> launch_payloads;
    std::map<std::string,std::array<std::optional<unsigned>,2>> source_stickers;
    std::vector<LaunchRecord> launch_batch;
    std::set<std::int64_t> blocked_bots;
    std::map<std::int64_t,std::int64_t> next_sequences;
    std::optional<std::pair<std::int64_t,std::int64_t>> sending;
    std::size_t launch_cursor=0;
    State(world::Registry&r,world::Scene&s,geo::Vehicles&v,world::WorldEvents&e,bots::Runtime&b,bots::NativeServices&svc):registry(r),scene(s),vehicles(v),events(e),runtime(b),services(svc),shot_scene(r,s),passive_scene(r,s,128),owner(*this){}
    void current(const native_battle::Battle*value=nullptr){if(!battle||(value&&battle!=value)||native_battle::current()!=battle||battle->generation!=generation||registry_epoch!=registry.epoch)throw p::Error("Native projectile owner is stale");}
    void error(const char*boundary,const std::exception&e){++rejected;PyErr_Clear();PySys_WriteStdout("[Offline LAN 0.9.22] Native projectile %s: %s\n",boundary,e.what());}
    p::Ref nativeField(PyObject*entity,const char*name){return p::Ref(bots::readNativeEntityField(nullptr,entity,name));}
    const bots::ServiceEntity* service(const Record&r){return services.find(actor(r));}
    void refresh(){
        current();record_cache.clear();record_order.clear();
        for(const auto&entry:battle->vehicles->actors)try{
            if(!entry.second)continue;const auto&a=*entry.second;Record r;r.key=entry.first;r.local=a.local;r.tombstone=a.tombstone;r.ready=a.ready;if(r.local||r.tombstone)continue;
            if(a.kind!="bot"&&a.kind!="player")continue;r.human=a.kind=="player";r.id=a.id;r.engine_id=a.engine_id;if(r.id<=0||r.engine_id<=0)continue;
            p::Ref entity(bots::readNativeEntity(nullptr,r.engine_id));if(entity.get()==Py_None)continue;r.started=p::flag(nativeField(entity.get(),"isStarted").get());if(!r.started)continue;
            const auto&state=a.state.resolved();auto position=nativeField(entity.get(),"position");r.current=plainPose(state,xyz(position.get()));
            // Native Runtime owns the worker Bot transform and aim. Its frozen
            // state is the unblended authority frame presented by the factory.
            if(!r.human){const auto bot=runtime.actors().find(r.id);if(bot!=runtime.actors().end()){const auto&a=bot->second;r.current.position=a.motion.position;r.current.yaw=a.motion.yaw;r.current.pitch=a.motion.pitch;r.current.roll=a.motion.roll;r.current.siege=a.siege.mode;r.current.turret_yaw=runtime.weapons().at(actor(r)).aim.turret_yaw;r.current.gun_pitch=runtime.weapons().at(actor(r)).aim.gun_pitch;}}
            r.detached=p::flag(nativeField(entity.get(),"isTurretDetached").get());auto health=nativeField(entity.get(),"health");r.alive=state.kind==stock_native_client_state::Value::Kind::Object&&state.object.count("alive")?(state.get("alive").truth()&&state.get("health").numeric(1.)>0.):p::wide(health.get())>0;
            const auto*s=service(r);r.pitch_hull_aiming=s&&s->pitch_hull_aiming[r.current.siege==2?1:0];CachedRecord row;row.record=r;row.actor=entry.second;row.entity=std::make_unique<p::Ref>(std::move(entity));record_cache.emplace(r.key,std::move(row));record_order.push_back(r);
        }catch(const std::exception&e){++record_failures;error("record",e);}
    }
    std::vector<Record> records()override{return record_order;}
    bool liveGeometry(const Record&r,const Pose&pose)const override{const auto*s=services.find(actor(r));return r.detached||(s&&s->pitch_hull_aiming[pose.siege==2?1:0]);}
    CollisionQuery collide(const Record&r,const Point&a,const Point&b,const std::optional<Pose>&historic)override{
        CollisionQuery out;out.start=rounded(a);out.end=rounded(b);out.length=double((vector(out.end)-vector(out.start)).length());const auto*s=service(r);const auto&pose=historic?*historic:r.current;
        if(!s){out.valid=false;return out;}auto geometry=s->geometry[pose.siege==2?1:0];geometry.detached=r.detached;
        if(!geometry.ready){out.valid=false;return out;}
        for(const auto&hit:geo::intersections(geometry,geometryPose(pose,bool(historic)),out.start,out.end)){Collision c;c.distance=hit.distance;c.cosine=hit.angle_cosine;c.component=hit.component;c.material_kind=hit.material_kind;c.material=hit.material;c.local_point=hit.local_point;c.world_point=hit.world_point;c.world_normal=hit.world_normal;out.collisions.push_back(c);}
        // Material pointers belong to the installed mode, not the temporary
        // detached flag copy used by the geometry traversal.
        for(auto&hit:out.collisions){const auto&materials=s->geometry[pose.siege==2?1:0].components[hit.component].materials;const auto material=materials.find(hit.material_kind);hit.material=material==materials.end()?nullptr:&material->second;}
        return out;
    }
    std::vector<double> segment(stock_native_bot_turret::Owner who,bool gun,const Point&a,const Point&b)override{
        const auto*s=services.find({who.human?ActorKind::Human:ActorKind::Bot,who.id});if(!s)return {};const auto&component=s->geometry[s->mode==1?1:0].components[gun?3:2];if(!component.tree)return {};std::vector<double> out;for(const auto&hit:BW::stockBspSegment(*component.tree,vector(a),vector(b)))out.push_back(hit.distance);return out;
    }
    std::optional<double> turretBlock(const Chord&c)override{return services.turrets().block_distance(rounded(c.start),rounded(c.end),double(owner.serverTime(c.end_time).value_or(0)),*this,double(owner.serverTime(c.start_time).value_or(0)));}
    SceneResult scenery(const Cursor&,FlightMeta&meta,const Point&a,const Point&b,const Point&direction)override{return shot_scene.trace(meta,a,b,direction,now,random,destructions);}
    std::vector<world::Destruction> takeDestructions()override{std::vector<world::Destruction> result;result.swap(destructions);for(const auto&row:result)if(!events.mirrorOnly(row)){++rejected;PyErr_Clear();}return result;}
    bool visible(const Point&start,const Point&contact,const Point&burst,double at){
        const auto a=rounded(start),b=rounded(contact),origin=rounded(burst);if(double((vector(b)-vector(origin)).length())<=.001)return true;const double distance=double((vector(b)-vector(a)).length());
        const auto turret=services.turrets().block_distance(a,b,double(owner.serverTime(at).value_or(0)),*this);if(turret&&*turret+.001<distance)return false;
        world::Hit hit;const auto status=passive_scene.horizontal(a,b,hit);if(status==world::QueryStatus::Failed)throw p::Error("Projectile blast visibility query failed");return status!=world::QueryStatus::Found||double((vector(hit.point)-vector(a)).length())+.001>=distance;
    }
    damage::Target target(const CachedRecord&r,const bots::ServiceEntity&s,const Pose&pose){
        const auto&descriptor=s.projectile[pose.siege==2?1:0];if(!descriptor)throw p::Error("Projectile target descriptor unavailable");auto raw=p::dict();p::set(raw.get(),"kind",p::text(r.record.human?"player":"bot"));p::set(raw.get(),"network_id",p::integer(r.record.id));p::set(raw.get(),"state",p::Ref(stock_native_client_state::write(r.actor->state)));damage::Target out;if(!damage::readProjectileTargetState(nullptr,raw.get(),r.entity->get(),*descriptor,out))throw p::Error("Projectile target state unavailable");out.position=pose.position;out.stun_config.minimum_duration=stun.minimum_duration;out.stun_config.factors=stun.factors;return out;
    }
    damage::GeometryWorld geometry(const Record&r,const bots::ServiceEntity&s,const Pose&pose,bool historical,const Flight&flight,double at){
        std::array<std::optional<unsigned>,2> values{};const auto frozen=source_stickers.find(flight.canonical.id);if(frozen!=source_stickers.end())values=frozen->second;
        // Installed attachment identity is changed only at the resource owner.
        // A historical pose never claims to reconstruct detached attachments.
        return damage::GeometryWorld(s.geometry[pose.siege==2?1:0],s.interior[pose.siege==2?1:0],geometryPose(pose,historical),[this,at](const Point&a,const Point&b,const Point&c){return visible(a,b,c,at);},values,r.detached);
    }
    damage::Resolution resolve(Flight&flight,const Cursor&cursor,const TerminalData&data)override{
        damage::Resolution result;const double elapsed=std::max(0.,cursor.cursor_time-cursor.launch_time);Point incoming;for(unsigned i=0;i<3;++i)incoming[i]=cursor.velocity[i]+cursor.gravity[i]*elapsed;
        const auto time=owner.serverTime(cursor.cursor_time).value_or(0);const auto checked_ms=std::max(flight.canonical.checked_ms,flight.canonical.segment_ms+std::int64_t(std::round(elapsed*1000.)));
        if(data.target){const auto found=record_cache.find(*data.target);if(found!=record_cache.end()&&found->second.record.alive){const auto&r=found->second;const auto*s=service(r.record);if(!s)throw p::Error("Projectile direct target geometry unavailable");const auto&pose=data.collision_pose?*data.collision_pose:r.record.current;auto victim=target(r,*s,pose);auto native=geometry(r.record,*s,pose,bool(data.collision_pose),flight,cursor.cursor_time);
                damage::DirectEvidence evidence;evidence.impact=data.impact;evidence.query_start=data.query.start;evidence.query_end=data.query.end;evidence.incoming=incoming;evidence.trajectory_start=data.trajectory_query.start;evidence.trajectory_end=data.trajectory_query.end;evidence.trajectory_start_time=data.trajectory_query.start_time;evidence.trajectory_end_time=data.trajectory_query.end_time;evidence.has_trajectory_query=true;evidence.range_distance=length(flight.meta.range_origin,data.impact);evidence.piercing_loss=data.piercing_loss;evidence.checked_distance=std::max({cursor.distance,flight.canonical.checked_distance,flight.acked_distance});evidence.elapsed_ms=checked_ms;evidence.impact_server_ms=time;evidence.battle_end_ms=battle_end_ms;
                for(const auto&hit:data.query.collisions){damage::ImpactLayer layer(hit.distance,hit.cosine,hit.component,hit.material,hit.material?hit.material->use_lining:true);layer.local_point=hit.local_point;layer.world_normal=hit.world_normal;evidence.collisions.push_back(layer);}
                if(!data.query.collisions.empty())evidence.world_normal=std::min_element(data.query.collisions.begin(),data.query.collisions.end(),[](const auto&a,const auto&b){return a.distance<b.distance;})->world_normal;
                result.direct=damage::direct(flight.meta.shot,victim,evidence,native,random,result.ricochet);
            }}
        if(flight.canonical.he&&!(result.direct&&result.direct->result==2)){
            const auto magnitude=std::sqrt(incoming[0]*incoming[0]+incoming[1]*incoming[1]+incoming[2]*incoming[2]);Point origin=rounded(data.impact);if(magnitude>1e-9){auto burst=vector(origin),direction=vector(incoming);direction.normalise();origin=point(burst-direction*.002f);}
            for(const auto&r:record_order){if(result.splash.size()>=30)break;if((data.target&&r.key==*data.target)||!r.alive||!r.started||r.local||r.tombstone)continue;
                try{const auto raw=record_cache.find(r.key);const auto*s=service(r);if(raw==record_cache.end()||!s)continue;const auto offset=flight.meta.presentation_offsets.find(r.key);auto historical=owner.historic(r.key,cursor.cursor_time-(offset==flight.meta.presentation_offsets.end()?0.:offset->second));if(!historical)throw p::Error("Projectile splash historic pose unavailable");const bool frozen=!liveGeometry(r,*historical);const auto&pose=frozen?*historical:r.current;auto victim=target(raw->second,*s,pose);auto native=geometry(r,*s,pose,frozen,flight,cursor.cursor_time);auto effect=damage::splash(flight.meta.shot,victim,data.impact,origin,data.impact,time,battle_end_ms,native,random);if(effect)result.splash.push_back(std::move(*effect));}
                catch(const std::exception&e){++effect_failures;error("splash",e);}
            }
        }
        return result;
    }
    void confirmed(const Launch&launch)override{
        const auto frozen=launch_payloads.find({launch.shooter,launch.sequence});if(frozen!=launch_payloads.end())source_stickers.emplace(launch.id,frozen->second.stickers);
        auto&last=confirmed_sequences[launch.shooter];if(launch.sequence<=last)return;auto&pending=confirmations[launch.shooter];pending.insert(launch.sequence);
        while(pending.count(last+1)){const auto next=last+1;if(!runtime.acknowledgeLaunch(launch.shooter,next))throw p::Error("Canonical native Bot launch acknowledgement is out of order");pending.erase(next);launch_payloads.erase({launch.shooter,next});last=next;}
        if(pending.empty())confirmations.erase(launch.shooter);
        battle->projectiles->bot_fire_seen[launch.shooter]=last;
    }
    static double builtinClock(PyObject* fn){if(!fn||!PyCFunction_Check(fn))throw p::Error("Native clock unavailable");const int flags=PyCFunction_GET_FLAGS(fn);PyObject*result=nullptr;if((flags&METH_NOARGS)!=0)result=PyCFunction_GET_FUNCTION(fn)(PyCFunction_GET_SELF(fn),nullptr);else if((flags&METH_VARARGS)!=0){p::Ref args(PyTuple_New(0));result=PyCFunction_GET_FUNCTION(fn)(PyCFunction_GET_SELF(fn),args.get());}else throw p::Error("Native clock signature invalid");p::Ref value(result);return p::scalar(value.get());}
    double processTime(){return builtinClock(process_clock?process_clock->get():nullptr);}

    bool observe(PyObject*message,double at){
        current();now=at;if(!PyDict_Check(message)||!std::isfinite(at))return false;auto epoch=optionalInteger(message,"authority_epoch");if(!epoch)epoch=optionalInteger(client->get(),"authority_epoch");const auto server=optionalInteger(message,"server_time_ms");double anchor=at;auto received=p::value(message,"_client_received_time");if(received.get()!=Py_None)anchor=std::max(0.,at-std::max(0.,std::min(60.,processTime()-p::scalar(received.get()))));
        auto rtt=p::value(client->get(),"minimum_rtt_ms");double one_way=0;try{if(rtt.get()!=Py_None)one_way=std::max(0.,std::min(.25,p::scalar(rtt.get())/2000.));else{auto fallback=p::value(client->get(),"rtt_ms");if(fallback.get()!=Py_None)one_way=std::max(0.,std::min(.25,p::scalar(fallback.get())/2000.));}}catch(...){PyErr_Clear();}
        anchor=std::max(0.,anchor-one_way);if(!owner.observe(epoch,server,anchor,at))return false;
        return true;
    }
    bool install(native_battle::Battle&b){
        if(battle)throw p::Error("Native projectile owner already installed");if(!b.projectiles||!b.startup||!b.engine.client)throw p::Error("Native projectile battle state is unavailable");battle=&b;generation=b.generation;client=std::make_unique<p::Ref>(p::retain(b.engine.client.get()));registry_epoch=registry.epoch;
        p::Ref module(PyImport_ImportModule("random"));auto instance=p::value(module.get(),"_inst");if(!random.install(instance.get()))throw p::Error("Native projectile RNG installation failed");p::Ref timing(PyImport_ImportModule("time"));auto clock=p::value(timing.get(),"monotonic");process_clock=std::make_unique<p::Ref>(clock.get()!=Py_None?p::retain(clock.get()):p::value(timing.get(),"clock"));
        p::Ref stun_module(PyImport_ImportModule("items.stun"));auto cfg=p::value(stun_module.get(),"g_cfg");if(!damage::readStunConfig(cfg.get(),stun))throw p::Error("Native projectile stun config unavailable");battle_end_ms=std::int64_t(std::round((native_battle::startup::prebattleSeconds(b)+native_battle::startup::battleSeconds(b))*1000.));
        p::Ref round_value(stock_native_client_state::write(b.start.get("round_id")));p::Ref round_string(PyObject_Str(round_value.get()));round=p::string(round_string.get());for(const auto&seen:b.projectiles->bot_fire_seen)confirmed_sequences[seen.first]=seen.second;
        const auto&server=*b.projectiles;auto epoch=server.epoch;if(!epoch)epoch=optionalInteger(client->get(),"authority_epoch");const double install_now=b.clock();owner.observe(epoch,server.server_time_ms,server.server_local_time,install_now);for(const auto&actor:runtime.actors()){const auto sequence=runtime.weapons().at({ActorKind::Bot,actor.first}).fire_seq;confirmed_sequences[actor.first]=std::max(confirmed_sequences[actor.first],sequence);}refresh();const auto&snapshot=b.startup->last_snapshot.resolved();if(snapshot.kind==stock_native_client_state::Value::Kind::Object){p::Ref raw(stock_native_client_state::write(snapshot));ingress(raw.get(),install_now);}return true;
    }
    bool matchingRound(PyObject*message){auto raw=p::value(message,"round_id");if(raw.get()==Py_None)return true;p::Ref text(PyObject_Str(raw.get()));return p::string(text.get())==round;}
    bool ingress(PyObject*message,double at){
        current();if(!PyDict_Check(message)||!matchingRound(message)||!observe(message,at))return false;refresh();auto rows=p::value(message,"projectiles");if(!PyList_Check(rows.get())&&!PyTuple_Check(rows.get()))return true;std::vector<Launch> accepted;std::set<std::string> reported;
        for(Py_ssize_t i=0;i<p::size(rows.get());++i){auto row=p::item(rows.get(),i);if(!PyDict_Check(row.get()))continue;auto id=p::value(row.get(),"projectile_id");if(id.get()!=Py_None)try{reported.insert(p::string(id.get()));}catch(...){PyErr_Clear();}Launch launch;if(readLaunch(row.get(),launch))accepted.push_back(std::move(launch));else{if(PyErr_Occurred())++rejected;PyErr_Clear();}}
        return owner.reconcile(accepted,reported,at);
    }
    bool event(PyObject*raw,double at){
        current();if(!PyDict_Check(raw)||!matchingRound(raw))return false;auto type=p::value(raw,"kind");const auto kind=p::string(type.get());
        if(kind=="projectile_impact"){auto id=p::value(raw,"projectile_id");return owner.terminalEvent(p::string(id.get()),optionalInteger(raw,"authority_epoch"));}
        if(kind=="projectile_ricochet"){const auto epoch=optionalInteger(raw,"authority_epoch");if(epoch&&owner.epoch()&&epoch!=owner.epoch())return false;}
        if(kind!="authority"&&kind!="shot"&&kind!="bot_shot"&&kind!="projectile_ricochet")return false;if(!observe(raw,at))return false;if(kind=="authority")return true;
        refresh();Launch launch;if(!readLaunch(raw,launch)){if(PyErr_Occurred())++rejected;PyErr_Clear();return false;}return owner.launch(launch,at);
    }
    p::Ref packedLaunch(std::int64_t bot,std::int64_t sequence,const FrozenLaunch&f){
        const auto&r=f.source;const auto&d=f.descriptor;auto args=p::list(),kwargs=p::dict();p::append(args.get(),p::text("bot"));p::append(args.get(),p::integer(bot));p::append(args.get(),p::integer(sequence));p::append(args.get(),p::integer(r.edge.shell));p::append(args.get(),p::numbers(r.origin));p::append(args.get(),p::numbers(f.velocity));p::append(args.get(),p::number(d.gravity));p::append(args.get(),p::number(d.maximum));p::append(args.get(),p::integer(r.artillery?r.maximum_time_ms:20000));const bool he=d.shot.shell.kind=="HIGH_EXPLOSIVE";p::append(args.get(),p::boolean(he));p::append(args.get(),p::number(he?d.shot.explosion_radius:0.));p::set(kwargs.get(),"authority_epoch",f.epoch?p::integer(*f.epoch):p::retain(Py_None));p::set(kwargs.get(),"penetration_factor",p::number(f.factor));p::set(kwargs.get(),"source_shot",shotWire(d));p::set(kwargs.get(),"burst_group_seq",p::integer(r.edge.group_seq));p::set(kwargs.get(),"burst_index",p::integer(r.edge.index));p::set(kwargs.get(),"burst_count",p::integer(r.edge.count));p::set(kwargs.get(),"launch_time_us",p::integer(r.launch_time_us));p::set(kwargs.get(),"launch_pose",p::tupleNumbers(std::array<double,6>{r.pose.position[0],r.pose.position[1],r.pose.position[2],r.pose.yaw,r.pose.pitch,r.pose.roll}));p::set(kwargs.get(),"shells_before_shot",r.has_shells_before?p::integer(r.shells_before):p::retain(Py_None));p::Ref tuple(PyTuple_New(4));PyTuple_SET_ITEM(tuple.get(),0,p::integer(bot).release());PyTuple_SET_ITEM(tuple.get(),1,p::integer(sequence).release());PyTuple_SET_ITEM(tuple.get(),2,args.release());PyTuple_SET_ITEM(tuple.get(),3,kwargs.release());return tuple;
    }
    PyObject* launches(PyObject*message){
        current();if(message!=Py_None){if(sending)throw p::Error("Native Bot launch transport result missing");auto type=p::value(message,"type");launch_batch.clear();launch_cursor=0;blocked_bots.clear();next_sequences.clear();if(p::string(type.get())!="bot_state")return PyList_New(0);auto admitted=p::value(message,"launches");if(admitted.get()!=Py_None){std::map<std::pair<std::int64_t,std::int64_t>,const LaunchRecord*> pending;for(const auto&row:runtime.weapons().pending())pending[{row.actor.id,row.edge.shot_seq}]=&row;for(Py_ssize_t i=0;i<p::size(admitted.get());++i){auto row=p::item(admitted.get(),i);const auto id=p::integral(row.get(),"id"),sequence=p::integral(row.get(),"fire_seq");if(id<=0||sequence<=0)throw p::Error("Bot launch publication identity invalid");if(sequence<=confirmed_sequences[id])continue;const auto found=pending.find({id,sequence});if(found==pending.end()){blocked_bots.insert(id);continue;}launch_batch.push_back(*found->second);}}}
        if(sending)throw p::Error("Native Bot launch transport result missing");auto out=p::list();
        while(launch_cursor<launch_batch.size()){
            const auto&r=launch_batch[launch_cursor++];const auto bot=r.actor.id,sequence=r.edge.shot_seq;const auto last=confirmed_sequences[bot];if(sequence<=last||blocked_bots.count(bot))continue;const auto next=next_sequences.find(bot);const auto expected=next==next_sequences.end()?last+1:next->second;if(sequence!=expected){blocked_bots.insert(bot);continue;}
            const auto key=std::make_pair(bot,sequence);auto frozen=launch_payloads.find(key);
            if(frozen==launch_payloads.end())try{
                const auto*s=services.find({ActorKind::Bot,bot});const auto state=runtime.actors().find(bot);if(!s||state==runtime.actors().end()||!s->available||!s->record_ready||!r.has_origin||r.launch_time_us<0)throw p::Error("Bot launch source unavailable");const auto&shots=s->launch_shots[state->second.mode==1?1:0];if(r.edge.shell<0||std::size_t(r.edge.shell)>=shots.size())throw p::Error("Bot launch shell unavailable");FrozenLaunch launch;launch.source=r;launch.descriptor=shots[std::size_t(r.edge.shell)];const auto cosmetics=s->projectile_stickers[state->second.mode==1?1:0].find(r.edge.shell);if(cosmetics!=s->projectile_stickers[state->second.mode==1?1:0].end())launch.stickers=cosmetics->second;const auto&d=launch.descriptor;const double horizontal=std::cos(r.pitch);Point direction{std::sin(r.yaw)*horizontal,std::sin(r.pitch),std::cos(r.yaw)*horizontal};const double n=length({},direction);if(n<=1e-6||d.speed<=0||d.gravity<=0||d.maximum<=0)throw p::Error("Bot launch trajectory invalid");for(unsigned i=0;i<3;++i)launch.velocity[i]=direction[i]*d.speed/(r.artillery?1.:n);
                if(r.artillery){if(r.maximum_time_ms<=0||r.maximum_time_ms>20000||r.gravity!=d.gravity||r.maximum_distance!=d.maximum)throw p::Error("Bot artillery launch descriptor changed");for(unsigned i=0;i<3;++i)if(std::abs(r.velocity[i]-launch.velocity[i])>1e-7)throw p::Error("Bot artillery launch velocity changed");launch.velocity=r.velocity;}
                launch.factor=damage::sampleShellValue(1.,random);launch.epoch=optionalInteger(client->get(),"authority_epoch");frozen=launch_payloads.emplace(key,std::move(launch)).first;
            }catch(const std::exception&e){blocked_bots.insert(bot);error("launch",e);continue;}
            sending=key;p::append(out.get(),packedLaunch(bot,sequence,frozen->second));break;
        }
        return out.release();
    }
    bool launchResult(std::int64_t bot,std::int64_t sequence,bool accepted){const auto key=std::make_pair(bot,sequence);if(!sending||*sending!=key)return false;sending.reset();if(accepted)next_sequences[bot]=sequence+1;else blocked_bots.insert(bot);return true;}
    PyObject* advance(native_battle::Battle&b,double at,bool force){
        current(&b);now=at;refresh();
        const auto start=std::chrono::steady_clock::now();owner.advance(at,force);
        for(auto at=source_stickers.begin();at!=source_stickers.end();)if(!owner.flights().count(at->first))at=source_stickers.erase(at);else ++at;
        const double elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
        const auto& metrics=owner.lastAdvanceMetrics();auto&perf=*b.projectiles;
        perf.active=std::size_t(metrics.cursor.active);
        perf.chords=std::size_t(metrics.cursor.chords);
        perf.debt=metrics.cursor.debt_after;
        perf.advance_seconds=std::max(0.,elapsed);
        perf.terminals=std::size_t(metrics.cursor.terminals);
        perf.scans=std::size_t(metrics.chord.scans);
        perf.candidates=std::size_t(metrics.chord.candidates);
        return packDispatches(owner.dispatches());
    }
};
NativeProjectiles::NativeProjectiles(world::Registry&r,world::Scene&s,geo::Vehicles&v,world::WorldEvents&e,bots::Runtime&b,bots::NativeServices&svc):state_(std::make_unique<State>(r,s,v,e,b,svc)){}
NativeProjectiles::~NativeProjectiles()=default;
bool NativeProjectiles::install(native_battle::Battle&battle){try{return state_->install(battle);}catch(const std::exception&e){if(!PyErr_Occurred())PyErr_SetString(PyExc_ValueError,e.what());return false;}}
bool NativeProjectiles::observe(PyObject*message,double now){try{return state_->observe(message,now);}catch(const std::exception&e){if(!PyErr_Occurred())PyErr_SetString(PyExc_ValueError,e.what());return false;}}
bool NativeProjectiles::ingress(PyObject*message,double now){try{return state_->ingress(message,now);}catch(const std::exception&e){if(!PyErr_Occurred())PyErr_SetString(PyExc_ValueError,e.what());return false;}}
bool NativeProjectiles::event(PyObject*event,double now){try{return state_->event(event,now);}catch(const std::exception&e){if(!PyErr_Occurred())PyErr_SetString(PyExc_ValueError,e.what());return false;}}
PyObject* NativeProjectiles::launches(PyObject*message){try{return state_->launches(message);}catch(const std::exception&e){if(!PyErr_Occurred())PyErr_SetString(PyExc_ValueError,e.what());return nullptr;}}
bool NativeProjectiles::launchResult(std::int64_t bot,std::int64_t sequence,bool accepted){return state_->launchResult(bot,sequence,accepted);}
PyObject* NativeProjectiles::advance(native_battle::Battle&battle,double now,bool force){try{return state_->advance(battle,now,force);}catch(const std::exception&e){if(!PyErr_Occurred())PyErr_SetString(PyExc_ValueError,e.what());return nullptr;}}
bool NativeProjectiles::acknowledge(std::uint64_t serial,bool sent){return state_->owner.acknowledge(serial,sent);}
PyObject* NativeProjectiles::statistics()const{try{p::Ref out(packMetrics(state_->owner.metrics()));p::set(out.get(),"ingress_rejected",p::integer(state_->rejected));p::set(out.get(),"record_failures",p::integer(state_->record_failures));p::set(out.get(),"effect_failures",p::integer(state_->effect_failures));return out.release();}catch(const std::exception&e){if(!PyErr_Occurred())PyErr_SetString(PyExc_ValueError,e.what());return nullptr;}}
}
