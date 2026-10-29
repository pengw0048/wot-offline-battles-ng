#include "stock_native_client_landing.hpp"
#include "stock_native_client_input.hpp"
#include "stock_native_bot_python.hpp"
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
namespace stock_native_client_landing {
namespace {
namespace p=stock_native_bot_python;
struct Limits {std::int64_t queue=0,contacts=0;double impact=0.;std::string capability;std::set<std::string> names;bool configured=false;};
Limits limits;
void put(PyObject*o,const char*name,const p::Ref&v){if(PyDict_Check(o)){p::set(o,name,v);return;}if(PyObject_SetAttrString(o,name,v.get())<0)throw p::Error("Native landing ledger write failed");}
void erase(PyObject*dict,const char*key){if(PyDict_GetItemString(dict,key)&&PyDict_DelItemString(dict,key)<0)throw p::Error("Native landing ledger erase failed");}
bool same(PyObject*a,PyObject*b){const int value=PyObject_RichCompareBool(a,b,Py_EQ);if(value<0)throw p::Error("Native landing identity is invalid");return value!=0;}
bool numeric(PyObject*v){return !PyBool_Check(v)&&(PyFloat_Check(v)||PyLong_Check(v)
#if PY_MAJOR_VERSION<3
 ||PyInt_Check(v)
#endif
 );}
bool weightNumeric(PyObject*v){return !PyBool_Check(v)&&(PyFloat_Check(v)
#if PY_MAJOR_VERSION<3
 ||PyInt_Check(v)
#else
 ||PyLong_Check(v)
#endif
 );}
// CPython 2.7 round(x,6), for the accepted nonnegative landing-speed range.
// Exact binary-rational arithmetic prevents a rounded product from inventing
// a half tie; unlike the projectile protocol, true ties round away from zero.
double roundSix(double value){
    if(value==0.)return value;int exponent=0;const double fraction=std::frexp(value,&exponent);
    const auto significand=std::uint64_t(std::ldexp(fraction,53));
    const auto lower=(significand&0xffffffffu)*15625u,upper=(significand>>32)*15625u;
    const auto low=lower+(upper<<32),high=(upper>>32)+(low<lower?1u:0u);
    const int shift=47-exponent;std::uint64_t result=0;
    if(shift>0&&shift<64){result=(low>>shift)|(high<<(64-shift));if((low&((std::uint64_t(1)<<shift)-1))>=(std::uint64_t(1)<<(shift-1)))++result;}
    else if(shift==64){result=high;if(low>=(std::uint64_t(1)<<63))++result;}
    else if(shift>64&&shift<128){const auto bits=shift-64;result=high>>bits;if((high&((std::uint64_t(1)<<bits)-1))>=(std::uint64_t(1)<<(bits-1)))++result;}
    else if(shift<=0)return value;
    return double(result)/1.e6;
}
bool falsyPlain(PyObject*v){if(!v||v==Py_None||v==Py_False)return true;if(PyDict_Check(v))return PyDict_Size(v)==0;if(PyList_Check(v))return PyList_GET_SIZE(v)==0;if(PyTuple_Check(v))return PyTuple_GET_SIZE(v)==0;if(numeric(v))return p::scalar(v)==0.;
#if PY_MAJOR_VERSION<3
    if(PyString_Check(v))return PyString_GET_SIZE(v)==0;
#endif
    if(PyUnicode_Check(v))return PyUnicode_GET_SIZE(v)==0;if(PySet_Check(v)||PyFrozenSet_Check(v))return PySet_GET_SIZE(v)==0;return false;}
p::Ref normalize(PyObject*rows){
    if(falsyPlain(rows))return p::list();
    if((!PyList_Check(rows)&&!PyTuple_Check(rows))||p::size(rows)>limits.contacts)throw std::invalid_argument("Invalid native landing module contacts");
    std::map<std::string,double> weights;
    for(Py_ssize_t i=0;i<p::size(rows);++i){auto row=p::item(rows,i);if(!PyDict_Check(row.get())||PyDict_Size(row.get())!=2)throw std::invalid_argument("Invalid native landing contact");auto*name=PyDict_GetItemString(row.get(),"name"),*weight=PyDict_GetItemString(row.get(),"weight");if(!name||!weight||!weightNumeric(weight))throw std::invalid_argument("Invalid native landing contact fields");
#if PY_MAJOR_VERSION<3
        if(!PyString_Check(name)&&!PyUnicode_Check(name))throw std::invalid_argument("Invalid native landing contact name");
#else
        if(!PyUnicode_Check(name))throw std::invalid_argument("Invalid native landing contact name");
#endif
        const auto key=p::string(name);const double amount=p::scalar(weight);if(!limits.names.count(key)||!std::isfinite(amount)||amount<=0.||amount>1.)throw std::invalid_argument("Invalid native landing contact weight");weights[key]=std::max(weights[key],amount);
    }
    auto out=p::list();for(const auto&value:weights){auto row=p::dict();p::set(row.get(),"name",p::text(value.first));p::set(row.get(),"weight",p::number(value.second));p::append(out.get(),row);}return out;
}
bool capable(PyObject*client){auto local=p::value(client,"capabilities"),remote=p::value(client,"server_capabilities"),name=p::text(limits.capability);const int a=PySequence_Contains(local.get(),name.get()),b=PySequence_Contains(remote.get(),name.get());if(a<0||b<0)throw p::Error("Native landing capabilities are unavailable");return a&&b;}
void requireConfigured(){if(!limits.configured)throw p::Error("Native landing protocol is not configured");}
PyObject*fail(const std::exception&e){if(!PyErr_Occurred())PyErr_SetString(PyExc_ValueError,e.what());return nullptr;}
PyObject*pendingPython(PyObject*,PyObject*args,PyObject*kwargs){PyObject*client;int rebind=0;static const char*names[]={"client","rebind",nullptr};if(!PyArg_ParseTupleAndKeywords(args,kwargs,"O|i",const_cast<char**>(names),&client,&rebind))return nullptr;try{return p::boolean(sendPending(client,rebind!=0)).release();}catch(const std::exception&e){return fail(e);}}
PyObject*observationPython(PyObject*,PyObject*args,PyObject*kwargs){PyObject*client,*impact,*contacts=Py_None;static const char*names[]={"client","impact_speed","module_contacts",nullptr};if(!PyArg_ParseTupleAndKeywords(args,kwargs,"OO|O",const_cast<char**>(names),&client,&impact,&contacts))return nullptr;try{if(!numeric(impact))Py_RETURN_FALSE;const auto sequence=sendObservation(client,PyFloat_Check(impact)?PyFloat_AS_DOUBLE(impact):p::scalar(impact),contacts);return sequence?p::integer(sequence).release():p::boolean(false).release();}catch(const std::exception&e){return fail(e);}}
PyObject*queuedPython(PyObject*,PyObject*client){try{return p::boolean(startQueued(client)).release();}catch(const std::exception&e){return fail(e);}}
}
bool sendPending(PyObject*client,bool rebind){
    requireConfigured();auto pending=p::value(client,"_landing_observation_pending");if(!PyDict_Check(pending.get()))return false;
    auto phase=p::value(client,"phase"),round=p::value(client,"round_id"),epoch=p::value(client,"authority_epoch"),input_round=p::value(client,"_input_seq_round");
    if(p::string(phase.get())!="battle"||round.get()==Py_None||epoch.get()==Py_None||!same(input_round.get(),round.get())||p::integral(client,"_input_seq")<=0)return false;
    auto existing=p::value(pending.get(),"wire");auto wire=[&](){if(!rebind&&PyDict_Check(existing.get()))return p::retain(existing.get());auto out=p::dict();p::set(out.get(),"type",p::text("landing_observation"));p::set(out.get(),"round_id",round);p::set(out.get(),"authority_epoch",p::integer(p::wide(epoch.get())));p::set(out.get(),"observation_seq",p::integer(p::integral(pending.get(),"observation_seq")));p::set(out.get(),"input_seq",p::integer(p::integral(pending.get(),"input_seq")));p::set(out.get(),"impact_speed",p::number(p::numeric(pending.get(),"impact_speed")));auto contacts=p::value(pending.get(),"module_contacts");if(!falsyPlain(contacts.get())){p::Ref copy(PySequence_List(contacts.get()));p::set(out.get(),"module_contacts",copy);}p::set(pending.get(),"wire",out);return out;}();
    p::Ref message(PyDict_Copy(wire.get()));const bool sent=stock_native_client_input::enqueue(client,message.get());p::set(pending.get(),"sent",p::boolean(sent));if(sent){erase(pending.get(),"retry_on_input");erase(pending.get(),"refresh_input_on_retry");}return sent;
}
std::int64_t sendObservation(PyObject*client,double impact,PyObject*contacts){
    requireConfigured();if(!std::isfinite(impact)||impact<0.||impact>limits.impact)return 0;
    auto phase=p::value(client,"phase"),player=p::value(client,"player_id");if(p::string(phase.get())!="battle"||!p::truth(client,"ready")||player.get()==Py_None||p::wide(player.get())<=0||!capable(client))return 0;
    auto normalized=[&](){try{return normalize(contacts);}catch(const std::exception&){PyErr_Clear();return p::retain(Py_None);}}();if(normalized.get()==Py_None)return 0;
    auto round=p::value(client,"round_id"),observed_round=p::value(client,"_landing_observation_round");if(!same(round.get(),observed_round.get())){put(client,"_landing_observation_round",round);put(client,"_landing_observation_seq",p::integer(0));put(client,"_landing_observation_pending",p::retain(Py_None));put(client,"_landing_observation_queue",p::list());}
    auto pending=p::value(client,"_landing_observation_pending");
    if(pending.get()!=Py_None){if(!PyDict_Check(pending.get()))throw p::Error("Native landing pending ledger is invalid");if(!p::truth(pending.get(),"reported")){if(!p::truth(pending.get(),"sent")){p::set(pending.get(),"input_seq",p::integer(p::integral(client,"_input_seq")));erase(pending.get(),"wire");if(!sendPending(client,true))return 0;}p::set(pending.get(),"reported",p::boolean(true));return p::integral(pending.get(),"observation_seq");}
        auto queue=p::value(client,"_landing_observation_queue");if(!PyList_Check(queue.get()))throw p::Error("Native landing queue is invalid");if(PyList_GET_SIZE(queue.get())>=limits.queue)return 0;auto queued=p::dict();p::set(queued.get(),"input_seq",p::integer(p::integral(client,"_input_seq")));p::set(queued.get(),"impact_speed",p::number(roundSix(impact)));p::set(queued.get(),"module_contacts",normalized);p::append(queue.get(),queued);
        if(!p::truth(pending.get(),"sent")&&p::truth(pending.get(),"retry_on_input")){if(p::truth(pending.get(),"refresh_input_on_retry")){p::set(pending.get(),"input_seq",p::integer(p::integral(client,"_input_seq")));erase(pending.get(),"wire");}sendPending(client,true);}return p::integral(pending.get(),"observation_seq");
    }
    auto fresh=p::dict();p::set(fresh.get(),"observation_seq",p::integer(p::integral(client,"_landing_observation_seq")+1));p::set(fresh.get(),"input_seq",p::integer(p::integral(client,"_input_seq")));p::set(fresh.get(),"impact_speed",p::number(roundSix(impact)));p::set(fresh.get(),"module_contacts",normalized);p::set(fresh.get(),"sent",p::boolean(false));p::set(fresh.get(),"reported",p::boolean(false));put(client,"_landing_observation_pending",fresh);if(!sendPending(client))return 0;p::set(fresh.get(),"reported",p::boolean(true));return p::integral(fresh.get(),"observation_seq");
}
bool startQueued(PyObject*client){
    requireConfigured();auto queue=p::value(client,"_landing_observation_queue");if(!PyList_Check(queue.get()))throw p::Error("Native landing queue is invalid");if(PyList_GET_SIZE(queue.get())==0)return false;auto queued=p::retain(PyList_GET_ITEM(queue.get(),0));if(PySequence_DelItem(queue.get(),0)<0)throw p::Error("Native landing dequeue failed");auto pending=p::dict();p::set(pending.get(),"observation_seq",p::integer(p::integral(client,"_landing_observation_seq")+1));p::set(pending.get(),"input_seq",p::integer(p::integral(queued.get(),"input_seq")));p::set(pending.get(),"impact_speed",p::number(p::numeric(queued.get(),"impact_speed")));auto contacts=p::value(queued.get(),"module_contacts");p::Ref copy(contacts.get()==Py_None?PyList_New(0):PySequence_List(contacts.get()));p::set(pending.get(),"module_contacts",copy);p::set(pending.get(),"sent",p::boolean(false));p::set(pending.get(),"reported",p::boolean(true));put(client,"_landing_observation_pending",pending);const bool sent=sendPending(client);if(!sent)p::set(pending.get(),"retry_on_input",p::boolean(true));return sent;
}
PyObject*configure(PyObject*,PyObject*module){try{Limits next;next.queue=p::integral(module,"MAX_LANDING_OBSERVATION_QUEUE");next.impact=p::numeric(module,"PLAYER_LANDING_MAX_IMPACT_SPEED");auto capability=p::value(module,"PLAYER_ENVIRONMENT_CAPABILITY");next.capability=p::string(capability.get());p::Ref environment(PyImport_ImportModule("gui.mods.offline_lan_0922.environment_modules")),devices(PyImport_ImportModule("gui.mods.offline_lan_0922.device_damage"));next.contacts=p::integral(environment.get(),"MAX_CONTACTS");auto spec=p::value(devices.get(),"_DEVICE_HP_SPEC"),crew=p::value(devices.get(),"CREW_HEALTH_NAMES");p::Ref device_names(PyDict_Keys(spec.get())),crew_names(PySequence_List(crew.get()));for(auto*names:{device_names.get(),crew_names.get()})for(Py_ssize_t i=0;i<p::size(names);++i){auto name=p::item(names,i);next.names.insert(p::string(name.get()));}if(next.queue<1||next.contacts<1||next.impact<=0.||!std::isfinite(next.impact)||next.capability.empty()||next.names.empty())throw p::Error("Native landing contract is invalid");next.configured=true;limits=std::move(next);Py_RETURN_NONE;}catch(const std::exception&e){return fail(e);}}
PyMethodDef*methods(){static PyMethodDef out[]={{"landing_configure",configure,METH_O,nullptr},{"landing_send_pending",reinterpret_cast<PyCFunction>(reinterpret_cast<void(*)(void)>(pendingPython)),METH_VARARGS|METH_KEYWORDS,nullptr},{"landing_send_observation",reinterpret_cast<PyCFunction>(reinterpret_cast<void(*)(void)>(observationPython)),METH_VARARGS|METH_KEYWORDS,nullptr},{"landing_start_queued",queuedPython,METH_O,nullptr},{nullptr,nullptr,0,nullptr}};return out;}
}
