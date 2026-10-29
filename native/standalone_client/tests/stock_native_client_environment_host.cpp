#include "../bigworld/stock_native_client_environment.cpp"
#include "../bigworld/stock_native_bot_python.hpp"
namespace {
namespace env=stock_native_client_environment;namespace p=stock_native_bot_python;
struct Test {env::Spotting core;std::map<std::string,env::Profile> profiles;bool initialized=false;};
Test&test(PyObject*c){auto*t=static_cast<Test*>(PyCapsule_GetPointer(c,"environment.test"));if(!t)throw p::Error("test environment missing");return *t;}
void destroy(PyObject*c){delete static_cast<Test*>(PyCapsule_GetPointer(c,"environment.test"));}
PyObject*fail(const std::exception&e){if(!PyErr_Occurred())PyErr_SetString(PyExc_RuntimeError,e.what());return nullptr;}
PyObject*create(PyObject*,PyObject*){try{auto*t=new Test;env::Profile profile;profile.base_view=400.;profile.binoculars=true;profile.binocular_factor=1.25;profile.binocular_delay=3.;profile.optional_devices={{7,3.},{8,3.}};for(const auto*key:{"player:1","bot:2","bot:3"})t->profiles[key]=profile;return PyCapsule_New(t,"environment.test",destroy);}catch(const std::exception&e){return fail(e);}}
PyObject*radio(PyObject*,PyObject*args){PyObject*c;double now,lease;int ally;if(!PyArg_ParseTuple(args,"Oddi",&c,&now,&lease,&ally))return nullptr;try{std::set<std::string> allies;if(ally)allies.insert("bot:3");test(c).core.radio({{"bot:2",now+lease}},allies);Py_RETURN_NONE;}catch(const std::exception&e){return fail(e);}}
PyObject*frame(PyObject*,PyObject*args){PyObject*c;double now,origin,enemy,ally,speed;int living,spg,hud,alive,known;if(!PyArg_ParseTuple(args,"Odiiidddiid",&c,&now,&living,&spg,&hud,&origin,&enemy,&ally,&alive,&known,&speed))return nullptr;try{
    auto&t=test(c);env::ActorView local;local.key="player:1";local.id=1;local.engine_id=11;local.team=1;local.local=local.ready=true;local.alive=living;local.speed=speed;
    env::ActorView target;target.key="bot:2";target.id=2;target.engine_id=12;target.team=2;target.ready=true;target.alive=alive;target.dead_marker_known=known;target.position={enemy,0.,0.};
    env::ActorView friendly;friendly.key="bot:3";friendly.id=3;friendly.engine_id=13;friendly.team=1;friendly.ready=true;friendly.position={ally,0.,0.};
    if(!t.initialized){t.core.seed(target);t.core.seed(friendly);t.initialized=true;}
    env::Frame input;input.now=now;input.local_key=local.key;input.local_team=1;input.presentation_origin={origin,0.,0.};input.spg_aiming=spg;input.hud_only=hud==1;input.radio_only=hud==2;
    auto probes=p::list();const auto result=t.core.frame(input,{local,target,friendly},t.profiles,[&](const env::ActorView&,const env::ActorView&target,const env::Profile&,const env::Profile&,double ls,double ts,bool){auto row=p::list();p::append(row.get(),p::text(target.key));p::append(row.get(),p::number(ls));p::append(row.get(),p::number(ts));p::append(probes.get(),row);return target.position[0]<400.&&std::fmod(now,3.)<.8;});
    auto output=p::dict(),transitions=p::list(),devices=p::list();for(const auto&edge:result.transitions){auto row=p::list();p::append(row.get(),p::text(edge.key));p::append(row.get(),p::boolean(edge.model));p::append(row.get(),p::boolean(edge.marker));p::append(transitions.get(),row);t.core.acknowledge(edge);}
    for(const auto&device:result.devices){auto row=p::list();p::append(row.get(),p::integer(device.id));p::append(row.get(),p::boolean(device.active));p::append(devices.get(),row);}
    p::set(output.get(),"transitions",transitions);p::set(output.get(),"probes",probes);p::set(output.get(),"vision",result.vision_radius?p::number(*result.vision_radius):p::retain(Py_None));p::set(output.get(),"devices",devices);return output.release();
}catch(const std::exception&e){return fail(e);}}
PyMethodDef methods[]={{"create",create,METH_NOARGS,""},{"radio",radio,METH_VARARGS,""},{"frame",frame,METH_VARARGS,""},{nullptr,nullptr,0,nullptr}};
}
PyMODINIT_FUNC initenvironment_test(){Py_InitModule("environment_test",methods);}
