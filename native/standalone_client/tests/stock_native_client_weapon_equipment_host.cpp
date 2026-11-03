#include "stock_native_client_weapon_equipment.hpp"
#include "stock_native_client_weapon_runtime.hpp"
#include "stock_native_bot_python.hpp"
#include "stock_native_battle_test_lifetime.hpp"
namespace p=stock_native_bot_python;namespace w=stock_native_client_weapon_runtime;namespace cs=stock_native_client_state;namespace shared=cs;
using w::EquipmentFrame;using w::equipmentFrame;using w::Edges;using w::Equipment;

namespace {
double test_time=0.;
PyObject*time(PyObject*,PyObject*){return p::number(test_time).release();}
PyObject*setTime(PyObject*,PyObject*v){try{test_time=p::scalar(v);Py_RETURN_NONE;}catch(const std::exception&){PyErr_SetString(PyExc_RuntimeError,"Invalid fixture time");return nullptr;}}
// Exercise the production bridge function with an already-installed runtime.
// The gun/HUD decision policy has its separate complete differential test.
struct RuntimeMirror {std::vector<Equipment>held;void equipment(std::vector<Equipment>v){held=std::move(v);}void equipment(Edges&,double,bool){}};
struct Owner {std::shared_ptr<cs::State>state=std::make_shared<cs::State>();w::EquipmentGate equipment_gate;cs::Value equipment_wire;std::uint64_t equipment_transaction=0,accepted_revision=0;bool have_equipment=false,advancing=false;std::unique_ptr<RuntimeMirror>runtime=std::make_unique<RuntimeMirror>();PyObject*battle=nullptr;double publication_time=0.;};
void destroyOwner(PyObject*c){delete static_cast<std::shared_ptr<Owner>*>(PyCapsule_GetPointer(c,"equipment.bridge.test"));}
std::shared_ptr<Owner>ensure(PyObject*battle){auto capsule=p::value(battle,"_native_equipment_test");if(capsule.get()!=Py_None)return *static_cast<std::shared_ptr<Owner>*>(PyCapsule_GetPointer(capsule.get(),"equipment.bridge.test"));auto o=std::make_shared<Owner>();o->battle=battle;auto*holder=new std::shared_ptr<Owner>(o);p::Ref c(PyCapsule_New(holder,"equipment.bridge.test",destroyOwner));if(PyObject_SetAttrString(battle,"_native_equipment_test",c.get())<0)throw p::Error("Test owner install failed");return o;}
bool active(const native_battle::Battle&,const Owner&o){return !p::truth(o.battle,"_test_retired");}
p::Ref field(PyObject*o,const char*n){return p::value(o,n);}
void replace(p::Ref&v,p::Ref replacement){v.~Ref();new(&v)p::Ref(std::move(replacement));}
void attribute(PyObject*o,const char*n,const p::Ref&v){if(PyObject_SetAttrString(o,n,v.get())<0)throw p::Error("Test slot assignment failed");}
std::vector<Equipment>equipmentObjects(PyObject*states){std::vector<Equipment>out;for(Py_ssize_t i=0;i<p::size(states);++i){auto raw=p::item(states,i),contract=field(raw.get(),"contract");Equipment e;e.compact=p::integral(contract.get(),"compactDescr");e.uses_left=int(p::integral(raw.get(),"uses_left"));e.active=p::truth(raw.get(),"active");e.ready_at=p::numeric(raw.get(),"ready_at");out.push_back(e);}return out;}
void queue(native_battle::Battle&,Owner&o,const Edges&,double now){o.publication_time=now;}
void emitSafe(native_battle::Battle&,Owner&o){auto states=p::value(o.battle,"_equipment_state");for(std::size_t i=0;i<o.runtime->held.size();++i){auto state=p::item(states.get(),i);attribute(state.get(),"ready_at",p::number(o.runtime->held[i].ready_at));}auto now=p::number(o.publication_time);p::Ref args(PyTuple_Pack(1,now.get()));p::call(o.battle,"_present_equipments",args.get());}
struct Guard {Owner&o;explicit Guard(Owner&v):o(v){o.advancing=true;}~Guard(){o.advancing=false;}};
PyObject*fail(const std::exception&e){if(!PyErr_Occurred())PyErr_SetString(PyExc_RuntimeError,e.what());return nullptr;}
std::map<PyObject*,std::shared_ptr<native_battle::Battle>> battles;
native_battle::Battle& battleFor(PyObject*shell){auto&b=battles[shell];if(!b){b=std::make_shared<native_battle::Battle>();b->local_player=p::integral(p::value(shell,"client").get(),"player_id");b->gun=std::make_unique<native_battle::LocalGun>();b->startup=std::make_unique<native_battle::Startup>();b->startup->client_ready_received=true;b->engine.shell=native_battle::borrow(shell);b->engine.avatar=native_battle::borrow(p::value(shell,"_avatar").get());b->engine.server=native_battle::borrow(p::value(shell,"_server").get());}return *b;}
std::shared_ptr<Owner>ensure(native_battle::Battle&b){return ensure(b.engine.shell.get());}
w::Equipment equipmentRow(const cs::Value&row,double ready){w::Equipment out;out.compact=std::int64_t(row.get("equipment").get("compactDescr").numeric());out.uses_left=int(row.get("usesLeft").numeric());out.active=row.get("active").truth();out.ready_at=ready;return out;}
PyObject*ingress(PyObject*,PyObject*args){PyObject*battle,*states;if(!PyArg_ParseTuple(args,"OO",&battle,&states))return nullptr;try{auto o=ensure(battle);o->runtime->equipment(equipmentObjects(states));o->have_equipment=true;Py_RETURN_NONE;}catch(const std::exception&e){return fail(e);}}
PyObject*equipmentSnapshot(PyObject*,PyObject*args){PyObject*shell,*snapshot;int present;if(!PyArg_ParseTuple(args,"OOi",&shell,&snapshot,&present))return nullptr;try{auto&battle=battleFor(shell);const auto raw=cs::read(snapshot);const bool accepted=w::equipmentSnapshot(battle,raw,present!=0);
 if(accepted){attribute(shell,"_equipment_revision",p::integer(battle.gun->equipment_revision));const cs::Value*local=nullptr;for(const auto&row:raw.get("players").array)if(std::int64_t(row.get("id").numeric())==battle.local_player)local=&row;
  if(local){const auto frame=equipmentFrame(local->get("equipment_states"),test_time);auto states=p::value(shell,"_equipment_state");for(std::size_t i=0;i<frame.clocks.size();++i){auto state=p::item(states.get(),i);const auto&clock=frame.clocks[i];attribute(state.get(),"ready_at",p::number(clock.ready_at));attribute(state.get(),"_auto_pending_since",clock.automatic?p::number(*clock.automatic):p::retain(Py_None));attribute(state.get(),"_ai_pending_since",clock.ai?p::number(*clock.ai):p::retain(Py_None));}}
 }return p::boolean(accepted).release();}catch(const std::exception&e){return fail(e);}}
PyObject*reset(PyObject*,PyObject*){battles.clear();Py_RETURN_NONE;}
void destroy(PyObject*c){delete static_cast<w::EquipmentGate*>(PyCapsule_GetPointer(c,"equipment.gate.test"));}
PyObject*create(PyObject*,PyObject*){return PyCapsule_New(new w::EquipmentGate(),"equipment.gate.test",destroy);}
PyObject*step(PyObject*,PyObject*args){PyObject*c,*rows,*result,*states;long long revision;double now;int commit=0;if(!PyArg_ParseTuple(args,"OOLOOd|i",&c,&rows,&revision,&result,&states,&now,&commit))return nullptr;try{
    auto*gate=static_cast<w::EquipmentGate*>(PyCapsule_GetPointer(c,"equipment.gate.test"));if(!gate)throw p::Error("Test equipment gate is unavailable");const auto frame=w::equipmentFrame(cs::read(rows),now);auto accepted=cs::read(result);
    if(commit){gate->commit(frame,revision,std::move(accepted));Py_RETURN_NONE;}
    const bool hot=gate->matches(frame,revision,accepted)&&states!=Py_None&&p::size(states)==Py_ssize_t(frame.clocks.size());
    if(hot)for(std::size_t i=0;i<frame.clocks.size();++i){auto state=p::item(states,i);const auto&clock=frame.clocks[i];const auto set=[&](const char*name,const p::Ref&value){if(PyObject_SetAttrString(state.get(),name,value.get())<0)throw p::Error("Test equipment slot assignment failed");};set("ready_at",p::number(clock.ready_at));set("_auto_pending_since",clock.automatic?p::number(*clock.automatic):p::retain(Py_None));set("_ai_pending_since",clock.ai?p::number(*clock.ai):p::retain(Py_None));}
    return p::boolean(hot).release();
}catch(const std::exception&e){if(!PyErr_Occurred())PyErr_SetString(PyExc_RuntimeError,e.what());return nullptr;}}
PyMethodDef methods[]={{"reset",reset,METH_NOARGS,""},{"time",time,METH_NOARGS,""},{"set_time",setTime,METH_O,""},{"equipment_snapshot",equipmentSnapshot,METH_VARARGS,""},{"equipment_ingress",ingress,METH_VARARGS,""},{"create",create,METH_NOARGS,""},{"step",step,METH_VARARGS,""},{nullptr,nullptr,0,nullptr}};
}
PyMODINIT_FUNC initequipment_snapshot_test(){Py_InitModule("equipment_snapshot_test",methods);}

namespace native_battle {
LocalGun::LocalGun()=default;
double Battle::clock()const{return test_time;}
namespace params {bool restoreEquipmentCold(Battle&b,const cs::Value&snapshot,bool present){auto shell=b.engine.shell.get();p::Ref raw(cs::write(snapshot));p::Ref args(PyTuple_Pack(2,raw.get(),present?Py_True:Py_False));const bool accepted=p::flag(p::call(shell,"_restore_local_equipment_snapshot_cold",args.get()).get());b.gun->equipment_revision=int(p::integral(shell,"_equipment_revision"));return accepted;}}
}
namespace stock_native_client_weapon_runtime {
using native_battle::Battle;using Owner=::Owner;using ::ensure;using ::active;using ::equipmentRow;using ::queue;using ::emitSafe;using ::Guard;
}
