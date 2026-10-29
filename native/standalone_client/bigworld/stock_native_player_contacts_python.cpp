#include "stock_native_player_contacts_python.hpp"
#include "stock_native_bot_python.hpp"
#include <algorithm>
#include <limits>

namespace stock_native_player_contacts {
namespace {
namespace p=stock_native_bot_python;
bool sequence(PyObject* value){return value&&(PyList_CheckExact(value)||PyTuple_CheckExact(value));}
Py_ssize_t size(PyObject* value){if(!sequence(value))throw p::Error("Native player contact sequence invalid");return PyList_CheckExact(value)?PyList_GET_SIZE(value):PyTuple_GET_SIZE(value);}
PyObject* item(PyObject* value,Py_ssize_t i){return PyList_CheckExact(value)?PyList_GET_ITEM(value,i):PyTuple_GET_ITEM(value,i);}
bool number(PyObject* value){
    if(!value||PyBool_Check(value))return false;
#if PY_MAJOR_VERSION<3
    return PyInt_CheckExact(value)||PyLong_CheckExact(value)||PyFloat_CheckExact(value);
#else
    return PyLong_CheckExact(value)||PyFloat_CheckExact(value);
#endif
}
double scalar(PyObject* value){if(!number(value))throw p::Error("Native player contact number must be plain data");return p::scalar(value);}
ID integer(PyObject* value){if(!number(value))throw p::Error("Native player contact integer must be plain data");return p::wide(value);}
double numeric(PyObject* value,const char* key,double fallback=0.,bool required=false){auto raw=p::field(value,key);if(!raw){if(required)throw p::Error("Native player contact field missing");return fallback;}return scalar(raw);}
std::uint32_t nativeID(PyObject* value){const auto id=integer(value);if(id<0||std::uint64_t(id)>std::numeric_limits<std::uint32_t>::max())throw p::Error("Native destructible identity out of range");return std::uint32_t(id);}
Token token(PyObject* value){const auto count=size(value);if(count<1||count>64)throw p::Error("Native player contact token count invalid");Token out;
    for(Py_ssize_t i=0;i<count;++i){auto row=item(value,i);if(size(row)!=3)throw p::Error("Native player contact token width invalid");
        const auto chunk=nativeID(item(row,0)),index=nativeID(item(row,1));auto material=item(row,2);
        const auto kind=material==Py_None?-1:integer(material);out.emplace(chunk,index,kind);}
    return out;
}
bool criticalValid(PyObject* raw){try{
    auto critical=p::field(raw,"critical");if(!critical||critical==Py_None)return true;
    if(!PyDict_CheckExact(critical))return false;
    auto devices=p::field(critical,"devices");if(!devices||devices==Py_None)return true;
    for(Py_ssize_t i=0;i<size(devices);++i){auto row=item(devices,i);if(!PyDict_CheckExact(row))return false;scalar(p::field(row,"hp"));}
    return true;
}catch(const std::exception&){PyErr_Clear();return false;}}
Descriptor descriptor(const stock_native_bot::HumanDescriptor& human,PyObject* raw){Descriptor out;
    out.body_minimum=human.body_minimum;out.body_maximum=human.body_maximum;out.mass=human.contact_mass;
    out.drive_forward=human.contact_drive_forward;out.drive_reverse=human.contact_drive_reverse;out.rotation_speed=human.contact_rotation_speed;
    out.pivot_available=human.pivot_available;out.rotation_around_center=human.rotation_around_center;out.track_center=human.track_center;
    // The fixed source has no traverse crew branch or module stat entry;
    // its factor is one after validating the critical device payload.
    out.traverse_valid=criticalValid(raw);return out;
}
}
bool readPlayerContacts(PyObject* players,const std::map<ID,stock_native_bot::HumanDescriptor>& descriptors,
    std::vector<Request>& output,std::uint64_t& rejected){
    try{
        std::vector<Request> result;if(!players||players==Py_None){output.clear();return true;}
        for(Py_ssize_t i=0;i<size(players);++i){auto player=item(players,i);if(!PyDict_CheckExact(player))continue;
            auto participating=p::field(player,"participating"),pose=p::field(player,"world_pose");
            if(participating!=Py_True||!pose||(pose!=Py_True&&!number(pose))||!p::flag(pose))continue;
            auto contacts=p::field(player,"destructible_contacts");if(!contacts||!PyList_CheckExact(contacts))continue;
            for(Py_ssize_t j=0;j<std::min<Py_ssize_t>(16,PyList_GET_SIZE(contacts));++j){auto raw=PyList_GET_ITEM(contacts,j);if(!PyDict_CheckExact(raw))continue;
                try{Request request;auto&r=request.receipt;r.player=integer(p::field(player,"id"));if(r.player<=0)continue;r.sequence=integer(p::field(raw,"seq"));r.token=token(p::field(raw,"token"));
                    const char* starts[]={"x","y","z"},*ends[]={"end_x","end_y","end_z"};
                    for(unsigned axis=0;axis<3;++axis){r.start[axis]=numeric(raw,starts[axis],0.,true);r.end[axis]=numeric(raw,ends[axis],0.,true);}
                    r.yaw=numeric(raw,"yaw",0.,true);r.end_yaw=numeric(raw,"end_yaw",0.,true);r.speed=numeric(raw,"speed",0.,true);r.dt=numeric(raw,"dt",0.,true);
                    r.pitch=numeric(raw,"pitch");r.roll=numeric(raw,"roll");const auto frozen=descriptors.find(r.player);
                    if(frozen!=descriptors.end())request.descriptor=descriptor(frozen->second,player);
                    result.push_back(std::move(request));
                }catch(const std::exception&){PyErr_Clear();++rejected;}
            }
        }
        output=std::move(result);return true;
    }catch(const std::exception& e){if(!PyErr_Occurred())PyErr_SetString(PyExc_ValueError,e.what());return false;}
}
PyObject* packPlayerVerdicts(const std::vector<Verdict>& values){try{
    auto result=p::list();for(const auto& value:values){auto row=p::dict(),keys=p::list();
        p::set(row.get(),"serial",p::integer(value.serial));p::set(row.get(),"player_id",p::integer(value.player));p::set(row.get(),"seq",p::integer(value.sequence));p::set(row.get(),"accepted",p::boolean(value.accepted));
        for(const auto& identity:value.token){auto key=p::list();p::append(key.get(),p::integer(std::get<0>(identity)));p::append(key.get(),p::integer(std::get<1>(identity)));
            p::append(key.get(),std::get<2>(identity)==-1?p::retain(Py_None):p::integer(std::get<2>(identity)));p::append(keys.get(),key);}
        p::set(row.get(),"token",keys);p::append(result.get(),row);
    }return result.release();
}catch(const std::exception& e){if(!PyErr_Occurred())PyErr_SetString(PyExc_ValueError,e.what());return nullptr;}}
}
