#include "stock_native_client_drive_view.hpp"
namespace drive=stock_native_client_drive;
namespace view=drive::view;
namespace p=stock_native_bot_python;
namespace stock_native_client_drive {
PlayerDriver::PlayerDriver(PlayerDescriptor descriptor,PlayerState state):descriptor_(std::move(descriptor)),state_(std::move(state)){}
}
struct Owner {drive::PlayerDriver driver{{},{}};bool retired=false;};
void dispose(PyObject*capsule){delete static_cast<std::shared_ptr<Owner>*>(PyCapsule_GetPointer(capsule,"test.player"));}
drive::DriverAccess resolvePlayer(PyObject*battle){auto*token=view::raw(battle,"_native_client_drive_owner");if(!token||token==Py_None)return {};auto owner=*static_cast<std::shared_ptr<Owner>*>(PyCapsule_GetPointer(token,"test.player"));return owner->retired?drive::DriverAccess{}:drive::DriverAccess{owner,&owner->driver,nullptr,nullptr};}
PyObject*install(PyObject*,PyObject*type){try{view::install(type,resolvePlayer);Py_RETURN_NONE;}catch(const std::exception&e){PyErr_SetString(PyExc_RuntimeError,e.what());return nullptr;}}
PyObject*bind(PyObject*,PyObject*battle){auto*owner=new std::shared_ptr<Owner>(std::make_shared<Owner>());p::Ref capsule(PyCapsule_New(owner,"test.player",dispose));if(PyObject_SetAttrString(battle,"_native_client_drive_owner",capsule.get())<0)return nullptr;Py_RETURN_NONE;}
PyObject*move(PyObject*,PyObject*args){PyObject*battle;double t;if(!PyArg_ParseTuple(args,"Od",&battle,&t))return nullptr;auto access=resolvePlayer(battle);auto&s=access.driver->state();s.position={t,2.*t,-t};s.yaw=t*.01;s.pitch=t*.02;s.roll=t*.03;s.speed=t*.04;s.turn_speed=t*.05;s.vertical_speed=t*.06;s.airborne=int(t)%2;s.push_x=t*.07;s.push_z=t*.08;s.slide_speed=t*.09;s.air_lateral_x=t*.10;s.air_lateral_z=t*.11;Py_RETURN_NONE;}
PyObject*retire(PyObject*,PyObject*battle){auto access=resolvePlayer(battle);if(access.driver)std::static_pointer_cast<Owner>(access.lifetime)->retired=true;Py_RETURN_NONE;}
namespace stock_native_client_drive {
DriverAccess existingPlayer(PyObject*battle){return ::resolvePlayer(battle);}
@LOCAL_DEATH@
}
PyMethodDef methods[]={{"install",install,METH_O,nullptr},{"bind",bind,METH_O,nullptr},{"move",move,METH_VARARGS,nullptr},{"retire",retire,METH_O,nullptr},{"local_death",drive::localDeath,METH_VARARGS,nullptr},{nullptr,nullptr,0,nullptr}};
PyMODINIT_FUNC initclient_drive_view_test(){Py_InitModule("client_drive_view_test",methods);}
