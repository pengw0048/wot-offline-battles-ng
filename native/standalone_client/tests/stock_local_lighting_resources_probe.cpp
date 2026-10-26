// Compile the actual DEFAULT-pool resource and target scope methods below.
#include "wot/stock_local_lighting.hpp"
#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <map>
#include <memory>
#include <string>
#include <vector>
using uint=unsigned;using uint32=unsigned;using DWORD=unsigned;using HRESULT=int;using UINT=unsigned;
#define FAILED(x) ((x)<0)
#define SUCCEEDED(x) ((x)>=0)
#define ERROR_MSG(...) (++errors)
static unsigned errors=0;
enum{D3DVERTEXTEXTURESAMPLER0=256,D3DUSAGE_DYNAMIC=1,D3DUSAGE_WRITEONLY=2,D3DPOOL_DEFAULT=0,D3DFMT_G32R32F=115,D3DFMT_G16R16F=112,D3DFMT_D24S8=75,D3DUSAGE_RENDERTARGET=4,D3DMULTISAMPLE_NONE=0,TRUE=1,IID_IDirect3DBaseTexture9=1};
namespace DX{
struct BaseTexture{unsigned refs=1;bool dead=false;virtual ~BaseTexture(){assert(!refs);}
void AddRef(){assert(refs);++refs;}void Release(){assert(refs);if(!--refs){dead=true;delete this;}}};
struct Surface;struct CubeTexture:BaseTexture{int GetCubeMapSurface(int face,unsigned level,Surface**);};
struct Surface:BaseTexture{BaseTexture* texture=nullptr;bool failContainer=false;int GetContainer(int,void** result){if(failContainer||!texture)return -1;texture->AddRef();*result=texture;return 0;}};
int CubeTexture::GetCubeMapSurface(int face,unsigned level,Surface** p){assert(face<6&&!level);*p=new Surface;(*p)->texture=this;return 0;}
struct Viewport{uint X,Y,Width,Height;float MinZ,MaxZ;};
}
template<class T>struct ComObjectWrap{
T* value=nullptr;ComObjectWrap()=default;ComObjectWrap(const ComObjectWrap&)=delete;
~ComObjectWrap(){if(value)value->Release();}T** operator&(){assert(!value);return &value;}
ComObjectWrap& operator=(T* p){if(p)p->AddRef();if(value)value->Release();value=p;return *this;}
operator bool()const{return value!=nullptr;}T* operator->()const{return value;}T* pComObject()const{return value;}};
using D3DCUBEMAP_FACES=int;
const float MATH_PI=3.14159265358979323846f;
enum{D3DCLEAR_TARGET=1,D3DCLEAR_ZBUFFER=2,D3DCLEAR_STENCIL=4};
struct Vector3{float x=0,y=0,z=0;Vector3(float a=0,float b=0,float c=0):x(a),y(b),z(c){};};
struct BoundingBox{Vector3 low{-2,-3,-4},high{5,6,7};const Vector3& minBounds()const{return low;}const Vector3& maxBounds()const{return high;}};
struct Vector4{float x=0,y=0,z=0,w=0;Vector4()=default;static Vector4 zero(){return {};}void set(float a,float b,float c,float d){x=a;y=b;z=c;w=d;}Vector4(float a,float b,float c,float d):x(a),y(b),z(c),w(d){}};
struct Matrix{int tag=0;Vector3 origin;static Matrix identity;
void setRotateX(float a){tag=a>0?3:2;}void setRotateY(float a){tag=std::fabs(a)>3?5:(a>0?0:1);}
void translation(const Vector3& p){origin=p;}void invert(const Matrix& m){*this=m;}
void perspectiveProjection(float f,float aspect,float n,float far){assert(aspect==1&&f>0&&n>0&&far>n);tag=100;}
void multiply(const Matrix&,const Matrix&){tag=200;}};Matrix Matrix::identity;
struct ShadowParameters{int algorithm=0;Vector4 parameters;int colourMask=15;};ShadowParameters shadowParameters;
struct ID3DXEffect{DX::BaseTexture* texture=nullptr;bool failTexture=false;~ID3DXEffect(){if(texture)texture->Release();}
int SetTexture(const char*,DX::BaseTexture* p){if(failTexture)return -1;if(p)p->AddRef();if(texture)texture->Release();texture=p;return 0;}
int SetInt(const char*,int){return 0;}int SetBool(const char*,bool){return 0;}int SetFloat(const char*,float){return 0;}int SetVector(const char*,const Vector4*){return 0;}};
struct Device{
std::map<unsigned,DX::BaseTexture*> samplers;int failGet=-1,failSet=-1;bool failCreateCube=false,failCreateDepth=false,failDepth=false;unsigned textureCalls=0,cubeCreates=0,depthCreates=0;
int GetTexture(unsigned slot,DX::BaseTexture** p){++textureCalls;if(int(slot)==failGet)return -1;*p=samplers[slot];if(*p)(*p)->AddRef();return 0;}
int SetTexture(unsigned slot,DX::BaseTexture* p){if(int(slot)==failSet)return -1;if(p)p->AddRef();if(samplers[slot])samplers[slot]->Release();samplers[slot]=p;return 0;}
unsigned clears=0;bool failClear=false;
int Clear(unsigned n,void*,unsigned flags,unsigned colour,float z,unsigned stencil){assert(!n&&flags==7&&colour==0xffffffff&&z==1&&!stencil);++clears;return failClear?-1:0;}
int SetDepthStencilSurface(DX::Surface*){return failDepth?-1:0;}
int CreateCubeTexture(unsigned w,unsigned levels,unsigned usage,unsigned format,unsigned pool,DX::CubeTexture** p,void*){assert(w==128&&levels==1&&usage==4&&format==112&&!pool);++cubeCreates;if(failCreateCube)return -1;*p=new DX::CubeTexture;return 0;}
int CreateDepthStencilSurface(unsigned w,unsigned h,unsigned format,unsigned multi,unsigned quality,int discard,DX::Surface** p,void*){assert(w==128&&h==128&&format==75&&!multi&&!quality&&discard);++depthCreates;if(failCreateDepth)return -1;*p=new DX::Surface;return 0;}
~Device(){for(auto& pair:samplers)if(pair.second)pair.second->Release();}
};
namespace Moo{
struct OmniLight{BoundingBox bounds;Vector3 p{2,3,4};const BoundingBox& worldBounds()const{return bounds;}const Vector3& worldPosition()const{return p;}float outerRadius()const{return 30;}};
struct SpotLight{float outerRadius()const{return 30;}float worldOuterRadius()const{return 60;}float cosConeAngle()const{return .8f;}};using OmniLightPtr=std::shared_ptr<OmniLight>;using SpotLightPtr=std::shared_ptr<SpotLight>;
struct VertexBuffer{bool live=false;unsigned releases=0;void release(){live=false;++releases;}int create(unsigned n,unsigned u,unsigned f,unsigned p,const char*){assert(n==64000&&u==3&&!f&&!p);live=true;return 0;}};
struct Visual{};using VisualPtr=std::shared_ptr<Visual>;
struct RenderTarget{
static unsigned alive,creates;static int failAt;DX::BaseTexture* texture=new DX::BaseTexture;DX::Surface* surface=new DX::Surface;
explicit RenderTarget(const char*){++alive;surface->texture=texture;}~RenderTarget(){--alive;surface->Release();texture->Release();}
bool create(unsigned w,unsigned h,bool cube,unsigned format,void* =nullptr,unsigned depth=0,bool reuse=true){assert(w==256&&h==256&&!cube&&format==115);assert(!depth||depth==75);assert(!depth||!reuse);return int(++creates)!=failAt;}
DX::BaseTexture* pTexture(){return texture;}int pSurface(ComObjectWrap<DX::Surface>& p){p=surface;return 0;}DX::Surface* depthBuffer(){return nullptr;}};
unsigned RenderTarget::alive=0,RenderTarget::creates=0;int RenderTarget::failAt=-1;struct RenderTargetPtr:std::shared_ptr<RenderTarget>{using std::shared_ptr<RenderTarget>::shared_ptr;RenderTargetPtr& operator=(RenderTarget* p){reset(p);return *this;}};
struct EffectMaterial{
ID3DXEffect definition,binding;struct Managed{ID3DXEffect* active;ID3DXEffect* pEffect(){return active;}}managed{&definition};
struct Properties{EffectMaterial* owner;bool select(Managed& m){m.active=&owner->binding;return true;}ID3DXEffect* existingStockBinding(){return &owner->binding;}}props{this};
bool setTechnique(const char*){return true;}Properties& properties(){return props;}Managed* pEffect(){return &managed;}};using EffectMaterialPtr=std::shared_ptr<EffectMaterial>;
enum{CONSTANTS_PER_SCREEN=1,CONSTANTS_ALL=2,RENDERING_PASS_SHADOWS=3};
struct Camera{float n,f,fov,aspect;Camera(float near=.1f,float far=1000.f,float field=1.f,float a=1.f):n(near),f(far),fov(field),aspect(a){}};
struct DrawContext{enum{OPAQUE_CHANNEL_MASK=1};unsigned phase;DrawContext(unsigned p):phase(p){assert(p==3);}void begin(unsigned m){assert(m==1);}void end(unsigned m){assert(m==1);}void flush(unsigned m){assert(m==1);}};
struct Context{Camera camera_;Matrix view_,projection_;void camera(const Camera& c){camera_=c;}const Camera& camera()const{return camera_;}void view(const Matrix& m){view_=m;}const Matrix& view()const{return view_;}void projection(const Matrix& m){projection_=m;}const Matrix& projection()const{return projection_;}void updateViewTransforms(){}Device gpu;bool failPush=false,failTarget=false,failViewport=false;unsigned pushes=0,pops=0,depth=0,updates=0,draws=0;bool drawSuccess=true;uint width=1920,height=1080;bool scene=true;
struct Snapshot{uint w,h;bool scene;};std::vector<Snapshot> stack;
void invalidateStateCache(){}Device* device(){return &gpu;}bool pushRenderTarget(){++pushes;if(failPush)return false;stack.push_back({width,height,scene});++depth;return true;}
void popRenderTarget(){++pops;assert(depth);--depth;auto saved=stack.back();stack.pop_back();width=saved.w;height=saved.h;scene=saved.scene;}
int setRenderTarget(unsigned,DX::Surface*){return failTarget?-1:0;}void screenWidth(uint w){width=w;}void screenHeight(uint h){height=h;}
struct Constants{Context* ctx;void updateSharedConstants(unsigned){++ctx->updates;}}constants{this};Constants& effectVisualContext(){return constants;}
struct Quad{Context* ctx;bool drawChecked(EffectMaterial& material,unsigned w,unsigned h,bool half){assert(w==256&&h==256&&half);++ctx->draws;material.managed.active=&material.definition;return ctx->drawSuccess;}}quad{this};Quad& fsQuad(){return quad;}};
Context& rc(){static Context ctx;return ctx;}
}
struct PlaneEq{Vector3 n;float d;PlaneEq(Vector3 normal,float distance):n(normal),d(distance){}};
struct ConvexHull{ConvexHull(unsigned count,PlaneEq* planes){assert(count==6&&planes[0].d==2&&planes[1].d==5&&planes[2].d==3&&planes[3].d==6&&planes[4].d==4&&planes[5].d==7);}};
struct IntersectionSet{};struct SceneIntersectContext;
struct IntersectSceneView{unsigned queries=0;void intersect(SceneIntersectContext&,ConvexHull&,IntersectionSet&){++queries;}};
struct Scene{IntersectSceneView view;template<class T>T* getView(){return &view;}};
struct SceneIntersectContext{explicit SceneIntersectContext(Scene&){}};
struct ClientSpace{Scene value;Scene& scene(){return value;}};using ClientSpacePtr=std::shared_ptr<ClientSpace>;
namespace DeprecatedSpaceHelpers{ClientSpacePtr space=std::make_shared<ClientSpace>();ClientSpacePtr cameraSpace(){return space;}}
struct SceneDrawContext{SceneDrawContext(Scene&,Moo::DrawContext&){}};
namespace DrawHelpers{unsigned culls=0,draws=0;std::vector<Moo::Camera> cameras;std::vector<int> faces;
void cull(SceneIntersectContext&,Scene&,const Matrix& m,IntersectionSet&){assert(m.tag==200);++culls;}
void drawSceneVisibilitySet(SceneDrawContext&,Scene&,const IntersectionSet&){auto& rc=Moo::rc();assert(!rc.scene&&shadowParameters.algorithm==1&&shadowParameters.parameters.x==30);assert(rc.width==rc.height&&(rc.width==128||rc.width==256));cameras.push_back(rc.camera());faces.push_back(rc.view().tag);++draws;}}
int setStockSceneViewport(DX::Viewport* v,bool scene){auto& rc=Moo::rc();assert(!v->X&&!v->Y&&v->MinZ==0&&v->MaxZ==1&&!scene);rc.scene=scene;return rc.failViewport?-1:0;}
struct StockDeferredBuffers{int quality=0;int shadowQuality()const{return quality;}};
StockDeferredBuffers* stockDeferredBuffers(){static StockDeferredBuffers b;return &b;}
class StockLocalLighting{public:struct State;std::unique_ptr<State> state_;StockLocalLighting();~StockLocalLighting();void createUnmanagedObjects();void deleteUnmanagedObjects();};
using LocalMath=wot::StockLocalLightMath;
@PRODUCTION@
StockLocalLighting::StockLocalLighting():state_(new State){}
StockLocalLighting::~StockLocalLighting(){deleteUnmanagedObjects();}
int main(){
auto& rc=Moo::rc();auto& gpu=rc.gpu;
{
StockLocalLighting owner;auto& state=*owner.state_;state.omniMaterial=std::make_shared<Moo::EffectMaterial>();state.spotMaterial=std::make_shared<Moo::EffectMaterial>();state.blurMaterial=std::make_shared<Moo::EffectMaterial>();
owner.createUnmanagedObjects();assert(state.shadows&&state.cube&&state.cubeDepth&&state.instances.live&&Moo::RenderTarget::alive==4);
auto* cube=state.cube.pComObject();auto* spot=state.spotTargets[1]->pTexture();auto* scratch=state.spotScratch->pTexture();
assert(gpu.SetTexture(0,cube)==0&&gpu.SetTexture(259,cube)==0&&gpu.SetTexture(2,spot)==0&&gpu.SetTexture(256,scratch)==0);
assert(state.omniMaterial->binding.SetTexture("map",cube)==0&&state.spotMaterial->binding.SetTexture("map",spot)==0&&state.blurMaterial->binding.SetTexture("map",scratch)==0);
assert(cube->refs==4&&spot->refs==3&&scratch->refs==3);
state.prepared=true;state.preparedOmnis.resize(1);state.preparedSpots.resize(1);
owner.deleteUnmanagedObjects();assert(!state.prepared&&state.preparedOmnis.empty()&&state.preparedSpots.empty()&&!state.instances.live);
assert(!state.cube&&!state.cubeDepth&&Moo::RenderTarget::alive==0&&!gpu.samplers[0]&&!gpu.samplers[259]&&!gpu.samplers[2]&&!gpu.samplers[256]);
assert(!state.omniMaterial->binding.texture&&!state.spotMaterial->binding.texture&&!state.blurMaterial->binding.texture);
owner.deleteUnmanagedObjects();assert(Moo::RenderTarget::alive==0);
owner.createUnmanagedObjects();assert(Moo::RenderTarget::alive==4);
auto omni=std::make_shared<Moo::OmniLight>();
assert(state.drawShadow(omni)&&DrawHelpers::draws==6&&DeprecatedSpaceHelpers::space->scene().view.queries==1);
for(auto camera:DrawHelpers::cameras)assert(camera.n==.001f&&camera.f==30&&camera.aspect==1&&camera.fov==MATH_PI*.5f);
assert(DrawHelpers::faces==std::vector<int>({0,1,2,3,0,5}));assert(rc.camera().n==.1f&&rc.camera().f==1000&&shadowParameters.algorithm==0&&shadowParameters.parameters.x==0);
StockLocalLighting::State::Spot spotLight;spotLight.light=std::make_shared<Moo::SpotLight>();
assert(state.drawShadow(spotLight,0)&&DrawHelpers::draws==7&&DrawHelpers::culls==1);
assert(DrawHelpers::cameras.back().n==.1f&&DrawHelpers::cameras.back().f==60&&rc.camera().f==1000);
assert(!rc.depth&&rc.scene&&rc.width==1920&&rc.height==1080);
const auto draws=DrawHelpers::draws;gpu.failClear=true;assert(!state.drawShadow(omni)&&DrawHelpers::draws==draws);gpu.failClear=false;
assert(!rc.depth&&rc.scene&&rc.camera().f==1000&&shadowParameters.algorithm==0);
DeprecatedSpaceHelpers::space.reset();assert(!state.drawShadow(omni)&&!state.drawShadow(spotLight,0));DeprecatedSpaceHelpers::space=std::make_shared<ClientSpace>();
rc.draws=0;
assert(state.blur(state.spotTargets[0]));assert(rc.draws==2&&rc.width==1920&&rc.height==1080&&rc.scene&&!rc.depth&&!state.blurMaterial->binding.texture);
rc.drawSuccess=false;assert(!state.blur(state.spotTargets[0]));assert(!state.blurMaterial->binding.texture&&!rc.depth&&rc.scene);rc.drawSuccess=true;
for(unsigned failure=0;failure<5;++failure){
rc.failPush=failure==0;gpu.failDepth=failure==1;rc.failTarget=failure==2;rc.failViewport=failure==3;gpu.failSet=-1;
ComObjectWrap<DX::Surface> s;state.spotTargets[0]->pSurface(s);gpu.SetTexture(0,state.spotTargets[0]->pTexture());gpu.failSet=failure==4?0:-1;
{TargetScope target;assert(!target.bind(s.pComObject(),nullptr,256,256));}
assert(!rc.depth&&rc.width==1920&&rc.height==1080&&rc.scene);
rc.failPush=rc.failTarget=rc.failViewport=gpu.failDepth=false;gpu.failSet=-1;gpu.SetTexture(0,nullptr);
}
gpu.failGet=0;assert(!detachTexture(state.cube.pComObject()));gpu.failGet=-1;
owner.deleteUnmanagedObjects();
stockDeferredBuffers()->quality=2;owner.createUnmanagedObjects();assert(!state.shadows&&!state.cube&&!Moo::RenderTarget::alive&&state.instances.live);
stockDeferredBuffers()->quality=0;Moo::RenderTarget::failAt=int(Moo::RenderTarget::creates)+2;gpu.failCreateCube=true;owner.createUnmanagedObjects();assert(!state.spotTargets[1]&&!state.cube&&state.cubeDepth);
owner.deleteUnmanagedObjects();assert(!Moo::RenderTarget::alive);gpu.failCreateCube=false;Moo::RenderTarget::failAt=-1;
}
assert(!Moo::RenderTarget::alive&&!errors);
}
