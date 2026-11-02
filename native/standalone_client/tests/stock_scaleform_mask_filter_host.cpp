// Pinned SDK filter-state bodies run against a parent-target observer.

#include <cstdio>
#include <vector>
#include <cassert>
using UInt32=unsigned;using DWORD=unsigned;struct VertexXY16iAlpha{};
enum { HS_InDisplay=0x8,HS_ViewValid=0x20,HS_CachedFilter=0x100,HS_DrawingFilter=0x80,HS_DrawingMask=0x40,HS_InRenderTarget=0x10,
D3DRS_STENCILENABLE=1,D3DRS_ZENABLE=2,D3DRS_COLORWRITEENABLE=3,D3DCOLORWRITEENABLE_ALL=15,
Event_Filter=1,D3DCLEAR_STENCIL=1,D3DCLEAR_ZBUFFER=2,Blend_Normal=0,PrimFill_SolidColor=0,
FALSE=0,TRUE=1,Target_Source=0,MaxTemporaryTextures=3 };
#define SF_ASSERT(x) assert(x)
#define SF_UNUSED(x) (void)(x)
struct Device { unsigned color=0,target=0; void SetRenderState(int s,unsigned v){if(s==D3DRS_COLORWRITEENABLE)color=v;} void Clear(int,int,int,int,float,int){} void SetStreamSource(int,int,int,int){} };
struct Matrix2F {float M[2][4]={};void AppendTranslation(float,float){}void AppendScaling(float,float){} float Sx()const{return 50;}float Sy()const{return 50;}float Tx()const{return 0;}float Ty()const{return 0;} Matrix2F(){} Matrix2F(const Matrix2F&,const Matrix2F&){} };
Matrix2F operator*(const Matrix2F&,const Matrix2F&){return {};}
struct RectF { RectF(float,float,float,float){} };
struct Cxform{};
struct HMatrix { Matrix2F m; Cxform cx;const Matrix2F& GetMatrix2D()const{return m;}const Cxform& GetCxform()const{return cx;} };
struct ImageSize {unsigned Width=50,Height=50;ImageSize(UInt32,UInt32){} };
template<class T>struct Rect {T x1=0,y1=0;T Width()const{return 50;}T Height()const{return 50;}};
struct Texture{ImageSize GetSize(){return {50,50};}};namespace D3D9{using Texture=::Texture;}
struct RenderTarget {void SetInUse(bool){}Texture*GetTexture(){static Texture t;return &t;}const Rect<int>&GetRect(){static Rect<int> r;return r;}};
enum{Wrap_Clamp,Sample_Linear,FF_Cxform=1,FF_AlphaWrite=2,PrimFill_Texture=3};
struct ImageFillMode{ImageFillMode(int,int){}};
struct Color { void GetRGBAFloat(float*){} };
struct Profile { bool ShouldDrawMask(){return true;} void SetDrawMode(int){}bool IsFilterCachingEnabled(){return true;} Color GetColor(unsigned){return {};} };
struct Event { void Begin(const char*){} };
struct FilterSet { unsigned GetFilterCount()const{return 1;} };
struct FilterPrimitive { enum {Cache_Uncached,Cache_PreTarget,Cache_Target}; int cache; HMatrix matrix;
 void GetCacheResults(RenderTarget**o,int){static RenderTarget t;*o=&t;}void SetCacheResults(int,int,int){}int GetCacheState()const{return cache;} bool GetMaskPresent()const{return false;}const HMatrix& GetFilterAreaMatrix()const{return matrix;}const FilterSet* GetFilters()const {static FilterSet f;return &f;} };

template<class T> struct Ptr { T* p=nullptr;Ptr(){}Ptr(int){}Ptr(T&x):p(&x){}Ptr& operator=(T&x){p=&x;return *this;}operator bool()const{return p;}T*operator->()const{return p;} };
struct FilterStackEntry { FilterPrimitive* pPrimitive; Ptr<RenderTarget> pRenderTarget; };
template<class T> struct Stack {std::vector<T> x;void PushBack(const T&v){x.push_back(v);}unsigned GetSize()const{return x.size();}T&Back(){return x.back();}};
struct ShaderDataType {template<class... A>void SetMatrix(A...){}template<class... A>void SetCxform(A...){}template<class... A>void SetTexture(A...){}template<class... A>void SetUniform(A...){}void Finish(int){} };
struct ShaderManager {using Shader=int;template<class... A>const Shader& SetFill(A...){static int s;return s;}template<class... A>void SetFilterFill(A...){} };
struct Uniform {enum {SU_mvp,SU_cxmul,SU_tex,SU_texgen};};struct PrimitiveBatch{enum {DP_Batch,DP_Single};};
struct MatrixState {Matrix2F UserView;};
struct TextureManager{void SetSamplerState(int,int){}};
struct Queue{enum{QPF_Filters};void SetQueueEmitFilter(int){}};using RenderQueueProcessor=Queue;
int MappedXY16iAlphaSolid[1],MappedXY16iAlphaTexture[2];
struct HAL { Device* pDevice;Profile Profiler;ShaderDataType ShaderData;ShaderManager SManager;
struct { struct {int GetPtr(){return 0;}}pMaskEraseBatchVertexBuffer;} Cache; Stack<FilterStackEntry> FilterStack;Stack<int> BlendModeStack;bool StencilAvailable=true,DepthBufferAvailable=true;
unsigned FillFlags=0,MaskStackTop=1,HALState=HS_InDisplay|HS_ViewValid|HS_DrawingMask;int CachedFilterIndex=0;
MatrixState state;MatrixState* Matrices=&state;TextureManager textures;TextureManager* pTextureManager=&textures;
struct {unsigned Filters=0;} AccumulatedStats;unsigned observed=99,testPasses=0;
Event GetEvent(int){return {};}bool checkState(unsigned,const char*){return true;}bool shouldRenderFilters(FilterPrimitive*){return true;}
RenderTarget*CreateTempRenderTarget(ImageSize,bool){static RenderTarget t;return &t;}
void PushRenderTarget(RectF,Ptr<RenderTarget>){pDevice->target=1;HALState|=HS_InRenderTarget;}
void PopRenderTarget(){pDevice->target=0;HALState&=~HS_InRenderTarget;}
void applyBlendMode(int,bool,bool){}Queue&GetRQProcessor(){static Queue q;return q;}
void setLinearStreamSource(int){}void drawPrimitive(int,int){if(pDevice->target==0)observed=pDevice->color;}void drawCachedFilter(FilterPrimitive*);
void PushFilters(FilterPrimitive*);void drawUncachedFilter(const FilterStackEntry&);
};

@PUSH_FILTERS@
void HAL::drawCachedFilter(FilterPrimitive* primitive){switch(primitive->GetCacheState()) {
@CACHED_TARGET@
default: assert(false);}}
void HAL::drawUncachedFilter(const FilterStackEntry& e) {
 const int MaxTemporaryTextures=3; Ptr<RenderTarget> temporaryTextures[3];unsigned passes=testPasses,pass=0;unsigned shaders[1]={0};int filter=0;
 const FilterSet* filters=e.pPrimitive->GetFilters();
 // The omitted source-target stage publishes its cache result before returning.
 e.pPrimitive->cache=FilterPrimitive::Cache_Target;
@UNCACHED_FINAL_COMPOSITE@

int main(){
 bool leaked=false;
 // Cached/uncached, one/nested masks, depth-only/stencil, and real-filter/CaB.
 for(unsigned mask: {0u,1u,3u})for(bool stencil: {false,true})for(bool cache: {false,true})for(unsigned passes: {0u,1u}){
  Device device;HAL hal;hal.pDevice=&device;hal.MaskStackTop=mask;hal.StencilAvailable=stencil;
  hal.HALState=HS_InDisplay|HS_ViewValid|(mask?HS_DrawingMask:0);hal.testPasses=passes;
  device.color=mask?0:15;FilterPrimitive primitive;primitive.cache=cache?FilterPrimitive::Cache_Target:FilterPrimitive::Cache_Uncached;
  hal.PushFilters(&primitive);
  // A cached target emits directly; the uncached path first returns to the parent.
  if(cache)hal.drawCachedFilter(&primitive);else hal.drawUncachedFilter(hal.FilterStack.Back());
  const unsigned expected=mask?0:15;
  if(hal.observed!=expected){
   assert(mask&&!cache&&hal.observed==15);leaked=true;
   std::printf("mask color leaked: depth=%u stencil=%u cached=%u passes=%u color=%u\n",mask,stencil,cache,passes,hal.observed);
  }
  assert(device.color==expected);
 }
 if(leaked)return 41;
 std::puts("mask filter parent color ownership passed: 48 cases");
}
