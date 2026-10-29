#include "pch.hpp"
#include "pyscript/script.hpp"
#include "stock_native_client_replica.hpp"
namespace stock_native_client_replica {
namespace {
PyMethodDef methods[]={
    {"create",create,METH_VARARGS,"Install a persistent visible replica owner."},
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
