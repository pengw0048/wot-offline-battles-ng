#include "pch.hpp"
#include "stock_native_client_environment.hpp"
#include "stock_native_client_survival_python.hpp"
#include "stock_native_client_drive_python.hpp"
#include "stock_native_client_drive_status.hpp"
#include "stock_native_client_query.hpp"
#include "stock_native_bot_descriptor.hpp"
#include "stock_native_bot_foliage_snapshot.hpp"
#include "stock_native_bot_python.hpp"
#include "stock_native_bot_resource_lifecycle.hpp"
#include "stock_native_bot_world_adapter.hpp"
#include "stock_native_destructible_backend.hpp"
#include "stock_native_world_events.hpp"
#include "pyscript/script.hpp"
#include "math/vector3.hpp"
#include <algorithm>
#include <memory>
#include <new>
namespace stock_native_client_environment {
namespace p=stock_native_bot_python;namespace shared=stock_native_client_state;
namespace trees=stock_native_bot_foliage;namespace world=stock_native_world;
namespace {
constexpr const char*capsule_name="offline.client.environment";
p::Ref field(PyObject*o,const char*n){if(!o||o==Py_None)return p::retain(Py_None);if(PyDict_Check(o))return p::value(o,n);auto name=p::text(n);auto*v=p::directAttribute(o,name.get());if(!v&&PyErr_ExceptionMatches(PyExc_AttributeError)){PyErr_Clear();return p::retain(Py_None);}return p::Ref(v);}
Point point(PyObject*v){if(PyList_Check(v)||PyTuple_Check(v))return p::numbers<3>(v);return {p::numeric(v,"x"),p::numeric(v,"y"),p::numeric(v,"z")};}
p::Ref error(const std::string&key,const std::exception&e){auto out=p::dict();p::set(out.get(),"entity",p::text(key));p::set(out.get(),"error",p::text(e.what()));return out;}
struct Installed {
    std::shared_ptr<shared::Actor> actor;
    p::Ref descriptor,record;
    shared::Value effective;
    Profile profile;
    std::uint64_t revision=0;
    bool dead_marker_known=false;double shot_until=0.;
    Installed(std::shared_ptr<shared::Actor>a,PyObject*d,PyObject*r):actor(std::move(a)),descriptor(p::retain(d)),record(p::retain(r)){}
};
struct Source:trees::TreePoses {
    stock_native_client_drive::SceneAccess access;
    std::set<std::uint32_t> waiting_chunks,loaded_chunks;
    bool loaded_available=false;
    explicit Source(stock_native_client_drive::SceneAccess a):access(std::move(a)){}
    bool waiting(std::uint32_t chunk)const override{return access.backend->waiting(chunk)||waiting_chunks.count(chunk)!=0;}
    bool loaded(std::uint32_t chunk)const override{return loaded_available?loaded_chunks.count(chunk)!=0:access.backend->loaded(chunk);}
    bool pose(trees::Key key,const trees::Point&center,const trees::Point&half,trees::Point&out,std::array<trees::Point,3>&axes)override{
        if(!access.scene->current())return false;
        access.scene->refresh(key,*access.registry);
        return access.backend->pose(key,center,half,out,axes);
    }
};
struct Owner {
    std::shared_ptr<shared::State> state;
    Source source;
    std::uint64_t epoch=0;
    p::Ref foliage,battle;
    std::unique_ptr<trees::Trees> trees;
    Spotting spotting;
    std::map<std::string,std::unique_ptr<Installed>> actors;
    std::map<std::string,Profile> profiles;
    std::int64_t space=0,local_id=0;int team=0;
    bool closed=false,spg=false;
    std::string strategic,arty;
    std::optional<double> server_time,received_at;
    Owner(std::shared_ptr<shared::State>s,stock_native_client_drive::SceneAccess a,PyObject*b,PyObject*f):state(std::move(s)),source(std::move(a)),epoch(state->epoch),foliage(p::retain(f)),battle(p::retain(b)){}
};
Owner&owner(PyObject*c){auto*o=static_cast<Owner*>(PyCapsule_GetPointer(c,capsule_name));if(!o)throw p::Error("Native environment owner is invalid");return *o;}
bool active(const Owner&o){return !o.closed&&!o.state->retired&&o.epoch==o.state->epoch&&o.source.access.scene->current();}
void destroy(PyObject*c){auto*o=static_cast<Owner*>(PyCapsule_GetPointer(c,capsule_name));if(o)delete o;else PyErr_Clear();}
std::optional<double> optional(PyObject*o,const char*n){auto v=field(o,n);return v.get()==Py_None?std::nullopt:std::optional<double>(p::scalar(v.get()));}
ActorView view(const Installed&i){const auto&a=*i.actor;ActorView out;out.key=a.key;out.engine_id=a.engine_id;out.id=a.id;out.local=a.local;out.team=int(a.state.get("team").numeric());out.alive=a.alive;out.ready=a.ready;out.missing=a.missing;out.tombstone=a.tombstone;out.position=a.local?Point{a.current[0],a.current[1],a.current[2]}:Point{float(a.current[0]),float(a.current[1]),float(a.current[2])};out.speed=a.speed;out.shot_until=i.shot_until;out.dead_marker_known=i.dead_marker_known;return out;}
Profile profile(PyObject*battle,const shared::Actor&a,PyObject*d){
    Profile out;const auto checkpoints=stock_native_bot_descriptor::checkpoints(d);if(checkpoints){out.checkpoints.available=true;out.checkpoints.points=*checkpoints;}
    auto chassis=field(d,"chassis"),hull=field(d,"hull"),turret=field(d,"turret"),gun=field(d,"gun");
    auto hp=field(chassis.get(),"hullPosition"),positions=field(hull.get(),"turretPositions");
    if(hp.get()!=Py_None&&positions.get()!=Py_None&&p::size(positions.get())>0){auto tp=p::item(positions.get(),0);const auto h=point(hp.get()),t=point(tp.get());for(unsigned j=0;j<3;++j)out.checkpoints.turret_mount[j]=h[j]+t[j];out.checkpoints.turret_mount_available=true;}
    const auto yaw=optional(gun.get(),"staticTurretYaw");out.checkpoints.static_turret_yaw_present=bool(yaw);out.checkpoints.static_turret_yaw=yaw.value_or(0.);
    auto local=p::boolean(a.local);p::Ref profile_args(PyTuple_Pack(2,d,local.get()));auto raw=p::call(battle,"_spotting_profile",profile_args.get());
    const auto&effective=a.state.get("effective_params").resolved();
    if(a.kind=="player"&&!a.local){
        const auto&spot=effective.get("spotting").resolved();if(spot.kind!=shared::Value::Kind::Object)throw p::Error("Remote player effective spotting profile is unavailable");
        raw.~Ref();new(&raw)p::Ref(shared::write(spot));
    }
    out.crew_vision=p::numeric(raw.get(),"vision_factor",1.);out.binoculars=p::truth(raw.get(),"has_binoculars");out.binocular_factor=p::numeric(raw.get(),"binocular_factor",1.);out.binocular_delay=p::numeric(raw.get(),"binocular_delay",3.);
    out.net=p::truth(raw.get(),"has_camouflage_net");out.net_delay=p::numeric(raw.get(),"camouflage_net_delay",3.);
    out.moving_aspect=p::numbers<2>(field(raw.get(),"invisibility_moving").get());out.still_aspect=p::numbers<2>(field(raw.get(),"invisibility_still").get());
    if(a.kind=="player"&&!a.local){const auto&c=effective.get("camouflage");out.camouflage={c.get("base_moving").numeric(),c.get("base_still").numeric()};out.shot_factor=c.get("shot_factor").numeric(1.);}
    else {p::Ref args(PyTuple_Pack(2,d,raw.get()));auto camo=p::call(battle,"_base_invisibility",args.get());out.camouflage=p::numbers<2>(camo.get());out.shot_factor=std::max(0.,std::min(1.,p::numeric(gun.get(),"invisibilityFactorAtShot",1.)));}
    out.base_view=p::numeric(turret.get(),"circularVisionRadius",400.);auto misc=field(d,"miscAttrs");out.misc_vision=p::numeric(misc.get(),"circularVisionRadiusFactor",1.);
    auto devices=field(d,"optionalDevices");if(devices.get()!=Py_None)for(Py_ssize_t j=0;j<p::size(devices.get());++j){auto device=p::item(devices.get(),j),id=field(device.get(),"id");const double delay=p::numeric(device.get(),"activateWhenStillSec");if(delay<=0.||!PyTuple_Check(id.get())||p::size(id.get())!=2)continue;out.optional_devices.emplace_back(p::wide(p::item(id.get(),1).get()),delay);}
    return out;
}
native_visibility::Pose pose(const shared::Actor&a){native_visibility::Pose out;out.position=a.local?Point{a.current[0],a.current[1],a.current[2]}:Point{float(a.current[0]),float(a.current[1]),float(a.current[2])};out.yaw=a.current[3];out.pitch=a.current[4];out.roll=a.current[5];out.relative_turret_yaw=a.current[6]-a.current[3];return out;}
double distance(const Point&a,const Point&b){return std::hypot(a[0]-b[0],a[2]-b[2]);}
double range(const Profile&p,double factor,double stationary){double v=std::max(50.,p.base_view)*std::max(0.,p.misc_vision*std::max(.5,std::min(1.,factor)))*std::max(0.,p.crew_vision);if(p.binoculars&&stationary>=std::max(0.,p.binocular_delay))v*=std::max(1.,p.binocular_factor);return std::max(50.,v);}
bool sight(Owner&o,const ActorView&observer,const ActorView&target,const Profile&source,const Profile&profile,double local_still,double target_still,bool fired,double now){
    const double separation=distance(observer.position,target.position);if(separation<=50.)return true;if(separation>445.)return false;
    const auto observer_actor=o.state->actors.find(observer.key),target_actor=o.state->actors.find(target.key);if(observer_actor==o.state->actors.end()||target_actor==o.state->actors.end())return false;
    native_visibility::PairInput pair;pair.observer=pose(*observer_actor->second);pair.target=pose(*target_actor->second);pair.observer_checkpoints=source.checkpoints;pair.target_checkpoints=profile.checkpoints;
    pair.observer_phase=o.server_time&&o.received_at?std::uint64_t(std::max(0.,std::trunc(*o.server_time+1000.*(now-*o.received_at)))/2000.):0;
    auto&d=pair.detection;d.distance=separation;d.view_range=range(source,stock_native_client_drive::statusFactor(*observer_actor->second,wot::offline_bot_step::CriticalStat::vision),local_still);
    d.base_camouflage=profile.camouflage;d.moving=std::abs(target.speed)>.5;d.fired_recently=fired;const auto&aspect=d.moving||(profile.net&&target_still<std::max(0.,profile.net_delay))?profile.moving_aspect:profile.still_aspect;d.additive=aspect[0];d.multiplier=aspect[1];d.shot_factor=profile.shot_factor;
    auto prepared=native_visibility::prepare_visibility(pair,o.trees->state().foliage);std::vector<std::uint8_t> clear;
    std::set<world::Key> broken;for(const auto&id:o.source.access.registry->skin.destroyed)broken.insert({std::get<0>(id),std::get<1>(id)});for(const auto&id:o.source.access.registry->skin.speculative)broken.insert({std::get<0>(id),std::get<1>(id)});
    for(std::size_t i=0;i<prepared.rays.size();++i){const auto&r=prepared.rays[i];world::Hit hit;const auto result=o.source.access.pipeline->horizontal(r.start,r.end,hit,&broken);if(result==world::QueryStatus::Failed)throw p::Error("Native local sight query failed");
        const auto length=[](const Point&a,const Point&b){return double((BW::Vector3(float(a[0]),float(a[1]),float(a[2]))-BW::Vector3(float(b[0]),float(b[1]),float(b[2]))).length());};
        const bool open=result==world::QueryStatus::Miss||length(hit.point,r.start)+1.5>=length(r.end,r.start);clear.push_back(open);if(native_visibility::should_stop(prepared,i,open))break;
    }
    return native_visibility::reduce_visibility(prepared,clear).detected;
}
void treeFrame(Owner&o,double now,bool force){
    if(o.source.access.events)for(const auto&key:o.source.access.events->takeActivatedTrees())o.trees->activate(key);
    if(o.trees->state().refreshing.empty()||(!force&&now+1.e-9<o.trees->state().next_refresh))return;
    auto runtime=field(o.battle.get(),"_runtime"),area=field(runtime.get(),"area_destructibles"),manager=field(area.get(),"g_destructiblesManager");
    std::set<trees::Key> active_bodies;for(const auto&body:o.source.access.backend->runtime().bodies())active_bodies.insert(body.order.key);
    o.source.waiting_chunks.clear();o.source.loaded_chunks.clear();o.source.loaded_available=false;
    offline_visibility::Update changes;o.trees->refresh(now,active_bodies,o.source,changes,force,p::truth(manager.get(),"forceNoAnimation"));
}
PyObject*fail(const std::exception&e){if(!PyErr_Occurred())PyErr_SetString(PyExc_RuntimeError,e.what());return nullptr;}
}
PyObject*create(PyObject*,PyObject*args){PyObject*battle;if(!PyArg_ParseTuple(args,"O",&battle))return nullptr;try{
    auto state=shared::fromBattle(battle);if(!state||state->retired)throw p::Error("Native environment state is unavailable");auto foliage=field(battle,"_foliage");auto o=std::make_unique<Owner>(std::move(state),stock_native_client_drive::sceneAccess(battle),battle,foliage.get());
    auto avatar=field(battle,"_avatar"),client=field(battle,"client");o->space=p::integral(avatar.get(),"spaceID");o->local_id=p::integral(client.get(),"player_id");o->team=int(p::integral(client.get(),"team"));
    trees::TreeState initial;if(!trees::readFoliageTreeState(battle,initial))throw p::Error("Native environment foliage is unavailable");o->trees=std::make_unique<trees::Trees>(std::move(initial));
    auto local=field(battle,"_local_descriptor"),type=field(local.get(),"type"),tags=field(type.get(),"tags"),spg=p::text("SPG");o->spg=tags.get()!=Py_None&&PySequence_Contains(tags.get(),spg.get())==1;
    auto runtime=field(battle,"_runtime"),input=field(runtime.get(),"avatar_input_handler"),modes=field(input.get(),"_CTRL_MODE");o->strategic=p::string(field(modes.get(),"STRATEGIC").get());o->arty=p::string(field(modes.get(),"ARTY").get());
    p::Ref cap(PyCapsule_New(o.get(),capsule_name,destroy));o.release();return cap.release();
}catch(const std::exception&e){return fail(e);}}
PyObject*ingest(PyObject*,PyObject*args){PyObject*c,*battle;if(!PyArg_ParseTuple(args,"OO",&c,&battle))return nullptr;try{
    auto&o=owner(c);auto errors=p::list();if(o.closed||o.state->retired||!o.source.access.scene->current())return errors.release();if(o.battle.get()!=battle||o.state!=shared::fromBattle(battle))throw p::Error("Native environment battle identity changed");
    if(o.epoch!=o.state->epoch){o.actors.clear();o.profiles.clear();o.spotting=Spotting();o.epoch=o.state->epoch;trees::TreeState initial;if(!trees::readFoliageTreeState(battle,initial))throw p::Error("Native environment reset foliage is unavailable");o.trees=std::make_unique<trees::Trees>(std::move(initial));}
    o.server_time=optional(battle,"_projectile_server_time_ms");o.received_at=optional(battle,"_projectile_server_local_time");auto records=field(battle,"_records");std::set<std::string> present;
    const auto accepted=o.state->actors;
    for(const auto&row:accepted){if(!active(o))break;auto a=row.second;present.insert(row.first);if(a->missing||a->tombstone||!a->ready)continue;try{
        auto*d=stock_native_client_query::descriptor(*a);if(!d)continue;auto key=p::text(row.first);auto*record=PyDict_Check(records.get())?PyDict_GetItem(records.get(),key.get()):nullptr;if(!record)continue;
        auto found=o.actors.find(row.first);const auto&effective=a->state.get("effective_params");
        if(found==o.actors.end()||found->second->actor!=a||found->second->descriptor.get()!=d||found->second->revision!=a->descriptor_revision||found->second->effective!=effective){
            auto item=std::make_unique<Installed>(a,d,record);item->profile=profile(battle,*a,d);if(!active(o))break;item->revision=a->descriptor_revision;item->effective=effective;auto v=view(*item);v.model=p::truth(record,"spot_visible");v.marker=p::truth(record,"spot_marker_visible",v.model);v.direct_until=p::numeric(record,"spot_until");v.radio_until=p::numeric(record,"radio_spot_until");v.next_probe=p::numeric(record,"spot_next");o.spotting.seed(v);o.profiles[row.first]=item->profile;o.actors[row.first]=std::move(item);
        }
        o.actors.at(row.first)->dead_marker_known=p::truth(record,"dead_marker_known");o.actors.at(row.first)->shot_until=p::numeric(record,"shot_penalty_until");
    }catch(const std::exception&e){PyErr_Clear();o.actors.erase(row.first);o.profiles.erase(row.first);p::append(errors.get(),error(row.first,e));}}
    for(auto i=o.actors.begin();i!=o.actors.end();)if(!present.count(i->first))i=o.actors.erase(i);else ++i;o.spotting.forget(present);return errors.release();
}catch(const std::exception&e){return fail(e);}}
PyObject*frame(PyObject*,PyObject*args){PyObject*c,*battle;double now;int hud_only=0;if(!PyArg_ParseTuple(args,"OOd|i",&c,&battle,&now,&hud_only))return nullptr;try{
    stock_native_client_drive::synchronizeStatus(battle,now);
    auto&o=owner(c);if(!active(o)||o.battle.get()!=battle)return p::retain(Py_None).release();auto errors=p::list();
    try{if(hud_only!=2)treeFrame(o,now,false);}catch(const std::exception&e){PyErr_Clear();p::append(errors.get(),error("fallen foliage",e));}
    Frame input;input.now=now;input.hud_only=hud_only==1;input.radio_only=hud_only==2;input.local_team=o.team;input.local_key="player:"+std::to_string(o.local_id);std::vector<ActorView> actors;
    for(const auto&row:o.actors){const auto a=o.state->actors.find(row.first);if(a==o.state->actors.end()||a->second!=row.second->actor)continue;actors.push_back(view(*row.second));if(a->second->local){input.presentation_origin={a->second->current[0],a->second->current[1],a->second->current[2]};input.local_vision_factor=stock_native_client_drive::statusFactor(*a->second,wot::offline_bot_step::CriticalStat::vision);}}
    auto spectated=field(battle,"_spectated_engine_id");if(spectated.get()!=Py_None){const auto id=p::wide(spectated.get());for(const auto&a:actors)if(a.engine_id==id)input.presentation_origin=a.position;}
    if(o.spg){auto avatar=field(battle,"_avatar"),handler=field(avatar.get(),"inputHandler"),mode=field(handler.get(),"_AvatarInputHandler__ctrlModeName");const auto name=p::string(mode.get());input.spg_aiming=name==o.strategic||name==o.arty;}
    const auto output=o.spotting.frame(input,actors,o.profiles,[&](const ActorView&local,const ActorView&target,const Profile&lp,const Profile&tp,double ls,double ts,bool fired){try{return sight(o,local,target,lp,tp,ls,ts,fired,now);}catch(const std::exception&e){PyErr_Clear();p::append(errors.get(),error(target.key,e));return false;}});
    auto result=p::dict(),transitions=p::list(),devices=p::list();for(const auto&t:output.transitions){auto row=p::dict();p::set(row.get(),"entity",p::text(t.key));p::set(row.get(),"engine_id",p::integer(t.engine_id));p::set(row.get(),"visible",p::boolean(t.model));p::set(row.get(),"marker",p::boolean(t.marker));p::append(transitions.get(),row);}
    for(const auto&d:output.devices){auto id=p::integer(d.id),on=p::boolean(d.active);p::append(devices.get(),p::Ref(PyTuple_Pack(2,id.get(),on.get())));}
    p::set(result.get(),"transitions",transitions);p::set(result.get(),"devices",devices);p::set(result.get(),"vision",output.vision_radius?p::number(*output.vision_radius):p::retain(Py_None));p::set(result.get(),"errors",errors);return result.release();
}catch(const std::exception&e){return fail(e);}}
PyObject*foliage(PyObject*,PyObject*args){PyObject*c,*battle;double now;int force=0;if(!PyArg_ParseTuple(args,"OOd|i",&c,&battle,&now,&force))return nullptr;try{auto&o=owner(c);auto errors=p::list();if(active(o)&&o.battle.get()==battle)try{treeFrame(o,now,force!=0);}catch(const std::exception&e){PyErr_Clear();p::append(errors.get(),error("fallen foliage",e));}return errors.release();}catch(const std::exception&e){return fail(e);}}
PyObject*lifecycle(PyObject*,PyObject*args){PyObject*c,*record;if(!PyArg_ParseTuple(args,"OO",&c,&record))return nullptr;try{auto&o=owner(c);if(active(o)){const auto id=p::integral(record,"engine_id");for(const auto&row:o.actors){auto&i=*row.second;if(i.actor->engine_id==id&&i.record.get()==record){i.dead_marker_known=p::truth(record,"dead_marker_known");i.shot_until=p::numeric(record,"shot_penalty_until");break;}}}return p::retain(Py_None).release();}catch(const std::exception&e){return fail(e);}}
PyObject*activate(PyObject*,PyObject*args){PyObject*c;unsigned long chunk,item;if(!PyArg_ParseTuple(args,"Okk",&c,&chunk,&item))return nullptr;try{auto&o=owner(c);if(item>0xffffUL||chunk>0xffffffffUL)return p::boolean(false).release();return p::boolean(active(o)&&o.trees->activate({std::uint32_t(chunk),std::uint32_t(item)})).release();}catch(const std::exception&e){return fail(e);}}
PyObject*radio(PyObject*,PyObject*args){PyObject*c,*message;double now;if(!PyArg_ParseTuple(args,"OOd",&c,&message,&now))return nullptr;try{
    auto&o=owner(c);if(!active(o)||p::string(field(message,"type").get())!="bot_observation")return p::boolean(false).release();auto round=field(message,"round_id");if(shared::read(round.get())!=o.state->round)return p::boolean(false).release();
    std::map<std::string,double> deadlines;std::set<std::string> allies;auto links=field(message,"radio_links"),contacts=field(message,"contacts");
    if(links.get()!=Py_None)for(Py_ssize_t i=0;i<p::size(links.get());++i){auto link=p::item(links.get(),i);if(p::string(field(link.get(),"kind").get())!="human"||p::integral(link.get(),"id")!=o.local_id)continue;auto peers=field(link.get(),"allies");if(peers.get()!=Py_None)for(Py_ssize_t j=0;j<p::size(peers.get());++j){auto peer=p::item(peers.get(),j);const auto kind=p::string(field(peer.get(),"kind").get());allies.insert((kind=="human"?"player:":"bot:")+std::to_string(p::integral(peer.get(),"id")));}}
    const auto local=o.state->actors.find("player:"+std::to_string(o.local_id));const bool spectating=local!=o.state->actors.end()&&!local->second->alive;
    if(contacts.get()!=Py_None)for(Py_ssize_t i=0;i<p::size(contacts.get());++i){auto contact=p::item(contacts.get(),i);if(p::integral(contact.get(),"observing_team")!=o.team)continue;auto kind=p::string(field(contact.get(),"target_kind").get());if(kind!="human"&&kind!="bot")continue;double lease=p::numeric(contact.get(),"time_left");if(lease<0.||lease>14.||p::truth(contact.get(),"visible")!=(lease>0.))throw p::Error("Native radio memory lease is invalid");
        if(!spectating){auto recipients=field(contact.get(),"radio_recipients");if(recipients.get()!=Py_None){lease=0.;for(Py_ssize_t j=0;j<p::size(recipients.get());++j){auto row=p::item(recipients.get(),j);if(p::string(field(row.get(),"kind").get())=="human"&&p::integral(row.get(),"id")==o.local_id)lease=std::max(lease,p::numeric(row.get(),"time_left"));}}
            else{auto ids=field(contact.get(),"visible_by_player_ids"),id=p::integer(o.local_id);if(ids.get()==Py_None||PySequence_Contains(ids.get(),id.get())!=1)lease=0.;}}
        deadlines[(kind=="human"?"player:":"bot:")+std::to_string(p::integral(contact.get(),"target_id"))]=now+lease;
    }
    o.spotting.radio(deadlines,std::move(allies));return p::boolean(true).release();
}catch(const std::exception&e){return fail(e);}}
PyObject*acknowledge(PyObject*,PyObject*args){PyObject*c,*raw;if(!PyArg_ParseTuple(args,"OO",&c,&raw))return nullptr;try{auto&o=owner(c);if(active(o)){Transition t;t.key=p::string(field(raw,"entity").get());t.engine_id=p::integral(raw,"engine_id");t.model=p::truth(raw,"visible");t.marker=p::truth(raw,"marker");const auto actor=o.state->actors.find(t.key);if(actor!=o.state->actors.end()&&actor->second->engine_id==t.engine_id){o.spotting.acknowledge(t);actor->second->visible=t.model;}}return p::retain(Py_None).release();}catch(const std::exception&e){return fail(e);}}
PyObject*close(PyObject*,PyObject*args){PyObject*c;if(!PyArg_ParseTuple(args,"O",&c))return nullptr;try{auto&o=owner(c);o.closed=true;o.actors.clear();o.profiles.clear();return p::retain(Py_None).release();}catch(const std::exception&e){return fail(e);}}
namespace {
PyMethodDef methods[]={{"drowning",stock_native_client_survival::drowningPython,METH_VARARGS,"Advance the visible native water warning."},{"overturn",stock_native_client_survival::overturnPython,METH_VARARGS,"Advance the visible native overturn warning."},{"retire_survival",stock_native_client_survival::retirePython,METH_VARARGS,"Retire the visible warning owner."},{"create",create,METH_VARARGS,"Bind local spotting and foliage to the existing visible native scene."},{"ingest",ingest,METH_VARARGS,"Install changed exact descriptor and lifecycle spotting data."},{"frame",frame,METH_VARARGS,"Advance local spotting clocks and accepted fallen foliage."},{"foliage",foliage,METH_VARARGS,"Refresh native fallen foliage without advancing the stock animator."},{"lifecycle",lifecycle,METH_VARARGS,"Publish one shot/death visibility fact at its accepted event edge."},{"activate",activate,METH_VARARGS,"Activate one accepted fallen tree."},{"radio",radio,METH_VARARGS,"Apply one complete server-owned radio visibility lease snapshot."},{"acknowledge",acknowledge,METH_VARARGS,"Commit one successful stock visibility presentation edge."},{"close",close,METH_VARARGS,"Retire the visible environment owner."},{nullptr,nullptr,0,nullptr}};
class Module final:public BW::Script::InitTimeJob {public:Module():BW::Script::InitTimeJob(1){}void init()override{if(!Py_InitModule("offline_client_environment_native",methods)){PyErr_Print();CRITICAL_MSG("Native client environment initialization failed\n");}}};Module module;
}
}
