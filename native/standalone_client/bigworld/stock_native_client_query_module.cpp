#include "pch.hpp"
#include "pyscript/script.hpp"
#include "stock_native_client_query.hpp"
namespace stock_native_client_query {namespace {
PyMethodDef methods[]={
    {"create",create,METH_VARARGS,"Bind visible queries to one shared native battle state."},
    {"ingest",ingest,METH_VARARGS,"Install changed native entity and descriptor resources."},
    {"statistics",statistics,METH_VARARGS,"Read current query resource and execution counters."},
    {"candidates",candidates,METH_VARARGS,"Query current visible actor bounds without Python attribute hooks."},
    {"segment_may_hit",segmentMayHit,METH_VARARGS,"Apply one current native actor broadphase and draw gate."},
    {"collisions",collisions,METH_VARARGS,"Resolve precise native descriptor BSP hits at the current pose."},
    {"outline",outline,METH_VARARGS,"Resolve cursor selection, scenery, wreck and landed turret occlusion."},
    {"flags",flags,METH_VARARGS,"Publish one discrete actor lifecycle transition."},
    {"turrets",turrets,METH_VARARGS,"Install changed accepted landed turret target bounds."},
    {"close",close,METH_VARARGS,"Retire query resources idempotently."},
    {nullptr,nullptr,0,nullptr}
};
class Module final:public BW::Script::InitTimeJob {
public:Module():BW::Script::InitTimeJob(1){}
    void init()override{if(!Py_InitModule("offline_client_query_native",methods)){PyErr_Print();CRITICAL_MSG("Native client query module initialization failed\n");}}
};Module module;
}}
