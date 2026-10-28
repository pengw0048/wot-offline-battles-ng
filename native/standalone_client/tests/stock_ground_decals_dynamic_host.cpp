// Exercise the production dynamic submission; D3DX is an explicit boundary.
#include "wot/stock_dynamic_decals.hpp"
#include "stock_ground_decals_data.hpp"
#include <cassert>
#include <cmath>
#include <cstring>
#include <memory>
#include <string>
#include <vector>
#include <type_traits>
#include <utility>
using std::string;
class Vector2 {public:float x,y;};
class Vector3 {public:float x=0,y=0,z=0;Vector3(){}Vector3(float a,float b,float c):x(a),y(b),z(c){}
 Vector3 operator-(const Vector3& b)const{return {x-b.x,y-b.y,z-b.z};}Vector3 operator-()const{return {-x,-y,-z};}
 Vector3 operator+(const Vector3& b)const{return {x+b.x,y+b.y,z+b.z};}Vector3 operator*(float b)const{return {x*b,y*b,z*b};}};
using D3DXVECTOR3=Vector3;
void D3DXVec3Normalize(Vector3* a,const Vector3* b){float l=std::sqrt(b->x*b->x+b->y*b->y+b->z*b->z);*a=l?*b*(1.f/l):Vector3();}
struct WorldTriangle {unsigned f=8;unsigned flags()const{return f;}Vector3 normal()const{return {0,1,0};}};
constexpr unsigned TRIANGLE_TERRAIN=8;
struct DecalRayHit {bool found=false;WorldTriangle hit;};
struct ClientSpace {bool dead=false;float distance=2;unsigned flags=8;bool cleared(){return dead;}
 float collide(const Vector3&,const Vector3&,DecalRayHit& out){out.found=distance!=-1;out.hit.f=flags;return distance;}};
using ClientSpacePtr=std::shared_ptr<ClientSpace>;
struct Matrix {float rows[4][4]={{1,0,0,0},{0,1,0,0},{0,0,1,0},{0,0,0,1}};float* operator[](unsigned i){return rows[i];}};
std::array<float,3> scaling;
void D3DXMatrixLookAtLH(Matrix* m,const Vector3* p,const Vector3*,const Vector3*){*m=Matrix();m->rows[3][0]=-p->x;m->rows[3][1]=-p->y;m->rows[3][2]=-p->z;}
void D3DXMatrixRotationZ(Matrix* m,float){*m=Matrix();}
void D3DXMatrixMultiply(Matrix* out,const Matrix* a,const Matrix* b){Matrix result;for(unsigned i=0;i<4;++i)for(unsigned j=0;j<4;++j){result[i][j]=0;for(unsigned k=0;k<4;++k)result[i][j]+=a->rows[i][k]*b->rows[k][j];}*out=result;}
Matrix* D3DXMatrixInverse(Matrix* out,void*,const Matrix* in){*out=*in;for(unsigned i=0;i<3;++i)out->rows[3][i]=-in->rows[3][i];return out;}
void D3DXMatrixScaling(Matrix* out,float x,float y,float z){scaling={{x,y,z}};*out=Matrix();out->rows[0][0]=x;out->rows[1][1]=y;out->rows[2][2]=z;}
struct D3DXQUATERNION {float x=0,y=0,z=0,w=1;};
void D3DXQuaternionRotationMatrix(D3DXQUATERNION* q,const Matrix*){*q={};}
struct BoundingBox {void setBounds(const Vector3&,const Vector3&){}void transformBy(const Matrix&){}};
struct DynamicTextureSet{};
struct DynamicDecal {wot::StockDynamicDecalInstance data;std::shared_ptr<DynamicTextureSet> textures;BoundingBox bounds;};
struct Owner {ClientSpacePtr space=std::make_shared<ClientSpace>();float time=12;std::vector<DynamicDecal> decals;bool retire=false,fail=false;unsigned loads=0;
 void syncSpace(){if(space&&space->cleared())space.reset();}
 std::shared_ptr<DynamicTextureSet> loadTextures(std::array<string,3>,string&){++loads;if(retire)space->dead=true;return fail?nullptr:std::make_shared<DynamicTextureSet>();}};
std::shared_ptr<Owner> renderer=std::make_shared<Owner>();unsigned nextDynamicDecalPriority=0;
namespace BW {using string=std::string;template<class T>using vector=std::vector<T>;}
using int32=int;template<class T>using SmartPointer=std::shared_ptr<T>;
template<class T>struct Singleton {static T& instance(){static T value;return value;}};
float* watchedCoefficient=nullptr;
#define BW_BEGIN_NAMESPACE
#define BW_END_NAMESPACE
#define MF_WATCH(path,value,...) watchedCoefficient=&value
@DYNAMIC_MANAGER@
@DYNAMIC_CONSTRUCTOR@
template<class T,class=void>struct PublicCoefficient:std::false_type{};
template<class T>struct PublicCoefficient<T,decltype(void(std::declval<T>().decalObjectImpactSizeCoefficient))>:std::true_type{};
static_assert(!PublicCoefficient<DynamicDecalManager>::value,"The real SDK coefficient must stay private");
bool dynamicFailure(string& error,const char* text){error=text;return false;}
unsigned queries=0;bool queryAdmitted=true,querySuccess=true;
bool queryStockGroundDecalProjection(const ClientSpacePtr&,const Vector3&,const Vector3&,const Vector2&,const WorldTriangle&,
 wot::GroundDecalMath::Projection& p,bool& admitted,string& error){++queries;p={{{0,-1,0}},1.75f,.65f,true};admitted=queryAdmitted;if(!querySuccess)error="query failed";return querySuccess;}
@PRODUCTION@
int main()
{
 auto& registry=DynamicDecalManager::instance();assert(watchedCoefficient&&registry.stockObjectImpactSizeCoefficient()==.5f);
 string error;const Vector3 start(0,2,0),end(0,-2,0);const Vector2 size{2,4};
 auto submit=[&](bool simple){return addStockDynamicDecal(10,start,end,size,.3f,"d","n","s",simple,error);};
 assert(submit(true)&&queries==0&&renderer->decals.size()==1&&scaling[0]==2&&scaling[1]==4&&scaling[2]==.5f);
 renderer->space->flags=16;*watchedCoefficient=.25f;
 assert(submit(true)&&queries==0&&scaling[0]==.5f&&scaling[1]==1&&scaling[2]==.5f);
 *watchedCoefficient=.75f;
 assert(submit(false)&&queries==0&&scaling[0]==1.5f&&scaling[1]==3);
 renderer->space->flags=8;assert(submit(false)&&queries==1&&scaling[0]==1.5f&&scaling[1]==3&&scaling[2]==.65f);
 unsigned count=renderer->decals.size(),loads=renderer->loads;queryAdmitted=false;
 assert(submit(false)&&queries==2&&renderer->decals.size()==count&&renderer->loads==loads);
 querySuccess=false;assert(!submit(false)&&!error.empty()&&renderer->decals.size()==count);
 querySuccess=queryAdmitted=true;renderer->space->distance=-1;
 assert(submit(false)&&queries==3&&error.empty());renderer->space->distance=2;
 renderer->fail=true;assert(!submit(true)&&renderer->decals.size()==count);renderer->fail=false;
 renderer->retire=true;assert(!submit(true)&&!error.empty()&&renderer->decals.size()==count);
 assert(!submit(true)&&!error.empty());renderer.reset();assert(!submit(true)&&!error.empty());
}
