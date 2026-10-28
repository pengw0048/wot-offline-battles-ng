#include <cassert>
#include <cmath>
#include <cstdint>
#include <string>
#include <map>
#include <vector>
using uint32=std::uint32_t;
namespace BW{using string=std::string;}
namespace wot { inline bool stockSimulationWorker(){return false;} }
struct Vector3 {
float x,y,z;Vector3(float a=0,float b=0,float c=0):x(a),y(b),z(c){}
Vector3 operator+(const Vector3& v)const{return {x+v.x,y+v.y,z+v.z};}
Vector3 operator-(const Vector3& v)const{return {x-v.x,y-v.y,z-v.z};}
bool operator==(const Vector3& v)const{return x==v.x&&y==v.y&&z==v.z;}
};
struct Vector4{float x,y,z,w;};
struct Matrix{float x=0,y=0,z=0;};
void D3DXVec3TransformNormal(Vector3* out,const Vector3* v,const Matrix*){*out=*v;}
void D3DXVec3Transform(Vector4* out,const Vector3* v,const Matrix* m){*out={v->x+m->x,v->y+m->y,v->z+m->z,1};}
void D3DXVec3Normalize(Vector3* out,const Vector3* v){const float n=sqrtf(v->x*v->x+v->y*v->y+v->z*v->z);if(n==0){*out={};return;}*out={v->x/n,v->y/n,v->z/n};}
struct Box{Vector3 minimum{-10,-1,-20},maximum{10,1,20};const Vector3& minBounds()const{return minimum;}const Vector3& maxBounds()const{return maximum;}};
struct Water{
bool initialised_=true,visible_=true,stockQueryDataReady_=false;Matrix transform_,invTransform_;Box bbActual_;
Vector3 pos;const Vector3& position()const{return pos;}
void height(float y){pos.y=y;transform_.y=y;invTransform_.y=-y;}
float stockQueryRayDistance(const Vector3& start,const Vector3& end)const;
float stockWaterGeometryDistance(const Vector3& start,const Vector3& end)const;
float stockGroundRayDistance(const Vector3& start,const Vector3& end)const;
};
float collideStockWorkerWater(const Water*,const Vector3&,const Vector3&){return -1.f;}
@WATER@
struct Waters:std::vector<Water*> {
unsigned ripples=0;Vector3 ripple;
static Waters& instance(){static Waters w;return w;}
void addMovement(const Vector3& a,const Vector3& b,float d){assert(a.y-b.y==10&&d==10);ripple=a-Vector3(0,5,0);++ripples;}
};
struct CollisionObstacle{};
constexpr unsigned TRIANGLE_PROJECTILENOCOLLIDE=128;
constexpr int COLLIDE_ALL=0,COLLIDE_BEFORE=1;
struct WorldTriangle{unsigned flags,material;unsigned collisionFlags()const{return flags;}unsigned materialKind()const{return material;}};
struct CollisionCallback{virtual ~CollisionCallback()=default;virtual int operator()(const CollisionObstacle&,const WorldTriangle&,float)=0;};
struct ClientSpace {
struct Triangle{WorldTriangle triangle;float distance;};std::vector<Triangle> triangles;
float collide(const Vector3&,const Vector3&,CollisionCallback& callback){for(const auto& t:triangles)if(callback({},t.triangle,t.distance)==COLLIDE_BEFORE)return t.distance;return -1;}
};
struct MaterialKinds {
std::map<unsigned,std::string> kinds;static MaterialKinds& instance(){static MaterialKinds m;return m;}
std::string userString(unsigned m,const char* key){assert(std::string(key)=="effect_material");return kinds[m];}
};
const char* groundNames[]={"ground","stone","wood","metal","snow","sand","water"};
@SURFACE@
int main(){
Water first,second;first.height(30);second.height(80);
assert(first.stockGroundRayDistance({0,1000,0},{0,-1000,0})==970);
first.initialised_=false;assert(first.stockGroundRayDistance({0,1000,0},{0,-1000,0})==-1);first.initialised_=true;
first.visible_=false;assert(first.stockGroundRayDistance({0,1000,0},{0,-1000,0})==-1);first.visible_=true;
assert(first.stockGroundRayDistance({0,1000,0},{0,1000,0})==-1);
assert(first.stockGroundRayDistance({0,1000,0},{0,2000,0})==-1);
assert(first.stockGroundRayDistance({0,1000,0},{0,999,0})==970); // The original query has no maximum segment-distance clip.
assert(first.stockGroundRayDistance({10.0005f,1000,0},{10.0005f,-1000,0})>0);
assert(first.stockGroundRayDistance({10.002f,1000,0},{10.002f,-1000,0})==-1);
Waters::instance().push_back(&first);Waters::instance().push_back(&second);
ClientSpace space;size_t ground=99;
MaterialKinds::instance().kinds[1]="snow";MaterialKinds::instance().kinds[2]="stone";
space.triangles={{{TRIANGLE_PROJECTILENOCOLLIDE,2},800},{{0,1},980}};
auto point=groundPoint(space,{0,2,0},ground);assert(point.y==30&&ground==6&&Waters::instance().ripples==1); // Registration order, not nearest water.
first.visible_=false;point=groundPoint(space,{0,2,0},ground);assert(point.y==80&&ground==6&&Waters::instance().ripples==2);
space.triangles={{{TRIANGLE_PROJECTILENOCOLLIDE,2},800},{{0,1},900}};
point=groundPoint(space,{0,2,0},ground);assert(point.y==100&&ground==4&&Waters::instance().ripples==2);
Waters::instance().clear();space.triangles.clear();point=groundPoint(space,{1,2,3},ground);assert(point.x==1&&point.y==2&&point.z==3&&ground==0);
space.triangles={{{0,3},999}};point=groundPoint(space,{0,2,0},ground);assert(point.y==1&&ground==0);
MaterialKinds::instance().kinds[3]="unknown";point=groundPoint(space,{0,2,0},ground);assert(point.y==1&&ground==7);
space.triangles.clear();first.height(-101);first.visible_=true;Waters::instance().push_back(&first);point=groundPoint(space,{0,2,0},ground);assert(point.y==2&&ground==0);
}
