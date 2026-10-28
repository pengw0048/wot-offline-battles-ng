// Real trace LOD wrapper, shared parser and activation owner in CPython 2.7.
// Other appearance inputs and post-LOD rendering are observable boundaries.
#include <Python.h>
#include <algorithm>
#include <cfloat>
#include <cstdint>
#include <map>
#include <memory>
#include <vector>
using uint32=std::uint32_t;
using uint=unsigned;
namespace Script {
int setData(PyObject*,double&,const char*);
int setData(PyObject*,float&,const char*);
int setData(PyObject*,uint&,const char*);
}
@CONVERSIONS@
@LOD_PARSER@
class PyObjectPlus {};
template<class T> struct WeakPyPtr {
    T* value;
    explicit WeakPyPtr(T* v=NULL):value(v) {}
};
struct PyObjectPtr {
    PyObject* value=NULL;
    PyObjectPtr& operator=(PyObject* next) {
        Py_XINCREF(next);Py_XDECREF(value);value=next;return *this;
    }
    ~PyObjectPtr() { Py_XDECREF(value); }
    PyObject* get() const { return value; }
    explicit operator bool() const { return value!=NULL; }
};
using Vector4ProviderPtr=PyObjectPtr;
namespace wot {
struct StockTraceTextureSet {};
struct StockVehicleTraceSide { bool left=false; };
}
struct StockComponentState : std::enable_shared_from_this<StockComponentState> {
    bool valid_=true;
    virtual ~StockComponentState() {}
    virtual void activate()=0;
    virtual void deactivate()=0;
    virtual void destroy()=0;
    virtual PyObject* wrap(bool)=0;
};
struct TraceState;
static std::vector<std::weak_ptr<TraceState>> activeTraces;
static bool vehicleTracesEnabled=true;
static unsigned submitted=0;
static bool sampleStockLodLink(PyObject* link,uint32& priority,float& distance) {
    return PyArg_ParseTuple(link,"If",&priority,&distance)!=0;
}
@STATE@
PyObject* TraceState::wrap(bool) { Py_RETURN_NONE; }
class PyVehicleTraces {
public:
    std::shared_ptr<TraceState> value=std::make_shared<TraceState>();
    std::shared_ptr<TraceState> state() {
        if (!value->valid_) {
            PyErr_SetString(PyExc_ValueError,"VehicleTraces has been destroyed");return {};
        }
        return value;
    }
    PyObject* py_setLodSettings(PyObject*);
};
@SETTINGS_WRAPPER@
struct Wrapper { PyObject_HEAD PyVehicleTraces* owner; };
static PyTypeObject traceType={PyVarObject_HEAD_INIT(NULL,0)};
static PyObject* create(PyTypeObject* type,PyObject* args,PyObject*) {
    if (!PyArg_ParseTuple(args,":VehicleTraces")) return NULL;
    Wrapper* object=(Wrapper*)type->tp_alloc(type,0);
    if (object) object->owner=new PyVehicleTraces();
    return (PyObject*)object;
}
static void dispose(Wrapper* self) {
    self->owner->value->destroy();delete self->owner;
    Py_TYPE(self)->tp_free((PyObject*)self);
}
static PyObject* settings(Wrapper* self,PyObject* args) { return self->owner->py_setLodSettings(args); }
static PyObject* snapshot(Wrapper* self,PyObject* args) {
    if (!PyArg_ParseTuple(args,"")) return NULL;
    auto value=self->owner->value;
    return Py_BuildValue("fIii",value->maxDistance,value->maxPriority,value->active,value->valid_);
}
static PyObject* activate(Wrapper* self,PyObject* args) {
    if (!PyArg_ParseTuple(args,"")) return NULL;
    self->owner->value->activate();Py_RETURN_NONE;
}
static PyObject* deactivate(Wrapper* self,PyObject* args) {
    if (!PyArg_ParseTuple(args,"")) return NULL;
    self->owner->value->deactivate();Py_RETURN_NONE;
}
static PyObject* destroy(Wrapper* self,PyObject* args) {
    if (!PyArg_ParseTuple(args,"")) return NULL;
    self->owner->value->destroy();Py_RETURN_NONE;
}
static PyObject* lod(Wrapper* self,PyObject* args) {
    PyObject* link;
    if (!PyArg_ParseTuple(args,"O:setLodLink",&link)) return NULL;
    self->owner->value->lod=link==Py_None?NULL:link;Py_RETURN_NONE;
}
static PyObject* one(Wrapper*,PyObject* args) {
    PyObject* value;if (!PyArg_ParseTuple(args,"O",&value)) return NULL;Py_RETURN_NONE;
}
static PyObject* two(Wrapper*,PyObject* args) {
    PyObject *a,*b;if (!PyArg_ParseTuple(args,"OO",&a,&b)) return NULL;Py_RETURN_NONE;
}
static PyObject* four(Wrapper*,PyObject* args) {
    PyObject *a,*b,*c,*d;if (!PyArg_ParseTuple(args,"OOOO",&a,&b,&c,&d)) return NULL;Py_RETURN_NONE;
}
static PyMethodDef methods[]={
    {"setLodSettings",(PyCFunction)settings,METH_VARARGS,NULL},
    {"snapshot",(PyCFunction)snapshot,METH_VARARGS,NULL},
    {"activate",(PyCFunction)activate,METH_VARARGS,NULL},
    {"deactivate",(PyCFunction)deactivate,METH_VARARGS,NULL},
    {"destroy",(PyCFunction)destroy,METH_VARARGS,NULL},
    {"setLodLink",(PyCFunction)lod,METH_VARARGS,NULL},
    {"setCompound",(PyCFunction)one,METH_VARARGS,NULL},
    {"setMovementInfo",(PyCFunction)one,METH_VARARGS,NULL},
    {"setFlyingInfo",(PyCFunction)two,METH_VARARGS,NULL},
    {"setTrackTraces",(PyCFunction)four,METH_VARARGS,NULL},
    {NULL,NULL,0,NULL}
};
static PyObject* tick(PyObject*,PyObject* args) {
    if (!PyArg_ParseTuple(args,"")) return NULL;
    submitted=0;
    for (const auto& weak:activeTraces) if (auto value=weak.lock()) value->update();
    return PyLong_FromUnsignedLong(submitted);
}
static PyObject* owners(PyObject*,PyObject* args) {
    if (!PyArg_ParseTuple(args,"")) return NULL;
    return PyLong_FromSize_t(activeTraces.size());
}
static PyMethodDef probeMethods[]={
    {"tick",tick,METH_VARARGS,NULL},{"owners",owners,METH_VARARGS,NULL},{NULL,NULL,0,NULL}
};
int main(int argc,char** argv) {
    if (argc!=3) return 2;
    Py_Initialize();
    traceType.tp_name=(char*)"Vehicular.VehicleTraces";
    traceType.tp_basicsize=sizeof(Wrapper);traceType.tp_flags=Py_TPFLAGS_DEFAULT;
    traceType.tp_new=create;traceType.tp_dealloc=(destructor)dispose;traceType.tp_methods=methods;
    if (PyType_Ready(&traceType)<0) return 3;
    PyObject* module=Py_InitModule("Vehicular",NULL);
    Py_INCREF(&traceType);PyModule_AddObject(module,"VehicleTraces",(PyObject*)&traceType);
    Py_InitModule("trace_probe",probeMethods);
    PySys_SetArgv(argc-1,argv+1);
    FILE* file=fopen(argv[1],"rb");if (!file) return 4;
    int result=PyRun_SimpleFileEx(file,argv[1],1);
    if (PyErr_Occurred()) { PyErr_Print();result=-1; }
    Py_Finalize();return result?1:0;
}
