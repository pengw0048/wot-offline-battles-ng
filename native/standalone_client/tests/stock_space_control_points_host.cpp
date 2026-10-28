// The SDK PY_AUTO converters and SuperModel::draw body are inserted unchanged.
// D3D/model loading are guarded fixture boundaries; no Windows pixels are claimed.
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-register"
#endif
#include <Python.h>
#ifdef __clang__
#pragma clang diagnostic pop
#endif
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>
#define BW_BEGIN_NAMESPACE namespace BW {
#define BW_END_NAMESPACE }
#define BW_GUARD
#define BW_GUARD_PROFILER(...)
#define TRANSFORM_ASSERT(x) assert(x)
#define MF_ASSERT(x) assert(x)
inline void assetMessage(const char*,const char*){}
#define ASSET_MSG(...) assetMessage(__VA_ARGS__)
#define BW_STATIC_ASSERT(x,m) static_assert(x,#m)
using uint32=unsigned; using uint=unsigned; using int64=long long;
namespace BW {
using string=std::string;template<class T>using vector=std::vector<T>;
template<class T>struct Ptr:std::shared_ptr<T>{using Base=std::shared_ptr<T>;Ptr()=default;Ptr(T* p):Base(p){}Ptr(std::nullptr_t):Base(){}template<class U>Ptr(const Ptr<U>& p):Base(p){}T* getObject()const{return this->get();}bool exists()const{return bool(*this);}};
template<class T>using SmartPointer=Ptr<T>;
class Vector4{public:float x,y,z,w;Vector4(float a=0,float b=0,float c=0,float d=0):x(a),y(b),z(c),w(d){}float& operator[](unsigned i){return (&x)[i];}};
class Vector3{public:float x,y,z;Vector3(float a=0,float b=0,float c=0):x(a),y(b),z(c){}Vector3 operator+(Vector3 p)const{return {x+p.x,y+p.y,z+p.z};}Vector3 operator-(Vector3 p)const{return {x-p.x,y-p.y,z-p.z};}float lengthSquared()const{return x*x+y*y+z*z;}};
struct Matrix{float v[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};static const Matrix identity;void row(unsigned i,Vector4 p){std::memcpy(v+i*4,&p,16);}Vector3 applyToOrigin()const{return {v[12],v[13],v[14]};}void translation(Vector3 p){v[12]=p.x;v[13]=p.y;v[14]=p.z;}void setScale(float a,float b,float c){*this=identity;v[0]=a;v[5]=b;v[10]=c;}};const Matrix Matrix::identity;
struct D3DXVECTOR3:Vector3{using Vector3::Vector3;};
void D3DXVec3TransformNormal(D3DXVECTOR3* out,const D3DXVECTOR3* p,const Matrix* m){*out={p->x*m->v[0]+p->y*m->v[4]+p->z*m->v[8],p->x*m->v[1]+p->y*m->v[5]+p->z*m->v[9],p->x*m->v[2]+p->y*m->v[6]+p->z*m->v[10]};}
void D3DXMatrixMultiply(Matrix* out,const Matrix* a,const Matrix* b){Matrix r;for(int i=0;i<4;++i)for(int j=0;j<4;++j){r.v[i*4+j]=0;for(int k=0;k<4;++k)r.v[i*4+j]+=a->v[i*4+k]*b->v[k*4+j];}*out=r;}
namespace Math{template<class T>T clamp(T lo,T v,T hi){return v<lo?lo:v>hi?hi:v;}}
class BoundingBox{public:Vector3 low,high;bool empty=true;static BoundingBox s_insideOut_;void transformBy(const Matrix& m){low=low+m.applyToOrigin();high=high+m.applyToOrigin();}void addBounds(const BoundingBox& b){if(empty){*this=b;return;}low={std::min(low.x,b.low.x),std::min(low.y,b.low.y),std::min(low.z,b.low.z)};high={std::max(high.x,b.high.x),std::max(high.y,b.high.y),std::max(high.z,b.high.z)};}};BoundingBox BoundingBox::s_insideOut_;using AABB=BoundingBox;
struct DataSection{std::string shader;void deleteSections(const char* n){assert(std::string(n)=="fx");shader.clear();}void writeString(const char* n,const char* v){assert(std::string(n)=="fx");shader=v;}};using DataSectionPtr=Ptr<DataSection>;struct XMLSection:DataSection{XMLSection(const char*){}};
struct ID3DXEffect{unsigned cleared=0;void SetTexture(const char*,void*){++cleared;}};
struct SceneObject{void* value=nullptr;struct Flags{bool dynamic=true,reflection=false,staticShadow=false,dynamicShadow=false;void isDynamic(bool v){dynamic=v;}void isReflectionVisible(bool v){reflection=v;}void isCastingStaticShadow(bool v){staticShadow=v;}void isCastingDynamicShadow(bool v){dynamicShadow=v;}} bits;template<class T>void handleAndType(T* p){value=p;}template<class T>T* getAs()const{return static_cast<T*>(value);}Flags& flags(){return bits;}};
namespace Moo{
struct BaseTexture{std::string name;explicit BaseTexture(std::string n):name(n){}};using BaseTexturePtr=Ptr<BaseTexture>;
struct TextureManager{std::vector<std::string> reads;static TextureManager* instance(){static TextureManager v;return &v;}BaseTexturePtr get(const std::string& n,bool,bool,bool,const char*){reads.push_back(n);return new BaseTexture(n);}};
struct EffectProperty{bool privateOwner=false;Ptr<EffectProperty> clone(){auto* p=new EffectProperty;p->privateOwner=true;return p;}};
struct EffectMaterial{struct Properties:std::map<unsigned,Ptr<EffectProperty>>{ID3DXEffect binding;ID3DXEffect* existingStockBinding(){return &binding;}} props;std::string shader;std::map<std::string,Vector4> vectors;std::map<std::string,float> floats;std::map<std::string,BaseTexturePtr> textures;void save(DataSectionPtr d){d->shader=shader;}Properties& properties(){return props;}void check(){assert(!props.empty());for(auto& p:props)assert(p.second->privateOwner);}bool setProperty(const char* n,Vector4 v,bool){check();assert(std::string(n)=="g_flagColor"||std::string(n)=="g_emblemTC");vectors[n]=v;return true;}bool setProperty(const char* n,float v,bool){check();assert(std::string(n)=="g_alpha");floats[n]=v;return true;}bool setProperty(const char* n,BaseTexturePtr v,bool){check();assert(std::string(n)=="flagEmblemMap"||std::string(n)=="flagBackgroundMap");textures[n]=v;return true;}};
using EffectMaterialPtr=Ptr<EffectMaterial>;
struct ComplexEffectMaterial{EffectMaterialPtr material=new EffectMaterial;static bool fail;bool load(DataSectionPtr s){material->shader=s->shader;static Ptr<EffectProperty> shared=new EffectProperty;material->props[1]=shared;return !fail;}EffectMaterialPtr baseMaterial()const{return material;}};bool ComplexEffectMaterial::fail=false;using ComplexEffectMaterialPtr=Ptr<ComplexEffectMaterial>;
struct Visual{struct PrimitiveGroup{ComplexEffectMaterialPtr material_=new ComplexEffectMaterial;};};
struct Node{Matrix m;const Matrix& worldTransform()const{return m;}};using NodePtr=Ptr<Node>;
enum ERenderingPassType{RENDERING_PASS_COLOR,RENDERING_PASS_SHADOWS};
struct DrawContext{unsigned immediate=0;ERenderingPassType pass=RENDERING_PASS_COLOR;void pushImmediateMode(){++immediate;}void popImmediateMode(){assert(immediate);--immediate;}ERenderingPassType renderingPassType()const{return pass;}};
struct RC{Matrix camera;const Matrix& invView()const{return camera;}unsigned frameTimestamp(){return 1;}};RC& rc(){static RC r;return r;}
}
struct SuperModel;
struct Fashion{virtual~Fashion()=default;};struct MaterialFashion:Fashion{virtual void dress(SuperModel&)=0;virtual void undress(SuperModel&){};};using MaterialFashionPtr=Ptr<MaterialFashion>;using MaterialFashionVector=std::vector<MaterialFashionPtr>;
struct ModelAnimation{float duration_=2.f;};struct SuperModelAnimation{float time=0.f;ModelAnimation source;const ModelAnimation* pSource(SuperModel&){return &source;}};using SuperModelAnimationPtr=Ptr<SuperModelAnimation>;using TmpTransforms=std::vector<SuperModelAnimation*>;
struct Model{static constexpr float LOD_AUTO_CALCULATE=-1.f;Moo::Visual::PrimitiveGroup group;Model* parent(){return nullptr;}void gatherMaterials(const char*,std::vector<Moo::Visual::PrimitiveGroup*>& out){out.push_back(&group);}static void incrementBlendCookie(){}};using ModelPtr=Ptr<Model>;
struct Sample{bool flag;Vector4 colour,uv;std::string emblem;float alpha,time;Vector3 position;};std::vector<Sample> samples;
struct SuperModel{std::string resource;ModelPtr model;Matrix world;float animationTime=0;static std::map<std::string,ModelPtr> shared;explicit SuperModel(const std::vector<std::string>& names):resource(names[0]){auto& entry=shared[resource];if(!entry)entry=new Model;model=entry;}bool isOK()const{return resource!="missing";}int nModels()const{return 1;}ModelPtr topModel(int){return model;}void cacheDefaultTransforms(const Matrix&){}Moo::NodePtr findNode(const char* name){assert(std::string(name)=="HP_UpFlag"||std::string(name)=="HP_FlagSound");Moo::NodePtr n=new Moo::Node;n->m.translation(std::string(name)=="HP_UpFlag"?Vector3(.5f,16.3f,0):Vector3(1,17,0));return n;}SuperModelAnimationPtr getAnimation(const char* name){assert(std::string(name)=="flag");return new SuperModelAnimation;}void localVisibilityBox(BoundingBox& b){b.empty=false;b.low={-1,0,-1};b.high={1,20,1};}void updateAnimations(const Matrix& m,const TmpTransforms* t,void*,float){world=m;if(t&&!t->empty())animationTime=(*t)[0]->time;}bool isTransformSane(const Matrix&)const{return true;}bool isLodVisible()const{return true;}bool isAnyVisibleDirtyTransforms(unsigned)const{return false;}void applyMaterialsAndTransforms(const MaterialFashionVector* fashions){if(fashions)for(auto& f:*fashions)f->dress(*this);}void removeMaterials(const MaterialFashionVector* fashions){if(fashions)for(auto i=fashions->rbegin();i!=fashions->rend();++i)(*i)->undress(*this);}void drawModels(Moo::DrawContext& c,const Matrix& m){assert(c.immediate==1);auto p=model->group.material_->baseMaterial();bool flag=resource=="flag";if(flag){assert(p->vectors.count("g_flagColor")&&p->vectors.count("g_emblemTC")&&p->textures.count("flagBackgroundMap"));}auto e=p->textures["flagEmblemMap"];samples.push_back({flag,p->vectors["g_flagColor"],p->vectors["g_emblemTC"],e?e->name:"",p->floats.count("g_alpha")?p->floats["g_alpha"]:1.f,animationTime,m.applyToOrigin()});}void draw(Moo::DrawContext&,const Matrix&,const MaterialFashionVector*);};std::map<std::string,ModelPtr> SuperModel::shared;
struct DogWatch{explicit DogWatch(const char*){}};struct ScopedDogWatch{explicit ScopedDogWatch(DogWatch&){}};
@SDK_SUPER_MODEL_DRAW@
class SceneDrawContext{public:Moo::DrawContext context;Moo::DrawContext& drawContext()const{return const_cast<Moo::DrawContext&>(context);}};
struct IDrawOperationTypeHandler{virtual~IDrawOperationTypeHandler()=default;virtual void doDraw(const SceneDrawContext&,const SceneObject*,size_t)=0;};struct ISpatialQueryOperationTypeHandler{virtual~ISpatialQueryOperationTypeHandler()=default;virtual void getWorldTransform(const SceneObject&,Matrix*)=0;virtual void getWorldVisibilityBoundingBox(const SceneObject&,AABB*)=0;};struct ITickOperationTypeHandler{virtual~ITickOperationTypeHandler()=default;virtual void doTick(SceneObject*,size_t,float)=0;virtual void doUpdateAnimations(SceneObject*,size_t,float)=0;};
template<class H>struct Operation{std::unique_ptr<H> handler;template<class T,class V>void addHandler(){handler.reset(new V);}};using DrawOperation=Operation<IDrawOperationTypeHandler>;using SpatialQueryOperation=Operation<ISpatialQueryOperationTypeHandler>;using TickOperation=Operation<ITickOperationTypeHandler>;
class Scene{public:DrawOperation draw;SpatialQueryOperation spatial;TickOperation tick;template<class T>T* getObjectOperation();};template<>DrawOperation* Scene::getObjectOperation(){return &draw;}template<>SpatialQueryOperation* Scene::getObjectOperation(){return &spatial;}template<>TickOperation* Scene::getObjectOperation(){return &tick;}
struct DynamicObjectHandle{unsigned id;};struct DynamicSceneProvider{unsigned next=1;std::map<unsigned,SceneObject> objects;std::function<void()> onAdd,onRemove;DynamicObjectHandle addObject(SceneObject o,const Matrix&,const BoundingBox& collision,const BoundingBox& visibility){assert(collision.empty&&!visibility.empty&&!o.bits.dynamic&&o.bits.reflection&&o.bits.staticShadow&&o.bits.dynamicShadow);auto id=next++;objects[id]=o;if(onAdd)onAdd();return {id};}void removeObject(DynamicObjectHandle h){assert(objects.erase(h.id)==1);if(onRemove)onRemove();}};
class ClientSpace{public:virtual~ClientSpace()=default;};using ClientSpacePtr=std::shared_ptr<ClientSpace>;
namespace CompiledSpace{
struct FourCC{std::string value;FourCC(const char* p):value(p){}};struct BinaryFormat{static constexpr size_t NOT_FOUND=size_t(-1);struct Stream{std::vector<char> bytes;size_t size(){return bytes.size();}const void* readRaw(size_t n){assert(n==bytes.size());return bytes.data();}}stream;unsigned version=2,opens=0,closes=0;bool missing=false;size_t findSection(FourCC v){assert(v.value=="WTCP");return missing?size_t(NOT_FOUND):0;}unsigned sectionVersion(size_t){return version;}Stream* openSection(size_t,bool){++opens;return &stream;}void closeSection(Stream*){++closes;std::fill(stream.bytes.begin(),stream.bytes.end(),'x');}};
struct StringTable{std::map<unsigned,std::string> strings={{1,"flag"},{2,"staff"},{3,"radius"},{4,"sound"}};const char* entry(unsigned n)const{auto i=strings.find(n);return i==strings.end()?nullptr:i->second.c_str();}};
struct CompiledSpace:ClientSpace{Scene scene_;DynamicSceneProvider provider;Scene& scene(){return scene_;}DynamicSceneProvider& dynamicScene(){return provider;}};
struct ILoader{virtual~ILoader()=default;bool loadFromSpace(ClientSpace* s,BinaryFormat& f,const DataSectionPtr& d,const Matrix& m,const StringTable& t){return doLoadFromSpace(s,f,d,m,t);}bool bind(){return doBind();}void unload(){doUnload();}virtual bool doLoadFromSpace(ClientSpace*,BinaryFormat&,const DataSectionPtr&,const Matrix&,const StringTable&)=0;virtual bool doBind()=0;virtual void doUnload()=0;virtual float percentLoaded()const=0;};
}
struct HostLink{const char* name;PyCFunction function;};std::vector<HostLink>& links(){static std::vector<HostLink> v;return v;}struct PyModuleMethodLink{PyModuleMethodLink(const char* module,const char* name,PyCFunction f,const char*){assert(std::string(module)=="BigWorld");links().push_back({name,f});}};
struct PyObjectPtr{enum{STEAL_REFERENCE};PyObject* value=nullptr;PyObjectPtr()=default;PyObjectPtr(PyObject* p,int):value(p){}PyObjectPtr(const PyObjectPtr& p):value(p.value){Py_XINCREF(value);}~PyObjectPtr(){Py_XDECREF(value);}PyObjectPtr& operator=(const PyObjectPtr& p){Py_XINCREF(p.value);Py_XDECREF(value);value=p.value;return *this;}PyObject* get()const{return value;}};
namespace Script{int setData(PyObject*,int&,const char* = "");int setData(PyObject*,BW::string&,const char* = "");int setData(PyObject*,Vector4&,const char* = "");PyObject* argCountError(const char*,int,int,...);}
template<class T>struct PyVector{static bool Check(PyObject*){return false;}T getVector(){return {};}};
@SCRIPT_BODIES@
@PYAUTO_MACROS@
}
#include "stock_space_control_points.cpp"
namespace BW {
struct StockControlPointRadius {bool active=true;};
std::shared_ptr<StockControlPointRadius> loadStockControlPointRadius(const char* resource,ClientSpace*,const Vector3&,float,float,const Vector4&){assert(std::string(resource)=="radius");return std::make_shared<StockControlPointRadius>();}
void drawStockControlPointRadius(const std::shared_ptr<StockControlPointRadius>& owner,const SceneDrawContext&){assert(owner&&owner->active);}
void retireStockControlPointRadius(const std::shared_ptr<StockControlPointRadius>& owner){if(owner)owner->active=false;}
bool stockControlPointRadiusBounds(const std::shared_ptr<StockControlPointRadius>&,BoundingBox& b){b.empty=false;b.low={0,0,0};b.high={1,1,1};return true;}
struct StockSpaceSoundOwner {unsigned next=0;std::set<unsigned> live;};
StockSpaceSounds acquireStockSpaceSounds(ClientSpace*){return std::make_shared<StockSpaceSoundOwner>();}
StockSpaceSoundHandle addStockSpaceControlPointSound(const StockSpaceSounds& o,const BW::string& e,const Vector3& p){assert(e=="sound");assert(p.x==101||p.x==201);assert(p.y==37);auto id=o->next++;o->live.insert(id);return id;}
void removeStockSpaceSound(const StockSpaceSounds& o,StockSpaceSoundHandle id){assert(o->live.erase(unsigned(id))==1);}
}

using namespace BW;
using namespace BW::CompiledSpace;
void populate(BinaryFormat& file){std::vector<wot::StockControlPointData::Definition> defs(3);for(unsigned i=0;i<3;++i){auto& d=defs[i];std::memcpy(d.transform,Matrix::identity.v,64);d.transform[12]=100.f+float(i)*100;d.transform[13]=20;d.team=i;d.flag=1;d.flagstaff=2;d.radiusVisual=3;d.sound=4;d.flagScale=3;d.visibilityMask=i==2?2:1;}file.stream.bytes.resize(8+defs.size()*124);unsigned header[]={124,unsigned(defs.size())};std::memcpy(file.stream.bytes.data(),header,8);std::memcpy(file.stream.bytes.data()+8,defs.data(),defs.size()*124);}
PyObject* checkOwner(PyObject*,PyObject*){
    BW::CompiledSpace::CompiledSpace space;BinaryFormat file;StringTable strings;Matrix mapping;mapping.translation({9000,9000,9000});StockControlPointSceneLoader loader(1);StockControlPointSceneLoader::registerHandlers(space.scene());populate(file);
    assert(loader.loadFromSpace(&space,file,{},mapping,strings)&&file.opens==file.closes&&loader.bind()&&loader.bind()&&space.provider.objects.size()==2);
    std::vector<SceneObject> objects;for(auto& v:space.provider.objects)objects.push_back(v.second);
    auto originalFlag=SuperModel::shared["flag"]->group.material_;auto originalStaff=SuperModel::shared["staff"]->group.material_;
    SceneDrawContext draw;space.scene().tick.handler->doTick(objects.data(),objects.size(),.25f);space.scene().tick.handler->doUpdateAnimations(objects.data(),objects.size(),.25f);
    samples.clear();space.scene().draw.handler->doDraw(draw,objects.data(),objects.size());assert(samples.size()==4&&draw.context.immediate==0);
    assert(samples[0].flag&&samples[0].colour.x==1.f&&samples[0].emblem=="system/maps/wg_emblem.dds");assert(samples[0].uv.x==2.f&&std::fabs(samples[0].uv.y-1.25f)<1e-6f&&std::fabs(samples[0].uv.w+.125f)<1e-6f);assert(samples[0].position.x==100.5f&&samples[0].position.y==36.3f&&samples[0].time==.25f);
    assert(samples[2].flag&&samples[2].colour.x==0.f&&samples[2].colour.y==1.f);assert(SuperModel::shared["flag"]->group.material_==originalFlag&&SuperModel::shared["staff"]->group.material_==originalStaff);
    space.scene().draw.handler->doDraw(draw,objects.data(),objects.size());assert(samples[4].time==.25f);
    Moo::rc().camera.translation({100,20,0});space.scene().draw.handler->doDraw(draw,objects.data(),objects.size());assert(samples[8].alpha==.15f&&samples[9].alpha==.15f);draw.context.pass=Moo::RENDERING_PASS_SHADOWS;space.scene().draw.handler->doDraw(draw,objects.data(),objects.size());assert(samples[12].alpha==1.f&&samples[13].alpha==1.f);draw.context.pass=Moo::RENDERING_PASS_COLOR;Moo::rc().camera.translation({0,0,0});
    FlagFashion retained;FlagSettings first;first.emblem="valid";retained.apply(first);auto oldTexture=retained.emblem;FlagSettings cleared;retained.apply(cleared);assert(retained.emblem==oldTexture);FlagFashion newborn;newborn.apply(cleared);assert(!newborn.emblem);
    space.provider.onRemove=[&]{loader.unload();space.scene().draw.handler->doDraw(draw,objects.data(),objects.size());};loader.unload();loader.unload();assert(space.provider.objects.empty()&&samples.size()==16);space.provider.onRemove={};
    populate(file);assert(loader.loadFromSpace(&space,file,{},mapping,strings)&&loader.bind());objects.clear();for(auto& v:space.provider.objects)objects.push_back(v.second);space.scene().tick.handler->doUpdateAnimations(objects.data(),objects.size(),0);samples.clear();space.scene().draw.handler->doDraw(draw,objects.data(),objects.size());assert(samples[0].emblem=="system/maps/wg_emblem.dds"&&samples[2].colour.y==1.f);loader.unload();
    populate(file);space.provider.onAdd=[&]{loader.unload();};assert(loader.loadFromSpace(&space,file,{},mapping,strings)&&!loader.bind()&&space.provider.objects.empty());space.provider.onAdd={};
    file.version=1;populate(file);assert(!loader.loadFromSpace(&space,file,{},mapping,strings));file.version=2;populate(file);Moo::ComplexEffectMaterial::fail=true;assert(loader.loadFromSpace(&space,file,{},mapping,strings)&&loader.bind()&&space.provider.objects.empty());loader.unload();Moo::ComplexEffectMaterial::fail=false;
    Py_RETURN_NONE;
}
int main(int argc,char** argv){assert(argc==3);Py_Initialize();std::vector<PyMethodDef> methods;for(auto& link:links())methods.push_back({const_cast<char*>(link.name),link.function,METH_VARARGS,NULL});methods.push_back({NULL,NULL,0,NULL});PyObject* module=Py_InitModule("BigWorld",methods.data());assert(module);static PyMethodDef checks[]={{const_cast<char*>("checkOwner"),checkOwner,METH_NOARGS,NULL},{NULL,NULL,0,NULL}};Py_InitModule("_flags",checks);PySys_SetArgv(argc-1,argv+1);FILE* input=fopen(argv[1],"rb");assert(input);int result=PyRun_SimpleFile(input,argv[1]);fclose(input);Py_Finalize();return result?1:0;}
