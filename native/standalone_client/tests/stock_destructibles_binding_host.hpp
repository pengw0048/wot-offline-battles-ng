// CPython/module/space boundaries for the complete production WGDE owner.
#include <array>
#include <functional>
#include <map>
#include <memory>
#include <algorithm>
#include <cstdint>
#include "stock_space_sounds_data.hpp"
#define ERROR_MSG(...) std::fprintf(stderr,__VA_ARGS__)
namespace BW {
template<class K,class V>using map=std::map<K,V>;
using uint=unsigned;using uint32=std::uint32_t;using uint64=std::uint64_t;using int64=std::int64_t;using SpaceID=int;
struct Vector3 {float x,y,z;Vector3(float a,float b,float c):x(a),y(b),z(c){}Vector3 operator+(const Vector3& b)const{return Vector3(x+b.x,y+b.y,z+b.z);}};
struct Matrix {
 float v[16];Matrix(){setZero();v[0]=v[5]=v[10]=v[15]=1.f;}
 void setZero(){std::memset(v,0,sizeof(v));}
 Vector3 applyToOrigin()const{return Vector3(v[12],v[13],v[14]);}
 void setTranslate(const Vector3& p){*this=Matrix();v[12]=p.x;v[13]=p.y;v[14]=p.z;}
};
Matrix* D3DXMatrixInverse(Matrix* out,void*,const Matrix* in){*out=*in;for(int n=12;n<15;++n)out->v[n]=-in->v[n];return out;}
Matrix* D3DXMatrixMultiply(Matrix* out,const Matrix* a,const Matrix* b){Matrix result;for(int r=0;r<4;++r)for(int c=0;c<4;++c){result.v[4*r+c]=0;for(int k=0;k<4;++k)result.v[4*r+c]+=a->v[4*r+k]*b->v[4*k+c];}*out=result;return out;}
struct MatrixProvider {Matrix value;void matrix(Matrix& out)const{out=value;}};
using MatrixProviderPtr=std::shared_ptr<MatrixProvider>;
struct PyFashion {static bool Check(PyObject* o){return PyCapsule_IsValid(o,"PyFashion");}};
namespace Script {
int setData(PyObject* o,int& value,const char* name="");
int setData(PyObject* o,uint& value,const char* name="");
int setData(PyObject* o,PyObjectPtr& value,const char* = ""){Py_INCREF(o);value=PyObjectPtr(o,PyObjectPtr::STEAL_REFERENCE);return 0;}
int setData(PyObject* o,MatrixProviderPtr& value,const char* name=""){
 if(!PyCapsule_IsValid(o,"MatrixProvider")){PyErr_Format(PyExc_TypeError,"%s must be a MatrixProvider",name);return -1;}
 value=std::make_shared<MatrixProvider>();value->value=*static_cast<Matrix*>(PyCapsule_GetPointer(o,"MatrixProvider"));return 0;
}
PyObject* getData(const Matrix& value){PyObject* out=PyTuple_New(16);if(!out)return NULL;for(int n=0;n<16;++n)PyTuple_SET_ITEM(out,n,PyFloat_FromDouble(value.v[n]));return out;}
PyObject* getData(const BW::string& value){return PyString_FromStringAndSize(value.data(),value.size());}
PyObject* getData(int value){return PyInt_FromLong(value);}
PyObject* getData(float value){return PyFloat_FromDouble(value);}
PyObject* getData(bool value){return PyBool_FromLong(value);}
template<class T>PyObject* getRetData(const T& value){return getData(value);}
}
class ClientSpace:public std::enable_shared_from_this<ClientSpace> {
public:explicit ClientSpace(int id):id_(id){}int id()const{return id_;}bool cleared()const{return cleared_;}int id_;bool cleared_=false;
};
class ClientSpacePtr:public std::shared_ptr<ClientSpace> {
public:ClientSpacePtr(){}ClientSpacePtr(ClientSpace* p):std::shared_ptr<ClientSpace>(p?p->shared_from_this():std::shared_ptr<ClientSpace>()){}
 ClientSpacePtr& operator=(ClientSpace* p){std::shared_ptr<ClientSpace>::operator=(p?p->shared_from_this():std::shared_ptr<ClientSpace>());return *this;}
};
// Compile the actual SDK SpaceManager declaration to reject invented methods.
class IClientSpaceFactory {};
class ScriptObject;class IEntityEmbodiment;class IOmniLightEmbodiment;class ISpotLightEmbodiment;class PyOmniLight;class PySpotLight;
using IEntityEmbodimentPtr=std::shared_ptr<IEntityEmbodiment>;
template<class A,class B>struct Event{struct EventDelegateList{};};
@SPACE_MANAGER@
SpaceManager::SpaceManager(){}SpaceManager::~SpaceManager(){}
SpaceManager& SpaceManager::instance(){static SpaceManager value;return value;}
ClientSpace* SpaceManager::space(SpaceID id){auto it=spaces_.find(id);return it==spaces_.end()?NULL:it->second;}
void SpaceManager::addSpace(ClientSpace* space){spaces_[space->id()]=space;}
void SpaceManager::delSpace(ClientSpace* space){auto i=spaces_.find(space->id());if(i!=spaces_.end()&&i->second==space)spaces_.erase(i);}
struct DataSection {};using DataSectionPtr=std::shared_ptr<DataSection>;
struct FourCC {uint32 value;explicit FourCC(const char* s){std::memcpy(&value,s,4);}};
namespace CompiledSpace {
struct StringTable {};
struct BinaryFormat {
 static constexpr size_t NOT_FOUND=size_t(-1);std::vector<unsigned char> bytes;bool present=true;unsigned version=1;
 struct Stream {std::vector<unsigned char>* data;size_t size()const{return data->size();}const void* readRaw(size_t size){assert(size==data->size());return data->data();}} stream{&bytes};
 size_t findSection(FourCC id){assert(id.value==FourCC("WGDE").value);return present?0:NOT_FOUND;}
 unsigned sectionVersion(size_t n){assert(n==0);return version;}
 Stream* openSection(size_t n,bool pin){assert(n==0&&pin);return &stream;}void closeSection(Stream* p){assert(p==&stream);}
};
struct ILoader {
 virtual ~ILoader(){}virtual bool loadMustSucceed()const{return false;}
 virtual bool doLoadFromSpace(ClientSpace*,BinaryFormat&,const DataSectionPtr&,const Matrix&,const StringTable&)=0;
 virtual bool doBind()=0;virtual void doUnload()=0;virtual float percentLoaded()const=0;
};
}
}
@INT_SET_DATA@
@UINT_SET_DATA@
@DESTRUCTIBLES_HEADER@
namespace BW {float queryStockDestructibleFallPitch(ClientSpace& space,const Vector3& origin,float height,float yaw){
 assert(space.id()==41&&origin.x==120&&origin.y==3&&origin.z==240&&height==7&&yaw==.25f);return .5f;
}}
@DESTRUCTIBLES_SOURCE@

namespace BW {
struct TestDestructibles:StockDestructibleObjects {
 bool retired=false;std::vector<std::array<int,5>> calls;
 bool snapshot(uint32 index,StockDestructibleSnapshot& out)const override {
  if(retired||index>=3){return false;}out.world.setTranslate(Vector3(120.f+index,3.f,240.f));out.hardpointWorld=out.world;
  out.category=2;out.height=7.f;out.effectScale=1.25f;out.effects[4]="ram";out.effects[5]="hit";return true;
 }
 bool transform(uint32 index,const Matrix& world,float wind,std::string&)override{calls.push_back({0,int(index),int(world.v[12]),int(world.v[14]),int(wind)});return !retired;}
 bool destroy(uint32 index,bool animate,bool shot,bool havok,std::string&)override{calls.push_back({1,int(index),animate,shot,havok});return !retired;}
 bool restore(std::string&)override{calls.push_back({2,0,0,0,0});return !retired;}
};
struct TestLoader:CompiledSpace::StockDestructiblesSceneLoader {
 using StockDestructiblesSceneLoader::doLoadFromSpace;using StockDestructiblesSceneLoader::doBind;using StockDestructiblesSceneLoader::doUnload;
};
std::shared_ptr<ClientSpace> nativeSpace;
std::shared_ptr<TestDestructibles> nativeObjects;
StockDestructibles nativeOwner;
std::unique_ptr<TestLoader> loader;
CompiledSpace::BinaryFormat file;
void appendWord(uint32 value){const auto* p=reinterpret_cast<const unsigned char*>(&value);file.bytes.insert(file.bytes.end(),p,p+4);}
void stop(){if(loader)loader->doUnload();if(nativeSpace)SpaceManager::instance().delSpace(nativeSpace.get());if(nativeObjects)nativeObjects->retired=true;loader.reset();nativeOwner.reset();nativeObjects.reset();nativeSpace.reset();}
}
static PyObject* prepare(PyObject*,PyObject* args){
 int space=41;if(!PyArg_ParseTuple(args,"|i",&space))return NULL;BW::stop();BW::file.bytes.clear();
 // Includes a duplicate reverse identity and one missing row. Raw count=4;
 // returned filenames compact to3. The last reference to object1 is module1.
 const unsigned words[]={12,1,0x8081,0,4,8,4,0,0,1,2,3,3,4,4,4,5,0,1,1,0x80000000u,0xffffffffu};
 for(unsigned value:words)BW::appendWord(value);
 BW::nativeSpace=std::make_shared<BW::ClientSpace>(space);BW::SpaceManager::instance().addSpace(BW::nativeSpace.get());
 BW::nativeObjects=std::make_shared<BW::TestDestructibles>();BW::nativeOwner=BW::acquireStockDestructibles(BW::nativeSpace.get());
 BW::registerStockDestructibleObjects(BW::nativeOwner,&BW::file,false,BW::nativeObjects);
 BW::registerStockDestructibleObjects(BW::nativeOwner,&BW::file,true,BW::nativeObjects);
 BW::loader.reset(new BW::TestLoader);if(!BW::loader->doLoadFromSpace(BW::nativeSpace.get(),BW::file,{},BW::Matrix(),BW::CompiledSpace::StringTable()))return PyErr_Format(PyExc_RuntimeError,"load failed");
 Py_RETURN_NONE;
}
static PyObject* bind(PyObject*,PyObject*){return PyBool_FromLong(BW::loader&&BW::loader->doBind());}
static PyObject* unload(PyObject*,PyObject*){if(BW::loader)BW::loader->doUnload();Py_RETURN_NONE;}
static PyObject* child(PyObject*,PyObject*){BW::playStockModuleDestructionAnimation(BW::nativeOwner,&BW::file,1,true,false);Py_RETURN_NONE;}
static PyObject* calls(PyObject*,PyObject*){PyObject* out=PyList_New(0);if(!out)return NULL;if(BW::nativeObjects)for(const auto& c:BW::nativeObjects->calls){PyObject* row=Py_BuildValue("(iiiii)",c[0],c[1],c[2],c[3],c[4]);PyList_Append(out,row);Py_DECREF(row);}return out;}
static PyObject* provider(PyObject*,PyObject* args){static BW::Matrix matrix;float x=0,z=0;if(!PyArg_ParseTuple(args,"ff",&x,&z))return NULL;matrix.setTranslate(BW::Vector3(x,0,z));return PyCapsule_New(&matrix,"MatrixProvider",NULL);}
static PyObject* fashion(PyObject*,PyObject*){static int value;return PyCapsule_New(&value,"PyFashion",NULL);}
