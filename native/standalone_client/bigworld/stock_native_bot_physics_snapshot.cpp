#include "stock_native_bot_physics_snapshot.hpp"
#include <Python.h>
#include <cmath>
#include <stdexcept>

namespace stock_native_bot_physics {
namespace {
struct Ref {
    PyObject *p;
    explicit Ref(PyObject*value):p(value){if(!p)throw std::runtime_error("Native physics installation read failed");}
    ~Ref(){Py_DECREF(p);}Ref(const Ref&)=delete;Ref&operator=(const Ref&)=delete;
};
PyObject * value(PyObject* object,const char* key) {
    if(!object)throw std::invalid_argument("Native physics object is unavailable");
    PyObject*p=nullptr;
    if(PyDict_Check(object)){p=PyDict_GetItemString(object,key);if(!p)p=Py_None;Py_INCREF(p);}
    else {p=PyObject_GetAttrString(object,key);if(!p&&PyErr_ExceptionMatches(PyExc_AttributeError)){PyErr_Clear();p=Py_None;Py_INCREF(p);}}
    return p;
}
double number(PyObject*p) {
    if(!p||p==Py_None||PyBool_Check(p))throw std::invalid_argument("Native physics number is unavailable");
    const double out=PyFloat_AsDouble(p);if(PyErr_Occurred()||!std::isfinite(out))throw std::invalid_argument("Native physics number is nonfinite");return out;
}
double scalar(PyObject*object,const char* key,double fallback) {Ref p(value(object,key));return p.p==Py_None?fallback:number(p.p);}
std::int64_t integer(PyObject*p) {
    const double d=number(p);if(d<0.||d>2147483647.||d!=std::floor(d))throw std::invalid_argument("Native physics identity is invalid");return std::int64_t(d);
}
std::int64_t identity(PyObject*object,const char* key) {Ref p(value(object,key));const auto out=integer(p.p);if(out<1)throw std::invalid_argument("Native physics actor identity is invalid");return out;}
Py_ssize_t size(PyObject*p) {if(!PyTuple_Check(p)&&!PyList_Check(p))throw std::invalid_argument("Native physics sequence is invalid");return PySequence_Size(p);}
double at(PyObject*p,unsigned i){Ref v(PySequence_GetItem(p,i));return number(v.p);}
Point point(PyObject*p) {if(size(p)!=3)throw std::invalid_argument("Native physics vector width is invalid");return {{at(p,0),at(p,1),at(p,2)}};}
bool truth(PyObject*p) {const int out=PyObject_IsTrue(p);if(out<0)throw std::invalid_argument("Native physics boolean is invalid");return out!=0;}
std::string text(PyObject*p) {
#if PY_MAJOR_VERSION >= 3
    const char*s=PyUnicode_AsUTF8(p);if(!s)throw std::invalid_argument("Native physics text is invalid");return s;
#else
    if(PyString_Check(p))return std::string(PyString_AS_STRING(p),PyString_GET_SIZE(p));
    Ref s(PyUnicode_AsUTF8String(p));return std::string(PyString_AS_STRING(s.p),PyString_GET_SIZE(s.p));
#endif
}
void bounds(const stock_native_bot_turret::Component& c) {
    for(unsigned i=0;i<3;++i)if(!std::isfinite(c.bounds.minimum[i])||!std::isfinite(c.bounds.maximum[i])||!std::isfinite(c.offset[i])||c.bounds.minimum[i]>=c.bounds.maximum[i])throw std::invalid_argument("Native physics component bounds are invalid");
}
stock_native_bot_turret::Owner owner(const std::string& kind,std::int64_t id) {
    if(kind!="bot"&&kind!="player")throw std::invalid_argument("Detached turret owner kind is invalid");return {kind=="player",id};
}
void pivot(PyObject*chassis,PyObject*physics,stock_native_bot_turret::MovingDescriptor& result) {
    try {
        Ref around(value(chassis,"rotationIsAroundCenter")),gauge(value(physics,"trackCenterOffset"));
        if(!PyBool_Check(around.p)||gauge.p==Py_None)return;
        const double amount=std::abs(number(gauge.p));if(amount<=0.||amount>100.)return;
        result.pivot_available=true;result.rotation_around_center=truth(around.p);result.track_center=amount;
    }catch(const std::exception&){PyErr_Clear();}
}
}
bool readPhysicsSnapshot(PyObject*battle,PyObject*raw,PyObject*descriptor,
    stock_native_bot_turret::MovingDescriptor geometry,bool detailed,PhysicsSnapshot&output) {
    try {
        PhysicsSnapshot result;bounds(geometry.chassis);bounds(geometry.hull);
        result.geometry.turret=std::move(geometry);result.geometry.detailed_suspension=detailed;result.config.detailed_suspension=detailed;
        Ref aiming(value(descriptor,"isPitchHullAimingAvailable"));result.geometry.pitch_hull_aiming=aiming.p!=Py_None&&truth(aiming.p);
        // Missing/partial pivot evidence intentionally leaves no proved arc,
        // exactly track_pivot_descriptor_params' None result.
        Ref chassis(value(descriptor,"chassis")),physics(value(descriptor,"physics"));
        if(chassis.p!=Py_None&&physics.p!=Py_None)pivot(chassis.p,physics.p,result.geometry.turret);
        PyObject * mounted=nullptr;
        Ref bots(value(battle,"_bots"));
        if(bots.p!=Py_None) {
            Ref pairs(value(bots.p,"_descriptor_pairs")),id(value(raw,"id"));
            if(pairs.p!=Py_None) {
                if(!PyDict_Check(pairs.p))throw std::invalid_argument("Native physics descriptor pairs are invalid");
                auto pair=PyDict_GetItem(pairs.p,id.p);
                if(pair&&pair!=Py_None) {
                    if(size(pair)!=2)throw std::invalid_argument("Native physics descriptor pair width is invalid");
                    Ref siege(PySequence_GetItem(pair,1));if(siege.p!=Py_None)mounted=PySequence_GetItem(pair,0);
                }
            }
        }
        if(mounted==Py_None){Py_DECREF(mounted);mounted=nullptr;}
        if(!mounted)mounted=value(descriptor,"defaultVehicleDescr");Ref travel(mounted);
        if(travel.p!=Py_None) {
            Ref p(value(travel.p,"physics")),limits(value(p.p,"speedLimits"));
            if(size(limits.p)!=2)throw std::invalid_argument("Native physics travel limits are invalid");
            const double forward=at(limits.p,0),reverse=at(limits.p,1);
            if(forward<0.||reverse<0.)throw std::invalid_argument("Native physics travel limits are negative");
            result.config.travel_forward=forward;result.config.travel_reverse=reverse;
        }
        Ref marker(value(raw,"pose_sample"));if(marker.p!=Py_None)result.memory.pose_sample=point(marker.p);
        result.memory.last_world_impact_time=scalar(raw,"_last_world_impact_time",-1.e30);
        const double attempts=scalar(raw,"_slope_pose_attempt",0.);
        if(attempts<0.||attempts>9007199254740991.||std::floor(attempts)!=attempts)throw std::invalid_argument("Native slope attempt counter is invalid");
        result.memory.slope_attempt=std::uint64_t(attempts);
        result.config.turret_yaw=scalar(raw,"turret_yaw",0.);output=std::move(result);return true;
    }catch(const std::exception&error){if(!PyErr_Occurred())PyErr_SetString(PyExc_ValueError,error.what());return false;}
}
bool readDetachedTurretRow(PyObject*row,stock_native_bot_turret::Record&output) {
    try {
        stock_native_bot_turret::Record result;Ref kind(value(row,"actor_kind"));result.owner=owner(text(kind.p),identity(row,"actor_id"));
        Ref created(value(row,"created_time_ms"));result.created_time_ms=double(integer(created.p));
        Ref seq(value(row,"motion_seq"));result.motion_sequence=seq.p==Py_None?0:integer(seq.p);
        if(seq.p!=Py_None&&result.motion_sequence<1)throw std::invalid_argument("Detached turret motion sequence is invalid");
        Ref attitude(value(row,"attitude")),spin(value(row,"spin")),flight(value(row,"flight"));result.attitude=point(attitude.p);result.spin=point(spin.p);
        Ref rest(value(flight.p,"rest")),duration(value(flight.p,"duration")),landed(value(flight.p,"landed")),body(value(flight.p,"body")),rest_angle(value(flight.p,"rest_attitude"));
        result.rest=point(rest.p);result.duration=number(duration.p);if(result.duration<0.)throw std::invalid_argument("Detached turret duration is negative");
        if(!PyBool_Check(landed.p))throw std::invalid_argument("Detached turret landing bit is invalid");result.landed=truth(landed.p);
        // Presence, including a None-valued malformed input, is significant
        // to the accepted schema. It is never inferred from a truth value.
        result.has_body=PyDict_Check(flight.p)&&PyDict_GetItemString(flight.p,"body");
        if(result.has_body&&!PyDict_Check(body.p))throw std::invalid_argument("Detached turret body is invalid");
        if(rest_angle.p!=Py_None)result.rest_attitude=point(rest_angle.p);
        Ref support(value(row,"support_key"));if(seq.p!=Py_None&&support.p!=Py_None) {
            const auto key=text(support.p);const auto split=key.find(':');if(split==std::string::npos||key.find(':',split+1)!=std::string::npos)throw std::invalid_argument("Detached turret support identity is invalid");
            const auto suffix=key.substr(split+1);std::size_t used=0;const auto id=std::stoll(suffix,&used);
            if(used!=suffix.size()||id<1||id>2147483647)throw std::invalid_argument("Detached turret support actor is invalid");result.support=owner(key.substr(0,split),id);
        }
        output=std::move(result);return true;
    }catch(const std::exception&error){if(!PyErr_Occurred())PyErr_SetString(PyExc_ValueError,error.what());return false;}
}
}
