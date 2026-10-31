#include "stock_native_human_ram.hpp"
#include "stock_native_bot_python.hpp"
#include "stock_native_bot_resource_lifecycle.hpp"
#include "stock_native_battle_bots.hpp"
#include <cmath>
#include <set>
#include <algorithm>

namespace stock_native_human_ram {
namespace {
namespace p=stock_native_bot_python;
void fields(PyObject*object,std::initializer_list<const char*>names){if(!PyDict_Check(object)||PyDict_Size(object)!=Py_ssize_t(names.size()))throw p::Error("Worker human ram probe fields are invalid");for(const auto*name:names)if(!PyDict_GetItemString(object,name))throw p::Error("Worker human ram probe field is missing");}
bool text(PyObject*object){
#if PY_MAJOR_VERSION>=3
    return PyUnicode_Check(object);
#else
    return PyString_Check(object)||PyUnicode_Check(object);
#endif
}
double number(PyObject*object){if(!object||object==Py_None)throw p::Error("Worker human ram probe number is missing");if(text(object)){
#if PY_MAJOR_VERSION>=3
    p::Ref parsed(PyFloat_FromString(object));
#else
    p::Ref parsed(PyFloat_FromString(object,nullptr));
#endif
    const auto result=PyFloat_AS_DOUBLE(parsed.get());if(!std::isfinite(result))throw p::Error("Worker human ram probe number is nonfinite");return result;}
    if(!PyFloat_Check(object)&&!PyLong_Check(object)&&!PyBool_Check(object)
#if PY_MAJOR_VERSION<3
        &&!PyInt_Check(object)
#endif
    )throw p::Error("Worker human ram probe number is not plain data");
    const auto result=PyFloat_AsDouble(object);if(PyErr_Occurred()||!std::isfinite(result))throw p::Error("Worker human ram probe number is invalid");return result;
}
ID exactID(PyObject*object){if(!object||object==Py_None||PyBool_Check(object))throw p::Error("Worker human ram probe integer is invalid");ID result;
    if(text(object)){p::Ref parsed(PyNumber_Long(object));result=p::wide(parsed.get());if(number(object)!=double(result))throw p::Error("Worker human ram probe integer is inexact");}
    else{const auto value=number(object);if(value!=std::floor(value)||value<1.||value>2147483647.)throw p::Error("Worker human ram probe integer is outside range");result=ID(value);}
    if(result<1||result>2147483647)throw p::Error("Worker human ram probe integer is outside range");return result;
}
double scalar(PyObject*object,const char*name){return number(p::field(object,name));}
struct InputBody {Body body;std::string vehicle;};
InputBody body(PyObject*raw){fields(raw,{"id","vehicle","x","y","z","yaw","pitch","roll","shape"});InputBody out;out.body.id=exactID(p::field(raw,"id"));auto*vehicle=p::field(raw,"vehicle"),*shape=p::field(raw,"shape");if(!text(vehicle)||PyObject_Length(vehicle)<1||PyObject_Length(vehicle)>80||p::size(shape)!=4)throw p::Error("Worker human ram probe body is invalid");out.vehicle=p::string(vehicle);auto&b=out.body;b.x=scalar(raw,"x");b.y=scalar(raw,"y");b.z=scalar(raw,"z");b.yaw=scalar(raw,"yaw");b.pitch=scalar(raw,"pitch");b.roll=scalar(raw,"roll");for(unsigned i=0;i<4;++i){auto part=p::item(shape,i);b.shape[i]=number(part.get());}if(std::abs(b.x)>5000.||std::abs(b.y)>5000.||std::abs(b.z)>5000.||b.shape[0]<.5||b.shape[0]>20.||b.shape[1]<.75||b.shape[1]>30.||b.shape[2]<-20.||b.shape[2]>=b.shape[3]||b.shape[3]>30.)throw p::Error("Worker human ram probe body is outside range");b.flags=offline_contact::Alive|offline_contact::Player|offline_contact::HasY;return out;}
XZ normal(PyObject*raw){if(p::size(raw)<2)throw p::Error("Worker human ram normal width is invalid");auto a=p::item(raw,0),b=p::item(raw,1);const auto x=number(a.get()),z=number(b.get()),length=std::sqrt(x*x+z*z);if(!std::isfinite(length)||length<=.000001)throw p::Error("Worker human ram normal is invalid");return {{x/length,z/length}};}
bool ready(native_battle::Battle&battle,const InputBody&input){auto*record=battle.vehicle("player:"+std::to_string(input.body.id));if(!record||!record->ready||record->tombstone)return false;if(record->kind!="player"||record->id!=input.body.id)throw p::Error("Worker human ram probe identity is invalid");const auto&vehicle=record->state.get("vehicle");const auto state_vehicle=vehicle.kind==native_battle::Value::Kind::Text?vehicle.text:vehicle.kind==native_battle::Value::Kind::Null?std::string{}:throw p::Error("Worker human ram probe vehicle is invalid");if(state_vehicle!=input.vehicle)throw p::Error("Worker human ram probe vehicle is invalid");auto entity=battle.entity(record->engine_id);if(entity.get()==Py_None)return false;auto started=p::Ref(stock_native_bot::readNativeEntityField(nullptr,entity.get(),"isStarted")),descriptor=p::Ref(stock_native_bot::readNativeEntityField(nullptr,entity.get(),"typeDescriptor"));return p::flag(started.get())&&descriptor.get()!=Py_None;}
bool geometric(const Body&a,const Body&b){const auto interval=[](const Body&v){const auto center=v.y+(v.shape[2]+v.shape[3])*.5,extent=(v.shape[3]-v.shape[2])*.5;return std::array<double,2>{{center-extent,center+extent}};};const auto av=interval(a),bv=interval(b);if(std::min(av[1],bv[1])-std::max(av[0],bv[0])<=.02)return false;const double low=std::max(a.y+a.shape[2],b.y+b.shape[2]),high=std::min(a.y+a.shape[3],b.y+b.shape[3]);if(high<=low)return false;return !stock_native_bot_armor::overlap_polygon(a,b).empty();}
}
PyObject* results(native_battle::Battle&battle,World&world,std::vector<std::string>*errors){try{auto out=p::list();if(!battle.worker||!battle.bots||!battle.bots->last_snapshot)return out.release();auto requests=p::value(battle.bots->last_snapshot.get(),"human_ram_probes");if(requests.get()==Py_None)return out.release();if((!PyList_Check(requests.get())&&!PyTuple_Check(requests.get()))||p::size(requests.get())>64)throw p::Error("Worker human ram probe batch is invalid");std::set<ID> seen;
    for(Py_ssize_t i=0;i<p::size(requests.get());++i)try{auto raw=p::item(requests.get(),i);fields(raw.get(),{"seq","contact_normal","first","second"});const auto sequence=exactID(p::field(raw.get(),"seq"));if(!seen.insert(sequence).second)throw p::Error("Worker human ram probe sequence is repeated");const auto n=normal(p::field(raw.get(),"contact_normal"));const auto first=body(p::field(raw.get(),"first")),second=body(p::field(raw.get(),"second"));if(first.body.id>=second.body.id)throw p::Error("Worker human ram probe order is invalid");if(n[0]*(first.body.x-second.body.x)+n[1]*(first.body.z-second.body.z)<=1.e-6)throw p::Error("Worker human ram probe orientation is invalid");if(!ready(battle,first)||!ready(battle,second)||!world.ready(first.body.id)||!world.ready(second.body.id))continue;if(!geometric(first.body,second.body))throw p::Error("Worker human ram probe geometry is invalid");const auto armor=world.armor(first.body,second.body,n);auto result=p::dict();p::set(result.get(),"seq",p::integer(sequence));p::set(result.get(),"first_id",p::integer(first.body.id));p::set(result.get(),"second_id",p::integer(second.body.id));p::set(result.get(),"available",p::boolean(bool(armor)));if(armor){for(auto value:armor->armor)if(!std::isfinite(value)||value<=0.)throw p::Error("Worker human ram native armor is invalid");p::set(result.get(),"armor_first",p::number(armor->armor[0]));p::set(result.get(),"armor_second",p::number(armor->armor[1]));if(!armor->tracks[0].empty()||!armor->tracks[1].empty()){p::set(result.get(),"tracks_first",p::strings(armor->tracks[0]));p::set(result.get(),"tracks_second",p::strings(armor->tracks[1]));}}p::append(out.get(),result);
    }catch(const std::exception&e){if(errors)errors->push_back(e.what());PyErr_Clear();}
    return out.release();}catch(const std::exception&e){if(errors){errors->push_back(e.what());PyErr_Clear();return PyList_New(0);}if(!PyErr_Occurred())PyErr_SetString(PyExc_ValueError,e.what());return nullptr;}}
}
