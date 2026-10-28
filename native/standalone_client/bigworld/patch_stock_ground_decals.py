"""Expose readonly original BSP/terrain geometry to the ground-decal owner."""
import argparse
from pathlib import Path

BSP_METHOD='''void BSPTree::visitStockDecalBox(const Vector3& center, const Vector3& first,
    const Vector3& second, const Vector3& third, CollisionVisitor* visitor) const
{
    if (!visitor) return;
    auto point=[](const Vector3& value) { return wot::GroundDecalMath::Point{{value.x,value.y,value.z}}; };
    wot::GroundDecalMath::visitBox(pRoot_,point(center),point(first),point(second),point(third),
        [](BSPNode* node) {
            const Vector3& normal=node->planeEq_.normal();
            return std::array<float,4>{{normal.x,normal.y,normal.z,node->planeEq_.d()}};
        },
        [](BSPNode* node,unsigned index) { return index?node->pBack_:node->pFront_; },
        [visitor](BSPNode* node) {
            for (uint32 index=0;index<node->numTriangles_+node->numSharedTriangles_;++index)
            {
                const WorldTriangle& triangle=index<node->numTriangles_?node->pTriangles_[index]:
                    *node->ppSharedTriangles_[index-node->numTriangles_];
                // 1179620 tests the low collision byte before its callback.
                if ((triangle.flags()&255u)!=255u) visitor->visit(triangle,0.f);
            }
        });
}

'''
TERRAIN_METHOD='''bool Terrain2SceneProvider::collectStockDecalTriangles(const SceneObject& object,
    const Vector3& center, const Vector3& first, const Vector3& second, const Vector3& third,
    BW::vector<WorldTriangle>& output)
{
    if (!object.isType<BlockInstance>()) return false;
    const BlockInstance* instance=object.getAs<BlockInstance>();
    if (!instance || !instance->pBlock_) return true;
    const Terrain::TerrainBlock2Ptr block=instance->pBlock_;
    const Vector3 translation=instance->transform_.applyToOrigin();
    // f5ee50 requests the actual highest LOD, then uses translation only.
    const Terrain::TerrainHeightMap2Ptr height=block->getHighestLodHeightMap(true);
    if (!height) return true;
    const Vector3 local=center-translation;
    RealWTriangleSet triangles;
    height->visitTriangles(triangles,local,first,second,third,local-second,second);
    for (const auto& triangle:triangles)
        output.push_back(WorldTriangle(triangle.v0()+translation,triangle.v1()+translation,
            triangle.v2()+translation,TRIANGLE_TERRAIN));
    return true;
}

'''


def patch(root):
    root=Path(root)
    pending={}
    def edit(name,before,after,present=None):
        source=pending.get(name,(root/name).read_text(encoding='utf-8-sig'))
        if source.count(present or after)==1:return
        if source.count(before)!=1:raise ValueError('Pinned ground decal collector changed: '+name+': '+repr(before))
        pending[name]=source.replace(before,after,1)
    # f5fb60 selects static objects; ab7fa0 forces selector10 (0x400),
    # produced by original SceneObjectFlags bit0x20. The public scene has no
    # collidable flag: its existing collision tree/inside-out bounds own it.
    context='lib/scene/scene_intersect_context.hpp'
    edit(context,'\tbool includeStaticObjects() const;',
         '\tbool stockDecalCollisionOnly() const { return stockDecalCollisionOnly_; }\n'
         '\tvoid stockDecalCollisionOnly(bool value) { stockDecalCollisionOnly_=value; }\n'
         '\tbool includeStaticObjects() const;')
    edit(context,'\tuint32 flags_;','\tbool stockDecalCollisionOnly_ = false;\n\tuint32 flags_;')
    dynamic='lib/space/dynamic_scene_provider.cpp'
    edit(dynamic,'\tif (!context.includeDynamicObjects())',
         '\tconst bool collisionOnly = context.stockDecalCollisionOnly();\n'
         '\tif (!(collisionOnly ? context.includeStaticObjects() : context.includeDynamicObjects()))')
    edit(dynamic,'\tvisibilityTree_.intersect( hull, nodeReferences );',
         '\tconst Octree& queryTree = collisionOnly ? collisionTree_ : visibilityTree_;\n'
         '\tqueryTree.intersect( hull, nodeReferences );')
    edit(dynamic,'\t\t\tvisibilityTreeContents_[nodeRef];',
         '\t\t\t(collisionOnly ? collisionTreeContents_ : visibilityTreeContents_)[nodeRef];')
    edit(dynamic,'\t\t\tconst AABB & bb = objectVisibilityBBs_[objHandle.index_];',
         '\t\t\tconst AABB & bb = (collisionOnly ? objectCollisionBBs_ : objectVisibilityBBs_)[objHandle.index_];\n'
         '\t\t\tconst SceneObject& object = objects_[objectLookup_[objHandle.index_]];\n'
         '\t\t\tif (collisionOnly && object.flags().isDynamic()) continue;')
    header='lib/physics2/bsp.hpp'
    edit(header,'\tvoid remapFlags( BSPFlagsMap& flagsMap );',
         '\tvoid visitStockDecalBox(const Vector3& center, const Vector3& first,\n'
         '\t\tconst Vector3& second, const Vector3& third, CollisionVisitor* visitor) const;\n\n'
         '\tvoid remapFlags( BSPFlagsMap& flagsMap );')
    source='lib/physics2/bsp.cpp'
    edit(source,'#include "bsp.hpp"','#include "bsp.hpp"\n#include "stock_ground_decals_bsp.hpp"')
    anchor='bool BSPTree::intersects( const WorldTriangle & triangle,\n\t\t\t\t\t\tconst WorldTriangle ** ppHitTriangle ) const'
    edit(source,anchor,BSP_METHOD+anchor,'void BSPTree::visitStockDecalBox(')
    header='lib/compiled_space/terrain2_scene_provider.hpp'
    edit(header,'\t\tstatic void registerHandlers( Scene & scene );',
         '\t\tstatic void registerHandlers( Scene & scene );\n'
         '\t\tstatic bool collectStockDecalTriangles(const SceneObject& object,\n'
         '\t\t\tconst Vector3& center, const Vector3& first, const Vector3& second, const Vector3& third,\n'
         '\t\t\tBW::vector<WorldTriangle>& output);')
    source='lib/compiled_space/terrain2_scene_provider.cpp'
    edit(source,'#include "terrain2_scene_provider.hpp"',
         '#include "terrain2_scene_provider.hpp"\n#include "terrain/terrain2/terrain_height_map2.hpp"')
    anchor='void Terrain2SceneProvider::registerHandlers( Scene & scene )'
    edit(source,anchor,TERRAIN_METHOD+anchor,'bool Terrain2SceneProvider::collectStockDecalTriangles(')
    directory=Path(__file__).parent
    headers={name:(directory/name).read_bytes()for name in ('stock_ground_decals_data.hpp','stock_ground_decals_bsp.hpp')}
    for name,text in pending.items():(root/name).write_text(text,encoding='utf-8')
    for name,data in headers.items():(root/'lib'/name).write_bytes(data)


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('engine_root',type=Path)
    patch(parser.parse_args().engine_root)
