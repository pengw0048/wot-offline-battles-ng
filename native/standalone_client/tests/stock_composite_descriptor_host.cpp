#include "stock_native_bot_python.hpp"
#include "stock_native_client_weapon_runtime.hpp"
#include "stock_native_client_weapon_equipment.hpp"
#include "descriptor_hydraulic_pitch.inc"
#include <memory>
namespace p=stock_native_bot_python;
namespace shared=stock_native_client_state;
using namespace stock_native_client_weapon_runtime;
struct Battle {struct Engine {p::Ref constants;explicit Engine(PyObject*c):constants(p::retain(c)){}}engine;explicit Battle(PyObject*c):engine(c){}};
#include "descriptor_field.inc"
#include "descriptor_metadata.inc"
#include "descriptor_equipment.inc"
struct RuntimeOwner {
 std::shared_ptr<shared::State>state=std::make_shared<shared::State>();
 std::shared_ptr<shared::Actor>actor=std::make_shared<shared::Actor>();
 p::Ref descriptor,gun_object;std::unique_ptr<Runtime>runtime;
 std::uint64_t descriptor_revision=0,accepted_revision=0;bool have_equipment=true;
 EquipmentGate equipment_gate;shared::Value equipment_wire;
 RuntimeOwner(PyObject*d,PyObject*g):descriptor(p::vehicleDescriptor(d)),gun_object(p::retain(g)){}
};
PyObject* raw_descriptor=nullptr;
namespace drive {void synchronizeStatus(Battle&,double){}}
namespace stock_native_client_query {PyObject*descriptor(const shared::Actor&){return raw_descriptor;}}
bool active(Battle&,RuntimeOwner&){return true;}
#include "descriptor_replace.inc"
#include "descriptor_refresh.inc"
struct Owner {std::unique_ptr<p::Ref>descriptor;bool pitch_valid=false;HydraulicPitch pitch;};
double number(PyObject*o,const char*k){return p::scalar(field(o,k).get());}
bool truth(PyObject*o,const char*k){return p::flag(field(o,k).get());}
#include "descriptor_pitch.inc"
PyObject* failure(const std::exception&e){if(!PyErr_Occurred())PyErr_SetString(PyExc_RuntimeError,e.what());return nullptr;}
PyObject* sequence(PyObject*,PyObject*args){PyObject*d,*g,*c,*modes;if(!PyArg_ParseTuple(args,"OOOO",&d,&g,&c,&modes))return nullptr;try{
 Battle b(c);RuntimeOwner o(d,g);raw_descriptor=d;
 Gun gun;gun.ammunition={27,9};gun.clip=3;gun.shot=1;gun.pending=0;gun.reload_time=2.7;gun.reload_duration=8.;gun.dispersion=.123;
 o.runtime=std::make_unique<Runtime>(gun,metadata(b,o.descriptor.get(),g));o.runtime->lastTick(12.5);auto*identity=o.runtime.get();Owner pitch;
 auto out=p::list();for(Py_ssize_t i=0;i<p::size(modes);++i){auto mode=p::item(modes,i);p::Ref called(PyObject_CallObject(mode.get(),nullptr));refreshOwner(b,o,13.+i);readPitch(pitch,d);
 if(o.runtime.get()!=identity||&o.runtime->gun()!=&gun||gun.ammunition!=std::vector<int>({27,9})||gun.clip!=3||gun.shot!=1||gun.pending!=0||gun.reload_time!=2.7||gun.reload_duration!=8.||gun.dispersion!=.123||o.runtime->lastTick()!=12.5)throw p::Error("Mode refresh reset the accepted gun frontier");
 auto row=p::dict();p::set(row.get(),"factors",p::tupleNumbers(o.runtime->metadata().descriptor_factors));p::set(row.get(),"pitch_valid",p::boolean(pitch.pitch_valid));p::set(row.get(),"pitch",p::tupleNumbers(std::array<double,3>{pitch.pitch.center_z,pitch.pitch.gun_minimum,pitch.pitch.gun_maximum}));p::append(out.get(),row);
 }return out.release();}catch(const std::exception&e){return failure(e);}}
PyObject* inspect(PyObject*,PyObject*args){PyObject*d,*g,*c;if(!PyArg_ParseTuple(args,"OOO",&d,&g,&c))return nullptr;try{Battle b(c);auto value=metadata(b,d,g);return p::tupleNumbers(value.descriptor_factors).release();}catch(const std::exception&e){return failure(e);}}
PyObject* direct(PyObject*,PyObject*args){PyObject*d;const char*n;if(!PyArg_ParseTuple(args,"Os",&d,&n))return nullptr;try{return field(d,n).release();}catch(const std::exception&e){return failure(e);}}
PyMethodDef methods[]={{"sequence",sequence,METH_VARARGS,""},{"inspect",inspect,METH_VARARGS,""},{"direct",direct,METH_VARARGS,""},{nullptr,nullptr,0,nullptr}};
PyMODINIT_FUNC initcomposite_descriptor_test(){Py_InitModule("composite_descriptor_test",methods);}
