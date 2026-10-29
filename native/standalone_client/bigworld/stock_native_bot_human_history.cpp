#include "stock_native_bot_human_history.hpp"
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
void setnum(PyObject*d,const char*k,double value){Ref v(PyFloat_FromDouble(value));if(v.get())PyDict_SetItemString(d,k,v.get());}
PyObject* motion(PyObject* left,PyObject* right,long long stamp){std::map<long long,PyObject*> rows;for(auto list:{left,right})for(Py_ssize_t i=0;i<count(list);++i){auto r=seq(list,i);long long id=0,s=0,t=0;if(count(r)!=21||!integer(seq(r,0),id)||!integer(seq(r,1),s)||!integer(seq(r,2),t)||t>stamp)continue;auto old=rows.find(id);long long prior=0;if(old==rows.end()||(integer(seq(old->second,1),prior)&&s>prior))rows[id]=r;}
 Ref result(PyList_New(0));if(!result.get())return nullptr;for(auto& e:rows){Ref row(PySequence_List(e.second));if(!row.get()||PyList_Append(result.get(),row.get())<0)return nullptr;}return result.release();}
struct Sample{long long time,revision;PyObject* state;};
PyObject* historical(PyObject* battle,long long id,long long revision,long long stamp){
 Ref idx(PyObject_GetAttrString(battle,"_ram_bot_history_index")),history(PyObject_GetAttrString(battle,"_ram_bot_history"));if(!idx.get()||!history.get()){PyErr_Clear();return nullptr;}
 auto timeline=lookup(idx.get(),id);std::vector<Sample> samples;for(Py_ssize_t i=0;i<count(timeline);++i){auto entry=seq(timeline,i);long long t=0,r=0;if(count(entry)!=2||!integer(seq(entry,0),t)||!integer(seq(entry,1),r))return nullptr;auto state=lookup(lookup(history.get(),r),id);samples.push_back({t,r,state});}
 const auto key=std::make_pair(stamp,revision);auto less=[](const Sample&a,const std::pair<long long,long long>&b){return std::make_pair(a.time,a.revision)<b;};auto lower=std::lower_bound(samples.begin(),samples.end(),key,less);auto upper=std::upper_bound(samples.begin(),samples.end(),key,[](const auto& a,const Sample& b){return a<std::make_pair(b.time,b.revision);});if(lower==samples.end()||upper==samples.begin())return nullptr;auto li=static_cast<size_t>((upper-samples.begin())-1),ri=static_cast<size_t>(lower-samples.begin());auto l=samples[li],r=samples[ri];if(l.time>stamp||l.revision>revision||r.time<stamp||r.revision<revision||!l.state||!r.state||!PyDict_Check(l.state)||!PyDict_Check(r.state))return nullptr;
 Ref result(PyDict_Copy(l.state));if(!result.get())return nullptr;double span=double(r.time-l.time);if(span>0.){double p=std::max(0.,std::min(1.,double(stamp-l.time)/span));for(const char*k:{"x","y","z","pitch","roll","aim_yaw","gun_pitch","yaw"}){auto a=field(l.state,k),b=field(r.state,k);if(!a||!b)continue;double av=number(a),delta=number(b)-av;if(k==std::string("yaw")){const double pi=3.14159265358979323846;delta=std::fmod(delta+pi,2*pi);if(delta<0)delta+=2*pi;delta-=pi;}setnum(result.get(),k,av+delta*p);}if(p>=1.){auto alive=field(r.state,"alive");PyDict_SetItemString(result.get(),"alive",truth(alive?alive:Py_True)?Py_True:Py_False);}}
 auto before=l,after=r;if(span==0.&&samples.size()>=2){size_t bi=li?li-1:li,ai=li?li:li+1;before=samples[bi];after=samples[ai];span=double(after.time-before.time);}for(const char*k:{"x","y","z"}){double v=span>0.&&before.state&&after.state?(number(field(after.state,k))-number(field(before.state,k)))*1000000./span:0.;std::string name=std::string("ram_v")+k;setnum(result.get(),name.c_str(),v);}Ref m(motion(l.time==r.time?nullptr:field(l.state,"ram_motion"),field(r.state,"ram_motion"),stamp));if(!m.get()||PyDict_SetItemString(result.get(),"ram_motion",m.get())<0)return nullptr;return result.release();}
}
PyObject* prepareHumanInput(PyObject* raw,PyObject* battle,PyObject* bot_runtime){
 Ref output(PyList_New(0)),states(PyObject_GetAttrString(bot_runtime,"states"));if(!output.get())return nullptr;if(!states.get())PyErr_Clear();
 for(Py_ssize_t i=0;i<count(raw);++i){auto player=seq(raw,i);if(!PyDict_Check(player)||field(player,"participating")!=Py_True||!truth(field(player,"world_pose")))continue;long long id;if(!integer(field(player,"id"),id)||id<=0)continue;Ref copy(PyDict_Copy(player)),decorated(PyList_New(0));if(!copy.get()||!decorated.get())return nullptr;auto contacts=field(player,"ram_contacts");if(!contacts||!PyList_Check(contacts)){auto legacy=field(player,"ram_contact");contacts=nullptr;if(legacy&&PyDict_Check(legacy)){Ref single(PyList_New(1));if(!single.get())return nullptr;Py_INCREF(legacy);PyList_SET_ITEM(single.get(),0,legacy);contacts=single.release();} }else Py_INCREF(contacts);Ref owned(contacts);
 for(Py_ssize_t j=0;j<count(contacts);++j){auto rawreceipt=seq(contacts,j);if(!PyDict_Check(rawreceipt))continue;Ref receipt(PyDict_Copy(rawreceipt));if(!receipt.get())return nullptr;long long bid=0,revision=0,stamp=0;bool valid=integer(field(receipt.get(),"bot_id"),bid)&&integer(field(receipt.get(),"bot_state_revision"),revision)&&integer(field(receipt.get(),"presentation_time_us"),stamp);auto pinned=field(receipt.get(),"ram_bot_state");long long pid=0;Ref botstate;
 if(valid&&pinned&&PyDict_Check(pinned)&&integer(field(pinned,"id"),pid)&&pid==bid){botstate.p=PyDict_New();auto profile=lookup(states.get(),bid);for(const char*k:{"mass","collision_shape","ram_profile","vehicle","team"}){auto v=field(profile,k);if(v)PyDict_SetItemString(botstate.get(),k,v);}if(PyDict_Update(botstate.get(),pinned)<0)return nullptr;if(!field(botstate.get(),"mass")||!field(botstate.get(),"collision_shape")||!field(botstate.get(),"ram_profile")){Py_DECREF(botstate.p);botstate.p=nullptr;}}
 else if(valid)botstate.p=historical(battle,bid,revision,stamp);if(botstate.get()&&PyDict_SetItemString(receipt.get(),"_ram_contact_bot_state",botstate.get())<0)return nullptr;if(PyList_Append(decorated.get(),receipt.get())<0)return nullptr;}
 if(PyDict_SetItemString(copy.get(),"ram_contacts",decorated.get())<0||PyList_Append(output.get(),copy.get())<0)return nullptr;}
 return output.release();
}
}
