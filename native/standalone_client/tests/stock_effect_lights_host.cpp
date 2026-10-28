// Production Python classes, attribute dispatch and compiled-space owner are
// compiled below. Geometry, scene and provider objects are host boundaries.
#include <Python.h>
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <strings.h>
#include <type_traits>
#define _stricmp strcasecmp
#include <vector>
#define BW_BEGIN_NAMESPACE namespace BW {
#define BW_END_NAMESPACE }
#define PY_GET_DOC(value) value
#define MF_ASSERT_DEV(value) assert(value)
#define MF_ASSERT(value) assert(value)
#define bw_snprintf std::snprintf
#define ERROR_MSG(...) std::fprintf(stderr, __VA_ARGS__)
#define BW_STATIC_ASSERT(value, message) static_assert(value, #message)
namespace BW {
using string=std::string;
template<class T> void bw_safe_delete(T*& p){delete p;p=NULL;}
template<class T> using vector = std::vector<T>;
class PyObjectPtr {
    PyObject* object_ = NULL;
public:
    enum { STEAL_REFERENCE };
    PyObjectPtr() = default;
    PyObjectPtr(PyObject* value) : object_(value) { Py_XINCREF(object_); }
    PyObjectPtr(PyObject* value, int) : object_(value) {}
    PyObjectPtr(const PyObjectPtr& other) : PyObjectPtr(other.object_) {}
    PyObjectPtr& operator=(const PyObjectPtr& other) {
        Py_XINCREF(other.object_); Py_XDECREF(object_); object_=other.object_; return *this;
    }
    PyObjectPtr& operator=(PyObject* value) {Py_XINCREF(value);Py_XDECREF(object_);object_=value;return *this;}
    ~PyObjectPtr() { Py_XDECREF(object_); }
    PyObject* get() const { return object_; }
    operator PyObject*()const{return object_;}
};
class ScriptObject : public PyObjectPtr {
public:
    enum ReferenceType { FROM_BORROWED_REFERENCE, FROM_NEW_REFERENCE };
    ScriptObject() = default;
    ScriptObject(PyObject* value, ReferenceType type) : PyObjectPtr(value, STEAL_REFERENCE) {
        if(type==FROM_BORROWED_REFERENCE) Py_XINCREF(value);
    }
    PyObject* newRef() const { Py_XINCREF(get()); return get(); }
};
class ScriptString : public ScriptObject {
public: ScriptString(PyObject* value, ReferenceType type) : ScriptObject(value,type) {}
};
class ScriptList : public ScriptObject {
public:
    explicit ScriptList(PyObject* value) : ScriptObject(value,FROM_NEW_REFERENCE) {}
    static ScriptList create() { return ScriptList(PyList_New(0)); }
};
struct PyFactoryMethodLink {
    PyFactoryMethodLink(const char*,const char*,PyTypeObject*) {}
};
namespace Script {
int setData(PyObject*,bool&,const char* = "");
int setData(PyObject*,int&,const char* = "");
int setData(PyObject*,float&,const char* = "");
int setData(PyObject*,double&,const char* = "");
PyObject* argCountError(const char*,int,int,...);
PyObject* getData(bool v) { return PyBool_FromLong(v); }
PyObject* getData(int v) { return PyInt_FromLong(v); }
PyObject* getData(float v) { return PyFloat_FromDouble(v); }
}
@SCRIPT_BODIES@
@PYAUTO_MACROS@
@PYOBJECT_PLUS_HEADER@
@PYOBJECT_PLUS_SOURCE@

struct Vector3 {
    float x=0,y=0,z=0;
    Vector3()=default; Vector3(float a,float b,float c):x(a),y(b),z(c){}
};
struct Vector4 {
    float x=0,y=0,z=0,w=0;
    Vector4()=default;Vector4(float a,float b,float c,float d):x(a),y(b),z(c),w(d){}
    float& operator[](unsigned i){return (&x)[i];}
    Vector4& operator*=(float v) { x*=v;y*=v;z*=v;w*=v;return *this; }
};
struct Matrix { static Matrix identity; Vector3 position; Vector3 applyToOrigin()const{return position;} };
Matrix Matrix::identity;
struct BoundingBox { void addBounds(Vector3){} void expandSymmetrically(float,float,float){} };
class AABB { public: float distance(Vector3)const{return 0;} };
class ConvexHull { public:bool intersects(Vector3,float)const{return true;} };
namespace Moo {
struct Colour {float r=0,g=0,b=0,a=0;};
struct OmniLight {
    static int alive;
    unsigned char flags=0;
    Vector3 point;
    Colour color;
    float inner=0,outer=0;
    int importance=0;
    OmniLight(){++alive;} ~OmniLight(){--alive;}
    unsigned char stockSourceFlags()const{return flags;}
    void stockSourceFlags(unsigned char v){flags=v;}
    const Vector3& position()const{return point;}
    const Vector3& worldPosition()const{return point;}
    void position(Vector3 v){point=v;}
    const Colour& colour()const{return color;}
    void colour(Colour v){color=v;}
    float innerRadius()const{return inner;}void innerRadius(float v){inner=v;}
    float outerRadius()const{return outer;}void outerRadius(float v){outer=v;}
    int priority()const{return importance;}void priority(int v){importance=v;}
    void worldTransform(const Matrix&){}
};
int OmniLight::alive=0;
using OmniLightPtr=std::shared_ptr<OmniLight>;
class LightContainer { public: std::vector<OmniLightPtr> lights;void addOmni(OmniLightPtr v){lights.push_back(v);} };
namespace PackedColours { const int RED=1; }
}
struct DebugDraw {static void bboxAdd(BoundingBox,int){}};
PyObject* providerType=NULL;
struct Provider {
    PyObjectPtr object;
    explicit Provider(PyObject* p):object(p){}
    void output(Vector4& value)const {
        PyObject* tuple=PyObject_GetAttrString(object.get(),"value");assert(tuple);
        assert(PyArg_ParseTuple(tuple,"ffff",&value.x,&value.y,&value.z,&value.w));Py_DECREF(tuple);
    }
    void matrix(Matrix& value)const {
        Vector4 vector;output(vector);value.position=Vector3(vector.x,vector.y,vector.z);
    }
};
template<int Kind> struct ProviderPtr : std::shared_ptr<Provider> {
    ProviderPtr()=default;
    explicit ProviderPtr(PyObject* value):std::shared_ptr<Provider>(value?new Provider(value):NULL){}
};
using MatrixProviderPtr=ProviderPtr<1>;
namespace Script {
template<int Kind> int setData(PyObject* value,ProviderPtr<Kind>& result,const char* name="") {
    if(value==Py_None){result=ProviderPtr<Kind>();return 0;}
    if(!value || !providerType || !PyObject_TypeCheck(value,reinterpret_cast<PyTypeObject*>(providerType))) {
        PyErr_Format(PyExc_TypeError,"%s must be a provider",name);return -1;
    }
    result=ProviderPtr<Kind>(value);return 0;
}
template<int Kind> PyObject* getData(const ProviderPtr<Kind>& value) {
    PyObject* object=value?value->object.get():Py_None;Py_INCREF(object);return object;
}
int setData(PyObject* value,Vector3& out,const char* = "") {
    return PyArg_ParseTuple(value,"fff",&out.x,&out.y,&out.z)?0:-1;
}
int setData(PyObject* value,Moo::Colour& out,const char* = "") {
    return PyArg_ParseTuple(value,"ffff",&out.r,&out.g,&out.b,&out.a)?0:-1;
}
PyObject* getData(Vector3 v){return Py_BuildValue("(fff)",v.x,v.y,v.z);}
PyObject* getData(Moo::Colour v){return Py_BuildValue("(ffff)",v.r,v.g,v.b,v.a);}
}
// The SDK conversion and Vector4Basic below are real. The generic pointer
// retains Python owners; non-Python scene pointers remain external views.
template<class T>class SmartPointer {
    T* value_=NULL;
    using Python=typename std::is_base_of<PyObject,T>::type;
    static void retain(T* value,std::true_type){Py_XINCREF(static_cast<PyObject*>(value));}
    static void retain(T*,std::false_type){}
    static void release(T* value,std::true_type){Py_XDECREF(static_cast<PyObject*>(value));}
    static void release(T*,std::false_type){}
public:
    SmartPointer()=default;
    SmartPointer(T* value):value_(value){retain(value_,Python());}
    SmartPointer(const SmartPointer& other):SmartPointer(other.value_){}
    SmartPointer& operator=(T* value){retain(value,Python());release(value_,Python());value_=value;return *this;}
    SmartPointer& operator=(const SmartPointer& other){return *this=other.value_;}
    ~SmartPointer(){release(value_,Python());}
    T* get()const{return value_;}T* operator->()const{return value_;}
    explicit operator bool()const{return value_!=NULL;}
};
unsigned nativeProviders=0;
class Vector4Provider : public PyObjectPlusWithVD {
    Py_Header(Vector4Provider,PyObjectPlusWithVD)
public:
    Vector4Provider(bool tick,PyTypeObject* type):PyObjectPlusWithVD(type){assert(!tick);++nativeProviders;}
    virtual ~Vector4Provider(){--nativeProviders;}
    virtual void output(Vector4&)=0;
    static PyObjectPtr coerce(PyObject*);
    PyObject* pyGet_value();
    PY_RO_ATTRIBUTE_SET(value)
};
using Vector4ProviderPtr=SmartPointer<Vector4Provider>;
// Native Math.Vector4 wrappers are outside this host. The SDK tuple parser,
// coercion, reference-counted converter and constant provider execute below.
template<class T>struct PyVector {static bool Check(PyObject*){return false;}T getVector(){return {};}};
namespace Script {
int setData(PyObject*,Vector4&,const char* = "");
int setData(PyObject*,Vector4ProviderPtr&,const char* = "");
PyObject* getData(Vector4 value){return Py_BuildValue("(ffff)",value.x,value.y,value.z,value.w);}
template<class T>PyObject* getData(const SmartPointer<T>& value){
    PyObject* object=value?static_cast<PyObject*>(value.get()):Py_None;Py_INCREF(object);return object;
}
}
@VECTOR4_CONVERSION@
@VECTOR4_BASIC_CLASS@;
@VECTOR4_BASIC_CONSTRUCTOR@
@VECTOR4_COERCE@
@VECTOR4_POINTER_CONVERTER@
@VECTOR4_VALUE_GETTER@
PY_TYPEOBJECT(Vector4Provider)
PY_BEGIN_METHODS(Vector4Provider) PY_END_METHODS()
PY_BEGIN_ATTRIBUTES(Vector4Provider) PY_ATTRIBUTE(value) PY_END_ATTRIBUTES()
PY_TYPEOBJECT(Vector4Basic)
PY_BEGIN_METHODS(Vector4Basic) PY_END_METHODS()
PY_BEGIN_ATTRIBUTES(Vector4Basic) PY_ATTRIBUTE(value) PY_END_ATTRIBUTES()
// This provider's sampling and animation fields are the external data boundary.
// Every light field sees an actual Vector4Provider and uses the SDK converter.
class HostVector4Provider : public Vector4Provider {
    Py_Header(HostVector4Provider,Vector4Provider)
public:
    PyObjectPtr payload;
    explicit HostVector4Provider(PyObject* value):Vector4Provider(false,&s_type_),payload(value){}
    void output(Vector4& value)override{Provider(payload.get()).output(value);}
    PyObject* pyGet_duration(){return PyObject_GetAttrString(payload.get(),"duration");}
    int pySet_duration(PyObject* value){return PyObject_SetAttrString(payload.get(),"duration",value);}
    PyObject* pyGet_keyframes(){return PyObject_GetAttrString(payload.get(),"keyframes");}
    int pySet_keyframes(PyObject* value){return PyObject_SetAttrString(payload.get(),"keyframes",value);}
};
PY_TYPEOBJECT(HostVector4Provider)
PY_BEGIN_METHODS(HostVector4Provider) PY_END_METHODS()
PY_BEGIN_ATTRIBUTES(HostVector4Provider) PY_ATTRIBUTE(duration) PY_ATTRIBUTE(keyframes) PY_END_ATTRIBUTES()
class ClientSpace {public:virtual ~ClientSpace()=default;};
template<class T> class SpacePtr {
    T* value_=NULL;
public:
    SpacePtr()=default; SpacePtr(T* value):value_(value){}
    SpacePtr& operator=(T* value){value_=value;return *this;}
    T* get()const{return value_;} T* operator->()const{return value_;}
    explicit operator bool()const{return value_!=NULL;}
};
using ClientSpacePtr=SmartPointer<ClientSpace>;
class ILightEmbodiment;
std::vector<ILightEmbodiment*> allOwners;
class ILightEmbodiment {
public:
    ILightEmbodiment(){allOwners.push_back(this);}
    virtual ~ILightEmbodiment(){allOwners.erase(std::find(allOwners.begin(),allOwners.end(),this));}
    void updateLocation(){doUpdateLocation();}
protected:virtual void doUpdateLocation()=0;
};
} // BW
@PY_OMNI_HEADER@
namespace BW {
namespace CompiledSpace {
struct LightScene {
    std::vector<ILightEmbodiment*> lights;
    int adds=0,removes=0;
    void addLightEmbodiment(ILightEmbodiment* p){lights.push_back(p);++adds;}
    void removeLightEmbodiment(ILightEmbodiment* p){
        auto i=std::find(lights.begin(),lights.end(),p);assert(i!=lights.end());lights.erase(i);++removes;
    }
};
class CompiledSpace : public ClientSpace {
public:LightScene scene; LightScene& dynamicLightScene(){return scene;}
};
}
CompiledSpace::CompiledSpace camera;
namespace DeprecatedSpaceHelpers {ClientSpacePtr cameraSpace(){return &camera;}}
class SpaceManager {
public:
    static SpaceManager& instance(){static SpaceManager value;return value;}
    IOmniLightEmbodiment* createOmniLightEmbodiment(const PyOmniLight& parent);
};
} // BW
@OMNI_HEADER@
@OMNI_SOURCE@
@PY_OMNI_SOURCE@
namespace BW {
IOmniLightEmbodiment* SpaceManager::createOmniLightEmbodiment(const PyOmniLight& p){return new OmniLightEmbodiment(p);}
} // BW

static PyObject* setProviderType(PyObject*,PyObject* value){BW::providerType=value;Py_RETURN_NONE;}
static PyObject* provider(PyObject*,PyObject* value){return new BW::HostVector4Provider(value);}
static PyObject* payload(PyObject*,PyObject* value){
    if(!BW::HostVector4Provider::Check(value)){PyErr_SetString(PyExc_TypeError,"Expected a sampled host provider");return NULL;}
    auto* result=static_cast<BW::HostVector4Provider*>(value)->payload.get();Py_INCREF(result);return result;
}
static PyObject* providerCount(PyObject*,PyObject*){return PyInt_FromLong(BW::nativeProviders);}
static PyObject* tick(PyObject*,PyObject*) {
    for(auto* owner:BW::allOwners){
        owner->updateLocation();
        static_cast<BW::OmniLightEmbodiment*>(owner)->doTick(.1f);
    }
    Py_RETURN_NONE;
}
static PyObject* state(PyObject*,PyObject*){
    return Py_BuildValue("(iii)",BW::Moo::OmniLight::alive,BW::camera.scene.adds,BW::camera.scene.removes);
}
static PyObject* shadowFlag(PyObject*,PyObject*){
    BW::Moo::LightContainer container;
    for(auto* owner:BW::camera.scene.lights)
        static_cast<BW::OmniLightEmbodiment*>(owner)->doIntersectAndAddToLightContainer(BW::AABB(),container);
    if(container.lights.empty())Py_RETURN_NONE;
    return PyInt_FromLong(container.lights.front()->stockSourceFlags());
}
static PyMethodDef hostMethods[]={
    {"setProviderType",setProviderType,METH_O,NULL},{"tick",tick,METH_NOARGS,NULL},
    {"provider",provider,METH_O,NULL},{"payload",payload,METH_O,NULL},{"providerCount",providerCount,METH_NOARGS,NULL},
    {"state",state,METH_NOARGS,NULL},{"shadowFlag",shadowFlag,METH_NOARGS,NULL},{NULL,NULL,0,NULL}};
static PyMethodDef bigworldMethods[]={
    {"PyOmniLight",BW::PyOmniLight::_pyNew,METH_VARARGS,NULL},
    {"PyChunkLight",BW::PyChunkLight::_pyNew,METH_VARARGS,NULL},{NULL,NULL,0,NULL}};
int main(int argc,char** argv){
    if(argc<2)return 2;
    Py_Initialize();PySys_SetArgv(argc-1,argv+1);
    Py_InitModule("_effect_lights_host",hostMethods);Py_InitModule("BigWorld",bigworldMethods);
    FILE* file=std::fopen(argv[1],"r");assert(file);
    int result=PyRun_SimpleFileEx(file,argv[1],1);
    if(!result)assert(BW::Moo::OmniLight::alive==0 && BW::camera.scene.lights.empty() && BW::nativeProviders==0);
    Py_Finalize();return result?1:0;
}
