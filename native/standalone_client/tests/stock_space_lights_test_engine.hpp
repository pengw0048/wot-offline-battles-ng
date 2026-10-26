// Engine-boundary doubles for the complete production BWLC scene owner.
#pragma once
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <typeinfo>
#include <vector>
#define BW_BEGIN_NAMESPACE namespace BW {
#define BW_END_NAMESPACE }
#define ASSET_MSG(...) std::fprintf(stderr,__VA_ARGS__)
namespace BW {
using uint8=std::uint8_t;using uint32=std::uint32_t;
template<class K,class V>using map=std::map<K,V>;
inline bool almostZero(float v){return std::fabs(v)<.0004f;}
template<class T>class SmartPointer:public std::shared_ptr<T>{
public:using std::shared_ptr<T>::shared_ptr;SmartPointer(){}SmartPointer(T* p):std::shared_ptr<T>(p){}
 T* getObject()const{return this->get();}
};
struct Vector3 {float x=0,y=0,z=0;Vector3(){}Vector3(float a,float b,float c):x(a),y(b),z(c){}
 Vector3 operator+(const Vector3& b)const{return {x+b.x,y+b.y,z+b.z};}
 Vector3 operator-(const Vector3& b)const{return {x-b.x,y-b.y,z-b.z};}
 float length()const{return std::sqrt(x*x+y*y+z*z);}
};
struct Matrix {float v[16]{};static const Matrix identity;Matrix(){v[0]=v[5]=v[10]=v[15]=1;}
 void preMultiply(const Matrix& b){Matrix out;for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c){out.v[r*4+c]=0;for(unsigned k=0;k<4;++k)out.v[r*4+c]+=b.v[r*4+k]*v[k*4+c];}*this=out;}
 Vector3 applyToOrigin()const{return {v[12],v[13],v[14]};}
 Vector3 applyPoint(const Vector3& p)const{return {p.x*v[0]+p.y*v[4]+p.z*v[8]+v[12],p.x*v[1]+p.y*v[5]+p.z*v[9]+v[13],p.x*v[2]+p.y*v[6]+p.z*v[10]+v[14]};}
};const Matrix Matrix::identity;
struct AABB {Vector3 low,high;AABB(){}AABB(Vector3 a,Vector3 b):low(a),high(b){}
 float distance(const Vector3& p)const{Vector3 d{std::max({low.x-p.x,0.f,p.x-high.x}),std::max({low.y-p.y,0.f,p.y-high.y}),std::max({low.z-p.z,0.f,p.z-high.z})};return d.length();}
};using BoundingBox=AABB;
struct ConvexHull {AABB bounds;bool intersects(const Vector3& p,float r)const{return bounds.distance(p)<=r;}};
struct DataSection{};using DataSectionPtr=SmartPointer<DataSection>;
struct FourCC{uint32 value;explicit FourCC(const char* s){std::memcpy(&value,s,4);}};
namespace SceneTypeSystem {using RuntimeTypeID=size_t;template<class T>RuntimeTypeID getSceneViewRuntimeID(){return typeid(T).hash_code();}}
class Scene;
class SceneProvider {
public:virtual ~SceneProvider(){}template<class T>T* getView(){return static_cast<T*>(getView(SceneTypeSystem::getSceneViewRuntimeID<T>()));}
protected:virtual void* getView(const SceneTypeSystem::RuntimeTypeID&){return nullptr;}
 template<class T,class Host>static void exposeView(Host* self,size_t type,void*& out){if(!out&&type==SceneTypeSystem::getSceneViewRuntimeID<T>())out=static_cast<T*>(self);}
};
using ConstSceneObjectCallback=std::function<void(const int&)>;using SceneObjectCallback=std::function<void(int&)>;
namespace Moo {
struct Colour {float r=0,g=0,b=0,a=0;Colour(){}Colour(float x,float y,float z,float w):r(x),g(y),b(z),a(w){}};
class OmniLight {
protected:Vector3 position_,worldPosition_;float inner_,outer_;Colour colour_;float multiplier_=1;uint8 flags_=0;
public:static unsigned alive;OmniLight(Colour c,Vector3 p,float i,float o):position_(p),worldPosition_(p),inner_(i),outer_(o),colour_(c){++alive;}
 virtual ~OmniLight(){--alive;}const Vector3& position()const{return position_;}const Vector3& worldPosition()const{return worldPosition_;}
 float outerRadius()const{return outer_;}float innerRadius()const{return inner_;}Colour colour()const{return colour_;}void colour(Colour c){colour_=c;}
 float multiplier()const{return multiplier_;}void multiplier(float x){multiplier_=x;}uint8 stockSourceFlags()const{return flags_;}void stockSourceFlags(uint8 x){flags_=x;}
 void stockCompiledWorldTransform(const Matrix& m){worldPosition_=m.applyPoint(position_);}
};unsigned OmniLight::alive=0;using OmniLightPtr=SmartPointer<OmniLight>;
class SpotLight:public OmniLight {
public:Vector3 direction_;float cone_;SpotLight(Colour c,Vector3 p,Vector3 d,float i,float o,float cone):OmniLight(c,p,i,o),direction_(d),cone_(cone){}
};using SpotLightPtr=SmartPointer<SpotLight>;
class LightContainer {public:std::vector<OmniLightPtr> omnis;std::vector<SpotLightPtr> spots;
 void addOmni(const OmniLightPtr& light){omnis.push_back(light);}void addSpot(const SpotLightPtr& light){spots.push_back(light);}
};
struct Node{};using NodePtr=SmartPointer<Node>;
struct Channel {void result(float frame,Matrix& out)const{out=Matrix();out.v[12]=frame;}};
using AnimationChannelPtr=SmartPointer<Channel>;
struct Binder {AnimationChannelPtr item;AnimationChannelPtr channel()const{return item;}};
struct Animation {Binder binder{AnimationChannelPtr(new Channel)};float totalTime()const{return 60;}
 uint32 nChannelBinders()const{return 1;}const Binder& channelBinder(uint32 i)const{assert(!i);return binder;}
};using AnimationPtr=SmartPointer<Animation>;
class AnimationManager {public:static bool missing;static AnimationManager& instance(){static AnimationManager value;return value;}
 AnimationPtr get(const char* n,const NodePtr&){assert(std::string(n)=="pulse.animation");return missing?AnimationPtr():AnimationPtr(new Animation);}
};bool AnimationManager::missing=false;
struct PackedColours {enum{RED,GREEN,MAGENTA};};
}
struct DebugDraw {static unsigned calls;static void bboxAdd(const AABB&,uint32){++calls;}};unsigned DebugDraw::calls=0;
class ILightSceneViewProvider {public:virtual ~ILightSceneViewProvider(){}virtual size_t intersect(const ConvexHull&,Moo::LightContainer&)const=0;virtual size_t intersect(const AABB&,Moo::LightContainer&)const=0;virtual void debugDrawLights()const=0;};
class ITickSceneProvider {public:virtual ~ITickSceneProvider(){}virtual void tick(float)=0;virtual void updateAnimations(float)=0;};
class Scene {public:std::vector<SceneProvider*> providers;std::function<void()> onAdd,onRemove;unsigned adds=0,removes=0;
 void addProvider(SceneProvider* p){assert(std::find(providers.begin(),providers.end(),p)==providers.end());providers.push_back(p);++adds;if(onAdd)onAdd();}
 void removeProvider(SceneProvider* p){assert(std::find(providers.begin(),providers.end(),p)!=providers.end());providers.erase(std::find(providers.begin(),providers.end(),p));++removes;if(onRemove)onRemove();}
};
class ClientSpace {public:Scene value;Scene& scene(){return value;}};
namespace CompiledSpace {
class BinaryFormat {public:static const size_t NOT_FOUND=size_t(-1);std::vector<unsigned char> bytes;uint32 version=1;bool available=true;unsigned opens=0,closes=0;
 struct Stream {std::vector<unsigned char>& bytes;size_t size()const{return bytes.size();}void* readRaw(size_t n){assert(n==bytes.size());return bytes.data();}};
 std::unique_ptr<Stream> stream;size_t findSection(FourCC tag){assert(tag.value==FourCC("BWLC").value);return available?0:NOT_FOUND;}
 uint32 sectionVersion(size_t){return version;}Stream* openSection(size_t){++opens;stream.reset(new Stream{bytes});return stream.get();}
 void closeSection(Stream* p){assert(stream.get()==p);++closes;std::fill(bytes.begin(),bytes.end(),0);stream.reset();}
};
class StringTable {public:bool missing=false;const char* entry(size_t key)const{return key==123&&!missing?"pulse.animation":nullptr;}};
class ILoader {ClientSpace* space_=nullptr;public:virtual ~ILoader(){}bool loadFromSpace(ClientSpace* p,BinaryFormat& f,const DataSectionPtr& ds,const Matrix& m,const StringTable& s){space_=p;return doLoadFromSpace(p,f,ds,m,s);}
 bool bind(){return doBind();}void unload(){doUnload();}ClientSpace& space()const{assert(space_);return *space_;}
protected:virtual bool doLoadFromSpace(ClientSpace*,BinaryFormat&,const DataSectionPtr&,const Matrix&,const StringTable&)=0;virtual bool doBind()=0;virtual void doUnload()=0;virtual float percentLoaded()const=0;
};
}
}
