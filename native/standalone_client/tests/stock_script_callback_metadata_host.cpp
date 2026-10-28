// Compile the pinned SDK profiler resolver/ask and actual Svarog script owner.
#include <Python.h>
#include <cassert>
#include <cstring>
#include <string>
#include <algorithm>
#define ENABLE_PROFILER 1
#define MF_ASSERT(value) assert(value)
#define MF_ASSERT_DEV(value) assert(value)
#define MEMTRACKER_SCOPED(value) ((void)0)
#define ERROR_MSG(...) ((void)0)
#define ARRAY_SIZE(value) (sizeof(value)/sizeof(value[0]))
#define PROFILER_SCOPED_DYNAMIC_STRING_CATEGORY(value,category) ((void)value)
class PyObjectPtr {
public:
    enum Ownership { STEAL_REFERENCE };
    PyObjectPtr(PyObject* p=NULL):value_(p) { Py_XINCREF(value_); }
    PyObjectPtr(PyObject* p,Ownership):value_(p) {}
    PyObjectPtr(const PyObjectPtr& other):PyObjectPtr(other.value_) {}
    ~PyObjectPtr() { Py_XDECREF(value_); }
    PyObjectPtr& operator=(PyObject* next) {
        Py_XINCREF(next);Py_XDECREF(value_);value_=next;return *this;
    }
    PyObjectPtr& operator=(const PyObjectPtr& next) { return *this=next.value_; }
    PyObject* get() const { return value_; }
    explicit operator bool() const { return value_!=NULL; }
private:PyObject* value_;
};
namespace BW {
using string=std::string;
class StringBuilder {
public:
    StringBuilder(char* output,size_t capacity):output_(output),capacity_(capacity) { output_[0]=0; }
    void append(const char* value) {
        const size_t used=std::strlen(output_);
        const size_t count=std::min(capacity_-used-1,std::strlen(value));
        std::memcpy(output_+used,value,count);output_[used+count]=0;
    }
    void append(char value) { char text[2]={value,0};append(text); }
private:char* output_;size_t capacity_;
};
}
namespace Script {
bool isFinalised() { return false; }
PyObject* ask(PyObject*,PyObject*,const char*,bool,bool);
bool call(PyObject*,PyObject*,const char*,bool=false);
}
@RESOLVER@
@ASK@
namespace Script {
@CALL@
}
struct StockComponentState {
    bool valid_=true;
    virtual ~StockComponentState() {}
    virtual void activate()=0;
    virtual void deactivate()=0;
    virtual void destroy()=0;
    virtual PyObject* wrap(bool)=0;
};
@COMPONENT@
class PySwingingAnimator {
public:
    PyObject* py_activate(PyObject*);
    PyObject* py_deactivate(PyObject*);
};
@NATIVE_METHODS@
static PySwingingAnimator native;
struct Wrapper { PyObject_HEAD };
static PyTypeObject nativeType={PyVarObject_HEAD_INIT(NULL,0)};
static PyObject* activate(Wrapper*,PyObject* args) { return native.py_activate(args); }
static PyObject* deactivate(Wrapper*,PyObject* args) { return native.py_deactivate(args); }
static PyObject* destroy(Wrapper*,PyObject* args) {
    if (!PyArg_ParseTuple(args,":destroy")) return NULL;Py_RETURN_NONE;
}
static PyMethodDef nativeMethods[]={
    {"activate",(PyCFunction)activate,METH_VARARGS,NULL},
    {"deactivate",(PyCFunction)deactivate,METH_VARARGS,NULL},
    {"destroy",(PyCFunction)destroy,METH_VARARGS,NULL},{NULL,NULL,0,NULL}
};
static PyObject* profile(PyObject*,PyObject* args) {
    PyObject* callable;if (!PyArg_ParseTuple(args,"O",&callable)) return NULL;
    char output[1024];resolvePythonModuleAndFunctionNames(callable,output,sizeof(output));
    if (PyErr_Occurred()) return NULL;
    return PyString_FromString(output);
}
static PyObject* ask(PyObject*,PyObject* args) {
    PyObject* callable;int print=0;
    if (!PyArg_ParseTuple(args,"O|i",&callable,&print)) return NULL;
    Py_INCREF(callable);
    PyObject* result=Script::ask(callable,PyTuple_New(0),"",false,print!=0);
    if (PyErr_Occurred()) { Py_XDECREF(result);return NULL; }
    // Match Script::call's failure receipt after ask has reported the error.
    if (!result && print) Py_RETURN_FALSE;
    return result;
}
static PyObject* component(PyObject*,PyObject* args) {
    PyObject* object;if (!PyArg_ParseTuple(args,"O",&object)) return NULL;
    ScriptComponentState owner(object);
    owner.activate();owner.deactivate();owner.destroy();
    if (PyErr_Occurred()) return NULL;
    Py_RETURN_NONE;
}
static PyMethodDef methods[]={
    {"profile",profile,METH_VARARGS,NULL},{"ask",ask,METH_VARARGS,NULL},
    {"component",component,METH_VARARGS,NULL},{NULL,NULL,0,NULL}
};
int main(int argc,char** argv) {
    if (argc!=2) return 2;
    Py_Initialize();
    nativeType.tp_name=(char*)"Vehicular.SwingingAnimator";
    nativeType.tp_basicsize=sizeof(Wrapper);nativeType.tp_flags=Py_TPFLAGS_DEFAULT;
    nativeType.tp_new=PyType_GenericNew;nativeType.tp_methods=nativeMethods;
    if (PyType_Ready(&nativeType)<0) return 3;
    PyObject* module=Py_InitModule("callback_probe",methods);
    Py_INCREF(&nativeType);PyModule_AddObject(module,"Native",(PyObject*)&nativeType);
    PySys_SetArgv(argc-1,argv+1);
    FILE* file=fopen(argv[1],"rb");if (!file) return 4;
    int result=PyRun_SimpleFileEx(file,argv[1],1);
    if (PyErr_Occurred()) { PyErr_Print();result=-1; }
    Py_Finalize();return result?1:0;
}
