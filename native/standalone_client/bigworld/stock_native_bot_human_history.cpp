#include "stock_native_bot_human_history.hpp"
#include "stock_native_battle_world.hpp"
#include <Python.h>
#ifdef hypot
#undef hypot
#endif
#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <vector>

namespace stock_native_bot {
namespace {
struct Ref { PyObject* p; explicit Ref(PyObject* v=nullptr):p(v){} ~Ref(){Py_XDECREF(p);} Ref(const Ref&)=delete; Ref& operator=(const Ref&)=delete; PyObject* get()const{return p;} PyObject* release(){auto v=p;p=nullptr;return v;} };
PyObject* field(PyObject* d,const char* k){return d&&PyDict_Check(d)?PyDict_GetItemString(d,k):nullptr;}
PyObject* seq(PyObject* p,Py_ssize_t i){if(PyList_Check(p))return PyList_GET_ITEM(p,i);if(PyTuple_Check(p))return PyTuple_GET_ITEM(p,i);return nullptr;}
Py_ssize_t count(PyObject* p){if(!p)return 0;if(PyList_Check(p))return PyList_GET_SIZE(p);if(PyTuple_Check(p))return PyTuple_GET_SIZE(p);return 0;}
// Only builtin numbers are accepted on the trusted wire: never call __int__.
bool integer(PyObject* p,long long& out){if(!p||PyBool_Check(p))return false;
#if PY_MAJOR_VERSION < 3
 if(PyInt_CheckExact(p)){out=PyInt_AS_LONG(p);return true;}
#endif
 if(PyLong_CheckExact(p)){out=PyLong_AsLongLong(p);if(PyErr_Occurred()){PyErr_Clear();return false;}return true;}
 if(PyFloat_CheckExact(p)){double d=PyFloat_AS_DOUBLE(p);if(std::isfinite(d)&&d>=-9007199254740991.&&d<=9007199254740991.){out=static_cast<long long>(d);return true;}}return false;}
double number(PyObject*p){if(!p)return 0.;
#if PY_MAJOR_VERSION < 3
 if(PyInt_CheckExact(p))return static_cast<double>(PyInt_AS_LONG(p));
#endif
 if(PyLong_CheckExact(p)){double n=PyLong_AsDouble(p);if(PyErr_Occurred()){PyErr_Clear();return 0.;}return n;}
 if(PyFloat_CheckExact(p))return PyFloat_AS_DOUBLE(p);return 0.;}
bool truth(PyObject* p){if(!p||p==Py_None||p==Py_False)return false;if(p==Py_True)return true;
#if PY_MAJOR_VERSION < 3
 if(PyInt_CheckExact(p))return PyInt_AS_LONG(p)!=0;
#endif
 if(PyLong_CheckExact(p)||PyFloat_CheckExact(p))return number(p)!=0.;if(PyString_Check(p))return PyString_GET_SIZE(p)!=0;if(PyList_Check(p)||PyTuple_Check(p))return count(p)!=0;if(PyDict_Check(p))return PyDict_Size(p)!=0;return false;}
PyObject* lookup(PyObject* dict,long long key){if(!dict||!PyDict_Check(dict))return nullptr;Ref k(PyLong_FromLongLong(key));return k.get()?PyDict_GetItem(dict,k.get()):nullptr;}
}
PyObject* prepareHumanInput(PyObject* raw,const native_battle::Battle& battle,PyObject* profiles){
 Ref output(PyList_New(0));if(!output.get())return nullptr;PyObject* states=profiles;
 for(Py_ssize_t i=0;i<count(raw);++i){auto player=seq(raw,i);if(!PyDict_Check(player)||field(player,"participating")!=Py_True||!truth(field(player,"world_pose")))continue;long long id;if(!integer(field(player,"id"),id)||id<=0)continue;Ref copy(PyDict_Copy(player)),decorated(PyList_New(0));if(!copy.get()||!decorated.get())return nullptr;auto contacts=field(player,"ram_contacts");if(!contacts||!PyList_Check(contacts)){auto legacy=field(player,"ram_contact");contacts=nullptr;if(legacy&&PyDict_Check(legacy)){Ref single(PyList_New(1));if(!single.get())return nullptr;Py_INCREF(legacy);PyList_SET_ITEM(single.get(),0,legacy);contacts=single.release();} }else Py_INCREF(contacts);Ref owned(contacts);
 for(Py_ssize_t j=0;j<count(contacts);++j){auto rawreceipt=seq(contacts,j);if(!PyDict_Check(rawreceipt))continue;Ref receipt(PyDict_Copy(rawreceipt));if(!receipt.get())return nullptr;long long bid=0,revision=0,stamp=0;bool valid=integer(field(receipt.get(),"bot_id"),bid)&&integer(field(receipt.get(),"bot_state_revision"),revision)&&integer(field(receipt.get(),"presentation_time_us"),stamp);auto pinned=field(receipt.get(),"ram_bot_state");long long pid=0;Ref botstate;
 if(valid&&pinned&&PyDict_Check(pinned)&&integer(field(pinned,"id"),pid)&&pid==bid){botstate.p=PyDict_New();auto profile=lookup(states,bid);for(const char*k:{"mass","collision_shape","ram_profile","vehicle","team"}){auto v=field(profile,k);if(v)PyDict_SetItemString(botstate.get(),k,v);}if(PyDict_Update(botstate.get(),pinned)<0)return nullptr;if(!field(botstate.get(),"mass")||!field(botstate.get(),"collision_shape")||!field(botstate.get(),"ram_profile")){Py_DECREF(botstate.p);botstate.p=nullptr;}}
 else if(valid){if(const auto body=native_battle::world::ramBotState(battle,bid,revision,stamp))botstate.p=stock_native_client_state::write(*body);}if(botstate.get()&&PyDict_SetItemString(receipt.get(),"_ram_contact_bot_state",botstate.get())<0)return nullptr;if(PyList_Append(decorated.get(),receipt.get())<0)return nullptr;}
 if(PyDict_SetItemString(copy.get(),"ram_contacts",decorated.get())<0||PyList_Append(output.get(),copy.get())<0)return nullptr;}
 return output.release();
}
}
