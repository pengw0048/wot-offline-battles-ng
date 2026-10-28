// Real CPython 2, SDK PyObjectPlus/macros, constructors and property conversion.
// Native simulation, attachment and resource allocation are explicit boundaries;
// render dispatch and sprite vertices are exercised by the separate render host.
@PYOBJECT_SUPPORT@
namespace Script {
int setData(PyObject*,double&,const char*);
int setData(PyObject*,float&,const char*);
PyObject* getData(float);
}
@PYOBJECT_PLUS_HEADER@
@PYOBJECT_PLUS_SOURCE@
@FLOAT_CONVERSION@

template<class T>class SmartPointer {
    std::shared_ptr<T> value_;
public:
    SmartPointer(T* value=NULL):value_(value){}
    T* operator->()const{return value_.get();}
    T* getObject()const{return value_.get();}
    bool hasObject()const{return bool(value_);}
    explicit operator bool()const{return bool(value_);}
};
#define IF_NOT_MF_ASSERT_DEV(value) if(!(value))
#define MF_EXIT(message) std::abort()
struct ParticleSystem {
    int actions=0;
    int& actionSet(){return actions;}
};
using ParticleSystemPtr=SmartPointer<ParticleSystem>;
struct MetaParticleSystem {
    SmartPointer<MetaParticleSystem> clone(){return new MetaParticleSystem();}
    void spawn(int){}
    void clear(){}
};
using MetaParticleSystemPtr=SmartPointer<MetaParticleSystem>;
struct ActionsHolder {ActionsHolder(int&,void*,bool){}};
class PyAttachment:public PyObjectPlus {
    Py_Header(PyAttachment,PyObjectPlus)
public:explicit PyAttachment(PyTypeObject* type):PyObjectPlus(type){}
};
PY_TYPEOBJECT(PyAttachment)
PY_BEGIN_METHODS(PyAttachment) PY_END_METHODS()
PY_BEGIN_ATTRIBUTES(PyAttachment) PY_END_ATTRIBUTES()
class PyMetaParticleSystem:public PyAttachment {
    Py_Header(PyMetaParticleSystem,PyAttachment)
public:
    PyMetaParticleSystem(MetaParticleSystemPtr,PyTypeObject* = &s_type_);
    @META_SCALE_DECLARATION@
    PY_METHOD_DECLARE(py_clone)
    PY_METHOD_DECLARE(py_force)
    PY_METHOD_DECLARE(py_clear)
private:
    MetaParticleSystemPtr pSystem_;
    bool dirty_;
    float lod_;
    float scale_;
};
class PyParticleSystem:public PyAttachment {
    Py_Header(PyParticleSystem,PyAttachment)
public:
    PyParticleSystem(ParticleSystemPtr,PyTypeObject* = &s_type_);
    @PARTICLE_SCALE_DECLARATION@
private:
    ParticleSystemPtr pSystem_;
    float lod_;
    ActionsHolder actionsHolder_;
    float scale_;
};
@META_CONSTRUCTOR@
@PARTICLE_CONSTRUCTOR@
PY_TYPEOBJECT(PyMetaParticleSystem)
PY_BEGIN_METHODS(PyMetaParticleSystem)
    PY_METHOD(clone) PY_METHOD(force) PY_METHOD(clear)
PY_END_METHODS()
PY_BEGIN_ATTRIBUTES(PyMetaParticleSystem)
    @META_SCALE_REGISTRATION@
PY_END_ATTRIBUTES()
PY_TYPEOBJECT(PyParticleSystem)
PY_BEGIN_METHODS(PyParticleSystem) PY_END_METHODS()
PY_BEGIN_ATTRIBUTES(PyParticleSystem)
    @PARTICLE_SCALE_REGISTRATION@
PY_END_ATTRIBUTES()
@CLONE_BINDING@
@FORCE_BINDING@
@CLEAR_BINDING@
static PyObject* create(PyObject*,PyObject* args){
    int meta=1;if(!PyArg_ParseTuple(args,"|i",&meta))return NULL;
    if(meta)return new PyMetaParticleSystem(new MetaParticleSystem());
    return new PyParticleSystem(new ParticleSystem());
}
int main(int argc,char** argv){
    assert(argc==3);Py_Initialize();PySys_SetArgv(argc-1,argv+1);
    PyMethodDef methods[]={{"create",create,METH_VARARGS,NULL},{NULL,NULL,0,NULL}};
    assert(Py_InitModule("_particle_scale_host",methods));
    FILE* input=std::fopen(argv[1],"r");assert(input);
    int result=PyRun_SimpleFileEx(input,argv[1],1);Py_Finalize();return result?1:0;
}
