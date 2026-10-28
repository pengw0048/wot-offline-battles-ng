// Compile the complete production query against explicit scene/terrain boundaries.
#include "stock_ground_decals_data.hpp"
#include <cassert>
#include <cmath>
#include <memory>
#include <string>
#include <vector>
#include <array>
namespace BW {
template<class T>using SmartPointer=std::shared_ptr<T>;
template<class T>using vector=std::vector<T>;
struct Vector2 {float x,y;};
struct Vector3 {
 float x=0,y=0,z=0;Vector3(){}Vector3(float a,float b,float c):x(a),y(b),z(c){}
 Vector3 operator+(const Vector3& b)const{return {x+b.x,y+b.y,z+b.z};}
 Vector3 operator-(const Vector3& b)const{return {x-b.x,y-b.y,z-b.z};}
 Vector3 operator*(float b)const{return {x*b,y*b,z*b};}
};
using D3DXVECTOR3=Vector3;
void D3DXVec3Normalize(Vector3* out,const Vector3* in){float l=std::sqrt(in->x*in->x+in->y*in->y+in->z*in->z);*out=l?*in*(1.f/l):Vector3();}
struct WorldTriangle {
 Vector3 values[3];unsigned flags_;
 WorldTriangle(Vector3 a,Vector3 b,Vector3 c,unsigned f=0):values{a,b,c},flags_(f){}
 const Vector3& v0()const{return values[0];}const Vector3& v1()const{return values[1];}const Vector3& v2()const{return values[2];}unsigned flags()const{return flags_;}
};
constexpr unsigned TRIANGLE_TERRAIN=8;
using RealWTriangleSet=vector<WorldTriangle>;
std::vector<unsigned> order;
struct SceneObject {
 unsigned type;void* instance;vector<WorldTriangle> triangles;
 template<class T>bool isType()const{return type==T::type;}
 template<class T>const T* getAs()const{return static_cast<const T*>(instance);}
};
struct IntersectionSet {
 using SceneObjectType=unsigned;vector<vector<SceneObject>> objects;
 unsigned maxType()const{return objects.size();}const vector<SceneObject>& objectsOfType(unsigned i)const{return objects[i];}
};
struct PlaneEq {Vector3 normal;float d;PlaneEq(Vector3 n,float v):normal(n),d(v){}};
struct ConvexHull {std::array<PlaneEq,6> planes;ConvexHull(unsigned count,const PlaneEq* p):planes{{p[0],p[1],p[2],p[3],p[4],p[5]}}{assert(count==6);}};
struct BoundingBox {
 Vector3 lo,hi;bool empty=true;
 void addBounds(const Vector3& p){if(empty){lo=hi=p;empty=false;}else{lo={std::fmin(lo.x,p.x),std::fmin(lo.y,p.y),std::fmin(lo.z,p.z)};hi={std::fmax(hi.x,p.x),std::fmax(hi.y,p.y),std::fmax(hi.z,p.z)};}}
 const Vector3& minBounds()const{return lo;}const Vector3& maxBounds()const{return hi;}
};
struct Scene;
struct SceneIntersectContext {bool stat=true,dynamic=true,collision=false;explicit SceneIntersectContext(const Scene&) {}
 void includeStaticObjects(bool v){stat=v;}void includeDynamicObjects(bool v){dynamic=v;}void stockDecalCollisionOnly(bool v){collision=v;}};
struct IntersectSceneView {
 IntersectionSet source;std::array<float,6> distances;
 void intersect(const SceneIntersectContext& context,const ConvexHull& hull,IntersectionSet& out){assert(context.stat&&!context.dynamic&&context.collision);out=source;for(unsigned i=0;i<6;++i)distances[i]=hull.planes[i].d;}
};
struct Scene {IntersectSceneView view;bool available=true;template<class T>T* getView(){return available?&view:nullptr;}};
struct ClientSpace {Scene scene_;bool dead=false;bool cleared()const{return dead;}Scene& scene(){return scene_;}};
namespace Terrain {
struct TerrainHeightMap2 {
 RealWTriangleSet triangles;std::array<Vector3,6> query;
 void visitTriangles(RealWTriangleSet& out,const Vector3& c,const Vector3& a,const Vector3& b,const Vector3& d,const Vector3& clip,const Vector3& n){query={{c,a,b,d,clip,n}};out=triangles;}
};using TerrainHeightMap2Ptr=SmartPointer<TerrainHeightMap2>;
struct TerrainBlock2 {TerrainHeightMap2Ptr height;bool requested=false;TerrainHeightMap2Ptr getHighestLodHeightMap(bool highest){assert(highest);requested=true;return height;}};
using TerrainBlock2Ptr=SmartPointer<TerrainBlock2>;
}
struct Matrix {Vector3 translation;Vector3 applyToOrigin()const{return translation;}};
namespace CompiledSpace {
struct Terrain2SceneProvider {
 struct BlockInstance {static constexpr unsigned type=1;Matrix transform_;Terrain::TerrainBlock2Ptr pBlock_;};
 static bool collectStockDecalTriangles(const SceneObject&,const Vector3&,const Vector3&,const Vector3&,const Vector3&,vector<WorldTriangle>&);
};
@TERRAIN@
struct StockStaticModelSceneLoader {
 static bool collectStockDecalTriangles(const SceneObject& object,const Vector3&,const Vector3&,const Vector3&,const Vector3&,vector<WorldTriangle>& output){
 if(object.type!=2)return false;order.push_back(object.type);output=object.triangles;return true;}
};
}
using MathPoint=wot::GroundDecalMath::Point;
MathPoint point(const Vector3& value){return {{value.x,value.y,value.z}};}
@QUERY@
}
int main()
{
 using namespace BW;
 auto space=std::make_shared<ClientSpace>();std::string error;bool admitted=false;wot::GroundDecalMath::Projection result;
 const Vector3 start(0,2,0),end(0,-2,0);const Vector2 size{2,2};
 const WorldTriangle nearest({-1,0,-1},{-1,0,1},{1,0,1},8);
 CompiledSpace::Terrain2SceneProvider::BlockInstance terrain;
 terrain.transform_.translation={7,0,9};terrain.pBlock_=std::make_shared<Terrain::TerrainBlock2>();
 terrain.pBlock_->height=std::make_shared<Terrain::TerrainHeightMap2>();
 terrain.pBlock_->height->triangles.push_back({{-8,.5f,-10},{-8,.5f,-8},{-6,.5f,-8}});
 SceneObject terrainObject{1,&terrain,{}};SceneObject staticObject{2,nullptr,{WorldTriangle({-1,0,-1},{-1,0,1},{1,0,1},256)}};
 auto& objects=space->scene_.view.source.objects;objects.resize(3);objects[1].push_back(terrainObject);
 assert(queryStockGroundDecalProjection(space,start,end,size,nearest,result,admitted,error)&&!admitted&&error.empty());
 assert(terrain.pBlock_->requested);const auto& q=terrain.pBlock_->height->query;
 assert(q[0].x==(-1.f/3.f)-7 && q[0].z==(1.f/3.f)-9 && q[4].y==-2.5f && q[5].y==2.5f);
 objects[2].push_back(staticObject);
 assert(queryStockGroundDecalProjection(space,start,end,size,nearest,result,admitted,error)&&admitted&&result.object);
 assert(std::abs(result.distance-1.75f)<.00001f&&result.axis[1]==-1.f&&result.depth==.65f);
 assert(space->scene_.view.distances[0]==0 && space->scene_.view.distances[1]==0 && space->scene_.view.distances[2]==2 && space->scene_.view.distances[3]==2);
 objects[1].clear();assert(queryStockGroundDecalProjection(space,start,end,size,nearest,result,admitted,error)&&admitted&&result.distance==2.f&&result.depth==.4f);
 objects[2].clear();assert(queryStockGroundDecalProjection(space,start,end,size,nearest,result,admitted,error)&&!admitted);
 space->scene_.available=false;assert(!queryStockGroundDecalProjection(space,start,end,size,nearest,result,admitted,error)&&!admitted&&!error.empty());
 space->dead=true;assert(!queryStockGroundDecalProjection(space,start,end,size,nearest,result,admitted,error)&&!admitted);
 assert(!queryStockGroundDecalProjection({},start,end,size,nearest,result,admitted,error)&&!admitted);
}
