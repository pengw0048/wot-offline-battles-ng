#include "stock_native_human_ram.hpp"
#include "stock_native_bot_python.hpp"
#include <Python.h>
namespace h=stock_native_human_ram;
namespace p=stock_native_bot_python;
struct Fixture:h::World {
    int mode=0;unsigned queries=0;
    bool ready(h::ID)override{return mode!=1;}
    std::optional<h::Plates> armor(const h::Body&,const h::Body&,const h::XZ&)override{++queries;if(mode==2)return {};if(mode==3)throw std::runtime_error("Fixture local geometry failure");h::Plates result;result.armor={{40.,80.}};if(mode!=4)result.tracks[0]={"leftTrackHealth"};return result;}
};
PyObject* run(PyObject*,PyObject*args){PyObject*battle;int mode;if(!PyArg_ParseTuple(args,"Oi",&battle,&mode))return nullptr;Fixture world;world.mode=mode;std::vector<std::string> errors;auto*rows=h::results(battle,world,&errors);if(!rows)return nullptr;try{auto messages=p::strings(errors);return Py_BuildValue("(NNI)",rows,messages.release(),world.queries);}catch(const std::exception&e){Py_DECREF(rows);if(!PyErr_Occurred())PyErr_SetString(PyExc_ValueError,e.what());return nullptr;}}
struct Services {
    struct Record{bool record_ready=true,available=true;}record;
    struct Armor{std::array<double,2>value;std::array<std::vector<std::string>,2>tracks;};
    const Record*find(offline_simulation::ActorKey){return &record;}
    std::optional<Armor>armor(const h::Body&a,const h::Body&b,const std::array<double,3>&normal){if(a.id!=1000001||b.id!=1000002||normal[0]!=-1.)throw std::runtime_error("Native human identity mapping failed");return Armor{{{40.,80.}},{}};}
};
struct Vehicles {struct Vehicle{bool ready=true;}vehicle;const Vehicle*find(offline_simulation::ActorKey){return &vehicle;}};
PyObject* adapter(PyObject*,PyObject*){try{Services services;Vehicles vehicles;h::NativeWorld<Services,Vehicles>world(services,vehicles);if(!world.ready(1))throw std::runtime_error("Native human world readiness failed");services.record.record_ready=false;if(world.ready(1))throw std::runtime_error("Native human pending readiness failed");h::Body a,b;a.id=1;b.id=2;const auto result=world.armor(a,b,{{-1.,0.}});return Py_BuildValue("(dd)",result->armor[0],result->armor[1]);}catch(const std::exception&e){PyErr_SetString(PyExc_ValueError,e.what());return nullptr;}}
PyMethodDef methods[]={{"run",run,METH_VARARGS,nullptr},{"adapter",adapter,METH_NOARGS,nullptr},{nullptr,nullptr,0,nullptr}};
PyMODINIT_FUNC inithuman_ram_test(){Py_InitModule("human_ram_test",methods);}
