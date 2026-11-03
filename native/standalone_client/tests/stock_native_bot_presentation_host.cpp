#define WOT_NATIVE_PRESENTATION_HOST_BUILD
#include "stock_native_bot_presentation_impl.cpp"
namespace stock_native_bot {namespace {
PyObject* presentationVector(const Point&v){return p::tupleNumbers(Point{{float(v[0]),float(v[1]),float(v[2])}}).release();}
Point presentationXYZ(PyObject*value){return p::numbers<3>(value);}
}}
static PyObject* verify(PyObject*,PyObject*){try {
    native_battle::Battle battle;stock_native_bot::Runtime runtime;stock_native_bot::NativeServices services;std::vector<std::string> errors;
    auto run=[&](double now){return stock_native_bot::presentNativeBotPoses(battle,runtime,now,services,errors);};
    const auto check=[](bool ok){if(!ok)throw std::runtime_error("Native presentation lifecycle mismatch");};
    check(run(1.)==1&&battle.poses==1&&battle.aims==1&&errors.empty());
    run(1.1);check(battle.poses==1&&battle.aims==1);
    auto&actor=runtime.values.at(1);actor.motion.speed=1.;++runtime.time.sample_us;run(1.2);check(battle.poses==2);
    actor.motion.position[0]=3.;actor.motion.yaw=3.1;actor.weapon.aim_yaw=-3.1;run(1.3);check(battle.relax&&std::abs(*battle.relax-.405)<1.e-8);
    actor.motion.yaw=-3.1;run(1.4);check(std::abs(battle.bots->yaw_rates.at(1)-(2.*3.14159265358979323846-6.2)/.1)<1.e-8);
    services.entity.geometry[0].static_turret_yaw=.25;services.entity.geometry[0].static_gun_pitch=-.1;
    actor.weapon.aim_yaw=.5;run(1.5);check(battle.aim==actor.motion.yaw+.25&&battle.pitch==-.1);
    battle.actor.ready=false;run(1.6);check(battle.bots->presented.empty());battle.actor.ready=true;++battle.generation;run(1.7);check(battle.bots->yaw_rates.at(1)==0.);
    battle.fail_pose=true;actor.motion.position[0]=7.;actor.weapon.aim_yaw=.6;const int poses=battle.poses,aims=battle.aims;
    run(1.8);check(!errors.empty()&&battle.poses==poses&&battle.aims==aims+1&&!battle.bots->presented.at(1).pose);
    errors.clear();battle.fail_pose=false;run(1.9);check(errors.empty()&&battle.poses==poses+1);
    Py_RETURN_TRUE;
}catch(const std::exception&e){PyErr_SetString(PyExc_AssertionError,e.what());return nullptr;}}
static PyMethodDef methods[]={{"verify",verify,METH_NOARGS,nullptr},{nullptr,nullptr,0,nullptr}};
extern "C" void initpresentation_test(){Py_InitModule("presentation_test",methods);}
