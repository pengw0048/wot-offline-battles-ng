#include "stock_native_client_weapon_equipment.hpp"
#include "stock_native_client_weapon_runtime.hpp"
#include "stock_native_bot_python.hpp"
namespace p=stock_native_bot_python;namespace w=stock_native_client_weapon_runtime;namespace cs=stock_native_client_state;namespace shared=cs;
using w::EquipmentFrame;using w::equipmentFrame;using w::Edges;using w::Equipment;
PyObject*equipmentSnapshot(PyObject*,PyObject*);
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
bool active(const Owner&o){return !p::truth(o.battle,"_test_retired");}
p::Ref field(PyObject*o,const char*n){return p::value(o,n);}
void replace(p::Ref&v,p::Ref replacement){v.~Ref();new(&v)p::Ref(std::move(replacement));}
void attribute(PyObject*o,const char*n,const p::Ref&v){if(PyObject_SetAttrString(o,n,v.get())<0)throw p::Error("Test slot assignment failed");}
std::vector<Equipment>equipmentObjects(PyObject*states){std::vector<Equipment>out;for(Py_ssize_t i=0;i<p::size(states);++i){auto raw=p::item(states,i),contract=field(raw.get(),"contract");Equipment e;e.compact=p::integral(contract.get(),"compactDescr");e.uses_left=int(p::integral(raw.get(),"uses_left"));e.active=p::truth(raw.get(),"active");e.ready_at=p::numeric(raw.get(),"ready_at");out.push_back(e);}return out;}
void queue(Owner&o,const Edges&,double now){o.publication_time=now;}
void emitSafe(Owner&o){auto now=p::number(o.publication_time);p::Ref args(PyTuple_Pack(1,now.get()));p::call(o.battle,"_present_equipments",args.get());}
struct Guard {Owner&o;explicit Guard(Owner&v):o(v){o.advancing=true;}~Guard(){o.advancing=false;}};
PyObject*fail(const std::exception&e){if(!PyErr_Occurred())PyErr_SetString(PyExc_RuntimeError,e.what());return nullptr;}
PyObject*ingress(PyObject*,PyObject*args){PyObject*battle,*states;if(!PyArg_ParseTuple(args,"OO",&battle,&states))return nullptr;try{auto o=ensure(battle);o->runtime->equipment(equipmentObjects(states));o->have_equipment=true;Py_RETURN_NONE;}catch(const std::exception&e){return fail(e);}}
void destroy(PyObject*c){delete static_cast<w::EquipmentGate*>(PyCapsule_GetPointer(c,"equipment.gate.test"));}
PyObject*create(PyObject*,PyObject*){return PyCapsule_New(new w::EquipmentGate(),"equipment.gate.test",destroy);}
PyObject*step(PyObject*,PyObject*args){PyObject*c,*rows,*result,*states;long long revision;double now;int commit=0;if(!PyArg_ParseTuple(args,"OOLOOd|i",&c,&rows,&revision,&result,&states,&now,&commit))return nullptr;try{
    auto*gate=static_cast<w::EquipmentGate*>(PyCapsule_GetPointer(c,"equipment.gate.test"));if(!gate)throw p::Error("Test equipment gate is unavailable");const auto frame=w::equipmentFrame(cs::read(rows),now);auto accepted=cs::read(result);
    if(commit){gate->commit(frame,revision,std::move(accepted));Py_RETURN_NONE;}
    const bool hot=gate->matches(frame,revision,accepted)&&states!=Py_None&&p::size(states)==Py_ssize_t(frame.clocks.size());
    if(hot)for(std::size_t i=0;i<frame.clocks.size();++i){auto state=p::item(states,i);const auto&clock=frame.clocks[i];const auto set=[&](const char*name,const p::Ref&value){if(PyObject_SetAttrString(state.get(),name,value.get())<0)throw p::Error("Test equipment slot assignment failed");};set("ready_at",p::number(clock.ready_at));set("_auto_pending_since",clock.automatic?p::number(*clock.automatic):p::retain(Py_None));set("_ai_pending_since",clock.ai?p::number(*clock.ai):p::retain(Py_None));}
    return p::boolean(hot).release();
}catch(const std::exception&e){if(!PyErr_Occurred())PyErr_SetString(PyExc_RuntimeError,e.what());return nullptr;}}
PyMethodDef methods[]={{"time",time,METH_NOARGS,""},{"set_time",setTime,METH_O,""},{"equipment_snapshot",equipmentSnapshot,METH_VARARGS,""},{"equipment_ingress",ingress,METH_VARARGS,""},{"create",create,METH_NOARGS,""},{"step",step,METH_VARARGS,""},{nullptr,nullptr,0,nullptr}};
}
PyMODINIT_FUNC initequipment_snapshot_test(){Py_InitModule("equipment_snapshot_test",methods);}
