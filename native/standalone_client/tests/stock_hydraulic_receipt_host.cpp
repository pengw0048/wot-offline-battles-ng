// Execute the production receipt sampler with Math at an explicit host boundary.
#include "stock_native_client_state.hpp"
#include "stock_native_client_drive_publication.hpp"
#include "stock_native_client_input.hpp"
#include "stock_native_bot_python.hpp"
namespace stock_native_client_drive {
PlayerDriver::PlayerDriver(PlayerDescriptor d,PlayerState s):descriptor_(std::move(d)),state_(std::move(s)){}
}
namespace receipt_fixture {
namespace p=stock_native_bot_python;
namespace cs=stock_native_client_state;
using stock_native_client_drive::PlayerDriver;
struct Local {p::Ref body;};
struct Battle {Local* local;};
struct Owner {std::shared_ptr<cs::State> shared; p::Ref math; bool current()const{return true;}};
p::Ref field(PyObject*o,const char*n){return !o||o==Py_None?p::retain(Py_None):p::value(o,n);}
double number(PyObject*o,const char*n){return p::scalar(field(o,n).get());}
using Point=cs::Point;
#include "hydraulic_receipt_xyz.inc"
p::Ref matrix(PyObject*math,PyObject*provider){auto factory=field(math,"Matrix");p::Ref args(PyTuple_Pack(1,provider));return p::Ref(PyObject_CallObject(factory.get(),args.get()));}
p::Ref poseProvider(Battle&b,const char*){return p::retain(b.local->body.get());}
#include "hydraulic_receipt_sampler.inc"
p::Ref bodyValue(const std::optional<std::array<double,8>>&value){return value?p::tupleNumbers(*value):p::retain(Py_None);}
PyObject*sample(PyObject*,PyObject*args){PyObject*math,*body,*entity;int enabled;if(!PyArg_ParseTuple(args,"OOOi",&math,&body,&entity,&enabled))return nullptr;try{
    Owner owner{std::make_shared<cs::State>(),p::retain(math)};owner.shared->local_player_id=1;auto actor=std::make_shared<cs::Actor>();owner.shared->actors["player:1"]=actor;Local local{p::retain(body)};Battle battle{&local};stock_native_client_drive::PlayerDescriptor desc;desc.hydraulic=enabled!=0;PlayerDriver driver(desc,{});driver.state().hydraulic_body=std::array<double,8>{};actor->hydraulic_body=driver.state().hydraulic_body;
    sampleHydraulicBody(owner,battle,entity,driver);auto out=p::dict();p::set(out.get(),"driver",bodyValue(driver.state().hydraulic_body));p::set(out.get(),"actor",bodyValue(actor->hydraulic_body));return out.release();
}catch(const std::exception&e){if(!PyErr_Occurred())PyErr_SetString(PyExc_ValueError,e.what());return nullptr;}}
PyObject*frozen(PyObject*,PyObject*){using namespace stock_native_client_drive;PlayerState state;state.position={1.,2.,3.};state.yaw=.2;state.hydraulic_body=std::array<double,8>{1.,2.1,3.2,.2,.3,.4,.5,.6};PublicationLedger ledger;ledger.barrier(state,{},state.position,state.yaw);ledger.barrier(state,{},{4.,5.,6.},.7);state.position={4.,5.,6.};state.yaw=.7;state.hydraulic_body=std::array<double,8>{4.,5.1,6.2,.7,.8,.9,1.,1.1};ledger.barrier(state,{},state.position,state.yaw);auto out=p::list();for(const auto&row:ledger.barriers())p::append(out.get(),bodyValue(row.state.hydraulic_body));return out.release();}
std::unique_ptr<cs::State> state;
PyObject*accept(PyObject*,PyObject*args){PyObject*row;int reset;if(!PyArg_ParseTuple(args,"Oi",&row,&reset))return nullptr;try{if(!state||reset)state=std::make_unique<cs::State>();auto message=cs::read(row);auto events=cs::snapshot(*state,message,double(state->revision+1));auto out=p::dict();auto*a=state->find("player:1");if(a){p::set(out.get(),"current",bodyValue(a->hydraulic_body));p::set(out.get(),"target",bodyValue(a->target_hydraulic_body));}p::set(out.get(),"events",p::integer(events.size()));return out.release();}catch(const std::exception&e){if(!PyErr_Occurred())PyErr_SetString(PyExc_ValueError,e.what());return nullptr;}}
PyMethodDef methods[]={{"sample",sample,METH_VARARGS,""},{"frozen",frozen,METH_NOARGS,""},{"accept",accept,METH_VARARGS,""},{nullptr,nullptr,0,nullptr}};
}
extern "C" void inithydraulic_receipt_test(){using namespace receipt_fixture;static std::vector<PyMethodDef> table;for(auto*m=methods;m->ml_name;++m)table.push_back(*m);for(auto*m=stock_native_client_input::methods();m->ml_name;++m)table.push_back(*m);table.push_back({nullptr,nullptr,0,nullptr});Py_InitModule("hydraulic_receipt_test",table.data());}
