// Production water queries and compiled-space ownership over native boundary
// fixtures. CPU producers are extracted unchanged from the patched engine.
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
#include <vector>
#define BW_BEGIN_NAMESPACE namespace BW {
#define BW_END_NAMESPACE }
#define BW_GUARD
#define BW_GUARD_PROFILER(...)
#define bw_safe_delete(p) do {delete p;p=nullptr;} while(0)
#define DEBUG_MSG(...)
#define ASSET_MSG(...)
#define MAX_SIM_TEXTURE_SIZE 256
#define D3DFMT_A16B16G16R16F 113
#define BANK_DIST_DEFAULT_VALUE std::make_pair(Vector2::zero(),10000.f)
namespace wot { bool hostWorker=true; bool stockSimulationWorker(){return hostWorker;} }
namespace BW {
using uint64=std::uint64_t;template<class T>using vector=std::vector<T>;
using uint32=std::uint32_t;using uint=unsigned;using string=std::string;
using DataSectionPtr=int;
struct Vector2 {float x=0,y=0;Vector2(){}Vector2(float a,float b):x(a),y(b){}static Vector2 zero(){return {};}};
struct Vector3 {
 float x=0,y=0,z=0;Vector3(){}Vector3(float a,float b,float c):x(a),y(b),z(c){}
 Vector3 operator+(const Vector3& v)const{return {x+v.x,y+v.y,z+v.z};}
 Vector3 operator-(const Vector3& v)const{return {x-v.x,y-v.y,z-v.z};}
 Vector3 operator*(float s)const{return {x*s,y*s,z*s};}
 bool operator==(const Vector3& v)const{return x==v.x&&y==v.y&&z==v.z;}
 float& operator[](int n){return (&x)[n];}const float& operator[](int n)const{return (&x)[n];}
};
struct Vector4 {float x=0,y=0,z=0,w=0;Vector4(){}Vector4(float a,float b,float c,float d):x(a),y(b),z(c),w(d){}};
struct Matrix {
 float m[4][4];Matrix(){setRotateY(0);}
 void setZero(){std::memset(m,0,sizeof m);}
 void setRotateY(float a){setZero();m[0][0]=cosf(a);m[2][2]=cosf(a);m[0][2]=-sinf(a);m[2][0]=sinf(a);m[1][1]=m[3][3]=1;}
 void setScale(const Vector3& v){setZero();m[0][0]=v.x;m[1][1]=v.y;m[2][2]=v.z;m[3][3]=1;}
 void translation(const Vector3& v){m[3][0]=v.x;m[3][1]=v.y;m[3][2]=v.z;}
 void postTranslateBy(const Vector3& v){m[3][0]+=v.x;m[3][1]+=v.y;m[3][2]+=v.z;}
 void invert(const Matrix& v){setZero();for(int a=0;a<3;++a)for(int b=0;b<3;++b)m[a][b]=v.m[b][a];m[3][3]=1;for(int b=0;b<3;++b)for(int a=0;a<3;++a)m[3][b]-=v.m[3][a]*m[a][b];}
 float* operator[](int n){return m[n];}const float* operator[](int n)const{return m[n];}
};
using D3DXMATRIX=Matrix;using D3DXVECTOR3=Vector3;using D3DXVECTOR4=Vector4;
void D3DXMatrixMultiply(Matrix* result,const Matrix* a,const Matrix* b){Matrix r;r.setZero();for(int x=0;x<4;++x)for(int y=0;y<4;++y)for(int n=0;n<4;++n)r[x][y]+=(*a)[x][n]*(*b)[n][y];*result=r;}
void D3DXVec3Transform(Vector4* out,const Vector3* v,const Matrix* m){float r[4]={};for(int y=0;y<4;++y){for(int n=0;n<3;++n)r[y]+=(*v)[n]*(*m)[n][y];r[y]+=(*m)[3][y];}*out={r[0],r[1],r[2],r[3]};}
void D3DXVec3TransformNormal(Vector3* out,const Vector3* v,const Matrix* m){Vector3 r;for(int y=0;y<3;++y)for(int n=0;n<3;++n)r[y]+=(*v)[n]*(*m)[n][y];*out=r;}
void D3DXVec3Normalize(Vector3* out,const Vector3* v){const float n=sqrtf(v->x*v->x+v->y*v->y+v->z*v->z);*out=n?(*v)*(1.f/n):Vector3();}
struct BoundingBox {
 Vector3 low{1,1,1},high{-1,-1,-1};BoundingBox(){}BoundingBox(const Vector3& a,const Vector3& b):low(a),high(b){}
 const Vector3& minBounds()const{return low;}const Vector3& maxBounds()const{return high;}
 void setBounds(const Vector3& a,const Vector3& b){low=a;high=b;}
 bool insideOut()const{return low.x>high.x||low.y>high.y||low.z>high.z;}
 static BoundingBox s_insideOut_;
};
BoundingBox BoundingBox::s_insideOut_;using AABB=BoundingBox;
struct DogWatch {explicit DogWatch(const char*){}void start(){}void stop(){}};
struct SceneDrawContext{};
struct SceneObject {
 void* pointer=nullptr;
 struct Flags{void isDynamic(bool){}void isReflectionVisible(bool){}} flags_;
 template<class T>T* getAs()const{return static_cast<T*>(pointer);}
 template<class T>void handleAndType(T* p){pointer=p;}Flags& flags(){return flags_;}
};
struct IDrawOperationTypeHandler{virtual ~IDrawOperationTypeHandler(){}virtual void doDraw(const SceneDrawContext&,const SceneObject*,size_t)=0;};
struct ISpatialQueryOperationTypeHandler{virtual ~ISpatialQueryOperationTypeHandler(){}virtual void getWorldTransform(const SceneObject&,Matrix*)=0;virtual void getWorldVisibilityBoundingBox(const SceneObject&,AABB*)=0;};
struct DrawOperation{template<class A,class B>void addHandler(){}};using SpatialQueryOperation=DrawOperation;
struct Scene{template<class T>T* getObjectOperation(){static T operation;return &operation;}};
using DynamicObjectHandle=unsigned;
struct DynamicScene {
 std::map<unsigned,SceneObject> objects;unsigned sequence=0;
 std::function<void()> onAdd,onRemove;
 unsigned addObject(const SceneObject& o,const Matrix&,const BoundingBox&,const BoundingBox&){unsigned h=++sequence;objects[h]=o;if(onAdd)onAdd();return h;}
 void removeObject(unsigned h){assert(objects.erase(h)==1);if(onRemove)onRemove();}
};
struct ClientSpace {int id;bool retired=false;explicit ClientSpace(int n):id(n){}virtual ~ClientSpace(){}bool cleared()const{return retired;}};
using ClientSpacePtr=std::shared_ptr<ClientSpace>;
namespace DeprecatedSpaceHelpers {ClientSpacePtr camera;ClientSpacePtr cameraSpace(){return camera;}}
namespace CompiledSpace {
struct FourCC{explicit FourCC(const char*){}};
struct BinaryFormat {
 static const size_t NOT_FOUND=size_t(-1);std::vector<char> bytes;
 size_t findSection(FourCC){return 0;}int sectionVersion(size_t){return 2;}
 struct Stream{BinaryFormat* owner;size_t size(){return owner->bytes.size();}const void* readRaw(size_t){return owner->bytes.data();}} stream{this};
 Stream* openSection(size_t,bool){return &stream;}void closeSection(Stream*){}
};
struct StringTable{const char* entry(uint32)const{return "water.odata";}};
struct ILoader{virtual ~ILoader(){}};
struct CompiledSpace:ClientSpace{DynamicScene scene;explicit CompiledSpace(int id):ClientSpace(id){}DynamicScene& dynamicScene(){return scene;}};
}
struct Water;
struct Waters:std::vector<Water*>{static Waters& instance(){static Waters waters;return waters;}static void addToDrawList(Water*){}static bool simulationEnabled(){return false;}static void* s_simulationEffect_;static void* s_effect_;};
void* Waters::s_simulationEffect_=nullptr;void* Waters::s_effect_=nullptr;
struct WaterScene{int refs=0;WaterScene(float,float){}void incRef(){++refs;}void addOwner(Water*){}};
namespace Moo {struct Context{bool supportsTextureFormat(int){return false;}};Context& rc(){static Context c;return c;}}
struct RompCollider{enum {TERRAIN_ONLY};static int createDefault(int){return 0;}};
unsigned liveTasks=0;
struct BackgroundTask{explicit BackgroundTask(const char*){++liveTasks;}virtual ~BackgroundTask(){--liveTasks;}};
struct TaskManager;
struct CStyleBackgroundTask:BackgroundTask,std::enable_shared_from_this<CStyleBackgroundTask>{
 using FUNC_PTR=void(*)(void*);FUNC_PTR bgFunc_;void* bgArg_;FUNC_PTR fgFunc_;void* fgArg_;
 CStyleBackgroundTask(const char*,FUNC_PTR,void*,FUNC_PTR,void*);
 void doBackgroundTask(TaskManager&);void doMainThreadTask(TaskManager&);
};
struct TaskManager {
 std::vector<std::shared_ptr<CStyleBackgroundTask>> background,foreground;
 void addBackgroundTask(CStyleBackgroundTask* task){background.emplace_back(task);}
 void addMainThreadTask(CStyleBackgroundTask* task){foreground.push_back(task->shared_from_this());}
};
TaskManager taskManager;
struct FileIOTaskManager {static TaskManager& instance(){return taskManager;}};
auto& pending=taskManager.background;
#include "stock_worker_water_tasks.inc"
struct SimpleMutex {int depth=0;void grab(){assert(depth==0);++depth;}void give(){assert(depth==1);--depth;}};
struct SimpleMutexHolder{SimpleMutex& mutex;explicit SimpleMutexHolder(SimpleMutex& m):mutex(m){mutex.grab();}~SimpleMutexHolder(){mutex.give();}};
unsigned deleted=0,drawInit=0;
std::function<void(Water*)> duringCPU;
struct Water {
 static bool background;
 static std::vector<void*> reuseSlots;
 static void* operator new(size_t size){if(reuseSlots.empty())return ::operator new(size);void* p=reuseSlots.back();reuseSlots.pop_back();return p;}
 static void operator delete(void* p){reuseSlots.push_back(p);}
 ~Water(){++deleted;}
#include "stock_worker_water_state.inc"
 struct WaterResources{std::string foamTexture_,waveTexture_,reflectionTexture_,transparencyTable_;};
 WaterState state_;WaterResources resources_;
 uint64 stockQueryGeneration_;
 bool initialised_=false,visible_=true,stockQueryDataReady_=false,createdCells_=false,enableSim_=false,drawSelf_=true;
 void* simulationEffect_=nullptr;
 Matrix transform_,invTransform_;BoundingBox bb_,bbDeep_,bbActual_;
 uint gridSizeX_=0,gridSizeZ_=0;uint nVertices_=0;
 std::vector<uint32> wAlpha_;std::vector<bool>wRigid_;std::vector<std::pair<Vector2,float>>bankDist;
 std::vector<int>wIndex_,activeSimulations_,vertexBuffers_;
 struct Cell{void initSimulation(int,float){}void initEdge(int,Cell*){}};
 std::vector<Cell>cells_;
 WaterScene* waterScene_=nullptr;
 using WaterRenderTargetMap=std::map<float,WaterScene*>;static WaterRenderTargetMap s_renderTargetMap_;
 static SimpleMutex* deletionLock_;
 Water(const WaterState& state,const WaterResources& resources,int):state_(state),resources_(resources),stockQueryGeneration_(++s_stockWaterQueryGeneration){
   transform_.setRotateY(state_.orientation_);transform_.postTranslateBy(state_.position_);invTransform_.invert(transform_);
   gridSizeX_=int(ceilf(state_.size_.x/state_.tessellation_)+1);gridSizeZ_=int(ceilf(state_.size_.y/state_.tessellation_)+1);
   wRigid_.resize(gridSizeX_*gridSizeZ_);wAlpha_.resize(gridSizeX_*gridSizeZ_);bankDist.resize(gridSizeX_*gridSizeZ_);
   if(!deletionLock_)deletionLock_=new SimpleMutex;
   Waters::instance().push_back(this);
#include "stock_worker_water_schedule.inc"
 }
 static bool backgroundLoad(){return background;}
 static bool stillValid(Water* w);
 static void deleteWater(Water* w);
 static void doCreateTables(void*);static void onCreateTablesComplete(void*);void setup2ndPhase();
 void rebuild(const WaterState&,const WaterResources&);
 bool loadTransparencyTable(){wAlpha_.assign(wAlpha_.size(),0xffffffffu);return true;}
 void buildTransparencyTable(){if(duringCPU)duringCPU(this);loadTransparencyTable();}
 void buildBankDistTable(){}uint createIndices(){return uint(wAlpha_.size());}void createCells(){cells_.resize(1);}
 void createUnmanagedObjects(){}void createManagedObjects(){initialised_=false;createdCells_=true;}
 void deleteManagedObjects(){}void releaseWaterScene(){}void startResLoader(){}
 const Vector3& position()const{return state_.position_;}const Matrix& transform()const{return transform_;}
 void stockBeginVisibleCells(){}void stockAddVisibleCell(const BoundingBox&){}void outsideVisible(bool){}
 float stockGroundRayDistance(const Vector3&,const Vector3&)const;
 float stockQueryRayDistance(const Vector3&,const Vector3&)const;
 float stockWaterGeometryDistance(const Vector3&,const Vector3&)const;
 float stockWaterRayDistance(const Vector3&,const Vector3&,bool)const;
};
Water::WaterRenderTargetMap Water::s_renderTargetMap_;
SimpleMutex* Water::deletionLock_=nullptr;bool Water::background=true;
std::vector<void*> Water::reuseSlots;
void removeStockWaterFromDrawList(Water*){}
}
#include "stock_ground_effects.hpp"
#include "stock_battle_scene_queries_data.hpp"
#include "stock_worker_water_producers.inc"
#include "stock_ground_water_query.inc"
#include "stock_worker_water_alpha.inc"
#include "stock_space_water_scene.cpp"
namespace BW {
struct Loader:CompiledSpace::StockWaterSceneLoader{using StockWaterSceneLoader::doLoadFromSpace;using StockWaterSceneLoader::doBind;using StockWaterSceneLoader::doUnload;};
using Definition=wot::StockSpaceWaterData::Definition;using Bounds=wot::StockSpaceWaterData::Bounds;
void scalar(Definition& d,size_t offset,float f){std::memcpy(&d.state[offset/4],&f,4);}
Definition definition(float x,float y,uint first,uint end){Definition d={};scalar(d,0,x);scalar(d,4,y);scalar(d,12,20);scalar(d,16,40);scalar(d,24,10);scalar(d,0x5c,10);scalar(d,0x80,10);d.state[0x88/4]=1;d.firstCell=first;d.endCell=end;return d;}
template<class T>void append(std::vector<char>& out,const std::vector<T>& values){uint32 n=sizeof(T),count=uint32(values.size());auto add=[&](const void* bytes,size_t size){const char* p=static_cast<const char*>(bytes);out.insert(out.end(),p,p+size);};add(&n,4);add(&count,4);if(count)add(values.data(),count*sizeof(T));}
bool singleSurface=false;
CompiledSpace::BinaryFormat fixture(){CompiledSpace::BinaryFormat file;
 if(singleSurface){append(file.bytes,std::vector<Definition>{definition(0,0,0,1)});append(file.bytes,std::vector<Bounds>{{{{-10,0,-20}},{{10,.001f,20}}}});return file;}
 // Surface 0 is farther than surface 1 at the same x/z. Surface 2 is far away;
 // surface 3 owns only a disabled cell and must never be admitted.
 append(file.bytes,std::vector<Definition>{definition(0,0,0,1),definition(0,5,1,2),definition(500,2,2,3),definition(1000,3,3,4)});
 append(file.bytes,std::vector<Bounds>{{{{-1,0,-1}},{{1,.001f,1}}},{{{-10,0,-20}},{{10,.001f,20}}},{{{-10,0,-20}},{{10,.001f,20}}},{{{1,1,1}},{{-1,-1,-1}}}});return file;}
float ray(float x=0,bool alpha=false,float endY=-10){return collideStockWater({x,10,0},{x,endY,0},alpha);}
void completeBackground(){auto tasks=pending;pending.clear();for(auto& task:tasks)task->doBackgroundTask(taskManager);}
void completeForeground(){auto tasks=taskManager.foreground;taskManager.foreground.clear();for(auto& task:tasks)task->doMainThreadTask(taskManager);}
void complete(){completeBackground();completeForeground();}
bool load(Loader& loader,const ClientSpacePtr& space){auto file=fixture();CompiledSpace::StringTable strings;return loader.doLoadFromSpace(space.get(),file,0,Matrix(),strings);}
}
int main(int argc,char**){using namespace BW;const bool baseline=argc>1;
 auto space=std::make_shared<CompiledSpace::CompiledSpace>(7);DeprecatedSpaceHelpers::camera=space;Loader first;
 assert(load(first,space));assert(ray()==-1);assert(first.doBind());assert(space->scene.objects.size()==3);
 auto* w=Waters::instance()[0];assert(!w->initialised_&&!w->stockQueryDataReady_);assert(ray()==-1);
 pending.front()->doBackgroundTask(taskManager);pending.erase(pending.begin());
 assert(ray()==-1); // Background tables alone do not publish readiness.
 completeForeground();
 if(baseline){assert(ray()==-1);std::puts("Original completion without CPU publication: worker misses water");first.doUnload();pending.clear();goto cleanup;}
 assert(ray()==10);assert(!w->initialised_&&w->visible_&&drawInit==0);
 complete();assert(ray(500)==8&&ray(1000)==-1&&ray(200)==-1);
 assert(ray(9)==10); // An enabled cell admits the whole original surface, not a clipped cell.
 assert(ray(0,false,9)==10); // Preserve infinite-ray behavior beyond end.
 assert(ray()==10); // Original surface order wins over the closer second surface (5).
 assert(ray(0,true)==10);w->wAlpha_.assign(w->wAlpha_.size(),0);assert(ray(0,true)==5);assert(ray()==10);
 w->visible_=false;assert(ray()==10);wot::hostWorker=false;assert(ray()==-1);
 w->initialised_=true;assert(ray()==-1);w->visible_=true;assert(ray()==10);wot::hostWorker=true;
 w->initialised_=false;w->createManagedObjects();assert(ray()==10); // GPU resets do not retire CPU geometry.
 duringCPU=[](Water* surface){assert(!surface->stockQueryDataReady_);assert(surface->stockGroundRayDistance({0,10,0},{0,-10,0})==-1);};
 {auto state=w->state_;state.position_.y=1;w->rebuild(state,w->resources_);}duringCPU={};assert(ray()==9);
 space->retired=true;assert(ray()==-1);space->retired=false;
 {auto other=std::make_shared<CompiledSpace::CompiledSpace>(7);DeprecatedSpaceHelpers::camera=other;assert(ray()==-1);DeprecatedSpaceHelpers::camera=space;}
 space->scene.onRemove=[&](){assert(ray()==-1&&ray(500)==-1);assert(!load(first,space));first.doUnload();};
 first.doUnload();space->scene.onRemove={};assert(ray()==-1&&Waters::instance().empty());first.doUnload();
 assert(load(first,space));assert(first.doBind());complete();assert(ray()==10&&ray(500)==8);first.doUnload();
 // Unload while native membership admission reenters: no partial registry.
 assert(load(first,space));space->scene.onAdd=[&](){assert(ray()==-1);assert(!first.doBind());first.doUnload();};
 assert(!first.doBind());space->scene.onAdd={};assert(space->scene.objects.empty()&&Waters::instance().empty());complete();assert(ray()==-1);
 // A previous mapping's removal cannot revoke a newly bound mapping in another loader.
 {Loader replacement;assert(load(first,space)&&first.doBind());complete();bool rebound=false;
  space->scene.onRemove=[&](){if(!rebound){rebound=true;assert(load(replacement,space)&&replacement.doBind());complete();assert(ray()==10);}};
  first.doUnload();space->scene.onRemove={};assert(ray()==10);replacement.doUnload();}
 assert(Waters::instance().empty());
 singleSurface=true;
 // A completed old producer is queued for the main thread, then its Water's
 // allocation is reused. That callback cannot publish the new owner's tables.
 assert(load(first,space)&&first.doBind());
 {Water* old=Waters::instance()[0];completeBackground();first.doUnload();
  assert(load(first,space)&&first.doBind());Water* fresh=Waters::instance()[0];assert(old==fresh);
  assert(!fresh->stockQueryDataReady_&&ray()==-1);const auto alpha=fresh->wAlpha_;
  completeForeground();assert(!fresh->stockQueryDataReady_&&fresh->wAlpha_==alpha&&ray()==-1);
  complete();assert(ray()==10);first.doUnload();}
 // A producer that has not started must likewise reject the recycled address.
 assert(load(first,space)&&first.doBind());
 {Water* old=Waters::instance()[0];auto oldTasks=pending;pending.clear();first.doUnload();
  assert(load(first,space)&&first.doBind());Water* fresh=Waters::instance()[0];assert(old==fresh);
  const auto alpha=fresh->wAlpha_;for(auto& task:oldTasks)task->doBackgroundTask(taskManager);
  completeForeground();assert(!fresh->stockQueryDataReady_&&fresh->wAlpha_==alpha&&ray()==-1);
  oldTasks.clear();complete();assert(ray()==10);first.doUnload();}
 // Rebuilding the same allocation invalidates its previously queued producer.
 assert(load(first,space)&&first.doBind());
 {Water* current=Waters::instance()[0];auto state=current->state_;state.position_.y=3;
  current->rebuild(state,current->resources_);current->wAlpha_.assign(current->wAlpha_.size(),0x12345678);
  const auto alpha=current->wAlpha_;complete();assert(current->wAlpha_==alpha&&ray()==7);first.doUnload();}
 assert(liveTasks==0);
 // Task-owned context survives either queue and is released if a queued task
 // is discarded during shutdown, without depending on its completion callback.
 assert(load(first,space)&&first.doBind());assert(liveTasks==1);pending.clear();assert(liveTasks==0);first.doUnload();
 assert(load(first,space)&&first.doBind());completeBackground();assert(liveTasks==1);
 taskManager.foreground.clear();assert(liveTasks==0);first.doUnload();
 Water::background=false;assert(load(first,space)&&first.doBind());assert(ray()==10&&liveTasks==0);first.doUnload();Water::background=true;
 std::puts("Worker water: producer readiness, all-callsite dispatch, space identity, alpha, order, disabled cells, unload and rebind passed");
cleanup:
 for(auto entry:Water::s_renderTargetMap_)delete entry.second;Water::s_renderTargetMap_.clear();
 for(void* slot:Water::reuseSlots)::operator delete(slot);Water::reuseSlots.clear();
 assert(!liveTasks&&!Water::deletionLock_);
 return 0;
}
