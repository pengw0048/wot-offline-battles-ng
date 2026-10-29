#define WOT_CLIENT_REPLICA_HOST_BUILD
#include "stock_native_client_state.cpp"
#include "stock_native_client_drive_dispatch.cpp"
#include "stock_native_client_drive_publication.cpp"
#include "stock_native_client_drive_runtime_host.cpp"
namespace {
namespace p=stock_native_bot_python;
struct PublicationWorld:World {bool landing=false;bool pending_landing()const override{return landing;}};
struct Sink:PlayerPublicationSink {
    PlayerDriver&driver;PublicationLedger&ledger;p::Ref log=p::list();unsigned mask,index=0;ID sequence=10;
    Sink(PlayerDriver&d,PublicationLedger&l,unsigned m):driver(d),ledger(l),mask(m){}
    bool operation(const p::Ref&value){p::append(log.get(),value);return (mask&(1u<<index++))!=0;}
    bool current(std::optional<bool>siege={})override{const bool accepted=operation(siege?p::boolean(*siege):p::retain(Py_None));if(accepted)++sequence;return accepted;}
    bool barriers()override{while(!ledger.barriers().empty()){const auto row=ledger.barriers().front();if(!current())return false;ledger.barrierAdmitted(row.sequence);driver.input_sent();}return true;}
    bool landing()override{if(!current()||!operation(p::text("landing")))return false;driver.landing_admitted();return true;}
    std::optional<ID>inputSequence()const override{return sequence;}
};
PyObject*run(PyObject*,PyObject*args){try{double elapsed;int siege,landing,barrier,stopped;unsigned mask;if(!PyArg_ParseTuple(args,"diiiii",&elapsed,&siege,&landing,&barrier,&stopped,&mask))return nullptr;
    PlayerDescriptor d;d.network_seconds=.1;d.motion.drive.mass=25000.;d.motion.drive.t[11]=1.;d.motion.drive.t[13]=0.;d.motion.drive.t[14]=1.;d.motion.drive.t[18]=1.;d.motion.drive.t[24]=1.;for(auto&v:d.motion.drive.terrain)v=1.;d.motion.half_width=1.;d.motion.half_length=2.;PlayerDriver driver(d,{});PublicationWorld world;world.flags.ready=false;driver.advance(world,.07);world.landing=landing!=0;world.flags.ready=stopped!=0;world.flags.alive=false;if(siege>=0)driver.state().siege_braking=siege!=0;PublicationLedger ledger;if(barrier)ledger.barrier(driver.state(),driver.controls(),driver.state().position,0.);const auto publication=driver.advance(world,elapsed);Sink sink(driver,ledger,mask);publishPlayer(driver,ledger,publication,sink);return Py_BuildValue("(NdOOK)",sink.log.release(),driver.input_elapsed(),driver.state().siege_pending?p::boolean(*driver.state().siege_pending).get():Py_None,driver.state().siege_braking?p::boolean(*driver.state().siege_braking).get():Py_None,static_cast<unsigned PY_LONG_LONG>(ledger.barriers().size()));
}catch(const std::exception&e){PyErr_SetString(PyExc_RuntimeError,e.what());return nullptr;}}
PyMethodDef methods[]={{"run",run,METH_VARARGS,""},{nullptr,nullptr,0,nullptr}};
}
PyMODINIT_FUNC initclient_dispatch_test(){Py_InitModule("client_dispatch_test",methods);}
