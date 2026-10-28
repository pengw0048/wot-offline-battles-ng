#include <Python.h>
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <memory>
#include <map>
#include <string>
#include <vector>
#define WOT_STOCK_WATER_RIPPLES_PORTABLE
#define BW_BEGIN_NAMESPACE
#define BW_END_NAMESPACE
#define PY_MODULE_FUNCTION(...)
#define BW_GUARD
#define INLINE
#define MF_ASSERT(x) assert(x)
#define D3DRS_COLORWRITEENABLE 1
#define D3DCOLORWRITEENABLE_ALPHA 8
#define D3DCOLORWRITEENABLE_RED 1
#define D3DCOLORWRITEENABLE_GREEN 2
#define D3DCOLORWRITEENABLE_BLUE 4
#define D3DPT_TRIANGLEFAN 6
#define SIM_BORDER_SIZE 3
using uint32=unsigned;using uint=unsigned;
namespace BW {using string=std::string;}
struct Vector3 {
    float x=0,y=0,z=0;
    Vector3()=default;Vector3(float a,float b,float c):x(a),y(b),z(c){}
    Vector3 operator+(Vector3 b)const{return {x+b.x,y+b.y,z+b.z};}
    Vector3 operator-(Vector3 b)const{return {x-b.x,y-b.y,z-b.z};}
    Vector3 operator*(float s)const{return {x*s,y*s,z*s};}
    Vector3& operator+=(Vector3 b){x+=b.x;y+=b.y;z+=b.z;return *this;}
    float length()const{return std::sqrt((x*x+y*y)+z*z);}
};
struct Vector4 {float x=0,y=0,z=0,w=0;};
struct Matrix {
    Vector3 scale{1,1,1},translation;
    Vector3 applyPoint(Vector3 p)const{return p-translation;}
    void setScale(float a,float b,float c){scale={a,b,c};translation={0,0,0};}
    void postTranslateBy(Vector3 p){translation+=p;}
    Vector4 row(int)const{return {translation.x,translation.y,translation.z,1};}
};
struct BoundingBox {
    Vector3 lo{-50,-10,-40},hi{50,0,40};
    bool clip(Vector3& a,Vector3& b)const {
        float t0=0,t1=1;const Vector3 delta=b-a;
        const float av[]={a.x,a.y,a.z},dv[]={delta.x,delta.y,delta.z};
        const float mn[]={lo.x,lo.y,lo.z},mx[]={hi.x,hi.y,hi.z};
        for(int i=0;i<3;++i){if(!dv[i]){if(av[i]<mn[i]||av[i]>mx[i])return false;continue;}
            float near=(mn[i]-av[i])/dv[i],far=(mx[i]-av[i])/dv[i];if(near>far)std::swap(near,far);
            t0=std::max(t0,near);t1=std::min(t1,far);if(t0>t1)return false;}
        b=a+delta*t1;a=a+delta*t0;return true;
    }
};
namespace Math {template<class T>T clamp(T a,T v,T b){return std::max(a,std::min(v,b));}}
using MovementPair=std::pair<Vector3,Vector3>;
using Movement=std::pair<MovementPair,float>;
using Movements=std::vector<Movement>;
struct Simulation {
    int size_=256;float idleTime_=999;Movements movements_;unsigned lastMovementFrame_=~0u;
    void addMovement(const Vector3&,const Vector3&,float);void updateMovements();
    const Movements& movements()const;void resetIdleTimer();int size()const;void clearMovements();
};
struct Snapshot {std::string technique;float strength=0;Matrix matrix;};
std::vector<Snapshot> draws;
bool materialAvailable=true;int selections=0,propagations=0;
struct Effect {
    bool mutableBinding=false;Snapshot snapshot;
    int SetInt(const char*,int){assert(!"Removed #1513 int selector");return -1;}
    int SetBool(const char*,bool){assert(mutableBinding);return 0;}
    int SetFloat(const char* name,float value){assert(mutableBinding);if(!strcmp(name,"impactStrength"))snapshot.strength=value;return 0;}
    int SetMatrix(const char* name,const Matrix* value){assert(mutableBinding&&!strcmp(name,"WorldViewProj"));snapshot.matrix=*value;return 0;}
    int CommitChanges(){assert(mutableBinding);return 0;}
};
template<class T>using ComObjectWrap=T*;
namespace Moo {
struct ManagedEffect {Effect definition,binding;Effect* active=&definition;ManagedEffect(){binding.mutableBinding=true;}Effect* pEffect(){return active;}};
struct EffectMaterial {
    std::shared_ptr<ManagedEffect> managed=std::make_shared<ManagedEffect>();std::string technique="WaterSimulation";
    struct Properties {bool select(ManagedEffect& m){++selections;if(!materialAvailable)return false;m.active=&m.binding;return true;}} properties_;
    Properties& properties(){return properties_;}std::shared_ptr<ManagedEffect>& pEffect(){return managed;}
    bool hTechnique(const char* name){technique=name;return true;}
    void begin(){assert(properties_.select(*managed));managed->binding.snapshot.technique=technique;}
    void beginPass(int){}void endPass(){}void end(){managed->active=&managed->definition;}
};
using EffectMaterialPtr=std::shared_ptr<EffectMaterial>;
EffectMaterialPtr currentEffect;
struct RenderContext {
    unsigned frame=1;Matrix camera;
    RenderContext(){camera.translation={0,30,0};}
    Matrix& invView(){return camera;}
    bool frameDrawn(unsigned& mark){bool same=mark==frame;mark=frame;return same;}
    void setRenderState(int,int){}
    void drawPrimitive(int primitive,int,int count){assert(primitive==6&&count==2);auto* effect=currentEffect->pEffect()->pEffect();assert(effect->mutableBinding);draws.push_back(effect->snapshot);}
};
RenderContext& rc(){static RenderContext context;return context;}
}
struct Block {bool push(){return true;}void pop(){}};
struct Waters;
struct SimulationManager {
    static SimulationManager& instance(){static SimulationManager manager;return manager;}
    void drawMovements(Moo::EffectMaterialPtr,Simulation*);
    void setupSim(Block*,ComObjectWrap<Effect>& effect,float){assert(effect->mutableBinding);}
    void simulateBlock(Moo::EffectMaterialPtr effect,Block*,int){assert(effect->technique=="WaterSimulation");++propagations;effect->begin();effect->end();}
};
struct SimulationCell:Simulation {
    Block block;Block* simulationTarget_=&block;bool active=true;int edge_=0;
    void simulate(Moo::EffectMaterialPtr,float,Waters&);
    void updateTime(float){}bool isActive()const{return active;}bool edgeActivation()const{return false;}
    void bindNeighbour(Moo::EffectMaterialPtr,int){}bool perturbed()const{return !movements_.empty();}
};
struct WaterCell:SimulationCell {using WaterCellVector=std::vector<WaterCell>;};
struct Water {
    bool initialised_=true,visible_=true;
    struct State {struct {float x=100,y=80;}size_;float simCellSize_=50,tessellation_=1;}state_;
    Matrix invTransform_;BoundingBox bbActual_;WaterCell::WaterCellVector cells_=WaterCell::WaterCellVector(4);
    static float s_maxSimDistance_;
    Water(){invTransform_.translation={0,20,0};}
    bool visible()const{return visible_;}
    bool addMovement(const Vector3&,const Vector3&,float);
};
float Water::s_maxSimDistance_=200;
struct Waters:std::vector<Water*> {
    static bool s_highQuality_;static Waters& instance(){static Waters list;return list;}
    static bool highQualityMode(){return s_highQuality_;}float movementImpact()const{return 20;}
    void addMovement(const Vector3&,const Vector3&,float);
};
bool Waters::s_highQuality_=false;
@SIMULATION_METHODS@
@WATER_ADD@
@WATERS_ADD@
@DRAW_MOVEMENTS@
@SIMULATE_CELL@
namespace Script {
int setData(PyObject* o,float& value,const char*="") {if(!PyFloat_Check(o)&&!PyInt_Check(o)){PyErr_SetString(PyExc_TypeError,"expected float");return -1;}value=float(PyFloat_AsDouble(o));return PyErr_Occurred()?-1:0;}
int setData(PyObject* o,Vector3& value,const char*="") {
    if(!PySequence_Check(o)||PySequence_Size(o)!=3){PyErr_SetString(PyExc_TypeError,"expected Vector3");return -1;}
    float* values=&value.x;for(int i=0;i<3;++i){PyObject* item=PySequence_GetItem(o,i);int result=setData(item,values[i]);Py_DECREF(item);if(result)return -1;}return 0;
}
}
#include "stock_water_ripples.cpp"
Water first,second;
PyObject* fixture(PyObject*,PyObject* args){
    const char* action;int mode=0;if(!PyArg_ParseTuple(args,"s|i",&action,&mode))return NULL;
    if(!strcmp(action,"clear")){for(Water* water:Waters::instance())for(auto& cell:water->cells_)cell.clearMovements();draws.clear();}
    else if(!strcmp(action,"frame"))++Moo::rc().frame;
    else if(!strcmp(action,"mode")){first.initialised_=mode!=1;first.visible_=mode!=2;materialAvailable=mode!=3;}
    else if(!strcmp(action,"one"))Waters::instance().resize(1);
    else if(!strcmp(action,"remove"))Waters::instance().clear();
    else if(!strcmp(action,"details")){
        PyObject* result=PyList_New(0);
        for(unsigned i=0;i<first.cells_.size();++i)for(const auto& movement:first.cells_[i].movements()){
            const Vector3 a=movement.first.first,b=movement.first.second;
            PyObject* row=Py_BuildValue("(ifffffff)",int(i),a.x,a.y,a.z,b.x,b.y,b.z,movement.second);
            PyList_Append(result,row);Py_DECREF(row);
        }
        return result;
    }
    else if(!strcmp(action,"render")){for(Water* water:Waters::instance())for(auto& cell:water->cells_)cell.simulate(Moo::currentEffect,1.f/30.f,Waters::instance());}
    else if(!strcmp(action,"inspect")){
        unsigned count=0;for(Water* water:Waters::instance())for(auto& cell:water->cells_)count+=unsigned(cell.movements().size());
        return Py_BuildValue("(iif)",count,int(draws.size()),draws.empty()?0:draws.back().strength);
    }
    Py_RETURN_NONE;
}
PyObject* binding(PyObject*,PyObject* args){return py_wg_addWaterRipples(args);}
int main(int argc,char** argv){
    assert(argc==2);Waters::instance().push_back(&first);Waters::instance().push_back(&second);
    Moo::currentEffect=std::make_shared<Moo::EffectMaterial>();Py_Initialize();
    PyMethodDef methods[]={{"wg_addWaterRipples",binding,METH_VARARGS,NULL},{"fixture",fixture,METH_VARARGS,NULL},{NULL,NULL,0,NULL}};
    assert(Py_InitModule("BigWorld",methods));FILE* script=fopen(argv[1],"r");assert(script);
    int result=PyRun_SimpleFile(script,argv[1]);fclose(script);Py_Finalize();return result?1:0;
}
