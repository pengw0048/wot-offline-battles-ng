#pragma once
#include <cassert>
#include <cstdint>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>
#define BW_BEGIN_NAMESPACE namespace BW {
#define BW_END_NAMESPACE }
#define ASSET_MSG(...) asset(__VA_ARGS__)
inline void asset(const char*,const char*){}
namespace BW {
using uint32=std::uint32_t;using string=std::string;
struct Vector3{float x,y,z;Vector3(float a=0,float b=0,float c=0):x(a),y(b),z(c){}};
struct Matrix{Vector3 p;void setTranslate(Vector3 v){p=v;}};
struct BoundingBox{Vector3 low,high;bool empty=true;BoundingBox()=default;BoundingBox(Vector3 a,Vector3 b):low(a),high(b),empty(false){};static BoundingBox s_insideOut_;
void setBounds(Vector3 a,Vector3 b){low=a;high=b;empty=false;}void transformBy(const Matrix& m){low={low.x+m.p.x,low.y+m.p.y,low.z+m.p.z};high={high.x+m.p.x,high.y+m.p.y,high.z+m.p.z};}};
BoundingBox BoundingBox::s_insideOut_;using AABB=BoundingBox;
struct DataSection{bool valid=true;};using DataSectionPtr=std::shared_ptr<DataSection>;
struct SceneObject{void* value=nullptr;struct Flags{bool dynamic=true,reflection=true;void isDynamic(bool v){dynamic=v;}void isReflectionVisible(bool v){reflection=v;}} bits;
template<class T>void handleAndType(T* p){value=p;}template<class T>T* getAs()const{return static_cast<T*>(value);}Flags& flags(){return bits;}};
struct SceneDrawContext{bool flares=true;bool renderFlares()const{return flares;}};
struct IDrawOperationTypeHandler{virtual ~IDrawOperationTypeHandler(){};virtual void doDraw(const SceneDrawContext&,const SceneObject*,size_t)=0;};
struct ISpatialQueryOperationTypeHandler{virtual ~ISpatialQueryOperationTypeHandler(){};virtual void getWorldTransform(const SceneObject&,Matrix*)=0;virtual void getWorldVisibilityBoundingBox(const SceneObject&,AABB*)=0;};
struct DrawOperation{std::unique_ptr<IDrawOperationTypeHandler> handler;template<class T,class H>void addHandler(){handler.reset(new H);}};
struct SpatialQueryOperation{std::unique_ptr<ISpatialQueryOperationTypeHandler> handler;template<class T,class H>void addHandler(){handler.reset(new H);}};
class Scene{public:DrawOperation draw;SpatialQueryOperation spatial;template<class T>T* getObjectOperation();};
template<>DrawOperation* Scene::getObjectOperation(){return &draw;}template<>SpatialQueryOperation* Scene::getObjectOperation(){return &spatial;}
struct DynamicObjectHandle{unsigned id;};
struct DynamicSceneProvider{unsigned next=1;std::map<unsigned,SceneObject> objects;std::map<unsigned,BoundingBox> boxes;std::function<void()> onAdd,onRemove;
DynamicObjectHandle addObject(SceneObject object,const Matrix&,const BoundingBox& collision,const BoundingBox& visible){assert(collision.empty&&!visible.empty&&!object.bits.dynamic&&!object.bits.reflection);unsigned id=next++;objects[id]=object;boxes[id]=visible;if(onAdd)onAdd();return {id};}
void removeObject(DynamicObjectHandle handle){assert(objects.erase(handle.id)==1);boxes.erase(handle.id);if(onRemove)onRemove();}};
struct ClientSpace{virtual ~ClientSpace(){};};
namespace CompiledSpace {
struct FourCC{std::string name;FourCC(const char* v):name(v){}};
struct BinaryFormat{static constexpr size_t NOT_FOUND=size_t(-1);struct Stream{std::vector<char> bytes;size_t size(){return bytes.size();}const void* readRaw(size_t n){assert(n==bytes.size());return bytes.data();}} stream;
unsigned version=2,opens=0,closes=0;bool missing=false,failOpen=false;
size_t findSection(FourCC code){assert(code.name=="BWfr");return missing?size_t(NOT_FOUND):0;}unsigned sectionVersion(size_t){return version;}Stream* openSection(size_t,bool raw){assert(raw);++opens;return failOpen?nullptr:&stream;}void closeSection(Stream* s){assert(s==&stream);++closes;std::fill(stream.bytes.begin(),stream.bytes.end(),'x');}};
struct StringTable{std::map<uint32,std::string> values;mutable std::vector<uint32> lookups;const char* entry(size_t key)const{lookups.push_back(uint32(key));auto f=values.find(uint32(key));return f==values.end()?nullptr:f->second.c_str();}};
struct CompiledSpace:ClientSpace{DynamicSceneProvider provider;DynamicSceneProvider& dynamicScene(){return provider;}Scene scene_;Scene& scene(){return scene_;}};
struct ILoader{unsigned status=0;virtual ~ILoader(){};bool loadFromSpace(ClientSpace* s,BinaryFormat& f,const DataSectionPtr& d,const Matrix& m,const StringTable& t){status=1;bool ok=doLoadFromSpace(s,f,d,m,t);status=ok?2:3;return ok;}bool bind(){return status==2&&doBind();}void unload(){doUnload();status=0;}virtual bool doLoadFromSpace(ClientSpace*,BinaryFormat&,const DataSectionPtr&,const Matrix&,const StringTable&)=0;virtual bool doBind()=0;virtual void doUnload()=0;virtual float percentLoaded()const=0;};
}
struct LensEffect{uint32 colour_=0xffffffff;float max=150,area_=1,fade=1;static unsigned loads;bool load(DataSectionPtr p){++loads;return p->valid;}void maxDistance(float v){max=v;}void area(float v){area_=v;}void fadeSpeed(float v){fade=v;}uint32 colour()const{return colour_;}void colour(uint32 v){colour_=v;}};unsigned LensEffect::loads=0;
struct LensEffectManager{struct Entry{Vector3 p;LensEffect effect;};std::map<size_t,Entry> entries;std::vector<size_t> forgotten;static LensEffectManager& instance(){static LensEffectManager value;return value;}void add(size_t id,Vector3 p,const LensEffect& le){entries[id]={p,le};}void forget(size_t id){forgotten.push_back(id);entries.erase(id);}};
struct BWResource{static std::function<void()> onOpen;static std::vector<std::string> opened;static DataSectionPtr openSection(const std::string& r){opened.push_back(r);if(onOpen)onOpen();if(r=="missing")return {};auto p=std::make_shared<DataSection>();p->valid=r!="invalid";return p;}};std::vector<std::string> BWResource::opened;std::function<void()> BWResource::onOpen;
}
