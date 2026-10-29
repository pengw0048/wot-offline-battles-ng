#define WOT_CLIENT_FEEDBACK_HOST_BUILD
#include "../bigworld/stock_native_client_drive_feedback.cpp"
namespace stock_native_client_drive {namespace {
PyObject*remote=nullptr;
p::Ref feedbackEntity(ID){return p::retain(remote?remote:Py_None);}
p::Ref feedbackVector(const Point&v){return p::tupleNumbers(v);}
}}
namespace stock_native_client_state {
std::shared_ptr<State>test_state=std::make_shared<State>();
std::shared_ptr<State>fromBattle(PyObject*){return test_state;}
}
namespace {
namespace p=stock_native_bot_python;namespace d=stock_native_client_drive;
std::unique_ptr<d::PlayerFeedback>feedback;
offline_contact::Body body(PyObject*o){offline_contact::Body b;b.x=p::numeric(o,"x");b.y=p::numeric(o,"y");b.z=p::numeric(o,"z");b.yaw=p::numeric(o,"yaw");b.vx=p::numeric(o,"vx");b.vy=p::numeric(o,"vy");b.vz=p::numeric(o,"vz");b.shape=p::numbers<4>(p::value(o,"shape").get());return b;}
PyObject*create(PyObject*,PyObject*args){try{PyObject*b;if(!PyArg_ParseTuple(args,"O",&b))return nullptr;++stock_native_client_state::test_state->epoch;stock_native_client_state::test_state->retired=false;feedback.reset(new d::PlayerFeedback(b));return p::retain(Py_None).release();}catch(const std::exception&e){PyErr_SetString(PyExc_RuntimeError,e.what());return nullptr;}}
PyObject*present(PyObject*,PyObject*args){try{PyObject*l,*r,*a,*b;double now;if(!PyArg_ParseTuple(args,"OOOOd",&l,&r,&a,&b,&now))return nullptr;d::remote=r;d::ContactPeer peer;peer.body=body(b);peer.engine_id=p::integral(r,"id");feedback->present(l,body(a),peer,now);return p::retain(Py_None).release();}catch(const std::exception&e){if(!PyErr_Occurred())PyErr_SetString(PyExc_RuntimeError,e.what());return nullptr;}}
PyObject*retire(PyObject*,PyObject*){++stock_native_client_state::test_state->epoch;return p::retain(Py_None).release();}
PyObject*observed(PyObject*,PyObject*args){PyObject*a,*b;double now;long first,second;if(!PyArg_ParseTuple(args,"llOOd",&first,&second,&a,&b,&now))return nullptr;feedback->observed(first,second,p::numbers<3>(a),p::numbers<3>(b),now);return p::retain(Py_None).release();}
PyObject*close(PyObject*,PyObject*){feedback.reset();return p::retain(Py_None).release();}
PyMethodDef methods[]={{"create",create,METH_VARARGS,""},{"present",present,METH_VARARGS,""},{"retire",retire,METH_NOARGS,""},{"observed",observed,METH_VARARGS,""},{"close",close,METH_NOARGS,""},{nullptr,nullptr,0,nullptr}};
}
PyMODINIT_FUNC initclient_feedback_test(){Py_InitModule("client_feedback_test",methods);}
