// SDK argument conversion/registration over the portable display owner fixture.
#include <cstdarg>
#include <deque>
#include <strings.h>
#define BW_NAMESPACE
#define BW_STATIC_ASSERT(expression, message) static_assert(expression, #message)
#define _stricmp strcasecmp
namespace BW {using string=std::string;}
template<class T> class SmartPointer
{
    T* value_;
public:
    SmartPointer(T* value=nullptr):value_(value){Py_XINCREF(value_);}
    SmartPointer(const SmartPointer& other):SmartPointer(other.value_){}
    ~SmartPointer(){Py_XDECREF(value_);}
    SmartPointer& operator=(T* value){Py_XINCREF(value);T* old=value_;value_=value;Py_XDECREF(old);return *this;}
    T* get()const{return value_;}T* getObject()const{return value_;}
};
using PyObjectPtr=SmartPointer<PyObject>;
struct HostMethodLink {std::string module;PyMethodDef def;};
std::deque<HostMethodLink>& hostMethodLinks(){static std::deque<HostMethodLink> links;return links;}
struct PyModuleMethodLink {
    PyModuleMethodLink(const char* m,const char* n,PyCFunction f,const char* d=nullptr){
        hostMethodLinks().push_back(HostMethodLink{m,PyMethodDef{n,f,METH_VARARGS,d}});
    }
};
unsigned callbackErrors=0;
namespace Script {
PyObject* argCountError(const char*,int,int,...);
int setData(PyObject*,SmartPointer<PyObject>&,const char* = "");
struct FiniTimeJob;
std::vector<FiniTimeJob*>& finiJobs(){static std::vector<FiniTimeJob*> jobs;return jobs;}
struct FiniTimeJob {FiniTimeJob(){finiJobs().push_back(this);}virtual~FiniTimeJob(){}virtual void fini()=0;};
bool call(PyObject* fn,PyObject* args,const char*,bool){
    PyObject* result=args?PyObject_CallObject(fn,args):nullptr;
    Py_XDECREF(fn);Py_XDECREF(args);
    if(!result){++callbackErrors;PyErr_Clear();return false;}
    Py_DECREF(result);return true;
}
}
@SCRIPT_CONVERTERS@
#undef PY_MODULE_FUNCTION
#undef PY_MODULE_ATTRIBUTE
@AUTO_MACROS@
// Other display wrappers have their existing explicit registration below;
// the new callback uses the actual SDK PY_AUTO module link above.
#undef PY_MODULE_FUNCTION
#undef PY_MODULE_ATTRIBUTE
#define PY_MODULE_FUNCTION(...)
#define PY_MODULE_ATTRIBUTE(...)
