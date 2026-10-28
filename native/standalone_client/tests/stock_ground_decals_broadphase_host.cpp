// SDK insertion and intersection execute over distinct real-purpose bounds.
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <vector>
#define MF_ASSERT(x) assert(x)
namespace BW {
using uint32=unsigned;
class Scene{};
struct Vector3 {float x,y,z;};
struct AABB {Vector3 lo,hi;bool insideOut()const{return lo.x>hi.x||lo.y>hi.y||lo.z>hi.z;}};
struct ConvexHull {AABB box;bool intersects(const AABB& b)const{return !b.insideOut()&&b.lo.x<=box.hi.x&&b.hi.x>=box.lo.x&&b.lo.y<=box.hi.y&&b.hi.y>=box.lo.y&&b.lo.z<=box.hi.z&&b.hi.z>=box.lo.z;}};
struct Matrix{};
template<class T>struct Table {
 std::vector<T> values;
 T& operator[](unsigned index){if(values.size()<=index)values.resize(index+1);return values[index];}
 const T& operator[](unsigned index)const{return values.at(index);}
};
struct Flags {bool dynamic=false;bool isDynamic()const{return dynamic;}};
struct SceneObject {unsigned id;Flags bits;const Flags& flags()const{return bits;}};
struct IntersectionSet {std::vector<SceneObject> objects;void insert(const SceneObject& object){objects.push_back(object);}};
struct DynamicObjectHandle {unsigned index_;};
struct Pool {unsigned next=0;DynamicObjectHandle create(){return {next++};}};
struct FakeOctree {
 using NodeIndex=unsigned;using NodeDataReference=unsigned;using RuntimeNodeDataReferences=std::vector<unsigned>;
 static constexpr unsigned INVALID_NODE=~0u;std::vector<AABB> boxes;mutable unsigned queries=0;
 unsigned insert(const AABB& b){if(b.insideOut())return INVALID_NODE;boxes.push_back(b);return boxes.size()-1;}
 unsigned dataOnLeaf(unsigned index){return index;}void updateNodeBound(unsigned){}
 void intersect(const ConvexHull& hull,RuntimeNodeDataReferences& output)const{++queries;for(unsigned i=0;i<boxes.size();++i)if(hull.intersects(boxes[i]))output.push_back(i);}
};
struct ObjectChangeView {unsigned adds=0;void notifyObjectsAdded(void*,const SceneObject*,unsigned n){adds+=n;}};
}
@CONTEXT_HEADER@
@CONTEXT_SOURCE@
namespace BW {
struct DynamicSceneProvider {
 using Octree=FakeOctree;using OctreeNodeContents=std::vector<DynamicObjectHandle>;using OctreeContents=Table<OctreeNodeContents>;using BoundingBoxCollection=Table<AABB>;
 struct DynamicObjectStorageInfo {unsigned collisionNode_,visibilityNode_;};
 struct Detail {static void insertObjectNode(DynamicObjectHandle,BoundingBoxCollection&,Octree&,OctreeContents&,Octree::NodeIndex*,bool);};
 std::vector<SceneObject> objects_;std::vector<DynamicObjectHandle> objectHandles_;Pool dynamicObjectHandles_;Table<unsigned> objectLookup_;
 BoundingBoxCollection objectCollisionBBs_,objectVisibilityBBs_;Table<Matrix> objectWorldXform_;Table<unsigned> objectDirtyFlags_;Table<DynamicObjectStorageInfo> objectStorageInfo_;
 Octree collisionTree_,visibilityTree_;OctreeContents collisionTreeContents_,visibilityTreeContents_;ObjectChangeView view;ObjectChangeView* pObjectChangeView_=&view;
 DynamicObjectHandle insertObject_internal(const SceneObject&,const Matrix&,const AABB&,const AABB&);
 DynamicObjectHandle addObject(const SceneObject&,const Matrix&,const AABB&,const AABB&);
 size_t intersect(const SceneIntersectContext&,const ConvexHull&,IntersectionSet&)const;
};
@INSERT_NODE@
@INSERT_OBJECT@
@ADD_OBJECT@
@DYNAMIC_INTERSECT@
// Terrain uses its original independent static scene octree and block bounds.
namespace CompiledSpace {
struct Span {unsigned a=0,b=0;unsigned first()const{return a;}unsigned last()const{return b;}unsigned range()const{return b-a+1;}};
using DataSpan=Span;
struct Terrain2SceneProvider {
 using Octree=FakeOctree;
 struct BlockInstance {bool pBlock_=true;AABB worldBB_;SceneObject sceneObject_;};
 Octree sceneOctree_;std::vector<Span> sceneOctreeContents_;std::vector<unsigned> sceneContents_;std::vector<BlockInstance> blockInstances_;
 bool isValid()const{return true;}
 size_t intersect(const SceneIntersectContext&,const ConvexHull&,IntersectionSet&)const;
};
@TERRAIN_INTERSECT@
}
}
int main()
{
 using namespace BW;
 Scene scene;SceneIntersectContext visual(scene);DynamicSceneProvider dynamic;
 const AABB hit{{-1,-1,-1},{1,1,1}},away{{20,20,20},{30,30,30}},empty{{1,1,1},{-1,-1,-1}};ConvexHull query{hit};
 // Real addObject -> insertObject_internal -> insertObjectNode owns both BBs.
 dynamic.addObject({1,{false}},Matrix{},hit,away);
 dynamic.addObject({2,{false}},Matrix{},away,hit);
 dynamic.addObject({3,{true}},Matrix{},hit,hit);
 dynamic.addObject({4,{false}},Matrix{},empty,hit);
 assert(dynamic.view.adds==4);
 IntersectionSet candidates;assert(dynamic.intersect(visual,query,candidates)==3);
 assert(candidates.objects[0].id==2&&candidates.objects[1].id==3&&candidates.objects[2].id==4);
 assert(dynamic.visibilityTree_.queries==1&&dynamic.collisionTree_.queries==0);
 SceneIntersectContext collision(scene);collision.includeStaticObjects(true);collision.includeDynamicObjects(false);collision.stockDecalCollisionOnly(true);
 candidates.objects.clear();assert(dynamic.intersect(collision,query,candidates)==1&&candidates.objects[0].id==1);
 assert(dynamic.visibilityTree_.queries==1&&dynamic.collisionTree_.queries==1);
 collision.includeStaticObjects(false);candidates.objects.clear();assert(dynamic.intersect(collision,query,candidates)==0&&candidates.objects.empty());
 collision.includeStaticObjects(true);
 CompiledSpace::Terrain2SceneProvider terrain;
 terrain.sceneOctree_.insert(hit);terrain.sceneOctreeContents_.push_back({0,1});terrain.sceneContents_={0,1};
 terrain.blockInstances_.push_back({true,hit,{5,{false}}});terrain.blockInstances_.push_back({true,away,{6,{false}}});
 assert(terrain.intersect(collision,query,candidates)==1&&candidates.objects.size()==1&&candidates.objects[0].id==5);
 collision.includeStaticObjects(false);candidates.objects.clear();assert(terrain.intersect(collision,query,candidates)==0);
 // An ordinary visual context remains unchanged after a collision query.
 candidates.objects.clear();assert(dynamic.intersect(visual,query,candidates)==3&&candidates.objects[0].id==2);
}
