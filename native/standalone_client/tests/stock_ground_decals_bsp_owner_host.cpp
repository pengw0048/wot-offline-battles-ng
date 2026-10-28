#include "stock_ground_decals_bsp.hpp"
#include <cassert>
namespace BW {
using uint32=unsigned;
struct Vector3 {float x,y,z;};
struct WorldTriangle {unsigned value;unsigned flags()const{return value;}};
struct PlaneEq {Vector3 n;float distance;const Vector3& normal()const{return n;}float d()const{return distance;}};
struct BSPNode {BSPNode* pFront_;BSPNode* pBack_;PlaneEq planeEq_;WorldTriangle* pTriangles_;unsigned numTriangles_;WorldTriangle** ppSharedTriangles_;unsigned numSharedTriangles_;};
struct CollisionVisitor {virtual~CollisionVisitor(){}virtual bool visit(const WorldTriangle&,float)=0;};
struct BSPTree {BSPNode* pRoot_;void visitStockDecalBox(const Vector3&,const Vector3&,const Vector3&,const Vector3&,CollisionVisitor*)const;};
@BSP@
}
int main()
{
 using namespace BW;
 WorldTriangle values[]={{1},{2},{3},{255}};WorldTriangle* shared[]={values};
 BSPNode front{nullptr,nullptr,{{1,0,0},0},values+1,1,nullptr,0};
 BSPNode back{nullptr,nullptr,{{1,0,0},0},values+2,2,nullptr,0};
 BSPNode root{&front,&back,{{1,0,0},0},values,1,shared,1};BSPTree tree{&root};
 struct Collector:CollisionVisitor {std::vector<unsigned> values;bool visit(const WorldTriangle& t,float)override{values.push_back(t.flags());return false;}}collector;
 tree.visitStockDecalBox({0,0,0},{2,0,0},{0,2,0},{0,0,2},&collector);
 assert((collector.values==std::vector<unsigned>{1,1,3,2}));
 tree.visitStockDecalBox({0,0,0},{2,0,0},{0,2,0},{0,0,2},nullptr);
 tree.pRoot_=nullptr;collector.values.clear();tree.visitStockDecalBox({0,0,0},{2,0,0},{0,2,0},{0,0,2},&collector);assert(collector.values.empty());
}
