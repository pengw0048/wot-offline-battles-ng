#include "stock_native_client_drive_install.cpp"
#include "stock_native_battle_test_lifetime.hpp"
namespace {
namespace p=stock_native_bot_python;
namespace d=stock_native_client_drive;
PyObject* hazards(PyObject*,PyObject*args) {
    PyObject* raw;if(!PyArg_ParseTuple(args,"O",&raw))return nullptr;
    try {auto out=p::list();for(unsigned mask:d::readPlayerHazards(raw))p::append(out.get(),p::integer(mask));return out.release();}
    catch(const std::exception&e){if(!PyErr_Occurred())PyErr_SetString(PyExc_RuntimeError,e.what());return nullptr;}
}
PyObject* initial(PyObject*,PyObject*args) {
    PyObject* raw;int admitted;if(!PyArg_ParseTuple(args,"Oi",&raw,&admitted))return nullptr;
    try {
        native_battle::Battle battle;battle.local=std::make_unique<native_battle::LocalVehicle>();auto&v=*battle.local;
        auto matrix=p::value(raw,"_local_matrix");if(matrix.get()!=Py_None)v.matrix=native_battle::borrow(matrix.get());
        v.position=p::numbers<3>(p::value(raw,"_local_position").get());v.yaw=p::numeric(raw,"_local_yaw");v.pitch=p::numeric(raw,"_local_pitch");v.roll=p::numeric(raw,"_local_roll");v.speed=p::numeric(raw,"_local_speed");
        stock_native_client_state::Actor actor;actor.local=true;actor.has_current=admitted!=0;
        // A previously admitted replica sample must never replace the attached local pose.
        actor.current={999.,999.,999.,1.,1.,1.,0.,0.};
        const auto state=d::readPlayerInitialState(battle,actor);
        return Py_BuildValue("(ddddddd)",state.position[0],state.position[1],state.position[2],state.yaw,state.pitch,state.roll,state.speed);
    }catch(const std::exception&e){if(!PyErr_Occurred())PyErr_SetString(PyExc_RuntimeError,e.what());return nullptr;}
}
PyMethodDef methods[]={{"hazards",hazards,METH_VARARGS,""},{"initial",initial,METH_VARARGS,""},{nullptr,nullptr,0,nullptr}};
}
PyMODINIT_FUNC initclient_drive_install_test(){Py_InitModule("client_drive_install_test",methods);}
