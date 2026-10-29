#include "stock_native_projectile_python_stub.hpp"
#include "stock_native_projectile_python.hpp"
#include "stock_native_bot_python.hpp"
#include <Python.h>
namespace q=stock_native_projectile;
namespace p=stock_native_bot_python;
struct Scene:stock_native_world::Scene {
    stock_native_world::QueryStatus segment(const stock_native_world::Point&,const stock_native_world::Point&,std::uint16_t,const stock_native_world::Filter&,stock_native_world::Hit&)override{return stock_native_world::QueryStatus::Miss;}
    stock_native_world::QueryStatus material(const stock_native_world::Point&,const stock_native_world::Point&,const stock_native_world::Point&,stock_native_world::MaterialHit&)override{return stock_native_world::QueryStatus::Miss;}
    stock_native_world::QueryStatus water(const stock_native_world::Point&,const stock_native_world::Point&,double&)override{return stock_native_world::QueryStatus::Miss;}
    stock_native_world::Admission refresh(const stock_native_world::Key&,stock_native_world::Registry&)override{return stock_native_world::Admission::Pending;}
    bool unresolved(const stock_native_world::Key&,std::vector<stock_native_world::Box>&)override{return false;}
    bool destroy(const stock_native_world::Destruction&)override{return false;}
    bool treeChunks(const std::vector<stock_native_world::Box>&,std::vector<std::uint32_t>&)override{return false;}
    bool turretSweep(const stock_native_world::TurretSweep&)override{return true;}
};
static stock_native_bot::Runtime bots;
static stock_native_bot::NativeServices services;
static stock_native_world::Registry registry;
static Scene scene;
static stock_native_world::WorldEvents events;
static stock_native_bot_geometry::Vehicles vehicles;
static std::unique_ptr<q::NativeProjectiles> owner;
PyObject* create(PyObject*,PyObject*battle){try{owner.reset();bots={};services={};registry={};stock_native_projectile_damage::LaunchDescriptor shot;shot.speed=100;shot.gravity=10;shot.maximum=500;shot.shot.shell.kind="ARMOR_PIERCING";shot.shot.shell.caliber=100;shot.shot.shell.damage=200;shot.shot.device_damage_mean=80;shot.shot.shell.maximum_distance=500;shot.shot.shell.piercing_near=100;shot.shot.shell.piercing_far=80;shot.shot.explosion_radius=10000./5555.;
    for(const auto id:{4,8}){bots.rows[id]={};bots.rows[id].motion.position={double(id)*30,0,0};bots.store.states[id]={};bots.store.states[id].fire_seq=p::integral(battle,"_fixture_fire_sequence");auto&service=services.rows[{offline_simulation::ActorKind::Bot,id}];service.launch_shots[0]={shot};service.launch_shots[1]={shot};service.projectile_stickers[0][0]={unsigned(id*100),unsigned(id*100+1)};}
    auto&victim=services.rows[{offline_simulation::ActorKind::Human,1}];victim.projectile_stickers[0][0]={900u,901u};victim.projectile[0]=stock_native_projectile_damage::TargetDescriptor{};victim.projectile[1]=*victim.projectile[0];for(auto&v:victim.geometry){auto&m=v.components[1].materials[1];m.armor=10;m.vehicle_damage_factor=1;m.ricochet=false;}
    for(const auto identity:{std::pair<int,int>{4,1},{4,2},{8,1}}){offline_simulation::weapons::LaunchRecord r;r.actor={offline_simulation::ActorKind::Bot,identity.first};r.edge.shot_seq=identity.second;r.edge.group_seq=identity.second;r.edge.count=1;r.has_origin=true;r.launch_time_us=1;r.origin={0,0,0};r.has_shells_before=true;r.shells_before=10;bots.store.launches.push_back(r);}
    owner=std::make_unique<q::NativeProjectiles>(registry,scene,vehicles,events,bots,services);if(!owner->install(battle))return nullptr;Py_RETURN_TRUE;
}catch(const std::exception&e){PyErr_SetString(PyExc_ValueError,e.what());return nullptr;}}
PyObject* advance(PyObject*,PyObject*args){PyObject*battle;double now;int force=0;if(!PyArg_ParseTuple(args,"Od|i",&battle,&now,&force))return nullptr;return owner->advance(battle,now,force!=0);}
PyObject* event(PyObject*,PyObject*args){PyObject*raw;double now;if(!PyArg_ParseTuple(args,"Od",&raw,&now))return nullptr;const auto result=owner->event(raw,now);if(PyErr_Occurred())return nullptr;return PyBool_FromLong(result);}
PyObject* ingress(PyObject*,PyObject*args){PyObject*raw;double now;if(!PyArg_ParseTuple(args,"Od",&raw,&now))return nullptr;const auto result=owner->ingress(raw,now);if(PyErr_Occurred())return nullptr;return PyBool_FromLong(result);}
PyObject* observe(PyObject*,PyObject*args){PyObject*raw;double now;if(!PyArg_ParseTuple(args,"Od",&raw,&now))return nullptr;const auto result=owner->observe(raw,now);if(PyErr_Occurred())return nullptr;return PyBool_FromLong(result);}
PyObject* ack(PyObject*,PyObject*args){unsigned PY_LONG_LONG serial;int sent;if(!PyArg_ParseTuple(args,"Ki",&serial,&sent))return nullptr;return PyBool_FromLong(owner->acknowledge(serial,sent!=0));}
PyObject* launch(PyObject*,PyObject*raw){return owner->launches(raw);}
PyObject* launchResult(PyObject*,PyObject*args){PY_LONG_LONG bot,sequence;int accepted;if(!PyArg_ParseTuple(args,"LLi",&bot,&sequence,&accepted))return nullptr;return PyBool_FromLong(owner->launchResult(bot,sequence,accepted!=0));}
PyObject* acks(PyObject*,PyObject*){auto out=p::list();for(const auto&row:bots.acks)p::append(out.get(),p::integers(std::array<std::int64_t,2>{row.first,row.second}));return out.release();}
PyObject* stale(PyObject*,PyObject*){++registry.epoch;Py_RETURN_NONE;}
PyObject* change(PyObject*,PyObject*){services.rows[{offline_simulation::ActorKind::Bot,4}].launch_shots[0][0].speed=110;services.rows[{offline_simulation::ActorKind::Bot,4}].projectile_stickers[0][0]={700u,701u};Py_RETURN_NONE;}
PyObject* vanish(PyObject*,PyObject*){services.rows.erase({offline_simulation::ActorKind::Bot,4});Py_RETURN_NONE;}
PyObject* clear(PyObject*,PyObject*){owner.reset();Py_RETURN_NONE;}
PyObject* fixtureClock(PyObject*,PyObject*){return PyFloat_FromDouble(1.);}
PyObject* stats(PyObject*,PyObject*){return owner->statistics();}
PyMethodDef methods[]={{"clock",fixtureClock,METH_NOARGS,nullptr},{"create",create,METH_O,nullptr},{"advance",advance,METH_VARARGS,nullptr},{"event",event,METH_VARARGS,nullptr},{"ingress",ingress,METH_VARARGS,nullptr},{"observe",observe,METH_VARARGS,nullptr},{"ack",ack,METH_VARARGS,nullptr},{"launch",launch,METH_O,nullptr},{"launch_result",launchResult,METH_VARARGS,nullptr},{"acks",acks,METH_NOARGS,nullptr},{"stale",stale,METH_NOARGS,nullptr},{"change",change,METH_NOARGS,nullptr},{"stats",stats,METH_NOARGS,nullptr},{"clear",clear,METH_NOARGS,nullptr},{"vanish",vanish,METH_NOARGS,nullptr},{nullptr,nullptr,0,nullptr}};
PyMODINIT_FUNC initprojectile_python_test(){Py_InitModule("projectile_python_test",methods);}
