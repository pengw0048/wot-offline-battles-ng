#define WOT_CLIENT_REPLICA_HOST_BUILD
#include "stock_native_client_state.cpp"
#include "stock_native_client_drive_status.cpp"
#include "stock_native_bot_combat.cpp"
#include <Python.h>
namespace {
namespace d=stock_native_client_drive;namespace cs=stock_native_client_state;namespace p=stock_native_bot_python;
struct Owner {std::shared_ptr<cs::Actor> actor=std::make_shared<cs::Actor>();std::shared_ptr<d::Status> status;};
const char*tag="drive.status.fixture";
void destroy(PyObject*c){delete static_cast<Owner*>(PyCapsule_GetPointer(c,tag));}
PyObject*create(PyObject*,PyObject*args){try{PyObject*raw;if(!PyArg_ParseTuple(args,"O",&raw))return nullptr;auto v=cs::read(raw);d::StatusDescriptor descriptor;
for(const auto&row:v.get("maxima").object)descriptor.maxima[row.first]=row.second.numeric();for(const auto&row:v.get("crew_roles").object)for(const auto&role:row.second.array)descriptor.crew_roles[row.first].insert(role.text);
auto*owner=new Owner;owner->status=d::installStatus(owner->actor,std::move(descriptor));return PyCapsule_New(owner,tag,destroy);}catch(const std::exception&e){PyErr_SetString(PyExc_RuntimeError,e.what());return nullptr;}}
PyObject*update(PyObject*,PyObject*args){try{PyObject*c,*raw,*clock;unsigned long revision;if(!PyArg_ParseTuple(args,"OOkO",&c,&raw,&revision,&clock))return nullptr;auto*owner=static_cast<Owner*>(PyCapsule_GetPointer(c,tag));if(!owner)return nullptr;owner->actor->state=cs::read(raw);owner->status->update(revision,clock==Py_None?std::optional<double>{}:p::scalar(clock));auto result=p::dict();
for(const char*key:{"reload","dispersion","aim_time","turret_speed","mobility","vision","signal","traverse","speed"})p::set(result.get(),key,p::number(owner->status->factor(key)));auto casualties=p::list();for(const auto&key:owner->status->casualties())p::append(casualties.get(),p::text(key));p::set(result.get(),"casualties",casualties);return result.release();}catch(const std::exception&e){PyErr_SetString(PyExc_RuntimeError,e.what());return nullptr;}}
PyMethodDef methods[]={{"create",create,METH_VARARGS,""},{"update",update,METH_VARARGS,""},{nullptr,nullptr,0,nullptr}};
}
extern "C" void initclient_drive_status_test(){Py_InitModule("client_drive_status_test",methods);}
