#include <Python.h>
#include <cassert>
#include <cmath>
#include <cstring>
#include <memory>
#include <string>
#include <map>
#include <array>
#include "wot/stock_trajectory_drawer.hpp"
#define WOT_STOCK_TRAJECTORY_PORTABLE
#define BW_BEGIN_NAMESPACE
#define BW_END_NAMESPACE
#define PY_MODULE_FUNCTION(...)
#define ERROR_MSG(...) (++errors)
#define FAILED(x) ((x)<0)
using uint32=unsigned;
int errors=0,passes=0,ends=0,draws=0;
bool inPass=false,fvfSet=false;
bool effectOK=true,passOK=true,queryHit=true,creationFails=false;
struct Vector2 {float x=0,y=0;};
struct Vector4 {float x=0,y=0,z=0,w=0;};
struct Vector3 {
    float x=0,y=0,z=0;
    Vector3()=default;Vector3(float a,float b,float c):x(a),y(b),z(c){}
    Vector3 operator+(Vector3 b)const{return {x+b.x,y+b.y,z+b.z};}
    Vector3 operator-(Vector3 b)const{return {x-b.x,y-b.y,z-b.z};}
    Vector3 operator*(float s)const{return {x*s,y*s,z*s};}
    float length()const{return std::sqrt((x*x+y*y)+z*z);}
    void normalise(){const float l=length();if(l){x/=l;y/=l;z/=l;}}
};
struct Matrix {
    Vector3 applyVector(Vector3 v)const{return v;}
    Vector3 applyToOrigin()const{return {0,100,0};}
};
class PyObjectPtr {
    PyObject* value=nullptr;
public:
    enum {STEAL_REFERENCE};
    PyObjectPtr()=default;
    PyObjectPtr(PyObject* v):value(v){Py_XINCREF(value);}
    PyObjectPtr(PyObject* v,int):value(v){}
    PyObjectPtr(const PyObjectPtr& b):value(b.value){Py_XINCREF(value);}
    PyObjectPtr& operator=(const PyObjectPtr& b){Py_XINCREF(b.value);Py_XDECREF(value);value=b.value;return *this;}
    PyObjectPtr& operator=(PyObject* v){Py_XINCREF(v);Py_XDECREF(value);value=v;return *this;}
    ~PyObjectPtr(){Py_XDECREF(value);}
    PyObject* get()const{return value;}
    explicit operator bool()const{return value!=nullptr;}
};
namespace Script {
int setData(PyObject* o,float& v,const char* ="") {
    if(!PyFloat_Check(o)&&!PyInt_Check(o)&&!PyLong_Check(o)){PyErr_SetString(PyExc_TypeError,"expected float");return -1;}
    v=float(PyFloat_AsDouble(o));return PyErr_Occurred()?-1:0;
}
int setData(PyObject* o,bool& v,const char* ="") {
    if(!PyInt_Check(o)){PyErr_SetString(PyExc_TypeError,"expected bool");return -1;}
    v=PyInt_AsLong(o)!=0;return 0;
}
int vector(PyObject* o,float* out,int count) {
    if(!PySequence_Check(o)||PySequence_Size(o)!=count){PyErr_SetString(PyExc_TypeError,"expected vector");return -1;}
    for(int i=0;i<count;++i){PyObjectPtr item(PySequence_GetItem(o,i),PyObjectPtr::STEAL_REFERENCE);if(setData(item.get(),out[i]))return -1;}
    return 0;
}
int setData(PyObject* o,int& v){if(!PyInt_Check(o)){PyErr_SetString(PyExc_TypeError,"expected int");return -1;}v=int(PyInt_AsLong(o));return 0;}
PyObject* getData(float value){return PyFloat_FromDouble(value);}
int setData(PyObject* o,Vector2& v){return vector(o,&v.x,2);}
int setData(PyObject* o,Vector3& v){return vector(o,&v.x,3);}
int setData(PyObject* o,Vector4& v){return vector(o,&v.x,4);}
PyObject* getData(bool value){return PyBool_FromLong(value);}
PyObject* getData(Vector3 v){return Py_BuildValue("(fff)",v.x,v.y,v.z);}
}
struct D3DXMATRIX {template<class...T> D3DXMATRIX(T...) {}};
struct D3DXVECTOR4 {float x,y,z,w;D3DXVECTOR4(float a,float b,float c,float d):x(a),y(b),z(c),w(d){}};
struct ID3DXEffect {
    std::map<std::string,float> parameters;
    int SetMatrix(const char* name,const D3DXMATRIX*){parameters[name]=1;return effectOK?0:-1;}
    int SetVector(const char* name,const D3DXVECTOR4*){parameters[name]=1;return effectOK?0:-1;}
    int SetBool(const char* name,bool v){parameters[name]=v;return effectOK?0:-1;}
    int SetInt(const char* name,int v){parameters[name]=float(v);return effectOK?0:-1;}
    int SetFloat(const char* name,float v){parameters[name]=v;return effectOK?0:-1;}
    int SetTexture(const char* name,void*){parameters[name]=0;return effectOK?0:-1;}
};
#define D3DFVF_XYZ 2
#define D3DFVF_DIFFUSE 64
#define D3DPT_LINELIST 2
namespace Moo {
class DeviceCallback {public:DeviceCallback(){if(creationFails)throw std::bad_alloc();}virtual ~DeviceCallback()=default;virtual void createManagedObjects(){}virtual void deleteManagedObjects(){}};
struct ManagedEffect {ID3DXEffect definition,binding;ID3DXEffect* selected=&definition;ID3DXEffect* pEffect(){return selected;}};
struct EffectMaterial {
    std::shared_ptr<ManagedEffect> effect=std::make_shared<ManagedEffect>();
    struct Properties {
        bool select(ManagedEffect& effect){effect.selected=&effect.binding;return effectOK;}
    } props;
    bool setTechnique(const char* name){assert(!strcmp(name,"standard"));return effectOK;}
    Properties& properties(){return props;}
    std::shared_ptr<ManagedEffect>& pEffect(){return effect;}
    bool begin(){assert(effect->selected==&effect->binding);return effectOK;}
    unsigned numPasses(){return 2;}
    bool beginPass(unsigned){++passes;inPass=passOK;fvfSet=false;return passOK;}
    bool endPass(){++ends;assert(inPass);inPass=false;return true;}
    bool end(){return true;}
};
using EffectMaterialPtr=std::shared_ptr<EffectMaterial>;
bool createEffect(EffectMaterialPtr& material,const char* name){assert(!strcmp(name,"system/materials/geometrics.fx"));material=std::make_shared<EffectMaterial>();return true;}
struct Camera {Vector3 nearPlanePoint(float,float)const{return {0,-1,1};}};
struct RenderContext {
    Matrix inverse;Camera cam;
    Matrix& invView(){return inverse;}Camera& camera(){return cam;}
    void setFVF(unsigned fvf){assert(inPass&&fvf==66);fvfSet=true;}
    int drawPrimitiveUP(unsigned primitive,unsigned count,const void* vertices,unsigned stride){
        assert(inPass&&fvfSet&&primitive==2&&count==3&&vertices&&stride==16);++draws;return 0;
    }
};
RenderContext& rc(){static RenderContext value;return value;}
}
struct AABB {Vector3 minBounds()const{return {-1000,-100,-1000};}Vector3 maxBounds()const{return {1000,100,1000};}};
struct ClientSpace {AABB bounds()const{return {};}};
using ClientSpacePtr=std::shared_ptr<ClientSpace>;
namespace DeprecatedSpaceHelpers {ClientSpacePtr cameraSpace(){static auto value=std::make_shared<ClientSpace>();return value;}}
float stockGunMarkerSegmentCollision(const Vector3& a,const Vector3& b){
    if(queryHit&&a.y>0&&b.y<=0)return (b-a).length()*a.y/(a.y-b.y);
    return -1;
}
namespace WorldTriangle {using Flags=unsigned short;}
float stockGunMarkerSegmentCollision(const Vector3& a,const Vector3& b,WorldTriangle::Flags skip,WorldTriangle::Flags required){return required || (skip&1)?-1:stockGunMarkerSegmentCollision(a,b);}
#include "stock_trajectory_drawer.cpp"
PyObject* fixture(PyObject*,PyObject* args) {
    const char* action;if(!PyArg_ParseTuple(args,"s",&action))return NULL;
    if(!strcmp(action,"init")){if(!initialiseStockTrajectoryDrawer())return NULL;}
    else if(!strcmp(action,"fail_init")){creationFails=true;bool result=initialiseStockTrajectoryDrawer();creationFails=false;if(!result)return NULL;}
    else if(!strcmp(action,"fini"))finaliseStockTrajectoryDrawer();
    else if(!strcmp(action,"tick"))tickStockTrajectoryDrawer(.1f);
    else if(!strcmp(action,"draw")){if(!drawStockTrajectoryDrawer()){PyErr_SetString(PyExc_RuntimeError,"draw failed");return NULL;}}
    else if(!strcmp(action,"fail"))effectOK=false;
    else if(!strcmp(action,"reset")){owner->deleteManagedObjects();owner->createManagedObjects();}
    else if(!strcmp(action,"miss"))queryHit=false;
    else if(!strcmp(action,"callback")){Py_INCREF(owner->callback.get());return owner->callback.get();}
    else if(!strcmp(action,"inspect")){
        auto& s=owner->state;return Py_BuildValue("(ffffiii)",s.current.first,s.current.second,s.current.firstHeight,s.current.secondHeight,draws,passes,ends);
    }
    Py_RETURN_NONE;
}
PyObject* drawerGetter(PyObject*,PyObject* args){return py_wg_trajectory_drawer(args);}
PyObject* simulateGetter(PyObject*,PyObject* args){return py_wg_simulateProjectileTrajectory(args);}
int main(int argc,char** argv) {
    assert(argc==2);Py_Initialize();
    PyMethodDef methods[]={{"wg_trajectory_drawer",drawerGetter,METH_VARARGS,NULL},{"fixture",fixture,METH_VARARGS,NULL},{"wg_simulateProjectileTrajectory",simulateGetter,METH_VARARGS,NULL},{NULL,NULL,0,NULL}};
    assert(Py_InitModule("BigWorld",methods));
    FILE* script=fopen(argv[1],"r");assert(script);const int result=PyRun_SimpleFile(script,argv[1]);fclose(script);
    finaliseStockTrajectoryDrawer();assert(!PyErr_Occurred());Py_Finalize();return result?1:0;
}
