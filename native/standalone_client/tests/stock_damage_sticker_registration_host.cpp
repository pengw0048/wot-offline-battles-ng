// Real CPython 2.7 and pinned Script converters; bounded Vector2 carrier only.
#if defined(_MSC_VER) && !defined(_CRT_SECURE_NO_WARNINGS)
#define _CRT_SECURE_NO_WARNINGS
#endif
#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include "stock_vehicle_stickers_data.hpp"
#include <cassert>
#include <cstdio>
#include <memory>
#include <string>
#if !defined(_WIN32)
#include <strings.h>
#define _stricmp strcasecmp
#else
#include <windows.h>
#include <cstdlib>
#endif
#define WOT_STOCK_DAMAGE_STICKER_REGISTRATION_PORTABLE
#define BW_BEGIN_NAMESPACE namespace BW {
#define BW_END_NAMESPACE }
#define PY_MODULE_FUNCTION(...)

struct Vector2
{
    float x=0.f,y=0.f;
    float& operator[](unsigned index){return index?y:x;}
};
static PyTypeObject vectorType = {PyObject_HEAD_INIT(NULL) 0};
template<class T>struct PyVector
{
    PyObject_HEAD
    T value;
    static bool Check(PyObject* object){return PyObject_TypeCheck(object,&vectorType);}
    T getVector()const{return value;}
};
struct PyObjectPtr
{
    enum Ownership{STEAL_REFERENCE};
    PyObject* value=NULL;
    PyObjectPtr(){}
    PyObjectPtr(PyObject* object,Ownership):value(object){}
    PyObjectPtr(const PyObjectPtr& other):value(other.value){Py_XINCREF(value);}
    PyObjectPtr& operator=(const PyObjectPtr& other){Py_XINCREF(other.value);Py_XDECREF(value);value=other.value;return *this;}
    ~PyObjectPtr(){Py_XDECREF(value);}
    PyObject* get()const{return value;}
};
namespace BW
{
using string=std::string;
namespace Script
{
int setData(PyObject*,BW::string&,const char*);
int setData(PyObject*,Vector2&,const char*);
int setData(PyObject*,float&,const char*);
int setData(PyObject*,double&,const char*);
int setData(PyObject*,bool&,const char*);
}
@CONVERTERS@
std::unique_ptr<wot::StockDamageStickerRegistry> registry;
unsigned registrations=0;
int registerStockDamageSticker(const BW::string& name,const BW::string& texture,
    const BW::string& bump,const Vector2& size,float variation,bool randomYaw)
{
    ++registrations;
    if(!registry)return -1;
    return registry->add(name,{texture,bump,{{size.x,size.y}},variation,randomYaw});
}
}
#include "@PRODUCTION_FILE@"

static PyObject* registerSticker(PyObject*,PyObject* args){return BW::py_wg_registerDamageSticker(args);}
static PyObject* setOwner(PyObject*,PyObject* args)
{
    int active;
    if(!PyArg_ParseTuple(args,"i",&active))return NULL;
    BW::registry.reset(active?new wot::StockDamageStickerRegistry():NULL);
    Py_RETURN_NONE;
}
static PyObject* calls(PyObject*,PyObject*){return PyInt_FromLong(BW::registrations);}
static PyObject* inspect(PyObject*,PyObject*)
{
    PyObject* result=PyList_New(0);
    if(!BW::registry)return result;
    for(size_t index=0;index<BW::registry->size();++index)
    {
        const auto* style=BW::registry->find(static_cast<int>(index));assert(style);
        PyObject* variants=PyList_New(0);
        for(const auto& value:style->variants)
        {
            PyObject* row=Py_BuildValue("(s#s#fffO)",value.texture.data(),Py_ssize_t(value.texture.size()),
                value.bumpTexture.data(),Py_ssize_t(value.bumpTexture.size()),value.size[0],value.size[1],
                value.variation,value.randomYaw?Py_True:Py_False);
            assert(row&&PyList_Append(variants,row)==0);Py_DECREF(row);
        }
        PyObject* row=Py_BuildValue("(s#O)",style->name.data(),Py_ssize_t(style->name.size()),variants);
        assert(row&&PyList_Append(result,row)==0);Py_DECREF(row);Py_DECREF(variants);
    }
    return result;
}
static PyObject* vectorTuple(PyObject* self,PyObject*)
{
    const auto value=reinterpret_cast<PyVector<Vector2>*>(self)->value;
    return Py_BuildValue("(ff)",value.x,value.y);
}
static PyObject* vector(PyObject*,PyObject* args)
{
    float x,y;if(!PyArg_ParseTuple(args,"ff",&x,&y))return NULL;
    auto* result=PyObject_New(PyVector<Vector2>,&vectorType);
    if(!result)return NULL;
    result->value.x=x;result->value.y=y;
    return reinterpret_cast<PyObject*>(result);
}
static PyMethodDef vectorMethods[]={{"tuple",vectorTuple,METH_NOARGS,NULL},{NULL,NULL,0,NULL}};
static PyMethodDef methods[]={
    {"wg_registerDamageSticker",registerSticker,METH_VARARGS,NULL},
    {"_owner",setOwner,METH_VARARGS,NULL},{"_inspect",inspect,METH_NOARGS,NULL},
    {"_calls",calls,METH_NOARGS,NULL},{"_vector",vector,METH_VARARGS,NULL},{NULL,NULL,0,NULL}};

int main(int argc,char** argv)
{
#if defined(_WIN32)
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    _set_error_mode(_OUT_TO_STDERR);
    _set_abort_behavior(0,_WRITE_ABORT_MSG|_CALL_REPORTFAULT);
#endif
    assert(argc==2);
    Py_NoSiteFlag=1;Py_Initialize();
    vectorType.tp_name=const_cast<char*>("Fixture.Vector2");vectorType.tp_basicsize=sizeof(PyVector<Vector2>);
    vectorType.tp_flags=Py_TPFLAGS_DEFAULT;vectorType.tp_methods=vectorMethods;
    vectorType.tp_dealloc=[](PyObject* object){PyObject_Del(object);};
    assert(PyType_Ready(&vectorType)==0);
    assert(Py_InitModule("BigWorld",methods));
    FILE* script=fopen(argv[1],"rb");assert(script);
    const int result=PyRun_SimpleFile(script,argv[1]);fclose(script);
    if(result){PyErr_Print();return 1;}
    BW::registry.reset();Py_Finalize();
    std::puts("Actual CPython 2.7 damage-sticker boundary and original registry records passed");
    return 0;
}
