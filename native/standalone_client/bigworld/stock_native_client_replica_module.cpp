#include "pch.hpp"
#include "pyscript/script.hpp"
#include "stock_native_client_replica.hpp"
namespace stock_native_client_replica {
namespace {
PyMethodDef methods[]={
    {"create",create,METH_VARARGS,"Install a persistent visible replica owner."},
    {"state",stateHandle,METH_VARARGS,"Share the typed client battle state."},
    {"snapshot",snapshot,METH_VARARGS,"Consume an accepted snapshot without Python entity upserts."},
    {"manifest",manifest,METH_VARARGS,"Consume cold roster lifecycle."},
    {"refresh_pending",refreshPending,METH_VARARGS,"Retry only asynchronous native entity owners."},
    {"flush_pending",flushPending,METH_VARARGS,"Visit only asynchronous creation and removal edges."},
    {"refresh",refresh,METH_VARARGS,"Attach asynchronous native entity owners."},
    {"probes",probes,METH_VARARGS,"Drain native remote-motion counters."},
    {"statistics",statistics,METH_VARARGS,"Read bounded snapshot bridge counters."},
    {"inspect",inspect,METH_VARARGS,"Materialize explicit diagnostic state."},
    {"ingest",ingest,METH_VARARGS,"Copy one accepted snapshot into typed history."},
    {"advance",advance,METH_VARARGS,"Advance confirmed remote presentation natively."},
    {"manages",manages,METH_VARARGS,"Check native presentation ownership for one cold update."},
    {"close",close,METH_VARARGS,"Retire remote presentation references idempotently."},
    {nullptr,nullptr,0,nullptr}
};
class Module final:public BW::Script::InitTimeJob {
public:
    Module():BW::Script::InitTimeJob(1){}
    void init()override{if(!Py_InitModule("offline_client_replica_native",methods)){PyErr_Print();CRITICAL_MSG("Native client replica module initialization failed\n");}}
};
Module module;
}
}
