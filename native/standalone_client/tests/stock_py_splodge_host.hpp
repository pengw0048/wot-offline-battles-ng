// Observable SDK graphics/space boundaries; production attachment and renderer bodies execute unchanged.
#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <iostream>
#include <cstdint>
#include <utility>
#define ERROR_MSG(...) std::fprintf(stderr, __VA_ARGS__)
#define SUCCEEDED(x) ((x)>=0)
#define FAILED(x) ((x)<0)
#define D3DSBT_ALL 1
#define ENABLE_TRANSFORM_VALIDATION 0
using uint32 = unsigned;
using HRESULT = int;
struct IDirect3DStateBlock9 { int Capture(){return 0;} int Apply(){return 0;} };
template<class T> struct ComObjectWrap {
 std::shared_ptr<T> p; T* operator->(){return p.get();} explicit operator bool()const{return bool(p);}
};
namespace BW {
struct Matrix {
 float m[16]; Matrix(){for(int i=0;i<16;++i)m[i]=(i%5==0)?1.f:0.f;}
 Vector3 applyToOrigin()const{return Vector3(m[12],m[13],m[14]);}
 Vector3 applyVector(Vector3 v)const{return Vector3(v.x*m[0]+v.y*m[4]+v.z*m[8],v.x*m[1]+v.y*m[5]+v.z*m[9],v.x*m[2]+v.y*m[6]+v.z*m[10]);}
 Vector3 applyPoint(Vector3 v)const{auto a=applyVector(v);auto b=applyToOrigin();return Vector3(a.x+b.x,a.y+b.y,a.z+b.z);}
 void translation(Vector3 v){m[12]=v.x;m[13]=v.y;m[14]=v.z;}
};
inline Vector3 operator+(Vector3 a,Vector3 b){return Vector3(a.x+b.x,a.y+b.y,a.z+b.z);}
struct Vector4{float x,y,z,w;Vector4(float a,float b,float c,float d):x(a),y(b),z(c),w(d){}};
struct BoundingBox {
 Vector3 low,high;bool empty=true;
 BoundingBox(){} BoundingBox(Vector3 a,Vector3 b):low(a),high(b),empty(false){}
 void add(Vector3 a){if(empty){low=high=a;empty=false;return;}low=Vector3(std::min(low.x,a.x),std::min(low.y,a.y),std::min(low.z,a.z));high=Vector3(std::max(high.x,a.x),std::max(high.y,a.y),std::max(high.z,a.z));}
 void addBounds(const BoundingBox& b){if(!b.empty){add(b.low);add(b.high);}}
 void transformBy(const Matrix& m){if(empty)return;auto a=low,b=high;empty=true;for(int i=0;i<8;++i)add(m.applyPoint(Vector3(i&1?b.x:a.x,i&2?b.y:a.y,i&4?b.z:a.z)));}
};
namespace Script {
int setData(PyObject* o,Matrix& m,const char* name="") {
 PyObject* data=PyObject_GetAttrString(o,"values");
 if(data && PyTuple_Check(data) && PyTuple_Size(data)==16){
  for(int i=0;i<16;++i){m.m[i]=float(PyFloat_AsDouble(PyTuple_GetItem(data,i)));if(PyErr_Occurred()){Py_DECREF(data);return -1;}}
  Py_DECREF(data);return 0;
 }
 Py_XDECREF(data);PyErr_Clear();PyErr_Format(PyExc_TypeError,"%s must be set to a MatrixProvider",name);return -1;
}
PyObject* getData(float f){return PyFloat_FromDouble(f);}
}
class MainLoopTask {public:virtual ~MainLoopTask(){} virtual bool init(){return true;}virtual void fini(){}virtual void draw(){}};
struct MainLoopTasks {static MainLoopTasks& root(){static MainLoopTasks r;return r;}void add(MainLoopTask*,const char*,const char*,void*){}};
struct IRendererPipeline {enum EType {TYPE_DEFERRED_SHADING,TYPE_FORWARD_SHADING};};
struct Renderer {static Renderer& instance(){static Renderer r;return r;}static Renderer* pInstance(){return &instance();}IRendererPipeline::EType pipelineType()const;};
namespace Moo {struct DrawContext;}
class PyAttachment:public PyObjectPlus {
public:
 static PyTypeObject s_type_;PyAttachment(PyTypeObject* t):PyObjectPlus(t){}
 virtual void tick(float){}virtual bool needsUpdate()const{return true;}virtual bool isLodVisible()const{return true;}
 virtual void updateAnimations(const Matrix&,float){}virtual void draw(Moo::DrawContext&,const Matrix&){}
 virtual void tossed(bool){}virtual void detach(){}virtual void leaveWorld(){}
 virtual void worldBoundingBox(BoundingBox&,const Matrix&,bool=false){}virtual void worldVisibilityBox(BoundingBox&,const Matrix&,bool=false){}
};
PyTypeObject PyAttachment::s_type_={PyVarObject_HEAD_INIT(&PyType_Type,0) "PyAttachment"};
class ClientSpace{public:bool retired=false;bool cleared()const{return retired;}};
using ClientSpacePtr=std::shared_ptr<ClientSpace>;
ClientSpacePtr currentSpace(new ClientSpace);
namespace DeprecatedSpaceHelpers {ClientSpacePtr cameraSpace(){return currentSpace;}}
namespace Moo {
int pipeline=1,callbacks=0,draws=0,loads=0,passes=0,bindings=0,clears=0;
std::vector<Matrix> worlds,locals;
class DeviceCallback{public:DeviceCallback(){++callbacks;}virtual ~DeviceCallback(){--callbacks;}virtual void deleteManagedObjects(){}virtual void deleteUnmanagedObjects(){}virtual bool recreateForD3DExDevice()const{return false;}};
struct GraphicsSetting{int activeOption(){return pipeline;}static std::shared_ptr<GraphicsSetting> getFromLabel(const char*){return std::make_shared<GraphicsSetting>();}};
enum{RENDERING_PASS_COLOR=0,CONSTANTS_PER_VIEW=0};
struct DrawContext {int pass=0;int renderingPassType(){return pass;}};
struct Device{int CreateStateBlock(int,ComObjectWrap<IDirect3DStateBlock9>* s){s->p=std::make_shared<IDirect3DStateBlock9>();return 0;}};
struct EffectVisualContext {const Matrix* world=NULL;const Matrix* worldMatrix(){return world;}void worldMatrix(const Matrix* p){world=p;}void updateSharedConstants(int){}};
struct Quad {template<class M>bool drawChecked(const M& m,unsigned,unsigned){++clears;if(!m.begin())return false;for(unsigned p=0;p<m.numPasses();++p){if(!m.beginPass(p)||!m.endPass())return false;}return m.end();}};
struct Size {unsigned Width=800,Height=600;};
struct RenderContext {Device d;Quad q;Size size;Quad& fsQuad(){return q;}const Size& backBufferDesc(){return size;}EffectVisualContext c;Device* device(){return &d;}EffectVisualContext& effectVisualContext(){return c;}void invalidateStateCache(){}};
RenderContext& rc(){static RenderContext c;return c;}
struct BaseTexture {void* pTexture(){return this;}};
using BaseTexturePtr=std::shared_ptr<BaseTexture>;
struct TextureManager{static TextureManager* instance(){static TextureManager m;return &m;}BaseTexturePtr get(const char* s){assert(std::strstr(s,"TankOcclusionMap_forward.dds"));return std::make_shared<BaseTexture>();}};
struct Vertices {int setVertices(bool,bool){return 0;}};
struct Primitive {int setPrimitives(){return 0;}unsigned nPrimGroups(){return 1;}int drawPrimitiveGroup(unsigned){++draws;return 0;}};
struct Group{unsigned groupIndex_=0;};
struct Geometry{std::shared_ptr<Vertices> vertices_=std::make_shared<Vertices>();std::shared_ptr<Primitive> primitives_=std::make_shared<Primitive>();std::vector<Group> primitiveGroups_={Group()};};
struct RenderSet{std::vector<Geometry> geometry_={Geometry()};};
struct Visual {bool isOK(){return true;}std::vector<RenderSet> sets={RenderSet()};const std::vector<RenderSet>& renderSets(){return sets;}};
using VisualPtr=std::shared_ptr<Visual>;
struct VisualManager{static VisualManager* instance(){static VisualManager m;return &m;}VisualPtr get(const char*){return std::make_shared<Visual>();}};
}
}
namespace BW {IRendererPipeline::EType Renderer::pipelineType()const{return static_cast<IRendererPipeline::EType>(Moo::pipeline);}}
struct ID3DXEffect {
 void* GetTechnique(unsigned i){return reinterpret_cast<void*>(uintptr_t(i+1));}
 int SetMatrix(const char* name,const BW::Matrix* m){if(std::strcmp(name,"g_worldMat")==0)BW::Moo::worlds.push_back(*m);else {assert(std::strcmp(name,"g_transform")==0);BW::Moo::locals.push_back(*m);}++BW::Moo::bindings;return 0;}
 int SetTexture(const char* n,void*){assert(std::strcmp(n,"g_shadowMap")==0);return 0;}
 int SetVector(const char* n,const BW::Vector4* v){assert(std::strcmp(n,"g_borderTC")==0 && v->x==0 && v->y==0 && v->z==1 && v->w==1);return 0;}
 int CommitChanges(){return 0;}
};
namespace BW {namespace Moo {
struct ManagedEffect{ID3DXEffect e;ID3DXEffect* pEffect(){return &e;}};
struct EffectMaterial {ManagedEffect effect;std::string technique;bool initFromEffect(const char* n){assert(std::strcmp(n,"shaders/shadow/wg_forward_tank_shadow.fx")==0);++loads;return true;}bool hTechnique(void*){return true;}bool setTechnique(const char* name){assert(!std::strcmp(name,"CLEAR_STENCIL")||!std::strcmp(name,"STENCIL_PREPARATION")||!std::strcmp(name,"SHADOW_PLANE"));technique=name;return true;}bool begin()const{return true;}bool end()const{return true;}bool beginPass(unsigned)const{++passes;return true;}bool endPass()const{return true;}unsigned numPasses()const{return technique=="CLEAR_STENCIL"?2:technique=="STENCIL_PREPARATION"?3:1;}ManagedEffect* pEffect(){return &effect;}};
// SDK smart pointer permits implicit raw-pointer construction/assignment.
template<class T> struct Ptr:std::shared_ptr<T>{using std::shared_ptr<T>::shared_ptr;Ptr(){}Ptr(T* p):std::shared_ptr<T>(p){}Ptr& operator=(T* p){this->reset(p);return *this;}};
using EffectMaterialPtr=Ptr<EffectMaterial>;
}}
