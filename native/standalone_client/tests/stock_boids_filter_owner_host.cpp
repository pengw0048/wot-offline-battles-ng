// Unchanged patched SDK class/cache/copy/updateModel bodies are inserted below.
// The embedding object model and final embodiment transform are boundaries;
// they do not replace the BoidsFilter's configuration or steering consumer.
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-register"
#endif
#include <Python.h>
#ifdef __clang__
#pragma clang diagnostic pop
#endif
#include "stock_boids_filter.hpp"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <memory>
#include <fstream>
#include <cstdlib>
#include <ctime>
#include <vector>
#define CSTDMF_DLL
#define BW_GUARD
#define MF_VERIFY(x) assert(x)
#define MF_ASSERT(x) assert(x)
#define IF_NOT_MF_ASSERT_DEV(x) if(!(x))
#define ERROR_MSG(...) ((void)0)
#define WARNING_MSG(...) ((void)0)
#define BW_STATIC_ASSERT(x,m) static_assert(x,#m)
#define Py_Header(a,b) public:static PyTypeObject s_type_;
#define PY_AUTO_CONSTRUCTOR_FACTORY_DECLARE(...)
#define PY_RW_ACCESSOR_ATTRIBUTE_DECLARE(...)
#define PY_SCRIPT_CONVERTERS_DECLARE(...)
#define private public
#define protected public
using uint=unsigned;using uint32=std::uint32_t;
namespace BW {
template<class T>using vector=std::vector<T>;
struct Vector3 {float x=0,y=0,z=0;Vector3()=default;Vector3(float a,float b,float c):x(a),y(b),z(c){}float& operator[](int i){return (&x)[i];}Vector3 operator+(const Vector3& v)const{return {x+v.x,y+v.y,z+v.z};}Vector3 operator-(const Vector3& v)const{return {x-v.x,y-v.y,z-v.z};}Vector3 operator*(float v)const{return {x*v,y*v,z*v};}Vector3& operator+=(const Vector3& v){return *this=*this+v;}Vector3& operator-=(const Vector3& v){return *this=*this-v;}Vector3& operator*=(float v){return *this=*this*v;}Vector3& operator/=(float v){return *this=*this*(1/v);}bool operator==(const Vector3& v)const{return x==v.x&&y==v.y&&z==v.z;}static Vector3 zero(){return {};}float lengthSquared()const{return (x*x+y*y)+z*z;}float length()const{return std::sqrt(lengthSquared());}void normalise(){float n=length();if(n){x/=n;y/=n;z/=n;}}float dotProduct(const Vector3& v)const{return x*v.x+y*v.y+z*v.z;}Vector3 crossProduct(const Vector3& v)const{return {y*v.z-z*v.y,z*v.x-x*v.z,x*v.y-y*v.x};}};Vector3 operator*(float f,const Vector3& v){return v*f;}
void D3DXVec3Normalize(Vector3* d,const Vector3* s){*d=*s;d->normalise();}
static const int Z_AXIS=2;
float unitRand(){return .5f;}
struct Matrix {float v[16];Matrix(){for(unsigned i=0;i<16;++i)v[i]=i%5==0?1.f:0.f;}void setTranslate(const Vector3& p){*this=Matrix();v[12]=p.x;v[13]=p.y;v[14]=p.z;}void preRotate(unsigned axis,float angle){Matrix rotation;float c=std::cos(angle),t=std::sin(angle);if(axis==0){rotation.v[5]=rotation.v[10]=c;rotation.v[6]=t;rotation.v[9]=-t;}else if(axis==1){rotation.v[0]=rotation.v[10]=c;rotation.v[2]=-t;rotation.v[8]=t;}else{rotation.v[0]=rotation.v[5]=c;rotation.v[1]=t;rotation.v[4]=-t;}Matrix output;for(unsigned r=0;r<4;++r)for(unsigned c0=0;c0<4;++c0){float sum=0;for(unsigned k=0;k<4;++k)sum+=rotation.v[r*4+k]*v[k*4+c0];output.v[r*4+c0]=sum;}*this=output;}void preRotateY(float a){preRotate(1,a);}void preRotateX(float a){preRotate(0,a);}void preRotateZ(float a){preRotate(2,a);}Vector3 applyToUnitAxisVector(int i)const{return {v[i*4],v[i*4+1],v[i*4+2]};}Vector3 applyToOrigin()const{return {v[12],v[13],v[14]};}};
void D3DXMatrixTranslation(Matrix* d,float x,float y,float z){d->setTranslate({x,y,z});}
void D3DXMatrixRotationY(Matrix* d,float a){*d=Matrix();d->preRotateY(a);}void D3DXMatrixRotationX(Matrix* d,float a){*d=Matrix();d->preRotateX(a);}void D3DXMatrixRotationZ(Matrix* d,float a){*d=Matrix();d->preRotateZ(a);}
void D3DXMatrixMultiply(Matrix* d,const Matrix* a,const Matrix* b){Matrix out;for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c){float total=0;for(unsigned k=0;k<4;++k)total+=a->v[r*4+k]*b->v[k*4+c];out.v[r*4+c]=total;}*d=out;}
struct IEntityEmbodiment {unsigned draws=0;Matrix world;void worldTransform(const Matrix& v){world=v;++draws;}const Matrix& worldTransform()const{return world;}};
struct ConnectionControl {static ConnectionControl& instance(){static ConnectionControl v;return v;}void* pReplayConnection(){return nullptr;}};
class PyBoidsFilter;class MovementFilterTarget{};
using IEntityEmbodimentPtr=std::shared_ptr<IEntityEmbodiment>;using EntityEmbodiments=std::vector<IEntityEmbodimentPtr>;
struct ScriptErrorPrint{};struct Entity:MovementFilterTarget {Vector3 goal;EntityEmbodiments models;unsigned landed=0;const Vector3& position(){return goal;}EntityEmbodiments& auxiliaryEmbodiments(){return models;}bool isDestroyed(){return false;}Entity& pPyEntity(){return *this;}void callMethod(const char* name,ScriptErrorPrint,bool){assert(std::string(name)=="boidsLanded");++landed;models.clear();}};
struct DogWatch{explicit DogWatch(const char*){}void start(){}void stop(){}};
struct MovementFilter {virtual~MovementFilter()=default;};
struct AvatarFilter:MovementFilter {explicit AvatarFilter(PyBoidsFilter*){}virtual void output(double,MovementFilterTarget&){}virtual bool tryCopyState(const MovementFilter&){return true;}};
struct PyFilter {MovementFilter* attached=nullptr;virtual~PyFilter()=default;MovementFilter* pAttachedFilter(){return attached;}const MovementFilter* pAttachedFilter()const{return attached;}};
struct PyAvatarFilter:PyFilter {explicit PyAvatarFilter(PyTypeObject*){}virtual MovementFilter* getNewFilter(){return nullptr;}virtual void onLosingAttachedFilter(){}};
@CLASSES@
PyTypeObject PyBoidsFilter::s_type_={PyObject_HEAD_INIT(NULL) 0};
@SDK_OWNER_BODIES@

namespace Script {int setData(PyObject*,double&,const char* = "");int setData(PyObject*,float&,const char* = "");int setData(PyObject*,Vector3&,const char* = "");}
template<class T>struct PyVector{static bool Check(PyObject*){return false;}T getVector(){return {};}};
@CONVERTERS@
}
#undef private
#undef protected
using namespace BW;
struct FilterHost {PyObject_HEAD PyBoidsFilter* owner;};
static PyTypeObject filterType={PyObject_HEAD_INIT(NULL) 0};
PyObject* factory(PyObject*,PyObject* args){if(!PyArg_ParseTuple(args,""))return nullptr;auto* p=PyObject_New(FilterHost,&filterType);if(!p)return nullptr;p->owner=new PyBoidsFilter;return reinterpret_cast<PyObject*>(p);}
void destroy(PyObject* object){auto* p=reinterpret_cast<FilterHost*>(object);delete p->owner->attached;delete p->owner;PyObject_Del(p);}
PyObject* getRate(PyObject* o,void*){auto v=reinterpret_cast<FilterHost*>(o)->owner->yprSpeed();return Py_BuildValue("(fff)",v.x,v.y,v.z);}int setRate(PyObject* o,PyObject* value,void*){Vector3 v;if(!value||Script::setData(value,v,"yprSpeed"))return -1;reinterpret_cast<FilterHost*>(o)->owner->yprSpeed(v);return 0;}
PyObject* getCentre(PyObject* o,void*){auto v=reinterpret_cast<FilterHost*>(o)->owner->deadZonePosition();return Py_BuildValue("(fff)",v.x,v.y,v.z);}int setCentre(PyObject* o,PyObject* value,void*){Vector3 v;if(!value||Script::setData(value,v,"deadZonePosition"))return -1;reinterpret_cast<FilterHost*>(o)->owner->deadZonePosition(v);return 0;}
PyObject* getRadius(PyObject* o,void*){return PyFloat_FromDouble(reinterpret_cast<FilterHost*>(o)->owner->deadZoneRadius());}int setRadius(PyObject* o,PyObject* value,void*){float v;if(!value||Script::setData(value,v,"deadZoneRadius"))return -1;reinterpret_cast<FilterHost*>(o)->owner->deadZoneRadius(v);return 0;}
PyObject* getSpeed(PyObject* o,void*){return PyFloat_FromDouble(reinterpret_cast<FilterHost*>(o)->owner->normalSpeed());}int setSpeed(PyObject* o,PyObject* value,void*){float v;if(!value||Script::setData(value,v,"speed"))return -1;reinterpret_cast<FilterHost*>(o)->owner->normalSpeed(v);return 0;}
PyObject* attach(PyObject*,PyObject* object){auto* owner=reinterpret_cast<FilterHost*>(object)->owner;assert(!owner->attached);owner->attached=owner->getNewFilter();Py_RETURN_NONE;}
PyObject* checkOwner(PyObject*,PyObject* object){auto* owner=reinterpret_cast<FilterHost*>(object)->owner;assert(owner->attached);auto* filter=owner->pAttachedFilter();assert(filter->yprSpeed()==owner->yprSpeed()&&filter->deadZonePosition()==owner->deadZonePosition()&&filter->deadZoneRadius()==owner->deadZoneRadius());BoidsFilter copy(owner);assert(copy.tryCopyState(*filter));assert(copy.yprSpeed()==filter->yprSpeed()&&copy.deadZonePosition()==filter->deadZonePosition()&&copy.deadZoneRadius()==filter->deadZoneRadius());filter->yprSpeed({.1f,.2f,.3f});filter->deadZonePosition({40,50,60});filter->deadZoneRadius(7);owner->onLosingAttachedFilter();owner->attached=nullptr;delete filter;assert(owner->yprSpeed()==Vector3(.1f,.2f,.3f)&&owner->deadZonePosition()==Vector3(40,50,60)&&owner->deadZoneRadius()==7);owner->attached=owner->getNewFilter();filter=owner->pAttachedFilter();assert(filter->deadZoneRadius()==7&&filter->deadZonePosition()==Vector3(40,50,60));Py_RETURN_NONE;}
PyObject* tickOwner(PyObject*,PyObject* object){auto* owner=reinterpret_cast<FilterHost*>(object)->owner;auto* filter=owner->pAttachedFilter();assert(filter);filter->boidData_.resize(1);auto& boid=filter->boidData_[0];boid.pos_={0,0,0};boid.dir_={0,0,1};boid.yaw_=boid.pitch_=boid.roll_=boid.dYaw_=0;boid.speed_=1;IEntityEmbodiment model;boid.updateModel({10,20,30},&model,*filter,.05f,30.f,0,1000);assert(model.draws==1&&boid.pos_.length()>0);return Py_BuildValue("(fff)",boid.yaw_,boid.pitch_,boid.roll_);}
PyObject* checkOutput(PyObject*,PyObject*){
    const char* randomPath=std::getenv("WOT_BOIDS_RANDOM_FIXTURE");if(randomPath&&*randomPath){std::ifstream file(randomPath,std::ios::binary);BWRandom generator(1513u);for(unsigned i=0;i<64;++i){uint32 expected;file.read(reinterpret_cast<char*>(&expected),4);assert(file&&generator()==expected);}}
    const char* path=std::getenv("WOT_BOIDS_OUTER_FIXTURE");
    if(path&&*path){std::ifstream file(path,std::ios::binary);unsigned count;file.read(reinterpret_cast<char*>(&count),4);assert(file);for(unsigned i=0;i<count;++i){float row[30];file.read(reinterpret_cast<char*>(row),sizeof(row));assert(file);PyBoidsFilter owner;std::unique_ptr<BoidsFilter> filter(owner.getNewFilter());Entity entity;entity.goal={row[16],row[17],row[18]};auto model=std::make_shared<IEntityEmbodiment>();std::copy(row,row+16,model->world.v);entity.models.push_back(model);filter->prevTime_=0;filter->output(0,entity);assert(filter->boidData_.size()==1&&model->draws==0);auto& boid=filter->boidData_[0];float actual[]={boid.pos_.x,boid.pos_.y,boid.pos_.z,boid.yaw_,boid.pitch_,boid.roll_};unsigned indices[]={19,20,21,25,26,27};for(unsigned j=0;j<6;++j)assert(std::fabs(actual[j]-row[indices[j]])<2e-6f);}}
    const char* modelPath=std::getenv("WOT_BOIDS_MODEL_FIXTURE");
    if(modelPath&&*modelPath){std::ifstream file(modelPath,std::ios::binary);unsigned count;file.read(reinterpret_cast<char*>(&count),4);for(unsigned i=0;i<count;++i){float r[52];file.read(reinterpret_cast<char*>(r),sizeof(r));assert(file);PyBoidsFilter owner;std::unique_ptr<BoidsFilter> filter(owner.getNewFilter());filter->boidData_.resize(1);auto& boid=filter->boidData_[0];boid.yaw_=r[0];boid.pitch_=r[1];boid.roll_=r[2];boid.dYaw_=r[3];boid.speed_=r[4];boid.pos_={r[5],r[6],r[7]};boid.dir_={r[8],r[9],r[10]};filter->yprSpeed_={r[11],r[12],r[13]};filter->deadZonePosition_={r[14],r[15],r[16]};filter->deadZoneRadius_=r[17];filter->normalSpeed_=r[18];filter->pitchToSpeedRatio_=r[19];IEntityEmbodiment model;boid.updateModel({r[22],r[23],r[24]},&model,*filter,r[20],30.f,int(r[21]),1000.f);assert(model.draws==1);float actual[]={boid.pos_.x,boid.pos_.y,boid.pos_.z,boid.dir_.x,boid.dir_.y,boid.dir_.z,boid.yaw_,boid.pitch_,boid.roll_,boid.dYaw_,boid.speed_};for(unsigned j=0;j<27;++j){float value=j<11?actual[j]:model.world.v[j-11],expected=r[25+j];if(std::fabs(value-expected)>4e-6f*(std::max)(1.f,std::fabs(expected)))std::fprintf(stderr,"model case=%u field=%u actual=%g expected=%g\n",i,j,value,expected);assert(std::isnan(value)==std::isnan(expected));if(!std::isnan(value))assert(std::fabs(value-expected)<=4e-6f*(std::max)(1.f,std::fabs(expected)));}}}
    PyBoidsFilter owner;std::unique_ptr<BoidsFilter> filter(owner.getNewFilter());Entity entity;entity.goal={10,-200,30};auto model=std::make_shared<IEntityEmbodiment>();model->world.setTranslate({2,3,4});entity.models.push_back(model);filter->prevTime_=0;filter->output(0,entity);assert(filter->boidData_[0].pos_==Vector3(12,-197,34));filter->output(.25,entity);assert(model->draws==3);filter->output(.25,entity);assert(model->draws==3);filter->output(20.25,entity);assert(model->draws==3);filter->output(20.35,entity);assert(model->draws==4);filter->state_=1;filter->boidData_[0].pos_=entity.goal+Vector3(.00001f,0,0);filter->boidData_[0].dir_={0,0,1};filter->output(20.40,entity);assert(entity.landed==1&&entity.models.empty());filter->output(20.45,entity);assert(filter->boidData_.empty());Py_RETURN_NONE;
}
int main(int argc,char** argv){assert(argc==2);Py_Initialize();filterType.tp_name="BoidsFilter";filterType.tp_basicsize=sizeof(FilterHost);filterType.tp_flags=Py_TPFLAGS_DEFAULT;filterType.tp_dealloc=destroy;static PyGetSetDef attrs[]={{const_cast<char*>("yprSpeed"),getRate,setRate,nullptr,nullptr},{const_cast<char*>("deadZonePosition"),getCentre,setCentre,nullptr,nullptr},{const_cast<char*>("deadZoneRadius"),getRadius,setRadius,nullptr,nullptr},{const_cast<char*>("speed"),getSpeed,setSpeed,nullptr,nullptr},{nullptr,nullptr,nullptr,nullptr,nullptr}};filterType.tp_getset=attrs;assert(PyType_Ready(&filterType)==0);static PyMethodDef methods[]={{const_cast<char*>("BoidsFilter"),factory,METH_VARARGS,nullptr},{nullptr,nullptr,0,nullptr}};assert(Py_InitModule("BigWorld",methods));static PyMethodDef checks[]={{const_cast<char*>("attach"),attach,METH_O,nullptr},{const_cast<char*>("checkOwner"),checkOwner,METH_O,nullptr},{const_cast<char*>("tick"),tickOwner,METH_O,nullptr},{const_cast<char*>("checkOutput"),checkOutput,METH_NOARGS,nullptr},{nullptr,nullptr,0,nullptr}};assert(Py_InitModule("_boids",checks));FILE* f=fopen(argv[1],"rb");assert(f);int status=PyRun_SimpleFile(f,argv[1]);fclose(f);Py_Finalize();return status?1:0;}
