#define WOT_CLIENT_TARGETING_HOST_BUILD
#define WOT_CLIENT_REPLICA_HOST_BUILD
#include "stock_native_client_state.cpp"
#include "stock_native_client_targeting.cpp"
#include "stock_native_client_targeting_python.cpp"
namespace stock_native_client_targeting {namespace {
double game_time=0.;
bool enginePresent(ID,PyObject*e){return truth(e,"owned",true);}
CursorRay sampleMouse(PyObject*b){auto raw=field(b,"_test_ray");auto a=p::item(raw.get(),0),d=p::item(raw.get(),1);auto direction=p::numbers<3>(d.get());float x=float(direction[0]),y=float(direction[1]),z=float(direction[2]);const float length=std::sqrt(float(float(x*x+y*y)+z*z));if(std::abs(length)>=1e-8f){const float inverse=1.f/length;x*=inverse;y*=inverse;z*=inverse;}if(float(float(x*x+y*y)+z*z)<=0.f)throw p::Error("#1513 mouse targeting ray is empty");return {p::numbers<3>(a.get()),{x,y,z}};}
}}
namespace stock_native_client_weapon_runtime {int livingSkillCount(PyObject*b,const std::string&){return int(stock_native_bot_python::integral(b,"_test_skill"));}}
namespace stock_native_client_query {
PyObject*registeredVehicle(PyObject*c,std::int64_t id){auto rows=stock_native_bot_python::value(c,"vehicles");auto key=stock_native_bot_python::integer(id);return PyDict_GetItem(rows.get(),key.get());}
OutlinePick pickOutline(PyObject*c,const stock_native_client_state::Point&,const stock_native_client_state::Point&,double maximum,double fov,std::optional<std::int64_t>,std::int64_t,double server,double epsilon){namespace p=stock_native_bot_python;auto raw=p::value(c,"pick");OutlinePick out;out.available=p::truth(raw.get(),"available",true);out.held_seen=p::truth(raw.get(),"held_seen");out.held_reason=p::string(p::value(raw.get(),"held_reason").get());auto chosen=p::value(raw.get(),"chosen"),depth=p::value(raw.get(),"depth");if(chosen.get()!=Py_None)out.chosen=p::wide(chosen.get());if(depth.get()!=Py_None)out.depth=p::scalar(depth.get());if(maximum!=710.||fov!=40.||epsilon!=.001||server<-1.)throw p::Error("Native cursor producer contract changed");return out;}
}
namespace {
namespace t=stock_native_client_targeting;namespace p=stock_native_bot_python;namespace cs=stock_native_client_state;
PyObject*timeValue(PyObject*,PyObject*){return PyFloat_FromDouble(t::game_time);}
PyObject*setTime(PyObject*,PyObject*args){if(!PyArg_ParseTuple(args,"d",&t::game_time))return nullptr;Py_RETURN_NONE;}
PyObject*install(PyObject*,PyObject*args){try{PyObject*b,*rows;if(!PyArg_ParseTuple(args,"OO",&b,&rows))return nullptr;auto existing=p::value(b,"_native_client_state");std::shared_ptr<cs::State>state;if(existing.get()==Py_None){state=std::make_shared<cs::State>();state->local_player_id=1;p::Ref capsule(cs::capsule(state));t::put(b,"_native_client_state",capsule);}else state=cs::fromBattle(b);std::set<std::string>seen;for(Py_ssize_t i=0;i<p::size(rows);++i){auto row=p::item(rows,i);const auto key=p::string(p::value(row.get(),"key").get());seen.insert(key);auto&actor=state->actors[key];if(!actor)actor=std::make_shared<cs::Actor>();actor->key=key;actor->engine_id=p::integral(row.get(),"engine_id");actor->local=p::truth(row.get(),"local");actor->ready=p::truth(row.get(),"ready",true);actor->has_current=true;actor->visible=p::truth(row.get(),"spot_visible",true);actor->tombstone=p::truth(row.get(),"tombstone");actor->alive=p::truth(row.get(),"alive",true);actor->descriptor_revision=p::integral(row.get(),"descriptor_revision");actor->state=cs::read(row.get());}for(auto i=state->actors.begin();i!=state->actors.end();)if(!seen.count(i->first))i=state->actors.erase(i);else ++i;Py_RETURN_NONE;}catch(const std::exception&e){return t::failure(e);}}
PyObject*status(PyObject*,PyObject*b){try{auto token=t::field(b,"_native_client_targeting");auto owner=t::lookup(token.get());if(!owner)Py_RETURN_NONE;return Py_BuildValue("(LdO)",static_cast<PY_LONG_LONG>(owner->expert.target()),owner->expert.due(),owner->outlined?p::integer(*owner->outlined).get():Py_None);}catch(const std::exception&e){return t::failure(e);}}
PyObject*lock(PyObject*,PyObject*args){int a,b,c,d,e,f,g;if(!PyArg_ParseTuple(args,"iiiiiii",&a,&b,&c,&d,&e,&f,&g))return nullptr;return p::boolean(t::lockHolds({a!=0,b!=0,c!=0,d!=0,e!=0,f!=0,g!=0})).release();}
PyObject*epoch(PyObject*,PyObject*b){++cs::fromBattle(b)->epoch;Py_RETURN_NONE;}
PyMethodDef methods[]={
{"time",timeValue,METH_NOARGS,""},{"set_time",setTime,METH_VARARGS,""},{"install",install,METH_VARARGS,""},{"status",status,METH_O,""},{"lock",lock,METH_VARARGS,""},{"epoch",epoch,METH_O,""},
{"prepare",t::prepare,METH_VARARGS,""},{"tick_expert",t::tickExpert,METH_VARARGS,""},{"monitor_expert",t::monitorExpert,METH_VARARGS,""},{"disable_expert",t::disableExpert,METH_VARARGS,""},{"outline",t::outline,METH_VARARGS,""},{"clear",t::clearOutline,METH_VARARGS,""},{"disable_outline",t::disableOutline,METH_VARARGS,""},{"validate_lock",t::validateLock,METH_VARARGS,""},{"retire",t::retire,METH_VARARGS,""},{nullptr,nullptr,0,nullptr}};
}
PyMODINIT_FUNC initclient_targeting_test(){Py_InitModule("client_targeting_test",methods);}
