// Observable SDK boundaries for the complete production occlusion owner.
#pragma once
#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <thread>
#include <vector>
#define BW_BEGIN_NAMESPACE namespace BW {
#define BW_END_NAMESPACE }
#define BW_GUARD
#define PROFILER_SCOPED(name)
#define MF_ASSERT(value) assert(value)
using HRESULT = std::int32_t;
using DWORD = std::uint32_t;
using UINT = unsigned;
using ULONG = unsigned;
using WORD = std::uint16_t;
constexpr HRESULT S_OK = 0, D3D_OK = 0, S_FALSE = 1, E_FAIL = -1;
#define FAILED(result) ((result) < 0)
#define SUCCEEDED(result) ((result) >= 0)
enum {D3DQUERYTYPE_OCCLUSION=8, D3DISSUE_BEGIN=2, D3DISSUE_END=1,
      D3DSBT_ALL=1, D3DPT_TRIANGLELIST=4, D3DFMT_INDEX16=101,
      D3DFVF_XYZ=2, D3DTS_WORLD=256, D3DTS_VIEW=2, D3DTS_PROJECTION=3,
      D3DRS_ZENABLE=7, D3DRS_ZWRITEENABLE=14, D3DRS_ZFUNC=23,
      D3DRS_CULLMODE=22, D3DRS_COLORWRITEENABLE=168,
      D3DCMP_LESSEQUAL=4, D3DCULL_CCW=3};
struct D3DVIEWPORT9 {DWORD X=0,Y=0,Width=800,Height=600;float MinZ=0,MaxZ=1;};
namespace fixture {
static std::string failure;
static std::vector<std::string> events;
static unsigned gets=0,draws=0,creates=0,stateApplies=0,cacheInvalidations=0;
static std::function<void()> onPass;
static std::array<float,24> vertices;
static std::array<WORD,36> indices;
inline HRESULT call(const char* name) {
  events.push_back(name);
  return failure == name ? E_FAIL : S_OK;
}
}
struct IDirect3DSurface9 {
  unsigned refs=1;
  ULONG AddRef(){return ++refs;}
  ULONG Release(){assert(refs);return --refs;}
};
struct IDirect3DQuery9 {
  unsigned refs=1,id=0,begins=0,ends=0,reads=0;
  HRESULT result=S_FALSE;
  DWORD pixels=0;
  ULONG AddRef(){assert(refs);return ++refs;}
  ULONG Release(){assert(refs);return --refs;}
  HRESULT Issue(DWORD flags){
    assert(refs);assert(flags==D3DISSUE_BEGIN||flags==D3DISSUE_END);
    if(flags==D3DISSUE_BEGIN)++begins;else ++ends;
    return fixture::call(flags==D3DISSUE_BEGIN?"query-begin":"query-end");
  }
  HRESULT GetData(void* out,DWORD size,DWORD flags){
    assert(refs&&size==sizeof(DWORD)&&flags==0);
    ++reads;++fixture::gets;
    if(FAILED(fixture::call("query-get")))return E_FAIL;
    if(result==S_OK)*static_cast<DWORD*>(out)=pixels;
    return result;
  }
};
static std::vector<std::unique_ptr<IDirect3DQuery9>> fixtureQueries;
struct IDirect3DDevice9;
struct IDirect3DStateBlock9 {
  IDirect3DDevice9* device;
  std::map<DWORD,DWORD> states;
  explicit IDirect3DStateBlock9(IDirect3DDevice9* value):device(value){}
  HRESULT Capture();
  HRESULT Apply();
  ULONG Release(){delete this;return 0;}
};
struct IDirect3DDevice9 {
  std::map<DWORD,DWORD> states{{D3DRS_ZWRITEENABLE,1},{D3DRS_COLORWRITEENABLE,15}};
  D3DVIEWPORT9 viewport;
  IDirect3DSurface9* depth=nullptr;
  HRESULT CreateQuery(unsigned type,IDirect3DQuery9** out){
    assert(type==D3DQUERYTYPE_OCCLUSION);++fixture::creates;*out=nullptr;
    if(FAILED(fixture::call("query-create")))return E_FAIL;
    fixtureQueries.emplace_back(new IDirect3DQuery9);
    auto* query=fixtureQueries.back().get();query->id=unsigned(fixtureQueries.size());*out=query;
    return S_OK;
  }
  HRESULT CreateStateBlock(unsigned type,IDirect3DStateBlock9** out){
    assert(type==D3DSBT_ALL);*out=nullptr;
    if(FAILED(fixture::call("state-create")))return E_FAIL;
    *out=new IDirect3DStateBlock9(this);return S_OK;
  }
  HRESULT SetRenderState(DWORD state,DWORD value){
    states[state]=value;return fixture::call("render-state");
  }
  HRESULT SetFVF(DWORD value){assert(value==D3DFVF_XYZ);return fixture::call("fvf");}
  HRESULT GetViewport(D3DVIEWPORT9* out){*out=viewport;return fixture::call("viewport-get");}
  HRESULT SetViewport(const D3DVIEWPORT9* value){viewport=*value;return fixture::call("viewport-set");}
  HRESULT GetDepthStencilSurface(IDirect3DSurface9** out){*out=depth;if(depth)depth->AddRef();return fixture::call("depth-get");}
  HRESULT SetDepthStencilSurface(IDirect3DSurface9* value){depth=value;return fixture::call("depth-set");}
  HRESULT DrawIndexedPrimitiveUP(unsigned type,unsigned minimum,unsigned count,unsigned primitives,
      const void* indices,unsigned format,const void* vertices,unsigned stride){
    assert(type==D3DPT_TRIANGLELIST&&minimum==0&&count==8&&primitives==12);
    assert(indices&&vertices&&format==D3DFMT_INDEX16&&stride==12);
    std::memcpy(fixture::vertices.data(),vertices,sizeof(fixture::vertices));
    std::memcpy(fixture::indices.data(),indices,sizeof(fixture::indices));
    ++fixture::draws;return fixture::call("draw");
  }
};
inline HRESULT IDirect3DStateBlock9::Capture(){states=device->states;return fixture::call("state-capture");}
inline HRESULT IDirect3DStateBlock9::Apply(){device->states=states;++fixture::stateApplies;return fixture::call("state-apply");}
namespace BW {
using uint32=std::uint32_t;using uint16=std::uint16_t;using uint=unsigned;
template<class T>using vector=std::vector<T>;
template<class T>class SmartPointer:public std::shared_ptr<T>{
public:using std::shared_ptr<T>::shared_ptr;SmartPointer(){}SmartPointer(T* value):std::shared_ptr<T>(value){}
  T* getObject()const{return this->get();}
};
struct Vector3 {
  float x=0,y=0,z=0;
  Vector3(){}Vector3(float a,float b,float c):x(a),y(b),z(c){}
  Vector3 operator+(const Vector3& value)const{return {x+value.x,y+value.y,z+value.z};}
  Vector3 operator-(const Vector3& value)const{return {x-value.x,y-value.y,z-value.z};}
  Vector3 operator*(float value)const{return {x*value,y*value,z*value};}
  Vector3& operator+=(const Vector3& value){x+=value.x;y+=value.y;z+=value.z;return *this;}
  float length()const{return std::sqrt(x*x+y*y+z*z);}
  void normalise(){const float size=length();if(size){x/=size;y/=size;z/=size;}}
};
struct Vector4 {
  float x=0,y=0,z=0,w=0;
  Vector4(){}Vector4(float a,float b,float c,float d):x(a),y(b),z(c),w(d){}
  float operator[](size_t index)const{return (&x)[index];}
};
struct Matrix {
  float v[4][4]{};
  static const Matrix identity;
  Matrix(){for(unsigned i=0;i<4;++i)v[i][i]=1;}
  void setIdentity(){*this=Matrix();}
  float* operator[](size_t row){return v[row];}
  const float* operator[](size_t row)const{return v[row];}
  const Vector4& row(unsigned index)const{return *reinterpret_cast<const Vector4*>(v[index]);}
  Vector4 applyPoint(const Vector4& p)const{
    Vector4 out;float* values=&out.x;
    for(unsigned c=0;c<4;++c)values[c]=p.x*v[0][c]+p.y*v[1][c]+p.z*v[2][c]+p.w*v[3][c];
    return out;
  }
  Vector3 applyPoint(const Vector3& p)const{const auto out=applyPoint(Vector4(p.x,p.y,p.z,1));return {out.x,out.y,out.z};}
  void applyPoint(Vector4& out,const Vector3& p)const{out=applyPoint(Vector4(p.x,p.y,p.z,1));}
  Vector3 applyToOrigin()const{return {v[3][0],v[3][1],v[3][2]};}
};
const Matrix Matrix::identity;
struct BoundingBox {
  Vector3 low,high;
  BoundingBox():low(1,1,1),high(-1,-1,-1){}
  BoundingBox(Vector3 a,Vector3 b):low(a),high(b){}
  const Vector3& minBounds()const{return low;}
  const Vector3& maxBounds()const{return high;}
  bool insideOut()const{return low.x>high.x||low.y>high.y||low.z>high.z;}
};
namespace Moo {
struct DeviceCallback {
  static std::vector<DeviceCallback*> owners;
  DeviceCallback(){owners.push_back(this);}
  virtual ~DeviceCallback(){owners.erase(std::find(owners.begin(),owners.end(),this));}
  virtual void deleteUnmanagedObjects(){}
  virtual bool createUnmanagedObjects(){return true;}
};
std::vector<DeviceCallback*> DeviceCallback::owners;
struct EffectMaterial {
  bool initFromEffect(const char* resource){assert(std::string(resource)=="shaders/std_effects/position.fx");return SUCCEEDED(fixture::call("material-load"));}
  bool begin(){return SUCCEEDED(fixture::call("material-begin"));}
  bool beginPass(unsigned index){assert(index==0);if(fixture::onPass)fixture::onPass();return SUCCEEDED(fixture::call("material-pass"));}
  void endPass(){fixture::call("material-end-pass");}
  void end(){fixture::call("material-end");}
};
using EffectMaterialPtr=SmartPointer<EffectMaterial>;
struct RenderContext {
  IDirect3DDevice9* device_=nullptr;
  Matrix world_;
  Matrix projection_;
  struct VisualContext {
    const Matrix* world=nullptr;
    const Vector4* palette=nullptr;
    uint32 paletteSize=0;
    const Matrix* previous=nullptr;
    uint32 previousPalette=0;
    const Matrix* instances=nullptr;
    uint32 instanceCount=0;
    const Matrix* worldMatrix()const{return world;}
    void worldMatrix(const Matrix* value){
      world=value?value:&Matrix::identity;palette=nullptr;paletteSize=0;
      previous=nullptr;previousPalette=0;
    }
    const Vector4* worldMatrixPalette()const{return palette;}
    uint32 worldMatrixPaletteSize()const{return paletteSize;}
    void worldMatrixPalette(const Vector4* value,uint32 size){
      palette=value;paletteSize=size;world=&Matrix::identity;
      previous=nullptr;previousPalette=0;
    }
    const Matrix* stockPreviousWorld()const{return previous;}
    void stockPreviousWorld(const Matrix* value){previous=value;}
    uint32 stockPreviousPaletteEntries()const{return previousPalette;}
    void stockPreviousPaletteEntries(uint32 value){previousPalette=value;}
    const Matrix* stockInstancingMatrices()const{return instances;}
    uint32 stockInstancingCount()const{return instanceCount;}
    void stockInstancingStream(const Matrix* value,uint32 count){instances=value;instanceCount=std::min(count,16u);}
  } visual;
  struct Camera {float nearPlane()const{return .1f;}float farPlane()const{return 1000.f;}} camera_;
  uint32 frame=1;
  IDirect3DDevice9* device()const{return device_;}
  const Matrix& world()const{return world_;}
  void world(const Matrix& value){world_=value;}
  VisualContext& effectVisualContext(){return visual;}
  const Matrix& invView()const{return world_;}
  const Matrix& viewProjection()const{return projection_;}
  const Camera& camera()const{return camera_;}
  uint32 frameTimestamp()const{return frame;}
  void invalidateStateCache(){++fixture::cacheInvalidations;}
};
inline RenderContext& rc(){static RenderContext value;return value;}
}
class StockSceneOcclusionMapping;
class Scene {};
struct SceneObject {
  std::shared_ptr<StockSceneOcclusionMapping> mapping;
  uint32 instance=0;
  BoundingBox bounds;
  bool eligible=true;
};
class IntersectionSet {
public:
  using SceneObjectType=unsigned;
  std::vector<std::vector<SceneObject>> buckets;
  unsigned maxType()const{return unsigned(buckets.size());}
  std::vector<SceneObject>& objectsOfType(unsigned type){return buckets[type];}
};
namespace CompiledSpace {
struct StockStaticModelSceneLoader {
  static bool occlusionTarget(const SceneObject& object,
      std::shared_ptr<StockSceneOcclusionMapping>& mapping,uint32& instance,BoundingBox& bounds){
    if(!object.eligible||!object.mapping)return false;
    mapping=object.mapping;instance=object.instance;bounds=object.bounds;return true;
  }
};
}
}
