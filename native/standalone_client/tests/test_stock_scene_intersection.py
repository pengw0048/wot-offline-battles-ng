"""Execute the SDK visibility traversal and count actual node-list copies."""
from pathlib import Path
import itertools
import os
import subprocess
import sys
import tarfile
import tempfile
import unittest

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import test_stock_space_models as fixtures


HOST = r'''
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <stdexcept>
#include <vector>
#define MF_ASSERT(value) assert(value)
unsigned copies=0;
struct DynamicObjectHandle{unsigned index_;};
struct Node:std::vector<DynamicObjectHandle>{
using std::vector<DynamicObjectHandle>::vector;
Node()=default;Node(const Node& other):std::vector<DynamicObjectHandle>(other){++copies;}
Node& operator=(const Node&)=default;
};
struct AABB{bool valid;unsigned category;bool insideOut()const{return !valid;}};
struct ConvexHull{unsigned mask;bool intersects(const AABB& box)const{return (box.category&mask)!=0;}};
struct SceneIntersectContext{bool dynamicEnabled,staticEnabled,collision;
bool includeDynamicObjects()const{return dynamicEnabled;}
bool includeStaticObjects()const{return staticEnabled;}
bool stockDecalCollisionOnly()const{return collision;}};
struct Flags{bool dynamic;bool isDynamic()const{return dynamic;}};
struct SceneObject{unsigned id;Flags bits;const Flags& flags()const{return bits;}};
struct IntersectionSet{std::vector<unsigned> objects;void insert(const SceneObject& value){objects.push_back(value.id);}};
struct Octree{using RuntimeNodeDataReferences=std::vector<unsigned>;using NodeDataReference=unsigned;
void intersect(const ConvexHull&,RuntimeNodeDataReferences& output)const{output={0,1,2};}};
struct DynamicSceneProvider{
using OctreeNodeContents=Node;
Octree visibilityTree_,collisionTree_;std::vector<Node> visibilityTreeContents_,collisionTreeContents_;
std::vector<AABB> objectVisibilityBBs_,objectCollisionBBs_;std::vector<SceneObject> objects_;std::vector<unsigned> objectLookup_;
size_t intersect(const SceneIntersectContext&,const ConvexHull&,IntersectionSet&)const;
};
@BODY@
int main(){
DynamicSceneProvider provider;
provider.visibilityTreeContents_.resize(3);
provider.visibilityTreeContents_[0]={{0},{1},{2}};
provider.visibilityTreeContents_[2]={{3},{4},{5}};
provider.collisionTreeContents_.resize(3);
provider.collisionTreeContents_[0]={{5},{3},{1}};
provider.collisionTreeContents_[2]={{4},{2},{0}};
provider.objects_={{100,{false}},{101,{false}},{102,{true}},{103,{false}},{104,{false}},{105,{true}}};
provider.objectLookup_={0,1,2,3,4,5};
provider.objectVisibilityBBs_={{true,1},{false,1},{true,2},{true,3},{true,4},{false,7}};
provider.objectCollisionBBs_={{false,7},{true,2},{true,1},{true,4},{true,3},{true,7}};
copies=0;unsigned queries=0;
for(bool collision:{false,true})for(bool dynamic:{false,true})for(bool statics:{false,true})
for(unsigned mask:{0u,1u,2u,3u,4u,7u}){
if(collision&&!@GROUND@)continue;
IntersectionSet output;const auto count=provider.intersect({dynamic,statics,collision},{mask},output);
std::vector<unsigned> expected;
if(collision?statics:dynamic){
 const unsigned visualOrder[]={0,1,2,3,4,5},collisionOrder[]={5,3,1,4,2,0};
 const auto& bounds=collision?provider.objectCollisionBBs_:provider.objectVisibilityBBs_;
 for(unsigned n=0;n<6;++n){const unsigned i=collision?collisionOrder[n]:visualOrder[n];
  if(bounds[i].valid&&(bounds[i].category&mask)&&(!collision||!provider.objects_[i].bits.dynamic))
   expected.push_back(provider.objects_[i].id);
 }
}
if(output.objects!=expected||count!=expected.size())throw std::runtime_error("visibility set changed");
++queries;
}
std::printf("%u visibility queries retained exact membership/order; node copies=%u\n",queries,copies);
if(copies)throw std::runtime_error("visibility traversal copied immutable node memberships");
}
'''


class SceneIntersectionTests(unittest.TestCase):
    def execute(self, root, name, body, ground=False, copies=False):
        source, binary = root / (name + '.cpp'), root / name
        source.write_text(HOST.replace('@BODY@', body).replace('@GROUND@', str(ground).lower()))
        built = subprocess.run(['c++', '-std=c++14', '-Wall', '-Wextra', '-Werror',
                                '-UNDEBUG', str(source), '-o', str(binary)], capture_output=True, text=True)
        self.assertEqual(built.returncode, 0, built.stderr)
        result = subprocess.run([str(binary)], capture_output=True, text=True)
        if copies:
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('copied immutable node memberships', result.stderr)
        else:
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn('node copies=0', result.stdout)
            self.assertIn('48 visibility queries' if ground else '24 visibility queries', result.stdout)

    def test_actual_sdk_membership_without_node_copies(self):
        fixtures.StockSpaceModelsTests.setUpClass()
        fixture = fixtures.StockSpaceModelsTests()
        with tempfile.TemporaryDirectory(prefix='visibility-membership-') as temporary:
            root = Path(temporary)
            fixture.fixture(root)
            path = root / 'lib/space/dynamic_scene_provider.cpp'
            original = path.read_text(encoding='utf-8-sig')
            fixtures.patch.patch(root)
            patched = path.read_text()
            signature = 'size_t DynamicSceneProvider::intersect('
            before, after = [fixtures.definition(text, signature) for text in (original, patched)]
            # Collision and its potentially re-entrant callbacks remain exact.
            self.assertEqual(patched.replace(after, before, 1), original)
            fixtures.patch.patch(root)
            self.assertEqual(path.read_text(), patched)
            for name, body in (('after', after), ('old-copy', before)):
                self.execute(root, name, body, copies=name == 'old-copy')

    def test_ground_decal_query_composes_in_both_orders_and_repeatedly(self):
        fixtures.StockSpaceModelsTests.setUpClass()
        fixture = fixtures.StockSpaceModelsTests()
        ground = fixtures.module('patch_stock_ground_decals')
        names = ('lib/physics2/bsp.hpp', 'lib/physics2/bsp.cpp',
                 'lib/compiled_space/terrain2_scene_provider.hpp',
                 'lib/compiled_space/terrain2_scene_provider.cpp',
                 'lib/scene/scene_intersect_context.hpp', 'lib/moo/dynamic_decal.hpp')
        archive = Path(os.environ.get('WOT_PUBLIC_ENGINE_ARCHIVE', '/tmp/wot-bigworld-engine-14.4.1.tar.gz'))
        with tarfile.open(archive) as original:
            files = dict(fixture.original)
            for relative in names:
                files[relative] = original.extractfile(next(name for name in original.getnames()
                    if name.endswith('/programming/bigworld/' + relative))).read()
        fixture.original = files
        signature = 'size_t DynamicSceneProvider::intersect('
        with tempfile.TemporaryDirectory(prefix='scene-ground-decal-') as temporary:
            directory = Path(temporary)
            reference = directory / 'ground-only'
            fixture.fixture(reference)
            ground.patch(reference)
            original = (reference / 'lib/space/dynamic_scene_provider.cpp').read_text()
            before = fixtures.definition(original, signature)
            self.execute(reference, 'old-copy', before, ground=True, copies=True)
            for index, order in enumerate(itertools.permutations((fixtures.patch.patch, ground.patch))):
                with self.subTest(order=[function.__module__ for function in order]):
                    root = directory / str(index)
                    fixture.fixture(root)
                    for function in order:
                        function(root)
                    snapshot = {p: p.read_bytes() for p in root.rglob('*') if p.is_file()}
                    for function in order:
                        function(root)
                    self.assertEqual(snapshot, {p: p.read_bytes() for p in root.rglob('*') if p.is_file()})
                    path = root / 'lib/space/dynamic_scene_provider.cpp'
                    patched = path.read_text()
                    after = fixtures.definition(patched, signature)
                    # The real collision callback traversal must remain untouched.
                    self.assertEqual(patched.replace(after, before, 1), original)
                    self.execute(root, 'after', after, ground=True)
                    path.write_text(patched.replace(
                        '(collisionOnly ? collisionTreeContents_ : visibilityTreeContents_)',
                        'unreviewedTreeContents_'))
                    snapshot = {p: p.read_bytes() for p in root.rglob('*') if p.is_file()}
                    with self.assertRaises(ValueError):
                        fixtures.patch.patch(root)
                    self.assertEqual(snapshot, {p: p.read_bytes() for p in root.rglob('*') if p.is_file()})


if __name__ == '__main__':
    unittest.main()
