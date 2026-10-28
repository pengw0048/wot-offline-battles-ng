// CPython and the pinned SDK PyObjectPlus, Python method, MetaParticleSystem
// and ParticleSystem clone bodies execute unchanged. Resource serialization,
// action/renderer allocation and attachment/rendering are explicit boundaries.
#include <Python.h>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>
#define BW_GUARD
#define ERROR_MSG(...) std::fprintf(stderr,__VA_ARGS__)
#define MF_ASSERT_DEV(value) assert(value)
#define MF_ASSERT(value) assert(value)
#define bw_snprintf std::snprintf
#define PY_GET_DOC(value) value
class PyObjectPtr {
    PyObject* value_=NULL;
public:
    enum StealReference {STEAL_REFERENCE};
    PyObjectPtr()=default;
    PyObjectPtr(PyObject* value):value_(value){Py_XINCREF(value_);}
    PyObjectPtr(PyObject* value,StealReference):value_(value){}
    PyObjectPtr(PyObject* value,bool alreadyIncremented):value_(value){if(!alreadyIncremented)Py_XINCREF(value_);}
    PyObjectPtr(const PyObjectPtr& other):value_(other.value_){Py_XINCREF(value_);}
    PyObjectPtr& operator=(const PyObjectPtr& other){return *this=other.value_;}
    PyObjectPtr& operator=(PyObject* value){Py_XINCREF(value);Py_XDECREF(value_);value_=value;return *this;}
    ~PyObjectPtr(){Py_XDECREF(value_);}
    PyObject* get()const{return value_;}
    PyObject* getObject()const{return value_;}
    explicit operator bool()const{return value_!=NULL;}
};
class ScriptObject : public PyObjectPtr {
public:
    enum ReferenceType {FROM_BORROWED_REFERENCE,FROM_NEW_REFERENCE};
    ScriptObject()=default;
    ScriptObject(PyObject* value,ReferenceType reference):PyObjectPtr(value,STEAL_REFERENCE){
        if(reference==FROM_BORROWED_REFERENCE)Py_XINCREF(value);
    }
    PyObject* newRef()const{Py_XINCREF(get());return get();}
};
class ScriptString : public ScriptObject {
public:ScriptString(PyObject* value,ReferenceType reference):ScriptObject(value,reference){}
};
class ScriptList : public ScriptObject {
public:
    ScriptList(PyObject* value):ScriptObject(value,FROM_NEW_REFERENCE){}
    static ScriptList create(){return ScriptList(PyList_New(0));}
};
namespace BW {
using string=std::string;
template<class T>using vector=std::vector<T>;
template<class T>struct CloneObject {const T* source;T* destination;CloneObject(const T* a,T* b):source(a),destination(b){}};
}
@PYOBJECT_PLUS_HEADER@
@PYOBJECT_PLUS_SOURCE@

template<class T>class SmartPointer {
    std::shared_ptr<T> value_;
    template<class U>friend class SmartPointer;
public:
    SmartPointer()=default;
    SmartPointer(T* value):value_(value){}
    template<class U>SmartPointer(const SmartPointer<U>& other):value_(other.value_){}
    T* operator->()const{return value_.get();}
    T* getObject()const{return value_.get();}
    explicit operator bool()const{return bool(value_);}
    bool hasObject()const{return bool(value_);}
};
using uint32=unsigned;
unsigned inverseMask=0;
bool stockTPSGroupDisabled(uint32 groups){return bool(groups&inverseMask);}
unsigned systemsAlive=0,metaAlive=0,actionClones=0,rendererClones=0;
struct Action {
    float rate=0;
    unsigned pending=0;
    SmartPointer<Action> clone()const{++actionClones;auto* result=new Action();result->rate=rate;return result;}
};
struct Renderer {
    float size=0;
    SmartPointer<Renderer> clone()const{++rendererClones;return new Renderer(*this);}
};
class ParticleSystem {
public:
    using Actions=std::vector<SmartPointer<Action>>;
    std::string name_;
    Actions actions_;
    SmartPointer<Renderer> pRenderer_;
    unsigned capacity_=0,particles=0;
    float configuredRate=0;
    int vizBox_=0;
    bool attached=false;
    explicit ParticleSystem(unsigned capacity):capacity_(capacity){++systemsAlive;}
    ~ParticleSystem(){--systemsAlive;}
    unsigned capacity()const{return capacity_;}
    void capacity(unsigned value){capacity_=value;}
    void pRenderer(SmartPointer<Renderer> value){pRenderer_=value;}
    void clearBoundingBox(){}
    // This fixture stands at the generic resource serializer boundary.
    void serialise(BW::CloneObject<ParticleSystem> value)const{value.destination->configuredRate=value.source->configuredRate;}
    ParticleSystem* clone()const;
};
@PARTICLE_CLONE@

class MetaParticleSystem;
class StringBuilder;
using MetaParticleSystemPtr=SmartPointer<MetaParticleSystem>;
class MetaParticleSystem {
public:
    std::vector<SmartPointer<ParticleSystem>> systems_;
    uint32 inverseRenderGroups_=0;
    bool attached_=false,inWorld_=false;
    MetaParticleSystem(){++metaAlive;}
    ~MetaParticleSystem(){--metaAlive;}
    void addSystem(SmartPointer<ParticleSystem> system){systems_.push_back(system);}
    void spawn(int count){for(auto& system:systems_)system->actions_[0]->pending+=unsigned(count);}
    void clear(){for(auto& system:systems_)system->particles=0;}
    MetaParticleSystemPtr clone()const;
    unsigned nSystems()const{return unsigned(systems_.size());}
    @BACKGROUND_LOAD_DECLARATION@
};
@META_CLONE@

class PyAttachment : public PyObjectPlus {
    Py_Header(PyAttachment,PyObjectPlus)
public:
    explicit PyAttachment(PyTypeObject* type):PyObjectPlus(type){}
};
PY_TYPEOBJECT(PyAttachment)
PY_BEGIN_METHODS(PyAttachment) PY_END_METHODS()
PY_BEGIN_ATTRIBUTES(PyAttachment) PY_END_ATTRIBUTES()
class PyMetaParticleSystem : public PyAttachment {
    Py_Header(PyMetaParticleSystem,PyAttachment)
public:
    explicit PyMetaParticleSystem(MetaParticleSystemPtr value):
        PyAttachment(&s_type_),pSystem_(value){}
    MetaParticleSystemPtr pSystem_;
    @CLONE_DECLARATION@
    PY_METHOD_DECLARE(py_force)
    PY_METHOD_DECLARE(py_clear)
    @BACKGROUND_FACTORY_DECLARATION@
};
PY_TYPEOBJECT(PyMetaParticleSystem)
PY_BEGIN_METHODS(PyMetaParticleSystem)
    @CLONE_REGISTRATION@
    PY_METHOD(force)
    PY_METHOD(clear)
PY_END_METHODS()
PY_BEGIN_ATTRIBUTES(PyMetaParticleSystem) PY_END_ATTRIBUTES()
@CLONE_BINDING@
@FORCE_BINDING@
@CLEAR_BINDING@

static PyMetaParticleSystem* instance(PyObject* value){
    if(!PyMetaParticleSystem::Check(value)){PyErr_SetString(PyExc_TypeError,"Expected the native particle wrapper");return NULL;}
    return static_cast<PyMetaParticleSystem*>(value);
}
static PyObject* create(PyObject*,PyObject* args){
    unsigned groups=0;if(!PyArg_ParseTuple(args,"|I",&groups))return NULL;
    MetaParticleSystemPtr meta=new MetaParticleSystem();meta->inverseRenderGroups_=groups;
    for(unsigned i=0;i<2;++i){
        auto* system=new ParticleSystem(256+i);system->name_="system"+std::to_string(i);
        system->configuredRate=3.5f+i;system->particles=7+i;system->attached=true;system->vizBox_=42;
        auto* action=new Action();action->rate=4.5f+i;action->pending=9;system->actions_.push_back(action);
        auto* renderer=new Renderer();renderer->size=2.5f+i;system->pRenderer(renderer);meta->addSystem(system);
    }
    meta->attached_=true;meta->inWorld_=true;
    return new PyMetaParticleSystem(meta);
}
static PyObject* inspect(PyObject*,PyObject* args){
    PyObject* object;if(!PyArg_ParseTuple(args,"O",&object))return NULL;
    auto* wrapper=instance(object);if(!wrapper)return NULL;
    const auto meta=wrapper->pSystem_;auto* system=meta->systems_[0].getObject();
    return Py_BuildValue("{s:K,s:I,s:i,s:i,s:I,s:I,s:f,s:f,s:f,s:I,s:K,s:K}",
        "owner",reinterpret_cast<unsigned long long>(meta.getObject()),"groups",meta->inverseRenderGroups_,
        "attached",meta->attached_,"inWorld",meta->inWorld_,"particles",system->particles,"capacity",system->capacity(),
        "configuredRate",system->configuredRate,"actionRate",system->actions_[0]->rate,"rendererSize",system->pRenderer_->size,
        "pending",system->actions_[0]->pending,
        "action",reinterpret_cast<unsigned long long>(system->actions_[0].getObject()),
        "renderer",reinterpret_cast<unsigned long long>(system->pRenderer_.getObject()));
}
static PyObject* mutate(PyObject*,PyObject* args){
    PyObject* object;float value;if(!PyArg_ParseTuple(args,"Of",&object,&value))return NULL;
    auto* wrapper=instance(object);if(!wrapper)return NULL;
    auto* system=wrapper->pSystem_->systems_[0].getObject();system->actions_[0]->rate=value;
    system->pRenderer_->size=value;system->particles=99;Py_RETURN_NONE;
}
static PyObject* mask(PyObject*,PyObject* args){
    if(!PyArg_ParseTuple(args,"I",&inverseMask))return NULL;Py_RETURN_NONE;
}
static PyObject* counts(PyObject*,PyObject*){
    return Py_BuildValue("(IIII)",metaAlive,systemsAlive,actionClones,rendererClones);
}
@BACKGROUND_SUPPORT@
int main(int argc,char** argv){
    assert(argc==3);Py_Initialize();PySys_SetArgv(argc-1,argv+1);
    PyMethodDef methods[]={{"create",create,METH_VARARGS,NULL},{"inspect",inspect,METH_VARARGS,NULL},
        {"mutate",mutate,METH_VARARGS,NULL},{"mask",mask,METH_VARARGS,NULL},{"counts",counts,METH_NOARGS,NULL},
        @BACKGROUND_METHODS@
        {NULL,NULL,0,NULL}};
    assert(Py_InitModule("_particle_clone_host",methods));
    FILE* input=std::fopen(argv[1],"r");assert(input);
    int result=PyRun_SimpleFileEx(input,argv[1],1);Py_Finalize();
    assert(metaAlive==0&&systemsAlive==0);return result?1:0;
}
