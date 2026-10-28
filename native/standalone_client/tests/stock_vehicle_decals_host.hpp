// Render/device/space boundaries for compiling the complete attachment owner.
// Record packing, transforms, admission and lifecycle are production code.
#include <array>
#include <cassert>
#include <cmath>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>
#define BW_BEGIN_NAMESPACE namespace BW {
#define BW_END_NAMESPACE }
#define ERROR_MSG(...) BW::hostError(__VA_ARGS__)
#define ENABLE_TRANSFORM_VALIDATION 0
#define Py_Header(T,B) public: static int s_type_;
#define PY_AUTO_METHOD_DECLARE(...)
#define PY_AUTO_CONSTRUCTOR_FACTORY_DECLARE(...)
#define PY_TYPEOBJECT(T) int T::s_type_=0;
#define PY_BEGIN_METHODS(...)
#define PY_END_METHODS(...)
#define PY_METHOD(...)
#define PY_BEGIN_ATTRIBUTES(...)
#define PY_END_ATTRIBUTES(...)
#define PY_FACTORY_NAMED(...)
using HRESULT=int;
#define FAILED(x) ((x)<0)
#define SUCCEEDED(x) ((x)>=0)
constexpr int D3DFMT_UNKNOWN=0,D3DFMT_DXT5=1,D3DRTYPE_TEXTURE=1,D3DPOOL_SYSTEMMEM=1;
constexpr int D3DERR_INVALIDCALL=-1,S_OK=0,D3DPOOL_DEFAULT=0;
using D3DFORMAT=int;
struct D3DSURFACE_DESC {unsigned Width=512,Height=512;int Format=1,Pool=1;};
struct IDirect3DSurface9 {void Release(){}};
struct IDirect3DBaseTexture9 {virtual ~IDirect3DBaseTexture9(){};int GetType(){return 1;}};
struct IDirect3DTexture9 : IDirect3DBaseTexture9 {
 unsigned GetLevelCount(){return 10;}
 HRESULT GetLevelDesc(unsigned mip,D3DSURFACE_DESC* output){if(mip>=10)return -1;*output={512u>>mip,512u>>mip,1,1};return 0;}
 HRESULT GetSurfaceLevel(unsigned,IDirect3DSurface9** p){static IDirect3DSurface9 s;*p=&s;return 0;}
 void Release(){delete this;}
};
struct IDirect3DDevice9 {
 HRESULT CreateTexture(unsigned,unsigned,unsigned,unsigned,int,int,IDirect3DTexture9** p,void*){*p=new IDirect3DTexture9;return 0;}
 HRESULT UpdateSurface(IDirect3DSurface9*,void*,IDirect3DSurface9*,void*){return 0;}
};
namespace BW {
using uint32=unsigned;using uint16=unsigned short;using PyTypeObject=int;
using string=std::string;template<class T>using vector=std::vector<T>;
int hostErrors=0;
void hostError(const char*,...){++hostErrors;}
struct Vector3 {float x=0,y=0,z=0;Vector3(){}Vector3(float a,float b,float c):x(a),y(b),z(c){}};
struct Matrix {
 float m[16]={};static Matrix identity;
 float operator()(unsigned r,unsigned c)const{return m[r*4+c];}
 Vector3 applyToOrigin()const{return {m[12],m[13],m[14]};}
 void translation(const Vector3& v){m[12]=v.x;m[13]=v.y;m[14]=v.z;}
 void setRotateX(float a){*this=identity;m[5]=std::cos(a);m[6]=std::sin(a);m[9]=-m[6];m[10]=m[5];}
 void multiply(const Matrix& a,const Matrix& b){Matrix r;for(unsigned i=0;i<4;++i)for(unsigned j=0;j<4;++j)for(unsigned k=0;k<4;++k)r.m[i*4+j]+=a.m[i*4+k]*b.m[k*4+j];*this=r;}
};
Matrix Matrix::identity=[](){Matrix m;for(unsigned i=0;i<4;++i)m.m[i*4+i]=1;return m;}();
struct BoundingBox {
 Vector3 lo,hi;bool empty=true;static bool culled;
 BoundingBox(){} BoundingBox(Vector3 a,Vector3 b):lo(a),hi(b),empty(false){}
 void transformBy(const Matrix& m){lo.x+=m.m[12];hi.x+=m.m[12];lo.y+=m.m[13];hi.y+=m.m[13];}
 void addBounds(const BoundingBox& b){lo=b.lo;hi=b.hi;empty=b.empty;}
 void calculateOutcode(const Matrix&){} unsigned combinedOutcode(){return culled?1:0;}
};bool BoundingBox::culled=false;
namespace Moo {
constexpr unsigned RENDERING_PASS_COLOR=1;
struct DrawContext {unsigned pass=1;unsigned renderingPassType()const{return pass;}};
struct DeviceCallback {virtual~DeviceCallback(){}virtual void deleteUnmanagedObjects(){}virtual bool recreateForD3DExDevice()const{return true;}};
struct BaseTexture {enum{STATUS_READY=1};static bool available;IDirect3DTexture9 texture;int status(){return available?1:0;}IDirect3DBaseTexture9* pTexture(){return &texture;}};
bool BaseTexture::available=true;using BaseTexturePtr=std::shared_ptr<BaseTexture>;
struct TextureManager {static TextureManager* instance(){static TextureManager m;return &m;}BaseTexturePtr getSystemMemoryTexture(const string&){return std::make_shared<BaseTexture>();}};
struct FormatNameMap {static int formatType(const string&){return 1;}};
struct Context {bool reflection=false;Matrix camera=Matrix::identity;Matrix invView(){return camera;}Matrix viewProjection(){return Matrix::identity;}float lodZoomFactor(){return 1.f;}bool reflectionScene(){return reflection;}IDirect3DDevice9* device(){static IDirect3DDevice9 d;return &d;}};
Context& rc(){static Context c;return c;}
}
namespace DX {using Texture=IDirect3DTexture9;using BaseTexture=IDirect3DBaseTexture9;}
template<class T>struct ComObjectWrap {
 std::shared_ptr<T> value;T* raw=nullptr;
 operator bool()const{return raw||bool(value);}T* pComObject()const{return value?value.get():raw;}
 T* operator->()const{return pComObject();}T** operator&(){return &raw;}
 ~ComObjectWrap(){if(raw)raw->Release();}
};
struct CollisionObstacle{};struct WorldTriangle {unsigned flags=0;unsigned collisionFlags()const{return flags;}};
constexpr int COLLIDE_ALL=0,COLLIDE_BEFORE=1;
struct CollisionCallback {virtual~CollisionCallback(){}virtual int operator()(const CollisionObstacle&,const WorldTriangle&,float)=0;};
struct ClientSpace {bool dead=false;bool cleared()const{return dead;}float hit=-1;Vector3 start,end;
 void collide(Vector3 a,Vector3 b,CollisionCallback& callback){start=a;end=b;if(hit>=0)callback(CollisionObstacle{},WorldTriangle{},hit);}};
using ClientSpacePtr=std::shared_ptr<ClientSpace>;
ClientSpacePtr hostSpace=std::make_shared<ClientSpace>();
namespace DeprecatedSpaceHelpers {ClientSpacePtr cameraSpace(){return hostSpace;}}
class PyAttachment {
public:explicit PyAttachment(int*){}virtual~PyAttachment(){}
 virtual void tick(float)=0;virtual bool needsUpdate()const=0;virtual bool isLodVisible()const=0;
 virtual void updateAnimations(const Matrix&,float)=0;virtual void draw(Moo::DrawContext&,const Matrix&)=0;
 virtual void detach(){}virtual void leaveWorld(){}
 virtual void localBoundingBox(BoundingBox&,bool=false){}virtual void localVisibilityBox(BoundingBox&,bool=false){}
 virtual void worldBoundingBox(BoundingBox&,const Matrix&,bool=false){}virtual void worldVisibilityBox(BoundingBox&,const Matrix&,bool=false){}
};
struct DataSection;using DataSectionPtr=std::shared_ptr<DataSection>;
struct DataSection {
 string name;
 DataSectionPtr openSection(const string& n){return std::make_shared<DataSection>(DataSection{n});}
 int readInt(const string&,int d){return d;}unsigned readUInt(const string& n,unsigned d=0){
 if(n=="minWidth"||n=="minHeight"){return 1;}
 if(n=="maxWidth"||n=="maxHeight"){return 4096;}
 return d;}
 string readString(const string& n){return n=="name"?"OtherDecals":"DXT5";}string asString(){return name;}
 void openSections(const string& n,vector<DataSectionPtr>& output){if(n=="type"||n=="atlas")output.push_back(std::make_shared<DataSection>());}
};
struct BWResource {static DataSectionPtr openSection(const string&){return std::make_shared<DataSection>();}};
struct AutoConfigString {explicit AutoConfigString(const char*){}string value(){return "decals.xml";}};
}
struct D3DXVECTOR3 {float x,y,z;D3DXVECTOR3(float a,float b,float c):x(a),y(b),z(c){}};
using D3DXMATRIX=BW::Matrix;
struct D3DXQUATERNION {float x=0,y=0,z=0,w=1;};
float D3DXVec3Length(const D3DXVECTOR3* v){return std::sqrt(v->x*v->x+v->y*v->y+v->z*v->z);}
void D3DXVec3Normalize(D3DXVECTOR3* out,const D3DXVECTOR3* in){float n=D3DXVec3Length(in);*out=n?D3DXVECTOR3(in->x/n,in->y/n,in->z/n):D3DXVECTOR3(0,0,0);}
void D3DXMatrixLookAtLH(D3DXMATRIX*,const D3DXVECTOR3*,const D3DXVECTOR3*,const D3DXVECTOR3*){}
void D3DXQuaternionRotationMatrix(D3DXQUATERNION*,D3DXMATRIX*){}
