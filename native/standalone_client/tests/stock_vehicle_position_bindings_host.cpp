// CPython 2.7 host for the actual vehicle LOD and suspension accessors.
// Camera, matrix and suspension-node storage are explicit fixtures.
#include "stock_battle_vehicle_animation_host.hpp"
#include "stock_vehicle_components.cpp"
#include <algorithm>
#include <memory>
#include <iostream>
namespace BW {
std::vector<std::string> hostErrors;
std::vector<HostFactory>& hostFactories() { static std::vector<HostFactory> v; return v; }
std::vector<PyTypeObject*>& hostTypes() { static std::vector<PyTypeObject*> v; return v; }
PyObject* hostMemberNames(PyObject*, bool) { return PyList_New(0); }
PyTypeObject PyObjectPlus::s_type_ = [] {
    PyTypeObject t = { PyVarObject_HEAD_INIT(&PyType_Type, 0) };
    t.tp_name="PyObjectPlus"; t.tp_basicsize=sizeof(PyObjectPlus);
    t.tp_flags=Py_TPFLAGS_DEFAULT|Py_TPFLAGS_BASETYPE; return t;
}();
const Matrix Matrix::identity = Matrix();
namespace Script { inline PyObject* getData(uint32 v) { return PyLong_FromUnsignedLong(v); }
inline int setData(PyObject* p,bool& v) {
    if(!PyInt_Check(p)){PyErr_SetString(PyExc_TypeError,"Expected bool");return -1;}
    v=PyInt_AsLong(p)!=0;return 0;
}
inline PyObject* getData(Vector3 v) { return Py_BuildValue("(fff)",v.x,v.y,v.z); } }
namespace Moo {
struct Context {
    Matrix view_, projection_;
    const Matrix& view() const { return view_; }
    const Matrix& viewProjection() const { return projection_; }
    float zoomFactor() const { return 1.f; }
};
Context& rc() { static Context c; return c; }
}
class StockTranslationLink : public PyObjectPlus {
    Py_Header(StockTranslationLink, PyObjectPlus)
public:
    explicit StockTranslationLink(PyObject* p):PyObjectPlus(&s_type_),position(p){}
    PyObjectPtr position;
};
PY_TYPEOBJECT(StockTranslationLink)
PY_BEGIN_METHODS(StockTranslationLink)
PY_END_METHODS()
PY_BEGIN_ATTRIBUTES(StockTranslationLink)
PY_END_ATTRIBUTES()
bool sampleStockTranslationLink(PyObject* p, Vector3& value) {
    if(!p || !StockTranslationLink::Check(p))return false;
    PyObjectPtr tuple(PySequence_Tuple(StockTranslationLink::getSelf(p)->position.get()),PyObjectPtr::STEAL_REFERENCE);
    return tuple && Script::setData(tuple.get(),value)==0;
}
#include "stock_vehicle_lod_owner.inc"
// The property uses the production getter and lifecycle guard against this
// recorded node store. addNodes/render queries are covered separately.
struct SuspensionState : public StockComponentState {
    std::vector<int> nodes;
    void activate() override {}
    void deactivate() override {}
    void destroy() override { valid_=false; nodes.clear(); }
    PyObject* wrap(bool) override { return NULL; }
};
#include "stock_suspension_property_owner.inc"
PyObject* link(PyObject*,PyObject* args) {
    PyObject* p; if(!PyArg_ParseTuple(args,"O",&p))return NULL; return new StockTranslationLink(p);
}
PyObject* tick(PyObject*,PyObject*) { updateLodCalculators(); Py_RETURN_NONE; }
PyObject* suspension(PyObject*,PyObject* args) {
    int count; if(!PyArg_ParseTuple(args,"i",&count))return NULL;
    auto state=std::make_shared<SuspensionState>();state->nodes.resize(count);
    return new Suspension(state,true);
}
PyObject* setNodes(PyObject*,PyObject* args) {
    PyObject* object;int count;if(!PyArg_ParseTuple(args,"Oi",&object,&count))return NULL;
    Suspension::getSelf(object)->state()->nodes.resize(count);Py_RETURN_NONE;
}
PyObject* errorCount(PyObject*,PyObject*) { return PyInt_FromLong(hostErrors.size()); }
PyMethodDef methods[]={
    {"link",link,METH_VARARGS,NULL},{"tick",tick,METH_NOARGS,NULL},
    {"suspension",suspension,METH_VARARGS,NULL},{"setNodes",setNodes,METH_VARARGS,NULL},
    {"errorCount",errorCount,METH_NOARGS,NULL},{NULL,NULL,0,NULL}};
}
int main(int argc,char**argv) {
    using namespace BW;assert(argc==2);Py_NoSiteFlag=1;Py_Initialize();
    assert(PyType_Ready(&PyObjectPlus::s_type_)==0);
    for(auto* type:hostTypes())assert(PyType_Ready(type)==0);
    std::map<std::string,std::vector<PyMethodDef>> modules;
    for(const auto&f:hostFactories())modules[f.module].push_back({f.name,(PyCFunction)f.function,METH_VARARGS|METH_KEYWORDS,NULL});
    for(auto&m:modules){m.second.push_back({NULL,NULL,0,NULL});assert(Py_InitModule(m.first.c_str(),m.second.data()));}
    assert(Py_InitModule("Host",methods));FILE* script=std::fopen(argv[1],"r");assert(script);
    int failed=PyRun_SimpleFileEx(script,argv[1],1);Py_Finalize();return failed?1:0;
}
