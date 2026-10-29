#pragma once
#include "stock_native_client_drive_python.hpp"
#include "stock_native_client_drive_runtime.hpp"
#include "stock_native_bot_python.hpp"

namespace stock_native_client_drive {namespace view {
namespace p=stock_native_bot_python;
using Resolve=DriverAccess(*)(PyObject*);
inline Resolve resolve=nullptr;
inline PyObject* dictionary(PyObject*object){auto**slot=_PyObject_GetDictPtr(object);return slot?*slot:nullptr;}
inline PyObject* raw(PyObject*object,const char*name){auto*dict=dictionary(object);return dict?PyDict_GetItemString(dict,name):nullptr;}
enum class Kind {Number,Flag,Position,Lateral};
struct Field {const char*name;double PlayerState::*number;bool PlayerState::*flag;Kind kind;};
inline Field fields[]={
    {"_local_position",nullptr,nullptr,Kind::Position},
    {"_local_yaw",&PlayerState::yaw,nullptr,Kind::Number},
    {"_local_pitch",&PlayerState::pitch,nullptr,Kind::Number},
    {"_local_roll",&PlayerState::roll,nullptr,Kind::Number},
    {"_local_speed",&PlayerState::speed,nullptr,Kind::Number},
    {"_local_turn_speed",&PlayerState::turn_speed,nullptr,Kind::Number},
    {"_local_vertical_speed",&PlayerState::vertical_speed,nullptr,Kind::Number},
    {"_local_push_x",&PlayerState::push_x,nullptr,Kind::Number},
    {"_local_push_z",&PlayerState::push_z,nullptr,Kind::Number},
    {"_local_slide_speed",&PlayerState::slide_speed,nullptr,Kind::Number},
    {"_local_last_pitch",&PlayerState::last_drive_pitch,nullptr,Kind::Number},
    {"_local_drive_turn",&PlayerState::drive_turn,nullptr,Kind::Number},
    {"_local_drive_throttle",&PlayerState::drive_throttle,nullptr,Kind::Number},
    {"_local_direction_command",&PlayerState::direction_command,nullptr,Kind::Number},
    {"_local_suspension_pitch_velocity",&PlayerState::pitch_speed,nullptr,Kind::Number},
    {"_local_suspension_roll_velocity",&PlayerState::roll_speed,nullptr,Kind::Number},
    {"_local_suspension_support_vertical_speed",&PlayerState::support_vertical_speed,nullptr,Kind::Number},
    {"_local_airborne",nullptr,&PlayerState::airborne,Kind::Flag},
    {"_local_fall_armed",nullptr,&PlayerState::grounded_once,Kind::Flag},
    {"_local_left_flying",nullptr,&PlayerState::left_flying,Kind::Flag},
    {"_local_right_flying",nullptr,&PlayerState::right_flying,Kind::Flag},
    {"_local_service_brake",nullptr,&PlayerState::service_brake,Kind::Flag},
    {"_local_air_lateral",nullptr,nullptr,Kind::Lateral},
};
inline PyObject* get(PyObject*object,void*closure){try{
    const auto&field=*static_cast<Field*>(closure);
    auto*worker=raw(object,"_worker_mode"),*replay=raw(object,"_replay_mode");
    // Reads never install a driver: initial admission itself reads these names.
    const bool original=(worker&&p::flag(worker))||(replay&&p::flag(replay));
    auto access=!original&&resolve?resolve(object):DriverAccess{};
    if(access.driver){const auto&s=access.driver->state();switch(field.kind){
        case Kind::Number:return p::number(s.*field.number).release();
        case Kind::Flag:return p::boolean(s.*field.flag).release();
        case Kind::Position:return p::tupleNumbers(s.position).release();
        case Kind::Lateral:return Py_BuildValue("(dd)",s.air_lateral_x,s.air_lateral_z);
    }}
    if(auto*value=raw(object,field.name))return p::retain(value).release();
    PyErr_SetString(PyExc_AttributeError,field.name);return nullptr;
}catch(const std::exception&e){if(!PyErr_Occurred())PyErr_SetString(PyExc_RuntimeError,e.what());return nullptr;}}
inline int set(PyObject*object,PyObject*value,void*closure){
    const auto&field=*static_cast<Field*>(closure);auto**slot=_PyObject_GetDictPtr(object);
    if(!slot){PyErr_SetString(PyExc_AttributeError,field.name);return -1;}
    if(!*slot){*slot=PyDict_New();if(!*slot)return -1;}
    // Cold setup/replay storage is not a second physical state writer.
    if(value)return PyDict_SetItemString(*slot,field.name,value);
    if(PyDict_DelItemString(*slot,field.name)==0)return 0;
    if(PyErr_ExceptionMatches(PyExc_KeyError)){PyErr_Clear();PyErr_SetString(PyExc_AttributeError,field.name);}return -1;
}
inline PyGetSetDef definitions[sizeof(fields)/sizeof(fields[0])]={};
inline bool prepared=false;
inline void install(PyObject*object,Resolve resolver){
    if(!PyType_Check(object))throw p::Error("Native player state views require the BattleRuntime class");
    auto*type=reinterpret_cast<PyTypeObject*>(object);
    if(!prepared){for(std::size_t i=0;i<sizeof(fields)/sizeof(fields[0]);++i)definitions[i]={const_cast<char*>(fields[i].name),get,set,const_cast<char*>("Read the current native player state without a compatibility mirror."),&fields[i]};prepared=true;}
    for(auto&definition:definitions){auto*existing=PyDict_GetItemString(type->tp_dict,definition.name);if(existing&&(Py_TYPE(existing)!=&PyGetSetDescr_Type||reinterpret_cast<PyGetSetDescrObject*>(existing)->d_getset!=&definition))throw p::Error("Native player state view conflicts with an existing class attribute");}
    resolve=resolver;
    for(auto&definition:definitions)if(!PyDict_GetItemString(type->tp_dict,definition.name)){p::Ref descriptor(PyDescr_NewGetSet(type,&definition));if(PyObject_SetAttrString(object,definition.name,descriptor.get())<0)throw p::Error("Native player state view installation failed");}
}
}}
