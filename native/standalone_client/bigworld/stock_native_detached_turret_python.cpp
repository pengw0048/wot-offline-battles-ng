#include "stock_native_detached_turret_python.hpp"
#include "stock_native_detached_turret_snapshot.hpp"
#include "stock_native_detached_turret_world.hpp"
#include "stock_native_bot_resource_lifecycle.hpp"
#include "stock_native_bot_python.hpp"

#include <cmath>
#include <algorithm>

namespace stock_native_detached_turret {
namespace p=stock_native_bot_python;
namespace {
Owner actor(PyObject*record){const auto kind=p::string(p::value(record,"kind").get());const auto id=p::integral(record,"network_id");if((kind!="bot"&&kind!="player")||id<1||id>2147483647)throw p::Error("Detached turret record identity is invalid");return {kind=="player",id};}
std::string name(Owner owner){return std::string(owner.human?"player:":"bot:")+std::to_string(owner.id);}
Point xyz(PyObject*value){if(PyTuple_Check(value)||PyList_Check(value))return p::numbers<3>(value);return {{p::numeric(value,"x"),p::numeric(value,"y"),p::numeric(value,"z")}};}
double angle(double a,double b){constexpr double pi=3.14159265358979323846;double out=std::fmod(b-a+pi,2.*pi);if(out<0.)out+=2.*pi;return out-pi;}
bool statePose(PyObject*state,LaunchInput&input){if(!state||!PyDict_Check(state))return false;for(const char*key:{"x","y","z"})if(!p::field(state,key))return false;input.position={{p::numeric(state,"x"),p::numeric(state,"y"),p::numeric(state,"z")}};input.attitude={{p::numeric(state,"yaw"),p::numeric(state,"pitch"),p::numeric(state,"roll")}};input.turret_yaw=p::numeric(state,"turret_yaw",angle(input.attitude[0],p::numeric(state,"aim_yaw",input.attitude[0])));return true;}
}
class PythonOwner::Impl:public LaunchSource {
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
    std::unique_ptr<p::Ref> battle_,battle_dict_;
    std::map<Owner,Cached> cache_;
    std::vector<std::string> errors_;
    std::uint64_t epoch_=0;
    double next_publish_=0.;
    std::string round_;
    p::Ref records(){return p::value(battle_->get(),"_records");}
    p::Ref record(Owner key){auto all=records(),text=p::text(name(key));if(!PyDict_Check(all.get()))throw p::Error("Detached turret records are unavailable");auto*value=PyDict_GetItem(all.get(),text.get());return p::retain(value?value:Py_None);}
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
    bool resourcePending(PyObject*entity,PyObject*descriptor){
        (void)entity;if(descriptor==Py_None)return true;
        for(const char*name:{"chassis","hull","turret","gun"}){bool present=false;auto part=resourceField(descriptor,name,present);if(!present||part.get()==Py_None)return false;auto tester=resourceField(part.get(),"hitTester",present);if(present&&tester.get()==Py_None)return true;if(!present)return false;auto box=resourceField(tester.get(),"bbox",present);if(present&&box.get()==Py_None)return true;auto model=resourceField(tester.get(),"_ModelHitTester__bspModel",present);if(present&&model.get()==Py_None)return true;}
        return false;
    }
    Cached& installEntity(Owner key,PyObject*entity){
        auto&cached=cache_[key];auto descriptor=p::Ref(stock_native_bot::readNativeEntityField(battle_->get(),entity,"typeDescriptor"));
        stock_native_bot::ResourceStamp stamp;require(stock_native_bot::readResourceStamp(entity,{descriptor.get()},stamp,battle_->get()),"Detached turret resource stamp");const bool pending=resourcePending(entity,descriptor.get());if(cached.entity&&cached.entity->get()==entity&&cached.descriptor&&cached.descriptor->get()==descriptor.get()&&cached.stamp==stamp&&(cached.complete||(cached.pending&&pending)))return cached;
        Cached result;result.stamp=std::move(stamp);result.entity=std::make_unique<p::Ref>(p::retain(entity));result.descriptor=std::make_unique<p::Ref>(p::retain(descriptor.get()));result.pending=pending;if(pending){cached=std::move(result);return cached;}
        try{require(readLaunchGeometry(descriptor.get(),result.launch),"Detached turret launch geometry");}catch(const std::exception&e){note(e);}
        try{require(readComponents(descriptor.get(),result.components),"Detached turret mass geometry");}catch(const std::exception&e){note(e);}
        try{require(readVehicleGeometry(descriptor.get(),result.geometry),"Detached turret support geometry");auto physics=p::value(descriptor.get(),"physics");result.mass=p::numeric(physics.get(),"weight");if(result.mass<=0.)throw p::Error("Vehicle collision mass is unavailable");result.support_valid=true;}catch(const std::exception&e){note(e);}
        result.complete=result.launch.geometry_ready&&!result.components.empty()&&result.support_valid;cached=std::move(result);return cached;
    }
    std::optional<LaunchInput> pose(Owner key,PyObject*raw_record,PyObject*entity,PyObject*state){
        auto&cached=installEntity(key,entity);auto input=cached.launch;input.ready=p::truth(raw_record,"ready");input.detached=p::flag(p::Ref(stock_native_bot::readNativeEntityField(battle_->get(),entity,"isTurretDetached")).get());
        if(statePose(state,input))return input;
        if(!key.human){const auto found=bots_.actors().find(key.id);if(found!=bots_.actors().end()){const auto&a=found->second;input.position=a.motion.position;input.attitude={{a.motion.yaw,a.motion.pitch,a.motion.roll}};input.turret_yaw=bots_.weapons().at({offline_simulation::ActorKind::Bot,key.id}).aim.turret_yaw;return input;}}
        auto saved=p::value(raw_record,"state");if(statePose(saved.get(),input))return input;
        auto position=p::Ref(stock_native_bot::readNativeEntityField(battle_->get(),entity,"position")),matrix=p::Ref(stock_native_bot::readNativeEntityField(battle_->get(),entity,"matrix"));if(position.get()==Py_None||matrix.get()==Py_None)return {};input.position=xyz(position.get());input.attitude={{p::numeric(matrix.get(),"yaw"),p::numeric(matrix.get(),"pitch"),p::numeric(matrix.get(),"roll")}};
        input.turret_yaw=nativeTurretYaw(vehicles_,key).value_or(0.);return input;
    }
    void current(PyObject*battle){if(!battle_||battle_->get()!=battle||epoch_!=registry_.epoch)throw p::Error("Detached turret owner is stale");}
    void sync(){auto proposals=p::dict();for(const auto&entry:launches_->proposals()){p::Ref packed(packRow(entry.second));if(entry.second.motion_sequence==0&&PyDict_DelItemString(packed.get(),"created_time_ms")<0)throw p::Error("Detached launch proposal shape is invalid");auto key=p::text(name(entry.first));if(PyDict_SetItem(proposals.get(),key.get(),packed.get())<0)throw p::Error("Detached turret proposal mirror failed");}p::set(battle_dict_->get(),"_detached_turret_proposals",proposals);}
    double serverTime(double now){auto time=p::value(battle_->get(),"_projectile_server_time_ms"),local=p::value(battle_->get(),"_projectile_server_local_time");if(time.get()==Py_None||local.get()==Py_None)return -1.;return std::max(0.,std::trunc(p::scalar(time.get())+1000.*(now-p::scalar(local.get()))));}
    void accepted(PyObject*raw){Row row;require(readRow(raw,row),"Detached turret canonical row");if(!launches_->accept(row))return;const auto found=cache_.find(row.owner);bodies_.accept(row,found==cache_.end()?std::vector<Component>{}:found->second.components);}
public:
    Impl(stock_native_bot::Runtime&bots,stock_native_world::Registry&registry,stock_native_world::Scene&scene,stock_native_bot_geometry::Vehicles&vehicles,std::uint16_t flags):bots_(bots),vehicles_(vehicles),registry_(registry),world_(bots,registry,scene,flags){}
    bool install(PyObject*battle){try{if(battle_)throw p::Error("Detached turret owner is already installed");battle_=std::make_unique<p::Ref>(p::retain(battle));epoch_=registry_.epoch;battle_dict_=std::make_unique<p::Ref>(PyDict_Check(battle)?p::retain(battle):p::value(battle,"__dict__"));if(!PyDict_Check(battle_dict_->get()))throw p::Error("Detached turret battle dictionary is unavailable");auto start=p::value(battle,"_start_message"),round=p::value(start.get(),"round_id");p::Ref round_text(PyObject_Str(round.get()));round_=p::string(round_text.get());launches_=std::make_unique<LaunchQueue>(round_);next_publish_=p::numeric(battle,"_next_turret_publish");auto old_bodies=p::value(battle,"_turret_bodies");if(PyDict_Check(old_bodies.get())&&PyDict_Size(old_bodies.get())>0)throw p::Error("Install native detached turret owner before legacy body simulation");auto all=records();if(!PyDict_Check(all.get()))throw p::Error("Detached turret records are unavailable");Py_ssize_t cursor=0;PyObject*key,*record;while(PyDict_Next(all.get(),&cursor,&key,&record)){try{
            // _records also contains the worker's local player:-1 space
            // carrier. Prefetch only combat participants, as module.records
            // does; launch and canonical ingress retain strict identities.
            if(!PyDict_Check(record))continue;
            const auto kind=p::string(p::value(record,"kind").get());
            if(kind!="bot"&&kind!="player")continue;
            if(p::integral(record,"network_id")<=0)continue;
            const auto owner=actor(record);auto entity=p::Ref(stock_native_bot::readNativeEntity(battle,p::integral(record,"engine_id")));if(entity.get()!=Py_None)installEntity(owner,entity.get());
        }catch(const std::exception&e){note(e);}}
        auto rows=p::value(battle,"_detached_turret_rows");if(PyDict_Check(rows.get())){cursor=0;while(PyDict_Next(rows.get(),&cursor,&key,&record))try{accepted(record);}catch(const std::exception&e){note(e);}}auto retries=p::value(battle,"_detached_turret_retry");if(PyDict_Check(retries.get())){cursor=0;while(PyDict_Next(retries.get(),&cursor,&key,&record))try{const auto text=p::string(key);if(text.compare(0,9,"proposal:")!=0)continue;const auto actor_name=text.substr(9);const auto split=actor_name.find(':');if(split==std::string::npos)continue;const auto id=std::stoll(actor_name.substr(split+1));const auto kind=actor_name.substr(0,split);if((kind!="bot"&&kind!="player")||id<1||id>2147483647)throw p::Error("Detached turret retry actor is invalid");launches_->retryAt({kind=="player",id},p::scalar(record));}catch(const std::exception&e){note(e);}}auto pending=p::value(battle,"_detached_turret_proposals");if(PyDict_Check(pending.get())){cursor=0;while(PyDict_Next(pending.get(),&cursor,&key,&record))try{auto copy=p::Ref(PyDict_Copy(record));if(!p::field(copy.get(),"created_time_ms"))p::set(copy.get(),"created_time_ms",p::integer(0));Row row;require(readRow(copy.get(),row));launches_->proposal(row);}catch(const std::exception&e){note(e);}}return true;}catch(const std::exception&e){if(!PyErr_Occurred())PyErr_SetString(PyExc_ValueError,e.what());return false;}}
    bool queue(PyObject*raw_record,PyObject*entity,PyObject*state,double now){try{if(!battle_)throw p::Error("Detached turret owner is not installed");current(battle_->get());if(!std::isfinite(now))throw p::Error("Detached turret launch time is invalid");const auto key=actor(raw_record);auto input=pose(key,raw_record,entity,state);if(!input)return false;world_.now(now);const bool result=launches_->queue(key,*input,now,world_);sync();return result;}catch(const std::exception&e){if(!PyErr_Occurred())PyErr_SetString(PyExc_ValueError,e.what());return false;}}
    std::optional<LaunchInput> lookup(Owner owner)override{auto raw=record(owner);if(raw.get()==Py_None)return {};auto entity=p::Ref(stock_native_bot::readNativeEntity(battle_->get(),p::integral(raw.get(),"engine_id")));return entity.get()==Py_None?std::nullopt:pose(owner,raw.get(),entity.get(),Py_None);}
    bool ingress(PyObject*message,double){try{if(!battle_)throw p::Error("Detached turret owner is not installed");current(battle_->get());if(!PyDict_Check(message))return false;auto incoming=p::value(message,"round_id");p::Ref text(PyObject_Str(incoming.get()));if(p::string(text.get())!=round_)return false;auto rows=p::value(message,"detached_turrets");if(rows.get()==Py_None||(!PyList_Check(rows.get())&&!PyTuple_Check(rows.get())))return false;const auto count=std::min<Py_ssize_t>(12,p::size(rows.get()));auto existing=p::value(battle_->get(),"_detached_turret_rows");if(!PyDict_Check(existing.get()))throw p::Error("Detached turret row mirror is unavailable");bool changed=false;for(Py_ssize_t i=0;i<count;++i)try{auto raw=p::item(rows.get(),i);Row row;require(readRow(raw.get(),row),"Detached turret canonical row");if(!launches_->accept(row))continue;auto r=record(row.owner);if(r.get()!=Py_None){auto entity=p::Ref(stock_native_bot::readNativeEntity(battle_->get(),p::integral(r.get(),"engine_id")));if(entity.get()!=Py_None)installEntity(row.owner,entity.get());}const auto cache=cache_.find(row.owner);bodies_.accept(row,cache==cache_.end()?std::vector<Component>{}:cache->second.components);auto key=p::text(name(row.owner));p::Ref packed(packRow(row));if(PyDict_SetItem(existing.get(),key.get(),packed.get())<0)throw p::Error("Detached turret canonical mirror failed");changed=true;}catch(const std::exception&e){note(e);}sync();return changed;}catch(const std::exception&e){if(!PyErr_Occurred())PyErr_SetString(PyExc_ValueError,e.what());return false;}}
    PyObject* rows()const{try{if(!launches_)throw p::Error("Detached turret owner is not installed");auto out=p::list();for(const auto&row:launches_->rows()){p::Ref packed(packRow(row));const auto pending=launches_->proposals().find(row.owner);if(pending!=launches_->proposals().end()&&row.motion_sequence==0&&PyDict_DelItemString(packed.get(),"created_time_ms")<0)throw p::Error("Detached launch proposal shape is invalid");p::append(out.get(),packed);}return out.release();}catch(const std::exception&e){if(!PyErr_Occurred())PyErr_SetString(PyExc_ValueError,e.what());return nullptr;}}
    PyObject* advance(PyObject*battle,double now){try{current(battle);if(!std::isfinite(now))throw p::Error("Detached turret frame time is invalid");world_.now(now);launches_->retry(now,*this,world_);for(const auto&e:launches_->failures)errors_.push_back(e.reason);launches_->failures.clear();auto result=p::value(battle,"_battle_result");const bool send_tail=result.get()!=Py_None&&launches_->pending()&&now>=next_publish_;auto tail=send_tail?p::dict():p::retain(Py_None);if(send_tail){next_publish_=now+.25;p::Ref all(rows());p::set(tail.get(),"detached_turrets",all);}const auto server_ms=serverTime(now);if(bodies_.due(server_ms)){
            std::vector<Vehicle> vehicles;std::vector<PlayerImpulse> impulses;auto physical_order=p::dict();auto snapshot=p::value(battle,"_last_snapshot"),players=p::value(snapshot.get(),"players");
            if(players.get()!=Py_None)for(Py_ssize_t i=0;i<p::size(players.get());++i)try{auto player=p::item(players.get(),i);const auto id=p::integral(player.get(),"id");if(id<=0||!p::truth(player.get(),"participating")||!p::truth(player.get(),"world_pose"))continue;require(readPlayerImpulses(player.get(),impulses));const Owner owner{true,id};auto order_key=p::text(name(owner));if(PyDict_SetItem(physical_order.get(),order_key.get(),Py_None)<0)throw p::Error("Detached turret player order allocation failed");auto cached=cache_.find(owner);if(cached==cache_.end()||!cached->second.support_valid)continue;VehiclePose pose;pose.position={{p::numeric(player.get(),"x"),p::numeric(player.get(),"y"),p::numeric(player.get(),"z")}};pose.attitude={{p::numeric(player.get(),"yaw"),p::numeric(player.get(),"terrain_pitch",p::numeric(player.get(),"pitch")-p::numeric(player.get(),"suspension_pitch")),p::numeric(player.get(),"roll")}};pose.separate_hull=true;pose.hull_position=pose.position;pose.hull_attitude={{pose.attitude[0],p::numeric(player.get(),"pitch"),pose.attitude[2]}};pose.aim_yaw=p::numeric(player.get(),"aim_yaw",pose.attitude[0]);pose.gun_pitch=p::numeric(player.get(),"gun_pitch");auto input=lookup(owner);if(input)vehicles.push_back(vehicle(owner,cached->second.geometry,pose,cached->second.mass,p::numeric(player.get(),"speed"),p::numeric(player.get(),"push_x"),p::numeric(player.get(),"push_z"),p::truth(player.get(),"alive",true),!input->detached));}catch(const std::exception&e){note(e);}
            for(const auto&entry:bots_.actors())try{const Owner owner{false,entry.first};auto input=lookup(owner);auto cached=cache_.find(owner);if(!input||cached==cache_.end()||!cached->second.support_valid)continue;const auto&a=entry.second;VehiclePose pose;pose.position=a.motion.position;pose.attitude={{a.motion.yaw,a.motion.terrain_pitch,a.motion.roll}};pose.separate_hull=true;pose.hull_position=pose.position;pose.hull_attitude={{a.motion.yaw,a.motion.pitch,a.motion.roll}};pose.aim_yaw=a.weapon.aim_yaw;pose.gun_pitch=bots_.weapons().at({offline_simulation::ActorKind::Bot,entry.first}).aim.gun_pitch;vehicles.push_back(vehicle(owner,cached->second.geometry,pose,cached->second.mass,a.motion.speed,a.motion.push_x,a.motion.push_z,a.motion.alive,!input->detached));}catch(const std::exception&e){note(e);}
            // Reproduce states=dict(player generator); states.update(bot generator)
            // at the plain-data ingress boundary. CPython's actual native dict
            // layout retains the target's string-hash ordering without a
            // callback from the rigid solver or an invented ID sort.
            auto raw_bots=p::value(battle,"_bots");if(raw_bots.get()!=Py_None){auto states=p::value(raw_bots.get(),"states");if(PyDict_Check(states.get())){Py_ssize_t cursor=0;PyObject*key,*value;while(PyDict_Next(states.get(),&cursor,&key,&value)){auto order_key=p::text(name({false,p::wide(key)}));if(PyDict_SetItem(physical_order.get(),order_key.get(),Py_None)<0)throw p::Error("Detached turret Bot order allocation failed");}}}
            std::map<std::string,Vehicle> by_name;for(auto&v:vehicles)by_name.emplace(name(v.owner),std::move(v));vehicles.clear();Py_ssize_t order_cursor=0;PyObject*order_key,*order_value;while(PyDict_Next(physical_order.get(),&order_cursor,&order_key,&order_value)){const auto found=by_name.find(p::string(order_key));if(found!=by_name.end())vehicles.push_back(std::move(found->second));}
            for(const auto owner:bodies_.order)try{auto input=lookup(owner);bodies_.available(owner,bool(input));const auto c=cache_.find(owner);if(c!=cache_.end())bodies_.components(owner,c->second.components);}catch(const std::exception&e){bodies_.available(owner,false);note(e);}
            for(auto&row:bodies_.update(server_ms,vehicles,impulses,world_))launches_->proposal(std::move(row));for(const auto&failure:bodies_.failures)errors_.push_back(failure.reason);
        }sync();return tail.release();}catch(const std::exception&e){if(!PyErr_Occurred())PyErr_SetString(PyExc_ValueError,e.what());return nullptr;}}
    PyObject* errors(){try{auto out=p::strings(errors_);errors_.clear();return out.release();}catch(const std::exception&e){if(!PyErr_Occurred())PyErr_SetString(PyExc_ValueError,e.what());return nullptr;}}
};
PythonOwner::PythonOwner(stock_native_bot::Runtime&bots,stock_native_world::Registry&registry,stock_native_world::Scene&scene,stock_native_bot_geometry::Vehicles&vehicles,stock_native_bot::NativeServices&,std::uint16_t flags):impl_(std::make_unique<Impl>(bots,registry,scene,vehicles,flags)){}
PythonOwner::~PythonOwner()=default;
bool PythonOwner::install(PyObject*battle){return impl_->install(battle);}
bool PythonOwner::queue(PyObject*record,PyObject*entity,PyObject*state,double now){return impl_->queue(record,entity,state,now);}
bool PythonOwner::ingress(PyObject*message,double now){return impl_->ingress(message,now);}
PyObject*PythonOwner::advance(PyObject*battle,double now){return impl_->advance(battle,now);}
PyObject*PythonOwner::rows()const{return impl_->rows();}
PyObject*PythonOwner::takeErrors(){return impl_->errors();}
}
