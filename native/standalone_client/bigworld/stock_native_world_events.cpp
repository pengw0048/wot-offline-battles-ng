#include "stock_native_world_events.hpp"
#include "stock_native_bot_python.hpp"
#include "stock_native_bot_value.hpp"
#include <algorithm>
#include <cmath>

namespace stock_native_world {
namespace {
namespace p=stock_native_bot_python;
namespace v=stock_native_bot_value;
using Value=stock_native_client_state::Value;
const char* kind(Kind value){switch(value){case Kind::Tree:return "tree";case Kind::Falling:return "column";case Kind::Fragile:return "fragile";case Kind::Structure:return "module";default:throw p::Error("Native destructible kind is unsupported");}}
MaterialKey identity(const Destruction&e){return {e.key.first,e.key.second,e.kind==Kind::Structure?e.material:-1};}
Value event(const Destruction&e){auto out=v::object();auto&d=out.object;d["destructible_kind"]=v::text(kind(e.kind));d["chunk_id"]=v::integer(e.key.first);d["item_index"]=v::integer(e.key.second);d["x"]=v::number(e.position[0]);d["y"]=v::number(e.position[1]);d["z"]=v::number(e.position[2]);d["fall_yaw"]=v::number(e.yaw);d["speed"]=v::number(e.speed);d["is_shot"]=v::boolean(e.shot);if(e.kind==Kind::Structure)d["mat_kind"]=v::integer(e.material);return out;}
Destruction parse(const Value&source,double now){
    const auto&raw=source.resolved();if(raw.kind!=Value::Kind::Object||!std::isfinite(now))throw p::Error("Canonical destructible event is invalid");Destruction out;out.now=now;
    const auto name=v::string(v::value(raw,"destructible_kind"));if(name=="tree")out.kind=Kind::Tree;else if(name=="column")out.kind=Kind::Falling;else if(name=="fragile")out.kind=Kind::Fragile;else if(name=="module")out.kind=Kind::Structure;else throw p::Error("Canonical destructible kind is invalid");
    const auto&shot=v::value(raw,"is_shot");const auto c=v::wide(v::value(raw,"chunk_id")),i=v::wide(v::value(raw,"item_index"));if(c<0||std::uint64_t(c)>0xffffffffULL||i<0||i>0xffff||shot.kind!=Value::Kind::Boolean)throw p::Error("Canonical destructible identity is invalid");out.key={std::uint32_t(c),std::uint32_t(i)};out.shot=shot.boolean;
    out.position={{v::scalar(v::value(raw,"x")),v::scalar(v::value(raw,"y")),v::scalar(v::value(raw,"z"))}};out.yaw=v::numeric(raw,"fall_yaw");out.speed=v::numeric(raw,"speed");
    if(out.kind==Kind::Structure){out.material=v::wide(v::value(raw,"mat_kind"));if(out.material<73||out.material>=86)throw p::Error("Canonical destructible module is invalid");}return out;
}
}
WorldEvents::WorldEvents(Registry&r,stock_native_destructible::Runtime&native,Scene&scene,std::uint32_t space):registry_(r),native_(native),scene_(scene),space_(space),epoch_(r.epoch){}
WorldEvents::~WorldEvents(){if(Py_IsInitialized())Py_XDECREF(battle_);}
bool WorldEvents::install(PyObject*battle){
    try {
        if(battle_)throw p::Error("Native world event owner is already installed");
        p::Ref area(PyImport_ImportModule("AreaDestructibles"));
        // Never leave an old per-body Python animator running beside native
        // authority. The bootstrap owner must be created before its first fall.
        auto animator=p::value(area.get(),"g_destructiblesAnimator");if(animator.get()!=Py_None){auto bodies=p::value(animator.get(),"_DestructiblesAnimator__bodies");if(bodies.get()!=Py_None)for(Py_ssize_t i=0;i<p::size(bodies.get());++i){auto body=p::item(bodies.get(),i);if(PyDict_Check(body.get())&&p::integral(body.get(),"spaceID",-1)==space_)throw p::Error("Native world installation encountered active stock destructible bodies; install before legacy authority advances");}
            if(bodies.get()==Py_None||p::size(bodies.get())==0){
                // Exact #1513 __stopUpdate writes this flag before cancelling
                // its native callback. Retire the empty legacy clock at the
                // ownership boundary; native Runtime is the only new writer.
                auto raw=p::value(animator.get(),"__dict__");if(!PyDict_Check(raw.get()))throw p::Error("Native destructible animator has no raw dictionary");
                p::set(raw.get(),"_DestructiblesAnimator__isUpdating",p::boolean(false));auto callback=p::value(raw.get(),"_DestructiblesAnimator__updateCallbackID");
                if(callback.get()!=Py_None){p::Ref bigworld(PyImport_ImportModule("BigWorld"));auto cancel=p::value(bigworld.get(),"cancelCallback");if(!PyCFunction_Check(cancel.get()))throw p::Error("Native destructible callback retirement requires the reviewed builtin");p::Ref args(PyTuple_Pack(1,callback.get())),result(PyObject_CallObject(cancel.get(),args.get()));p::set(raw.get(),"_DestructiblesAnimator__updateCallbackID",p::retain(Py_None));}
            }}
        battle_=p::retain(battle).release();return true;
    }catch(const std::exception&e){if(!PyErr_Occurred())PyErr_SetString(PyExc_ValueError,e.what());return false;}
}
void WorldEvents::record(const Destruction&e,bool canonical){
    registry_.skin.speculative.erase(identity(e));
    if(e.kind!=Kind::Tree)return;
    activated_trees_.insert(e.key);
    if(canonical)trusted_published_.insert(e.key);
}
std::vector<std::pair<std::uint64_t,Value>> WorldEvents::pack(PyObject*battle){
    if(battle!=battle_||epoch_!=registry_.epoch)throw p::Error("Native world publication owner is stale");std::vector<Event> events=registry_.journal;for(const auto&row:trusted_)events.push_back(row.second);std::sort(events.begin(),events.end(),[](const Event&a,const Event&b){return a.sequence<b.sequence;});std::vector<std::pair<std::uint64_t,Value>> out;for(const auto&e:events){const bool trusted=trusted_.count(e.destruction.key)&&trusted_.at(e.destruction.key).sequence==e.sequence;if(!trusted)record(e.destruction,false);out.emplace_back(e.sequence,event(e.destruction));packed_=std::max(packed_,e.sequence);}return out;
}
bool WorldEvents::ack(std::uint64_t sequence){
    try{if(!battle_||epoch_!=registry_.epoch||sequence>packed_)return false;for(const auto&e:registry_.journal)if(e.sequence<=sequence)record(e.destruction,true);for(auto i=trusted_.begin();i!=trusted_.end();)if(i->second.sequence<=sequence){record(i->second.destruction,true);i=trusted_.erase(i);}else ++i;registry_.acknowledge(sequence);return true;}catch(const std::exception&e){if(!PyErr_Occurred())PyErr_SetString(PyExc_ValueError,e.what());return false;}
}
bool WorldEvents::ingress(const Value&raw,double now){
    try{if(!battle_||epoch_!=registry_.epoch)throw p::Error("Native canonical destructible owner is stale");const auto e=parse(raw,now);const auto id=identity(e);if(registry_.isolated(e.key))return false;if(registry_.skin.destroyed.count(id)||registry_.skin.destroyed.count(MaterialKey{e.key.first,e.key.second,-1})){record(e,true);return true;}
        const auto live=registry_.instances.find(e.key),baked=registry_.baked_instances.find(e.key);const Instance*proof=live!=registry_.instances.end()?&live->second:baked!=registry_.baked_instances.end()?&baked->second:nullptr;if(proof&&proof->kind!=e.kind)return false;if(!proof&&e.kind!=Kind::Tree)return false;
        const auto admitted=scene_.refresh(e.key,registry_);if(admitted==Admission::Isolated)return false;const bool logical=e.kind==Kind::Tree&&admitted!=Admission::Admitted;
        if(!logical&&!native_.destroy(e))return false;registry_.skin.destroyed.insert(id);registry_.pending[id]=now;++registry_.generation;if(e.kind==Kind::Tree)registry_.skin.tree_instances.insert(e.key);if(!logical&&admitted!=Admission::Admitted)replay_pending_[id]=e;record(e,true);return true;
    }catch(const std::exception&e){if(!PyErr_Occurred())PyErr_SetString(PyExc_ValueError,e.what());return false;}
}
bool WorldEvents::mirrorOnly(const Destruction& event){
    try{if(!battle_||epoch_!=registry_.epoch)throw p::Error("Native projectile mirror owner is stale");record(event,false);return true;}catch(const std::exception&e){if(!PyErr_Occurred())PyErr_SetString(PyExc_ValueError,e.what());return false;}
}
bool WorldEvents::mappingChanged(double now){
    try{if(epoch_!=registry_.epoch)return false;const auto revision=scene_.streamRevision();if(revision==stream_revision_)return true;for(const auto&e:native_.acceptedOrders())scene_.refresh(e.key,registry_);native_.mappingChanged(now);stream_revision_=revision;for(auto i=replay_pending_.begin();i!=replay_pending_.end();)if(native_.applied(i->second)){record(i->second,true);i=replay_pending_.erase(i);}else ++i;return true;}catch(const std::exception&e){if(!PyErr_Occurred())PyErr_SetString(PyExc_ValueError,e.what());return false;}
}
TrustedTreeStatus WorldEvents::trustedTreeStatus(const Key&key){if(trusted_.count(key)||trusted_published_.count(key))return TrustedTreeStatus::Tree;for(const auto&e:registry_.journal)if(e.destruction.kind==Kind::Tree&&e.destruction.key==key)return TrustedTreeStatus::Tree;return scene_.trustedTree(key);}
ContactResult WorldEvents::commitTrustedTrees(const std::set<MaterialKey>&token,const Point&start,const Point&end,double end_yaw,double speed,double now){
    ContactResult out;bool pending=false,hard=false;const Point a{{float(start[0]),float(start[1]),float(start[2])}},b{{float(end[0]),float(end[1]),float(end[2])}};const double dx=b[0]-a[0],dz=b[2]-a[2],yaw=dx*dx+dz*dz>1.e-16?std::atan2(dx,dz):end_yaw+(speed<0.?3.14159265358979323846:0.);
    if(token.empty()||token.size()>64||!std::isfinite(speed)||!std::isfinite(now)||!std::isfinite(yaw)){out.status=ContactStatus::Hard;return out;}
    for(const auto&id:token){if(std::get<2>(id)!=-1){hard=true;continue;}const Key k{std::get<0>(id),std::get<1>(id)};if(trusted_published_.count(k))continue;bool frozen=false;for(const auto&e:registry_.journal)if(e.destruction.kind==Kind::Tree&&e.destruction.key==k)frozen=true;if(frozen){pending=true;continue;}if(!trusted_.count(k)){const auto status=trustedTreeStatus(k);if(status==TrustedTreeStatus::Pending){pending=true;continue;}if(status!=TrustedTreeStatus::Tree){hard=true;continue;}Destruction e;e.kind=Kind::Tree;e.key=k;e.position=b;e.yaw=yaw;e.speed=speed;e.now=now;scene_.refresh(k,registry_);const auto value=registry_.instances.find(k);if(value!=registry_.instances.end())for(unsigned i=0;i<3;++i)e.position[i]=float(value->second.origin[i]);trusted_[k]={registry_.next_sequence++,e};}pending=true;}
    out.status=hard?ContactStatus::Hard:pending?ContactStatus::Pending:ContactStatus::Crushed;out.pending=pending;out.contacts=token;return out;
}
}
