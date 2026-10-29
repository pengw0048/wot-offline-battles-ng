#if !defined(WOT_RESOURCE_LIFECYCLE_HOST_BUILD)
#include "pch.hpp"
#include "stock_bsp_collision_model.hpp"
#include "connection_control.hpp"
#include "entity.hpp"
#include "py_entity.hpp"
#else
#include <Python.h>
#endif
#include "stock_native_bot_resource_lifecycle.hpp"
#include <stdexcept>
#include <limits>
#include <cstring>

namespace stock_native_bot {
namespace {
struct Ref {
    PyObject* p;
    explicit Ref(PyObject* value):p(value){if(!p)throw std::runtime_error("Native resource lifecycle read failed");}
    ~Ref(){Py_XDECREF(p);} Ref(const Ref&)=delete;Ref& operator=(const Ref&)=delete;
    PyObject* release(){auto result=p;p=nullptr;return result;}
};
PyObject* value(PyObject* object,const char* name){
    if(!object||object==Py_None){Py_INCREF(Py_None);return Py_None;}
    if(PyDict_Check(object)){auto result=PyDict_GetItemString(object,name);if(!result)result=Py_None;Py_INCREF(result);return result;}
    auto result=PyObject_GetAttrString(object,name);
    if(!result&&PyErr_ExceptionMatches(PyExc_AttributeError)){PyErr_Clear();Py_INCREF(Py_None);return Py_None;}
    return result;
}
PyObject* nativeField(PyObject* object,const char* name){
    if(!object||object==Py_None||PyDict_Check(object))return value(object,name);
    Ref key(PyString_FromString(name));auto* result=PyObject_GenericGetAttr(object,key.p);
    if(!result&&PyErr_ExceptionMatches(PyExc_AttributeError)){PyErr_Clear();Py_INCREF(Py_None);return Py_None;}
    return result;
}
const void* token(PyObject* object){return !object||object==Py_None?nullptr:object;}
bool plainFlag(PyObject* object){
    if(!object||object==Py_None||object==Py_False)return false;if(object==Py_True)return true;
#if PY_MAJOR_VERSION<3
    if(PyInt_CheckExact(object))return PyInt_AS_LONG(object)!=0;
#endif
    if(PyLong_CheckExact(object)){const int result=PyObject_IsTrue(object);if(result<0)throw std::runtime_error("Native resource flag invalid");return result!=0;}
    throw std::runtime_error("Native resource flag is not plain data");
}
void modelTokens(PyObject* object,ResourceStamp& stamp){
    stamp.owners.push_back(token(object));
#if defined(WOT_RESOURCE_LIFECYCLE_HOST_BUILD)
    stamp.owners.push_back(token(object));stamp.owners.push_back(token(object));
#else
    stamp.owners.push_back(token(object)?BW::stockBspCollisionModelModel(object):nullptr);
    stamp.owners.push_back(token(object)?BW::stockBspCollisionModelTree(object):nullptr);
#endif
}
}
PyObject* readNativeEntity(PyObject* battle,std::int64_t engine_id){try{
    Ref factory(value(battle,"_remote_factory"));Ref vehicles(value(factory.p,"_vehicles"));
    if(PyDict_Check(vehicles.p)){Ref key(PyLong_FromLongLong(engine_id));auto entity=PyDict_GetItem(vehicles.p,key.p);
        if(entity&&entity!=Py_None){Py_INCREF(entity);return entity;}}
#if !defined(WOT_RESOURCE_LIFECYCLE_HOST_BUILD)
    auto* control=BW::ConnectionControl::pInstance();
    auto* entity=control&&engine_id>=std::numeric_limits<BW::EntityID>::min()&&
        engine_id<=std::numeric_limits<BW::EntityID>::max()?
        control->findEntity(static_cast<BW::EntityID>(engine_id)):nullptr;
    if(entity){auto* python=entity->pPyEntity().newRef();if(python)return python;}
#endif
    Py_INCREF(Py_None);return Py_None;
}catch(const std::exception& error){if(!PyErr_Occurred())PyErr_SetString(PyExc_RuntimeError,error.what());return nullptr;}}
PyObject* readNativeEntityField(PyObject* battle,PyObject* entity,const char* name){try{
    if(!std::strcmp(name,"isTurretDetached")){
        Ref hp(readNativeEntityField(battle,entity,"health")),confirmed(nativeField(entity,"_Vehicle__turretDetachmentConfirmed"));
        long health=0;if(hp.p!=Py_None){health=PyLong_AsLong(hp.p);if(PyErr_Occurred())throw std::runtime_error("Native vehicle health is not an integer");}
        // constants.SPECIAL_VEHICLE_HEALTH.IS_TURRET_DETACHED in #1513.
        return PyBool_FromLong(health<0&&(health|-13L)==-13L&&plainFlag(confirmed.p));
    }
    if(battle&&entity&&entity!=Py_None&&(!std::strcmp(name,"health")||!std::strcmp(name,"isCrewActive")||
        !std::strcmp(name,"position")||!std::strcmp(name,"yaw")||!std::strcmp(name,"matrix")||!std::strcmp(name,"speedInfo"))){
        Ref runtime(value(battle,"_runtime")),compatibility(value(runtime.p,"compatibility")),overlays(value(compatibility.p,"_vehicle_property_overlays"));
        if(PyDict_Check(overlays.p)){Ref key(PyLong_FromVoidPtr(entity));auto* overlay=PyDict_GetItem(overlays.p,key.p);
            if(overlay&&PyDict_Check(overlay)){auto* result=PyDict_GetItemString(overlay,name);if(result){Py_INCREF(result);return result;}}}
    }
    return nativeField(entity,name);
}catch(const std::exception& error){if(!PyErr_Occurred())PyErr_SetString(PyExc_RuntimeError,error.what());return nullptr;}}
bool readResourceStamp(PyObject* entity,const std::vector<PyObject*>& descriptors,ResourceStamp& output,PyObject*battle){try{
    ResourceStamp result;result.owners.push_back(token(entity));
    Ref started(readNativeEntityField(battle,entity,"isStarted")),detached(readNativeEntityField(battle,entity,"isTurretDetached"));
    result.flags.push_back(plainFlag(started.p));result.flags.push_back(plainFlag(detached.p));
    Ref appearance(readNativeEntityField(battle,entity,"appearance")),filter(readNativeEntityField(battle,entity,"filter"));
    result.owners.push_back(token(appearance.p));result.owners.push_back(token(filter.p));
    Ref critical(readNativeEntityField(battle,entity,"_offline_critical_descriptor"));
    result.owners.push_back(token(critical.p));
    for(auto* descriptor:descriptors){result.owners.push_back(token(descriptor));
        for(const char* name:{"chassis","hull","turret","gun"}){
            Ref part(value(descriptor,name)),tester(value(part.p,"hitTester")),model(value(tester.p,"_ModelHitTester__bspModel"));
            modelTokens(model.p,result);
        }
    }
    output=std::move(result);return true;
}catch(const std::exception& error){if(!PyErr_Occurred())PyErr_SetString(PyExc_RuntimeError,error.what());return false;}}
}
