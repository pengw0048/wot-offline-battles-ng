#define WOT_CLIENT_REPLICA_HOST_BUILD
#include "stock_native_client_state.cpp"
#include "stock_native_client_drive_status.cpp"
#include "stock_native_client_critical.cpp"
#include "stock_native_bot_combat.cpp"
#include "stock_native_client_drive_runtime.hpp"
#include <Python.h>
namespace stock_native_client_weapon_runtime {double activeEnginePowerFactor(int){return 1.;}}
namespace stock_native_client_drive {
// Scene identity, siege state and equipment power are fixture boundaries.
// The status-to-motion condition consumer below is copied from production.
struct StatusConditionsProbe {
    std::shared_ptr<stock_native_client_state::Actor> local_;
    bool battle_over_=false;int overturn_level_=0,battle_=0;
    struct Driver {
        struct Descriptor {bool hydraulic=false;} descriptor_;
        struct State {std::optional<bool> siege_pending;} state_;
        const Descriptor&descriptor()const{return descriptor_;}
        const State&state()const{return state_;}
    } driver_;
    explicit StatusConditionsProbe(std::shared_ptr<stock_native_client_state::Actor> actor):local_(std::move(actor)){}
    void current()const{}
    Conditions conditions()const;
};
// @PRODUCTION_CONDITIONS@
}
namespace {
namespace d=stock_native_client_drive;namespace cs=stock_native_client_state;namespace p=stock_native_bot_python;
struct Owner {std::shared_ptr<cs::Actor> actor=std::make_shared<cs::Actor>();std::shared_ptr<d::Status> status;std::shared_ptr<stock_native_client_critical::Owner> repair;std::map<std::string,int> extras;};
const char*tag="drive.status.fixture";
void destroy(PyObject*c){delete static_cast<Owner*>(PyCapsule_GetPointer(c,tag));}
PyObject*create(PyObject*,PyObject*args){try{PyObject*raw;if(!PyArg_ParseTuple(args,"O",&raw))return nullptr;auto v=cs::read(raw);d::StatusDescriptor descriptor;
for(const auto&row:v.get("maxima").object)descriptor.maxima[row.first]=row.second.numeric();for(const auto&row:v.get("regeneration").object)descriptor.regeneration[row.first]=row.second.numeric();for(const auto&row:v.get("crew_roles").object)for(const auto&role:row.second.array)descriptor.crew_roles[row.first].insert(role.text);
auto*owner=new Owner;owner->actor->local=true;for(const auto&row:v.get("extra_indexes").object)owner->extras[row.first]=int(row.second.numeric());owner->status=d::installStatus(owner->actor,std::move(descriptor));return PyCapsule_New(owner,tag,destroy);}catch(const std::exception&e){PyErr_SetString(PyExc_RuntimeError,e.what());return nullptr;}}
PyObject*snapshot(const Owner&owner){auto result=p::dict();
const auto conditions=d::StatusConditionsProbe(owner.actor).conditions();p::set(result.get(),"tracked",p::boolean(conditions.tracked));p::set(result.get(),"engine_dead",p::boolean(conditions.engine_dead));
for(const char*key:{"reload","dispersion","aim_time","turret_speed","mobility","vision","signal","traverse","speed"})p::set(result.get(),key,p::number(owner.status->factor(key)));auto casualties=p::list();for(const auto&key:owner.status->casualties())p::append(casualties.get(),p::text(key));p::set(result.get(),"casualties",casualties);
auto destroyed=p::list(),yellow=p::list(),hp=p::dict();for(const auto&row:owner.status->critical().devices){if(row.second.destroyed)p::append(destroyed.get(),p::text(row.first));if(row.second.critical)p::append(yellow.get(),p::text(row.first));if(row.second.hp)p::set(hp.get(),row.first.c_str(),p::number(*row.second.hp));}p::set(result.get(),"destroyed",destroyed);p::set(result.get(),"critical",yellow);p::set(result.get(),"hp",hp);if(owner.repair){p::set(result.get(),"report",p::Ref(cs::write(owner.repair->state().report)));p::set(result.get(),"owned",p::boolean(owner.repair->state().owned));}return result.release();}
PyObject*update(PyObject*,PyObject*args){try{PyObject*c,*raw,*clock;unsigned long revision;if(!PyArg_ParseTuple(args,"OOkO",&c,&raw,&revision,&clock))return nullptr;auto*owner=static_cast<Owner*>(PyCapsule_GetPointer(c,tag));if(!owner)return nullptr;owner->actor->state=cs::read(raw);owner->status->update(revision,clock==Py_None?std::optional<double>{}:p::scalar(clock));return snapshot(*owner);}catch(const std::exception&e){PyErr_SetString(PyExc_RuntimeError,e.what());return nullptr;}}
PyObject*repair(PyObject*,PyObject*args){try{PyObject*c;double dt,now;if(!PyArg_ParseTuple(args,"Odd",&c,&dt,&now))return nullptr;auto*owner=static_cast<Owner*>(PyCapsule_GetPointer(c,tag));if(!owner)return nullptr;if(!owner->repair){stock_native_client_critical::Metadata metadata;metadata.maxima=owner->status->metadata().maxima;metadata.regeneration=owner->status->metadata().regeneration;metadata.extra_indexes=owner->extras;owner->repair=std::make_shared<stock_native_client_critical::Owner>(metadata,owner->status->critical());owner->status->trackOverlay(owner->repair);auto current=owner->status->critical();owner->repair->reconcile(*owner->actor,current);owner->status->localTracks(current);}
const auto effects=owner->repair->advance(owner->status->critical(),dt,now,true,.57,owner->status->casualties());owner->status->localTracks(owner->repair->tracks());p::Ref out(snapshot(*owner));auto closed=p::list();for(const auto&edge:effects.progress)if(edge.close)p::append(closed.get(),p::text(edge.name));p::set(out.get(),"progress_closed",closed);return out.release();}catch(const std::exception&e){PyErr_SetString(PyExc_RuntimeError,e.what());return nullptr;}}
PyMethodDef methods[]={{"create",create,METH_VARARGS,""},{"update",update,METH_VARARGS,""},{"repair",repair,METH_VARARGS,""},{nullptr,nullptr,0,nullptr}};
}
extern "C" void initclient_drive_status_test(){Py_InitModule("client_drive_status_test",methods);}
