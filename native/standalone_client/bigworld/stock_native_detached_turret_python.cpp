#include "stock_native_detached_turret_python.hpp"
#include "stock_native_detached_turret_snapshot.hpp"
#include "stock_native_detached_turret_world.hpp"
#include "stock_native_bot_resource_lifecycle.hpp"
#include "stock_native_battle_startup.hpp"
#include "stock_native_battle_world.hpp"
#include "stock_native_battle_world_imports.hpp"

#include <cmath>
#include <algorithm>

namespace stock_native_detached_turret {
namespace p=stock_native_bot_python;
namespace cs=stock_native_client_state;
using native_battle::Battle;
using cs::Actor;
using cs::Value;
namespace {
std::string name(Owner owner){return std::string(owner.human?"player:":"bot:")+std::to_string(owner.id);}
Owner identity(const Actor&record){
    if((record.kind!="bot"&&record.kind!="player")||record.id<1||record.id>2147483647)throw p::Error("Detached turret record identity is invalid");
    return {record.kind=="player",record.id};
}
// str(round_id) of the pinned runtime: the launch seed and the ingress round match use this text.
std::optional<std::string> roundText(const Value&value){
    const auto&v=value.resolved();
    if(v.kind==Value::Kind::Integer)return std::to_string(v.integer);
    if(v.kind==Value::Kind::Text)return v.text;
    return std::nullopt;
}
// Strict field readers with the p::numeric/integral/truth law: absent or null yields the fallback.
const Value* member(const Value&object,const char*key){
    const auto&o=object.resolved();if(o.kind!=Value::Kind::Object)return nullptr;
    const auto found=o.object.find(key);if(found==o.object.end())return nullptr;
    const auto&v=found->second.resolved();return v.kind==Value::Kind::Null?nullptr:&v;
}
double number(const Value&object,const char*key,double fallback=0.){
    const auto*v=member(object,key);if(!v)return fallback;
    if(v->kind==Value::Kind::Integer)return double(v->integer);
    if(v->kind==Value::Kind::Number&&std::isfinite(v->number))return v->number;
    throw p::Error(std::string("Detached turret number is invalid: ")+key);
}
std::int64_t integer(const Value&object,const char*key){
    const auto*v=member(object,key);if(!v)return 0;
    if(v->kind==Value::Kind::Integer)return v->integer;
    if(v->kind==Value::Kind::Number&&std::isfinite(v->number)&&std::floor(v->number)==v->number&&std::fabs(v->number)<9.2e18)return std::int64_t(v->number);
    throw p::Error(std::string("Detached turret integer is invalid: ")+key);
}
bool flag(const Value&object,const char*key,bool fallback=false){const auto*v=member(object,key);return v?v->truth():fallback;}
Point xyz(PyObject*value){if(PyTuple_Check(value)||PyList_Check(value))return p::numbers<3>(value);return {{p::numeric(value,"x"),p::numeric(value,"y"),p::numeric(value,"z")}};}
double angle(double a,double b){constexpr double pi=3.14159265358979323846;double out=std::fmod(b-a+pi,2.*pi);if(out<0.)out+=2.*pi;return out-pi;}
bool statePose(const Value&state,LaunchInput&input){
    const auto&s=state.resolved();if(s.kind!=Value::Kind::Object)return false;
    for(const char*key:{"x","y","z"})if(!s.object.count(key))return false;
    input.position={{number(s,"x"),number(s,"y"),number(s,"z")}};
    input.attitude={{number(s,"yaw"),number(s,"pitch"),number(s,"roll")}};
    input.turret_yaw=number(s,"turret_yaw",angle(input.attitude[0],number(s,"aim_yaw",input.attitude[0])));
    return true;
}
}
class NativeOwner::Impl:public LaunchSource {
    struct Cached {
        std::unique_ptr<p::Ref> entity,descriptor;
        LaunchInput launch;
        VehicleGeometry geometry;
        std::vector<Component> components;
        double mass=0.;
        bool support_valid=false,complete=false,pending=false;stock_native_bot::ResourceStamp stamp;
    };
    stock_native_bot::Runtime& bots_;
    stock_native_bot_geometry::Vehicles& vehicles_;
    stock_native_world::Registry& registry_;
    NativeWorld world_;
    Runtime bodies_;
    std::unique_ptr<LaunchQueue> launches_;
    // The round this owner serves; every entry point is fenced by its identity and generation.
    const Battle* battle_=nullptr;
    std::uint64_t generation_=0,epoch_=0;
    std::map<Owner,Cached> cache_;
    std::vector<std::string> errors_;
    double next_publish_=0.;
    std::string round_;
    void note(const std::exception&e){errors_.push_back(e.what());PyErr_Clear();}
    void require(bool value,const char*context="Detached turret input"){
        if(value)return;PyObject*type=nullptr,*detail=nullptr,*trace=nullptr;PyErr_Fetch(&type,&detail,&trace);std::string message=context;
        if(detail){PyObject*text=PyObject_Str(detail);if(text){try{message+=": "+p::string(text);}catch(const std::exception&){PyErr_Clear();}Py_DECREF(text);}else PyErr_Clear();}
        Py_XDECREF(type);Py_XDECREF(detail);Py_XDECREF(trace);throw p::Error(message);
    }
    p::Ref resourceField(PyObject*object,const char*name,bool&present){
        if(PyDict_Check(object)){auto*value=PyDict_GetItemString(object,name);present=value!=nullptr;return p::retain(value?value:Py_None);}
        auto*value=PyObject_GetAttrString(object,name);if(!value&&PyErr_ExceptionMatches(PyExc_AttributeError)){PyErr_Clear();present=false;return p::retain(Py_None);}present=true;return p::Ref(value);
    }
    bool resourcePending(PyObject*descriptor){
        if(descriptor==Py_None)return true;
        for(const char*name:{"chassis","hull","turret","gun"}){bool present=false;auto part=resourceField(descriptor,name,present);if(!present||part.get()==Py_None)return false;auto tester=resourceField(part.get(),"hitTester",present);if(present&&tester.get()==Py_None)return true;if(!present)return false;auto box=resourceField(tester.get(),"bbox",present);if(present&&box.get()==Py_None)return true;auto model=resourceField(tester.get(),"_ModelHitTester__bspModel",present);if(present&&model.get()==Py_None)return true;}
        return false;
    }
    Cached& installEntity(Owner key,PyObject*entity){
        auto&cached=cache_[key];auto descriptor=p::Ref(stock_native_bot::readNativeEntityField(nullptr,entity,"typeDescriptor"));
        stock_native_bot::ResourceStamp stamp;require(stock_native_bot::readResourceStamp(entity,{descriptor.get()},stamp),"Detached turret resource stamp");const bool pending=resourcePending(descriptor.get());if(cached.entity&&cached.entity->get()==entity&&cached.descriptor&&cached.descriptor->get()==descriptor.get()&&cached.stamp==stamp&&(cached.complete||(cached.pending&&pending)))return cached;
        Cached result;result.stamp=std::move(stamp);result.entity=std::make_unique<p::Ref>(p::retain(entity));result.descriptor=std::make_unique<p::Ref>(p::retain(descriptor.get()));result.pending=pending;if(pending){cached=std::move(result);return cached;}
        try{require(readLaunchGeometry(descriptor.get(),result.launch),"Detached turret launch geometry");}catch(const std::exception&e){note(e);}
        try{require(readComponents(descriptor.get(),result.components),"Detached turret mass geometry");}catch(const std::exception&e){note(e);}
        try{require(readVehicleGeometry(descriptor.get(),result.geometry),"Detached turret support geometry");auto physics=p::value(descriptor.get(),"physics");result.mass=p::numeric(physics.get(),"weight");if(result.mass<=0.)throw p::Error("Vehicle collision mass is unavailable");result.support_valid=true;}catch(const std::exception&e){note(e);}
        result.complete=result.launch.geometry_ready&&!result.components.empty()&&result.support_valid;cached=std::move(result);return cached;
    }
    std::optional<LaunchInput> pose(Owner key,const Actor&record,PyObject*entity,const Value*state){
        auto&cached=installEntity(key,entity);auto input=cached.launch;input.ready=record.ready;input.detached=p::flag(p::Ref(stock_native_bot::readNativeEntityField(nullptr,entity,"isTurretDetached")).get());
        if(state&&statePose(*state,input))return input;
        if(!key.human){const auto found=bots_.actors().find(key.id);if(found!=bots_.actors().end()){const auto&a=found->second;input.position=a.motion.position;input.attitude={{a.motion.yaw,a.motion.pitch,a.motion.roll}};input.turret_yaw=bots_.weapons().at({offline_simulation::ActorKind::Bot,key.id}).aim.turret_yaw;return input;}}
        if(statePose(record.state,input))return input;
        auto position=p::Ref(stock_native_bot::readNativeEntityField(nullptr,entity,"position")),matrix=p::Ref(stock_native_bot::readNativeEntityField(nullptr,entity,"matrix"));if(position.get()==Py_None||matrix.get()==Py_None)return {};input.position=xyz(position.get());input.attitude={{p::numeric(matrix.get(),"yaw"),p::numeric(matrix.get(),"pitch"),p::numeric(matrix.get(),"roll")}};
        input.turret_yaw=nativeTurretYaw(vehicles_,key).value_or(0.);return input;
    }
    void current(const Battle&b)const{
        if(!launches_)throw p::Error("Detached turret owner is not installed");
        if(&b!=battle_||b.generation!=generation_||epoch_!=registry_.epoch)throw p::Error("Detached turret owner is stale");
    }
    void accepted(const Row&row){
        if(!launches_->accept(row))return;
        const auto found=cache_.find(row.owner);
        bodies_.accept(row,found==cache_.end()?std::vector<Component>{}:found->second.components);
    }
    // The world reconcile law for one newly accepted row.
    static void publish(native_battle::DetachedTurrets&turrets,const Row&row){
        const auto key=name(row.owner);
        if(!turrets.rows.count(key))turrets.order.push_back(key);
        turrets.rows[key]=row;turrets.geometry.erase(key);
    }
    void bodies(const Battle&b,double server_ms){
        std::vector<Vehicle> vehicles;std::vector<PlayerImpulse> impulses;auto physical_order=p::dict();
        static const Value none;
        const auto&players=b.startup?b.startup->last_snapshot.get("players").resolved():none;
        if(players.kind!=Value::Kind::Null&&players.kind!=Value::Kind::Array)throw p::Error("Detached turret snapshot players are invalid");
        for(const auto&player:players.array)try{
            const auto id=integer(player,"id");if(id<=0||!flag(player,"participating")||!flag(player,"world_pose"))continue;
            const auto*pushes=member(player,"turret_pushes");p::Ref raw(pushes?p::Ref(cs::write(*pushes)):p::retain(Py_None));require(readPlayerImpulses(id,raw.get(),impulses));
            const Owner owner{true,id};auto order_key=p::text(name(owner));if(PyDict_SetItem(physical_order.get(),order_key.get(),Py_None)<0)throw p::Error("Detached turret player order allocation failed");
            auto cached=cache_.find(owner);if(cached==cache_.end()||!cached->second.support_valid)continue;
            VehiclePose pose;pose.position={{number(player,"x"),number(player,"y"),number(player,"z")}};
            pose.attitude={{number(player,"yaw"),number(player,"terrain_pitch",number(player,"pitch")-number(player,"suspension_pitch")),number(player,"roll")}};
            pose.separate_hull=true;pose.hull_position=pose.position;pose.hull_attitude={{pose.attitude[0],number(player,"pitch"),pose.attitude[2]}};
            pose.aim_yaw=number(player,"aim_yaw",pose.attitude[0]);pose.gun_pitch=number(player,"gun_pitch");
            auto input=lookup(owner);
            if(input)vehicles.push_back(vehicle(owner,cached->second.geometry,pose,cached->second.mass,number(player,"speed"),number(player,"push_x"),number(player,"push_z"),flag(player,"alive",true),!input->detached));
        }catch(const std::exception&e){note(e);}
        for(const auto&entry:bots_.actors())try{const Owner owner{false,entry.first};auto input=lookup(owner);auto cached=cache_.find(owner);if(!input||cached==cache_.end()||!cached->second.support_valid)continue;const auto&a=entry.second;VehiclePose pose;pose.position=a.motion.position;pose.attitude={{a.motion.yaw,a.motion.terrain_pitch,a.motion.roll}};pose.separate_hull=true;pose.hull_position=pose.position;pose.hull_attitude={{a.motion.yaw,a.motion.pitch,a.motion.roll}};pose.aim_yaw=a.weapon.aim_yaw;pose.gun_pitch=bots_.weapons().at({offline_simulation::ActorKind::Bot,entry.first}).aim.gun_pitch;vehicles.push_back(vehicle(owner,cached->second.geometry,pose,cached->second.mass,a.motion.speed,a.motion.push_x,a.motion.push_z,a.motion.alive,!input->detached));}catch(const std::exception&e){note(e);}
        // Reproduce states=dict(player generator); states.update(bot generator)
        // at the plain-data ingress boundary. CPython's actual native dict
        // layout retains the target's string-hash ordering without a
        // callback from the rigid solver or an invented ID sort.
        for(const auto&entry:bots_.actors()){auto order_key=p::text(name({false,entry.first}));if(PyDict_SetItem(physical_order.get(),order_key.get(),Py_None)<0)throw p::Error("Detached turret Bot order allocation failed");}
        std::map<std::string,Vehicle> by_name;for(auto&v:vehicles)by_name.emplace(name(v.owner),std::move(v));vehicles.clear();Py_ssize_t order_cursor=0;PyObject*order_key,*order_value;while(PyDict_Next(physical_order.get(),&order_cursor,&order_key,&order_value)){const auto found=by_name.find(p::string(order_key));if(found!=by_name.end())vehicles.push_back(std::move(found->second));}
        for(const auto owner:bodies_.order)try{auto input=lookup(owner);bodies_.available(owner,bool(input));const auto c=cache_.find(owner);if(c!=cache_.end())bodies_.components(owner,c->second.components);}catch(const std::exception&e){bodies_.available(owner,false);note(e);}
        for(auto&row:bodies_.update(server_ms,vehicles,impulses,world_))launches_->proposal(std::move(row));for(const auto&failure:bodies_.failures)errors_.push_back(failure.reason);
    }
public:
    Impl(stock_native_bot::Runtime&bots,stock_native_world::Registry&registry,stock_native_world::Scene&scene,stock_native_bot_geometry::Vehicles&vehicles,std::uint16_t flags):bots_(bots),vehicles_(vehicles),registry_(registry),world_(bots,registry,scene,flags){}
    void install(Battle&b){
        if(launches_)throw p::Error("Detached turret owner is already installed");
        if(!b.world)throw p::Error("Detached turret world is unavailable");
        const auto round=roundText(b.start.get("round_id"));if(!round)throw p::Error("Detached turret round identity is unavailable");
        battle_=&b;generation_=b.generation;epoch_=registry_.epoch;round_=*round;launches_=std::make_unique<LaunchQueue>(round_);
        // The actor table also holds the worker's local player:-1 space carrier. Prefetch only
        // combat participants; launch and canonical ingress retain strict identities.
        for(const auto&entry:b.vehicles->actors)try{
            const auto&record=*entry.second;if((record.kind!="bot"&&record.kind!="player")||record.id<=0)continue;
            const auto owner=identity(record);auto entity=p::Ref(stock_native_bot::readNativeEntity(nullptr,record.engine_id));if(entity.get()!=Py_None)installEntity(owner,entity.get());
        }catch(const std::exception&e){note(e);}
        // Rows the world reconcile accepted before this owner existed (round start, loading snapshot).
        const auto&turrets=b.world->turrets;
        for(const auto&key:turrets.order){const auto row=turrets.rows.find(key);if(row!=turrets.rows.end())try{accepted(row->second);}catch(const std::exception&e){note(e);}}
    }
    bool queue(Battle&b,const Actor&record,PyObject*entity,const Value*state,double now){
        current(b);if(!std::isfinite(now))throw p::Error("Detached turret launch time is invalid");
        const auto key=identity(record);auto input=pose(key,record,entity,state);if(!input)return false;
        world_.now(now);return launches_->queue(key,*input,now,world_);
    }
    std::optional<LaunchInput> lookup(Owner owner)override{
        const auto*record=battle_->vehicle(name(owner));if(!record)return {};
        auto entity=p::Ref(stock_native_bot::readNativeEntity(nullptr,record->engine_id));
        return entity.get()==Py_None?std::nullopt:pose(owner,*record,entity.get(),nullptr);
    }
    bool ingress(Battle&b,const Value&message){
        current(b);const auto&m=message.resolved();if(m.kind!=Value::Kind::Object||roundText(m.get("round_id"))!=round_)return false;
        const auto&rows=m.get("detached_turrets").resolved();if(rows.kind!=Value::Kind::Array)return false;
        if(!b.world)throw p::Error("Detached turret world is unavailable");
        const auto count=std::min<std::size_t>(12,rows.array.size());bool changed=false;
        for(std::size_t i=0;i<count;++i)try{
            p::Ref raw(cs::write(rows.array[i]));Row row;require(readRow(raw.get(),row),"Detached turret canonical row");if(!launches_->accept(row))continue;
            if(const auto*record=b.vehicle(name(row.owner))){auto entity=p::Ref(stock_native_bot::readNativeEntity(nullptr,record->engine_id));if(entity.get()!=Py_None)installEntity(row.owner,entity.get());}
            const auto cache=cache_.find(row.owner);bodies_.accept(row,cache==cache_.end()?std::vector<Component>{}:cache->second.components);
            publish(b.world->turrets,row);changed=true;
        }catch(const std::exception&e){note(e);}
        return changed;
    }
    p::Ref rows()const{
        if(!launches_)throw p::Error("Detached turret owner is not installed");
        auto out=p::list();for(const auto&row:launches_->rows()){p::Ref packed(packRow(row));const auto pending=launches_->proposals().find(row.owner);if(pending!=launches_->proposals().end()&&row.motion_sequence==0&&PyDict_DelItemString(packed.get(),"created_time_ms")<0)throw p::Error("Detached launch proposal shape is invalid");p::append(out.get(),packed);}return out;
    }
    p::Ref advance(Battle&b,double now){
        current(b);if(!std::isfinite(now))throw p::Error("Detached turret frame time is invalid");
        world_.now(now);launches_->retry(now,*this,world_);for(const auto&e:launches_->failures)errors_.push_back(e.reason);launches_->failures.clear();
        const bool send_tail=b.result.resolved().kind!=Value::Kind::Null&&launches_->pending()&&now>=next_publish_;
        auto tail=send_tail?rows():p::retain(Py_None);if(send_tail)next_publish_=now+.25;
        const auto server=native_battle::projectiles::serverTimeMs(b,now);const double server_ms=server?*server:-1.;
        if(bodies_.due(server_ms))bodies(b,server_ms);
        return tail;
    }
    std::vector<std::string> errors(){auto out=std::move(errors_);errors_.clear();return out;}
};
NativeOwner::NativeOwner(stock_native_bot::Runtime&bots,stock_native_world::Registry&registry,stock_native_world::Scene&scene,stock_native_bot_geometry::Vehicles&vehicles,std::uint16_t flags):impl_(std::make_unique<Impl>(bots,registry,scene,vehicles,flags)){}
NativeOwner::~NativeOwner()=default;
void NativeOwner::install(Battle&b){impl_->install(b);}
bool NativeOwner::queue(Battle&b,const Actor&vehicle,PyObject*entity,const Value*state,double now){return impl_->queue(b,vehicle,entity,state,now);}
bool NativeOwner::ingress(Battle&b,const Value&message){return impl_->ingress(b,message);}
p::Ref NativeOwner::advance(Battle&b,double now){return impl_->advance(b,now);}
p::Ref NativeOwner::rows()const{return impl_->rows();}
std::vector<std::string> NativeOwner::takeErrors(){return impl_->errors();}
}
