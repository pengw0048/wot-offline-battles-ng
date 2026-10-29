#include "stock_native_client_survival_python.cpp"
#include <map>
namespace {
namespace p=stock_native_bot_python;namespace d=stock_native_client_drive;namespace s=stock_native_client_state;
std::map<PyObject*,std::shared_ptr<d::PlayerDriver>> drivers;
double serverTime=100.;
PyObject* failure(const std::exception&e){if(!PyErr_Occurred())PyErr_SetString(PyExc_RuntimeError,e.what());return nullptr;}
PyObject*timeValue(PyObject*,PyObject*){return PyFloat_FromDouble(serverTime);}
PyObject*install(PyObject*,PyObject*args){PyObject*b;if(!PyArg_ParseTuple(args,"O",&b))return nullptr;try{
    auto state=std::make_shared<s::State>();state->local_player_id=3;auto actor=std::make_shared<s::Actor>();actor->local=true;actor->id=3;actor->engine_id=10;actor->key="player:3";actor->state=s::read(p::value(b,"authoritative").get());state->actors[actor->key]=actor;
    p::Ref capsule(s::capsule(state));stock_native_client_survival::put(b,"_native_client_state",capsule);
    d::PlayerDescriptor descriptor;descriptor.motion.drive.mass=25000.;descriptor.motion.half_length=3.;descriptor.motion.half_width=1.5;descriptor.motion.drive.t[11]=descriptor.motion.drive.t[14]=descriptor.motion.drive.t[18]=descriptor.motion.drive.t[24]=1.;for(auto&v:descriptor.motion.drive.terrain)v=1.;descriptor.network_seconds=.1;
    drivers[b]=std::make_shared<d::PlayerDriver>(descriptor,d::PlayerState{});Py_RETURN_NONE;
}catch(const std::exception&e){return failure(e);}}
PyObject*pose(PyObject*,PyObject*args){PyObject*b,*data;if(!PyArg_ParseTuple(args,"OO",&b,&data))return nullptr;try{auto&s=drivers.at(b)->state();s.position=p::numbers<3>(p::value(data,"position").get());s.yaw=p::numeric(data,"yaw");s.pitch=p::numeric(data,"pitch");s.roll=p::numeric(data,"roll");auto up=p::value(data,"up");s.surface_up_cosine=up.get()==Py_None?std::nullopt:std::optional<double>(p::scalar(up.get()));Py_RETURN_NONE;}catch(const std::exception&e){return failure(e);}}
PyObject*levels(PyObject*,PyObject*args){PyObject*b;if(!PyArg_ParseTuple(args,"O",&b))return nullptr;try{auto v=stock_native_client_survival::levels(b);return Py_BuildValue("(ii)",v[0],v[1]);}catch(const std::exception&e){return failure(e);}}
PyObject*close(PyObject*,PyObject*args){PyObject*b;if(!PyArg_ParseTuple(args,"O",&b))return nullptr;try{stock_native_client_survival::retire(b);s::fromBattle(b)->retired=true;drivers.erase(b);Py_RETURN_NONE;}catch(const std::exception&e){return failure(e);}}
PyObject*reset(PyObject*,PyObject*){drivers.clear();Py_RETURN_NONE;}
PyMethodDef methods[]={{"install",install,METH_VARARGS,""},{"pose",pose,METH_VARARGS,""},{"time",timeValue,METH_VARARGS,""},{"levels",levels,METH_VARARGS,""},{"drowning",stock_native_client_survival::drowningPython,METH_VARARGS,""},{"overturn",stock_native_client_survival::overturnPython,METH_VARARGS,""},{"close",close,METH_VARARGS,""},{"reset",reset,METH_VARARGS,""},{nullptr,nullptr,0,nullptr}};
}
namespace stock_native_client_survival {namespace {
water::Pose nativeBody(PyObject*b,const drive::PlayerState&s){auto value=field(b,"body");if(value.get()==Py_None)return {s.position,s.yaw,s.pitch,s.roll};return {xyz(field(value.get(),"position").get()),number(value.get(),"yaw"),number(value.get(),"pitch"),number(value.get(),"roll")};}
double nativeDepth(PyObject*b,const water::Point&p){put(b,"sample",stock_native_bot_python::tupleNumbers(p));return number(b,"water",-1.);}
}}
namespace stock_native_bot {
PyObject*readNativeEntity(PyObject*b,std::int64_t){return p::value(b,"entity").release();}
PyObject*readNativeEntityField(PyObject*,PyObject*e,const char*n){return p::value(e,n).release();}
}
namespace stock_native_client_drive {DriverAccess driverAccess(PyObject*b){auto owner=drivers.at(b);return {owner,owner.get(),nullptr,nullptr};}}
extern "C" void initclient_survival_runtime_test(){Py_InitModule("client_survival_runtime_test",methods);}
