// Engine-boundary doubles for compiling the complete production scene owner.
// Scene notifications are synchronous and cache readers borrow their real owner.
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <typeinfo>
#include <vector>
#define BW_BEGIN_NAMESPACE namespace BW {
#define BW_END_NAMESPACE }
#define ERROR_MSG(...) std::fprintf(stderr,__VA_ARGS__)
#define MF_ASSERT(x) assert(x)
#define BW_GUARD
namespace BW {
using uint32=std::uint32_t;using int64=std::int64_t;using string=std::string;
template<class T>using vector=std::vector<T>;
template<class T> class SmartPointer:public std::shared_ptr<T> {
public:using std::shared_ptr<T>::shared_ptr;SmartPointer(){}SmartPointer(T* p):std::shared_ptr<T>(p){}
 T* getObject()const{return this->get();}
};
class Vector3 {public:float x=0,y=0,z=0;Vector3(){}Vector3(float a,float b,float c):x(a),y(b),z(c){}};
struct Vector4 {float x,y,z,w;float operator[](size_t n)const{return (&x)[n];}Vector4()=default;Vector4(float a,float b,float c,float d):x(a),y(b),z(c),w(d){}};
struct Matrix {
 float v[4][4]{};Matrix(){for(unsigned i=0;i<4;++i)v[i][i]=1;}void setZero(){std::memset(v,0,sizeof(v));}
 void row(int r,const Vector4& value){std::memcpy(v[r],&value,16);}
 const Vector4& row(int r)const{return *reinterpret_cast<const Vector4*>(v[r]);}
 float* operator[](size_t r){return v[r];}const float* operator[](size_t r)const{return v[r];}
 void multiply(const Matrix& a,const Matrix& b){Matrix out;for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c){out[r][c]=0;for(unsigned k=0;k<4;++k)out[r][c]+=a[r][k]*b[k][c];}*this=out;}
 Vector3 applyVector(const Vector3& p)const{return {p.x*v[0][0]+p.y*v[1][0]+p.z*v[2][0],p.x*v[0][1]+p.y*v[1][1]+p.z*v[2][1],p.x*v[0][2]+p.y*v[1][2]+p.z*v[2][2]};}
 Vector3 applyPoint(const Vector3& p)const{Vector3 r=applyVector(p);r.x+=v[3][0];r.y+=v[3][1];r.z+=v[3][2];return r;}
 bool invert(const Matrix& m){*this=Matrix();for(unsigned n=0;n<3;++n)v[3][n]=-m[3][n];return true;}
};
Vector3* D3DXVec3TransformCoord(Vector3* out,const Vector3* value,const Matrix* matrix){*out=matrix->applyPoint(*value);return out;}
Vector3* D3DXVec3TransformNormal(Vector3* out,const Vector3* value,const Matrix* matrix){*out=matrix->applyVector(*value);return out;}
Matrix* D3DXMatrixMultiply(Matrix* out,const Matrix* a,const Matrix* b){out->multiply(*a,*b);return out;}
struct BoundingBox {
 Vector3 low{1,1,1},high{-1,-1,-1};static const BoundingBox s_insideOut_;
 BoundingBox(){}BoundingBox(Vector3 a,Vector3 b):low(a),high(b){}
 bool insideOut()const{return low.x>high.x||low.y>high.y||low.z>high.z;}
 void transformBy(const Matrix& m){low.x+=m[3][0];low.y+=m[3][1];low.z+=m[3][2];high.x+=m[3][0];high.y+=m[3][1];high.z+=m[3][2];}
};
const BoundingBox BoundingBox::s_insideOut_{};using AABB=BoundingBox;
struct BinaryBlock{};using BinaryPtr=SmartPointer<BinaryBlock>;
struct DataSection {BinaryPtr readBinary(const char* name){assert(std::string(name)=="bsp2");return new BinaryBlock;}};
using DataSectionPtr=SmartPointer<DataSection>;
struct PrimitiveFile{static DataSectionPtr get(const char* name){assert(std::string(name)=="bsp.primitives");return new DataSection;}};
struct FourCC{uint32 value;explicit FourCC(const char* n){std::memcpy(&value,n,4);}};
struct SceneTypeSystem{using RuntimeTypeID=const std::type_info*;};
class SceneObject {
public:
 void* value=nullptr;const std::type_info* type_=nullptr;struct Flags{void isDynamic(bool){}void isReflectionVisible(bool){}void isCastingStaticShadow(bool){}void isCastingDynamicShadow(bool){}} bits;
 template<class T>void type(){type_=&typeid(T);}template<class T>bool isType()const{return type_&&*type_==typeid(T);}void handle(void* p){value=p;}Flags& flags(){return bits;}template<class T>T* getAs()const{return static_cast<T*>(value);}
 SceneTypeSystem::RuntimeTypeID type()const{return type_;}
};
class WorldTriangle {public:using Flags=std::uint16_t;Flags value=0;Vector3 vertices[3];
 WorldTriangle(Flags f=0):value(f){}WorldTriangle(Vector3 a,Vector3 b,Vector3 c,Flags f=0):value(f),vertices{a,b,c}{}
 const Vector3& v0()const{return vertices[0];}const Vector3& v1()const{return vertices[1];}const Vector3& v2()const{return vertices[2];}
 unsigned materialKind()const{return value>>8;}Flags flags()const{return value;}void flags(Flags f){value=f;}};
struct CollisionVisitor {virtual ~CollisionVisitor(){}virtual bool visit(const WorldTriangle&,float)=0;};
using RealWTriangleSet=std::vector<WorldTriangle>;
static std::array<Vector3,4> lastDecalBox;
struct BSPTree{RealWTriangleSet values;uint32 numTriangles()const{return uint32(values.size());}WorldTriangle* triangles(){return values.data();}BoundingBox boundingBox()const{return BoundingBox({-1,-1,-1},{1,1,1});}
 void visitStockDecalBox(const Vector3& center,const Vector3& first,const Vector3& second,const Vector3& third,CollisionVisitor* visitor){lastDecalBox={{center,first,second,third}};for(const auto& triangle:values)visitor->visit(triangle,0.f);}};
struct BSPTreeTool {
 static BSPTree* loadBSP(const BinaryPtr&){auto* p=new BSPTree;p->values={{1},{2}};return p;}
 static BSPTree* buildBSP(RealWTriangleSet& values){auto* p=new BSPTree;p->values=values;return p;}
};
struct CollisionState {unsigned triangles=0,flags=0;};struct SweepParams{};
struct CollisionObstacle{CollisionObstacle(const Matrix*,const Matrix*,const SceneObject&){}};
struct BSPCollisionInterface {const BSPTree& bsp;BSPCollisionInterface(CollisionObstacle&,const BSPTree& b):bsp(b){}};
struct SweepHelpers{template<class T>static bool collideIntoObject(const T&,const SweepParams&,BSPCollisionInterface& b,const BoundingBox&,const Matrix&,CollisionState& state){state.triangles=b.bsp.numTriangles();state.flags=state.triangles?b.bsp.values[0].flags():0;return state.triangles!=0;}};
namespace Moo {
struct BaseTexture {std::string name;};using BaseTexturePtr=SmartPointer<BaseTexture>;
struct TextureManager{static TextureManager* instance(){static TextureManager p;return &p;}BaseTexturePtr get(const char* n){auto* t=new BaseTexture;t->name=n;return t;}};
bool missingProperties=false;
struct EffectMaterial {
 unsigned properties=0;bool boolean=true;float floating=0;int integer=0;Vector4 vector;std::string texture;bool isolated=false;
 void replaceDefaults() {isolated=true;}
 bool setProperty(const char*,bool v,bool create){assert(create&&isolated);if(missingProperties)return false;++properties;boolean=v;return true;}
 bool setProperty(const char*,float v,bool create){assert(create&&isolated);if(missingProperties)return false;++properties;floating=v;return true;}
 bool setProperty(const char*,int v,bool create){assert(create&&isolated);if(missingProperties)return false;++properties;integer=v;return true;}
 bool setProperty(const char*,const Matrix&,bool create){assert(create&&isolated);if(missingProperties)return false;++properties;return true;}
 bool setProperty(const char*,const Vector4& v,bool create){assert(create&&isolated);if(missingProperties)return false;++properties;vector=v;return true;}
 bool setProperty(const char*,const BaseTexturePtr& v,bool create){assert(create&&isolated);if(missingProperties)return false;++properties;texture=v?v->name:"";return true;}
};using EffectMaterialPtr=SmartPointer<EffectMaterial>;
struct ComplexEffectMaterial {EffectMaterialPtr base=new EffectMaterial;bool initFromEffect(const char* n){return std::string(n)=="original.fx";}const EffectMaterialPtr& baseMaterial()const{return base;}};
struct Vertices{};using VerticesPtr=SmartPointer<Vertices>;
struct Primitive{uint32 nPrimGroups(){return 3;}};using PrimitivePtr=SmartPointer<Primitive>;
static bool missingGeometry=false;
struct VerticesManager{static VerticesManager* instance(){static VerticesManager v;return &v;}VerticesPtr get(const char* n){assert(std::string(n)=="compiled.vertices");return missingGeometry?VerticesPtr():VerticesPtr(new Vertices);}};
struct PrimitiveManager{static PrimitiveManager* instance(){static PrimitiveManager p;return &p;}PrimitivePtr get(const char* n){assert(std::string(n)=="indices");return new Primitive;}};
static std::set<const void*> liveCaches;static std::vector<std::string> destruction;
struct StreamedDataCache {
 struct EntryInfo{};
 bool valid;StreamedDataCache(const char* n,bool create):valid(std::string(n)=="cache.anca"){assert(!create);liveCaches.insert(this);}
 ~StreamedDataCache(){destruction.push_back("cache");liveCaches.erase(this);}bool good(){return valid;}
 const EntryInfo* findEntryData(const char* n,unsigned version,unsigned){assert(version==6);static EntryInfo e;return valid&&std::string(n)=="source.animation"?&e:nullptr;}
};
struct StreamedAnimation {static StreamedDataCache* s_loadingAnimCache_;};StreamedDataCache* StreamedAnimation::s_loadingAnimCache_=nullptr;
static std::vector<float> sampledFrames;
static std::function<void()> onAnimation;
static unsigned channelCalls=0;
struct Channel {
 unsigned index;void nodeless(float,float)const{}
 void result(float frame,Matrix& out)const{++channelCalls;auto callback=std::move(onAnimation);onAnimation=nullptr;if(callback)callback();out=Matrix();out[3][0]=index==0?frame:1;}
};
struct Binder {SmartPointer<Channel> value;const SmartPointer<Channel>& channel()const{return value;}};
struct Animation {
 StreamedDataCache* cache=nullptr;std::vector<Binder> binders;
 ~Animation(){if(cache){assert(liveCaches.count(cache));destruction.push_back("animation");}}
 bool load(const char* n){assert(std::string(n)=="source.animation");cache=StreamedAnimation::s_loadingAnimCache_;if(!cache)return false;for(unsigned i=0;i<2;++i){auto* c=new Channel;c->index=i;binders.push_back({SmartPointer<Channel>(c)});}return true;}
 unsigned nChannelBinders()const{return unsigned(binders.size());}const Binder& channelBinder(unsigned n)const{return binders[n];}
 float totalTime()const{return 20;}void touchStockCompiledFrame(float frame)const{assert(cache&&liveCaches.count(cache));sampledFrames.push_back(frame);}
};using AnimationPtr=SmartPointer<Animation>;
enum ERenderingPassType{RENDERING_PASS_COLOR,RENDERING_PASS_REFLECTION,RENDERING_PASS_SHADOWS,RENDERING_PASS_DEPTH};
struct DrawContext {
 struct InstanceData {
  uint32 stockPreviousPaletteEntries_=0;bool stockPreviousWorld_=false,stockAnimatedWorld_=false;uint32 paletteSize_=0;
  Matrix* matrix(){assert(!paletteSize_);return reinterpret_cast<Matrix*>(this+1);}Vector4* palette(){assert(paletteSize_);return reinterpret_cast<Vector4*>(this+1);}
 };
 struct Item{ComplexEffectMaterial* material;unsigned group;InstanceData* packet;};
 std::vector<std::unique_ptr<unsigned char[]>> bytes;std::vector<Item> items;ERenderingPassType pass=RENDERING_PASS_COLOR;
 ERenderingPassType renderingPassType(){return pass;}
 InstanceData* allocStockInstanceData(uint32 bones){bytes.emplace_back(new unsigned char[sizeof(InstanceData)+(bones?bones*2*48:128)]);auto* d=new(bytes.back().get())InstanceData;d->paletteSize_=bones*2;d->stockPreviousPaletteEntries_=bones;d->stockPreviousWorld_=!bones;return d;}
 void drawRenderOp(ComplexEffectMaterial* m,Vertices* v,Primitive* p,uint32 group,InstanceData* packet,const BoundingBox&){assert(m&&v&&p);items.push_back({m,group,packet});}
 void clear(){items.clear();bytes.clear();}
};
struct RenderContext{uint32 frame=0;Matrix projection_;const Matrix& projection()const{return projection_;}uint32 frameTimestamp()const{return frame;}void nextFrame(){++frame;}};RenderContext& rc(){static RenderContext r;return r;}
}
struct LodSettings{static LodSettings& instance(){static LodSettings v;return v;}float stockCompiledLodScale()const{return 1;}float stockCompiledLodDefault(unsigned n)const{assert(n<5);return 1;}};
static int decor=0;int stockDecorLevel(){return decor;}
struct SceneDrawContext {Moo::DrawContext& draw;Matrix view;bool lowDetailed=false;bool stockLowDetailedMode()const{return lowDetailed;}Moo::DrawContext& drawContext()const{return draw;}const Matrix& lodCameraView()const{return view;}};
struct IDrawOperationTypeHandler{virtual ~IDrawOperationTypeHandler(){}virtual void doDraw(const SceneDrawContext&,const SceneObject*,size_t)=0;};
struct ISpatialQueryOperationTypeHandler{virtual ~ISpatialQueryOperationTypeHandler(){}virtual void getWorldTransform(const SceneObject&,Matrix*)=0;virtual void getWorldVisibilityBoundingBox(const SceneObject&,AABB*)=0;};
struct ICollisionTypeHandler{virtual ~ICollisionTypeHandler(){}virtual bool doCollide(const SceneObject&,const Vector3&,const Vector3&,const SweepParams&,CollisionState&)const=0;virtual bool doCollide(const SceneObject&,const WorldTriangle&,const Vector3&,const SweepParams&,CollisionState&)const=0;};
struct ITickOperationTypeHandler{virtual ~ITickOperationTypeHandler(){}virtual void doTick(SceneObject*,size_t,float)=0;virtual void doUpdateAnimations(SceneObject*,size_t,float)=0;};
template<class H>struct Operation {std::unique_ptr<H> handler;template<class T,class Handler>void addHandler(){handler.reset(new Handler);}};
using DrawOperation=Operation<IDrawOperationTypeHandler>;using SpatialQueryOperation=Operation<ISpatialQueryOperationTypeHandler>;using CollisionOperation=Operation<ICollisionTypeHandler>;
struct TickOperation:Operation<ITickOperationTypeHandler> {
 ITickOperationTypeHandler* getHandler(SceneTypeSystem::RuntimeTypeID type){return type?handler.get():nullptr;}
 void tick(SceneObject*,size_t,float);void tick(SceneTypeSystem::RuntimeTypeID,SceneObject*,size_t,float);
 void updateAnimations(SceneObject*,size_t,float);void updateAnimations(SceneTypeSystem::RuntimeTypeID,SceneObject*,size_t,float);
};
class Scene {public:std::map<size_t,std::shared_ptr<void>> operations;template<class T>T* getObjectOperation(){auto& p=operations[typeid(T).hash_code()];if(!p)p=std::make_shared<T>();return static_cast<T*>(p.get());}};
struct DynamicObjectHandle {unsigned index;};
struct DynamicSceneProvider {
 std::map<unsigned,SceneObject> nodes;unsigned next=1;std::function<void()> onAdd,onRemove;
 DynamicObjectHandle addObject(const SceneObject& o,const Matrix&,const BoundingBox&,const BoundingBox&){DynamicObjectHandle h{next++};nodes.emplace(h.index,o);auto f=std::move(onAdd);onAdd=nullptr;if(f)f();return h;}
 void removeObject(const DynamicObjectHandle& h){assert(nodes.count(h.index));auto f=std::move(onRemove);onRemove=nullptr;if(f)f();assert(nodes.count(h.index));nodes.erase(h.index);}
};
class ClientSpace{public:virtual ~ClientSpace(){}};
// This rendering fixture has no active native audio owner. Any attempted
// publication beyond acquire's unavailable result is an invalid boundary use.
struct StockSpaceSoundOwner{};
using StockSpaceSounds=std::shared_ptr<StockSpaceSoundOwner>;
using StockSpaceSoundHandle=std::uint64_t;
StockSpaceSounds acquireStockSpaceSounds(ClientSpace*){return {};}
StockSpaceSoundHandle addStockSpaceModelSound(const StockSpaceSounds&,uint32,const Matrix&){assert(false);return 0;}
void removeStockSpaceSound(const StockSpaceSounds&,StockSpaceSoundHandle){assert(false);}
void registerStockSpaceObjectSound(const StockSpaceSounds&,const void*,bool,uint32,StockSpaceSoundHandle){assert(false);}
namespace CompiledSpace {
struct CompiledSpace:ClientSpace{Scene scene_;DynamicSceneProvider dynamic;Scene& scene(){return scene_;}DynamicSceneProvider& dynamicScene(){return dynamic;}};
struct StringTable {std::map<uint32,std::string> values;const char* entry(uint32 key)const{auto i=values.find(key);return i==values.end()?nullptr:i->second.c_str();}};
struct BinaryFormat {
 static const size_t NOT_FOUND=size_t(-1);
 struct Stream{std::vector<unsigned char>& data;explicit Stream(std::vector<unsigned char>& b):data(b){}size_t size()const{return data.size();}void* readRaw(size_t n){return n<=data.size()?data.data():nullptr;}};
 std::vector<std::vector<unsigned char>> sections;unsigned closes=0;
 size_t findSection(FourCC f){const char* n[]={"BSMI","BSMO","BSMA"};for(size_t i=0;i<3;++i)if(f.value==FourCC(n[i]).value)return i;return NOT_FOUND;}
 unsigned sectionVersion(size_t i){return i?1:2;}Stream* openSection(size_t i,bool){return new Stream(sections[i]);}void closeSection(Stream* s){++closes;delete s;}
};
class ILoader {
 enum Status{UNLOADED,LOADING,LOADED,FAILED};Status status=UNLOADED;
public:virtual ~ILoader(){}virtual bool loadMustSucceed()const{return false;}
 bool loadFromSpace(ClientSpace* s,BinaryFormat& f,const DataSectionPtr& d,const Matrix& m,const StringTable& t){assert(status!=LOADING&&status!=LOADED);status=LOADING;bool ok=doLoadFromSpace(s,f,d,m,t);status=ok?LOADED:FAILED;return ok;}
 bool bind(){if(status==FAILED)return false;assert(status==LOADED);return doBind();}void unload(){doUnload();status=UNLOADED;}
protected:virtual bool doLoadFromSpace(ClientSpace*,BinaryFormat&,const DataSectionPtr&,const Matrix&,const StringTable&)=0;virtual bool doBind()=0;virtual void doUnload()=0;virtual float percentLoaded()const=0;
};
}
}
