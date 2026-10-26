"""Execute pinned provider methods and its real generation-based HandleTable.

Only octree insertion and event delivery are bounded engine shims. The provider
bookkeeping, swap removal and handle allocation/release run actual source.
"""
from pathlib import Path
import importlib.util
import subprocess
import tarfile
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('patch_stock_dynamic_scene_ownership',
                                             ROOT / 'bigworld/patch_stock_dynamic_scene_ownership.py')
PATCH = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PATCH)


def method(source, name, return_type):
    start = source.index('DynamicSceneProvider::'+name+'(')
    body = source.index('{', start)
    depth, end = 1, body+1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return return_type+' '+source[start:end]


PRELUDE = r'''
#include <cassert>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>
#define MF_ASSERT(x) assert(x)
namespace BW {
using uint32=std::uint32_t;
template<class T>using vector=std::vector<T>;
'''

HARNESS = r'''
using DynamicObjectHandle=Handle<24,8>;
struct SceneObject { unsigned id=0; };
struct Matrix { int value=0; };
struct AABB { int value=0; };
template<class T>struct Lookup {
 std::vector<T> data;
 T& operator[](size_t index){assert(index<4096);if(index>=data.size())data.resize(index+1);return data[index];}
};
struct DynamicSceneProvider;
struct ObjectChangeSceneView {
 std::function<void(DynamicSceneProvider*,const SceneObject&)> removed;
 std::vector<unsigned> removedIds;
 void notifyObjectsAdded(DynamicSceneProvider*,const SceneObject*,size_t count){assert(count==1);}
 void notifyObjectsRemoved(DynamicSceneProvider* provider,const SceneObject* objects,size_t count){
  assert(count==1);const unsigned identity=objects[0].id;removedIds.push_back(identity);
  auto observer=std::move(removed);removed=nullptr;if(observer)observer(provider,objects[0]);
  // The event's snapshot must survive arbitrary vector mutation by listeners.
  assert(objects[0].id==identity);
 }
};
struct DynamicSceneProvider {
 using BoundingBoxCollection=Lookup<AABB>;
 struct Octree {using NodeIndex=size_t;std::set<size_t> nodes;size_t next=0;};
 using OctreeContents=std::map<size_t,std::vector<DynamicObjectHandle>>;
 struct DynamicObjectStorageInfo {Octree::NodeIndex collisionNode_,visibilityNode_;};
 struct Detail {
  static void insertObjectNode(DynamicObjectHandle h,BoundingBoxCollection&,Octree& tree,OctreeContents& entries,Octree::NodeIndex* node,bool){
   *node=tree.next++;assert(tree.nodes.insert(*node).second);entries[*node].push_back(h);
  }
  static void removeObjectNode(DynamicObjectHandle h,Octree::NodeIndex node,Octree& tree,OctreeContents& entries){
   assert(tree.nodes.erase(node)==1);auto found=entries.find(node);assert(found!=entries.end());
   auto& list=found->second;auto at=std::find(list.begin(),list.end(),h);assert(at!=list.end());list.erase(at);entries.erase(found);
  }
 };
 DynamicSceneProvider(){dynamicObjectHandles_.grow(3);pObjectChangeView_=&events;}
 DynamicObjectHandle addObject(const SceneObject&,const Matrix&,const AABB&,const AABB&);
 void removeObject(const DynamicObjectHandle&);
 void updateObject(const DynamicObjectHandle&,const Matrix&,const AABB&,const AABB&);
 DynamicObjectHandle insertObject_internal(const SceneObject&,const Matrix&,const AABB&,const AABB&);
 void removeObject_internal(const DynamicObjectHandle&);
 void updateObject_internal(const DynamicObjectHandle&,const Matrix&,const AABB&,const AABB&);
 ObjectChangeSceneView events;ObjectChangeSceneView* pObjectChangeView_;
 std::vector<SceneObject> objects_;std::vector<DynamicObjectHandle> objectHandles_;
 HandleTable<DynamicObjectHandle> dynamicObjectHandles_;
 Lookup<size_t> objectLookup_;Lookup<AABB> objectCollisionBBs_,objectVisibilityBBs_,latestObjectCollisionBBs_,latestObjectVisibilityBBs_;
 Lookup<Matrix> objectWorldXform_,latestObjectWorldXform_;
 Lookup<DynamicObjectStorageInfo> objectStorageInfo_;Lookup<unsigned char> objectDirtyFlags_;
 uint32 numDirtyObjects_=0;Octree collisionTree_,visibilityTree_;OctreeContents collisionTreeContents_,visibilityTreeContents_;
};
'''

TEST = r'''
}
using namespace BW;
void verify(DynamicSceneProvider& p){
 assert(p.objects_.size()==p.objectHandles_.size());
 assert(p.objects_.size()==p.collisionTree_.nodes.size()&&p.objects_.size()==p.visibilityTree_.nodes.size());
 unsigned dirty=0;
 for(size_t i=0;i<p.objects_.size();++i){const auto h=p.objectHandles_[i];assert(p.dynamicObjectHandles_.isValid(h));assert(p.objectLookup_[h.index_]==i);dirty+=p.objectDirtyFlags_[h.index_]!=0;}
 assert(dirty==p.numDirtyObjects_);
}
int main(int argc,char** argv){
 const std::string mode=argc>1?argv[1]:"all";DynamicSceneProvider p;Matrix matrix;AABB bounds;
 const auto a=p.addObject(SceneObject{11},matrix,bounds,bounds);
 const auto b=p.addObject(SceneObject{22},matrix,bounds,bounds);
 const auto c=p.addObject(SceneObject{33},matrix,bounds,bounds);verify(p);
 if(mode=="baseline"){p.removeObject(a);assert(!p.dynamicObjectHandles_.isValid(a));return 0;}
 if(mode=="reentrant"){
  p.events.removed=[&](DynamicSceneProvider* current,const SceneObject& object){assert(object.id==11);current->removeObject(a);};
  p.removeObject(a);assert(p.events.removedIds.size()==1);verify(p);return 0;
 }
 // A caller may pass a reference to a packed handle entry that swap-remove
 // overwrites. Public removal must snapshot that handle before mutation.
 {DynamicSceneProvider alias;
  const auto x=alias.addObject(SceneObject{1},matrix,bounds,bounds);
  const auto y=alias.addObject(SceneObject{2},matrix,bounds,bounds);
  const auto z=alias.addObject(SceneObject{3},matrix,bounds,bounds);
  alias.removeObject(alias.objectHandles_.front());
  assert(!alias.dynamicObjectHandles_.isValid(x)&&alias.dynamicObjectHandles_.isValid(z));
  verify(alias);assert(alias.objectLookup_[z.index_]==0);
  alias.removeObject(y);alias.removeObject(z);verify(alias);
 }
 p.updateObject(a,matrix,bounds,bounds);p.updateObject(c,matrix,bounds,bounds);assert(p.numDirtyObjects_==2);
 // Remove the middle/first packed slot, preserve the moved handle lookup,
 // discard only the retired dirty flag and advance the real generation.
 p.removeObject(a);assert(!p.dynamicObjectHandles_.isValid(a));verify(p);
 assert(p.objectLookup_[c.index_]==0&&p.objects_[0].id==33&&p.numDirtyObjects_==1);
 const auto replacement=p.addObject(SceneObject{44},matrix,bounds,bounds);
 assert(replacement.index_==a.index_&&replacement.generation_!=a.generation_);
 p.removeObject(a);assert(p.objects_.size()==3);verify(p);
 // Repeat-remove and stale handles stay harmless after exact slot reuse.
 p.removeObject(replacement);p.removeObject(replacement);verify(p);
 p.removeObject(c);assert(p.numDirtyObjects_==0);verify(p);
 p.removeObject(b);verify(p);assert(p.objects_.empty());
 assert(p.dynamicObjectHandles_.size()==3);
 // A remove listener can remove another object and allocate enough new
 // members to reallocate every packed vector. Event identity remains stable.
 auto outer=p.addObject(SceneObject{101},matrix,bounds,bounds);
 auto other=p.addObject(SceneObject{102},matrix,bounds,bounds);
 std::vector<DynamicObjectHandle> added;
 p.events.removed=[&](DynamicSceneProvider* current,const SceneObject& object){
  assert(object.id==101);current->removeObject(outer);current->removeObject(other);
  for(unsigned i=0;i<100;++i)added.push_back(current->addObject(SceneObject{200+i},matrix,bounds,bounds));
  current->updateObject(added.back(),matrix,bounds,bounds);verify(*current);
 };
 p.removeObject(outer);verify(p);assert(p.objects_.size()==100&&p.numDirtyObjects_==1);
 assert(p.events.removedIds[p.events.removedIds.size()-2]==101&&p.events.removedIds.back()==102);
 for(const auto& h:added)p.removeObject(h);verify(p);assert(p.objects_.empty()&&p.numDirtyObjects_==0);
 // No expansion over repeated complete teardown: the original FIFO pool
 // reuses released slots rather than leaking a new index on every map.
 const size_t poolSize=p.dynamicObjectHandles_.size();
 for(unsigned batch=0;batch<50;++batch){
  std::vector<DynamicObjectHandle> handles;
  for(unsigned i=0;i<100;++i)handles.push_back(p.addObject(SceneObject{1000+i},matrix,bounds,bounds));
  for(const auto& h:handles)p.removeObject(h);verify(p);
 }
 assert(p.dynamicObjectHandles_.size()==poolSize);
 std::puts("Production dynamic-scene retirement, real handle generations, swap lookup and reentry passed");
}
'''


class StockDynamicSceneOwnershipTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        archive_path = Path('/tmp/wot-bigworld-engine-14.4.1.tar.gz')
        if not archive_path.exists():
            raise unittest.SkipTest('Pinned public-engine archive is required')
        cls.temporary = tempfile.TemporaryDirectory(prefix='stock-dynamic-ownership-')
        cls.root = Path(cls.temporary.name)
        with tarfile.open(archive_path) as archive:
            names = archive.getnames()
            def read(relative):
                matches = [name for name in names if name.endswith('/programming/bigworld/'+relative)]
                assert len(matches)==1
                return archive.extractfile(matches[0]).read().decode('utf-8-sig')
            cls.original = read('lib/space/dynamic_scene_provider.cpp')
            pool = read('lib/cstdmf/object_pool.hpp')
        # The target is MSVC. Adapt only two old public-header parsing defects
        # for the host compiler; the real create/release/isValid/FIFO bodies
        # and their stored generation data remain unchanged.
        pool = pool[pool.index('template <size_t indexBits'):pool.index('/**\n * This class is used to store a contiguous')]
        assert pool.count('return entries.capacity();')==1
        assert pool.count('const HandleType::base GENERATION_MASK')==1
        cls.pool = pool.replace('return entries.capacity();','return entries_.capacity();').replace(
            'const HandleType::base GENERATION_MASK','const typename HandleType::base GENERATION_MASK')
        path = cls.root / 'lib/space/dynamic_scene_provider.cpp'
        path.parent.mkdir(parents=True)
        path.write_text(cls.original)
        PATCH.patch(cls.root)
        cls.patched = path.read_text()
        cls.baseline = cls.build('baseline', cls.original)
        cls.binary = cls.build('patched', cls.patched)

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    @classmethod
    def build(cls, name, implementation):
        methods = '\n'.join(method(implementation, function, returns) for function, returns in (
            ('addObject', 'DynamicObjectHandle'), ('removeObject', 'void'), ('updateObject', 'void'),
            ('insertObject_internal', 'DynamicObjectHandle'), ('removeObject_internal', 'void'),
            ('updateObject_internal', 'void')))
        source, binary = cls.root / (name+'.cpp'), cls.root / name
        source.write_text(PRELUDE+cls.pool+HARNESS+methods+TEST)
        subprocess.run(['c++','-std=c++14','-O1','-g','-Wall','-Wextra','-Werror',
                        '-fsanitize=address,undefined','-fno-omit-frame-pointer',str(source),'-o',str(binary)],check=True)
        return binary

    def test_original_source_exposes_handle_leak_and_remove_reentry(self):
        for mode in ('baseline','reentrant'):
            result = subprocess.run([str(self.baseline),mode],capture_output=True)
            self.assertNotEqual(result.returncode,0)
            self.assertIn(b'assert',result.stderr.lower())

    def test_patched_production_methods_and_real_handle_pool(self):
        for mode in ('all','reentrant'):
            subprocess.run([str(self.binary),mode],check=True)

    def test_patch_idempotence_and_failed_transaction(self):
        path = self.root / 'lib/space/dynamic_scene_provider.cpp'
        PATCH.patch(self.root)
        self.assertEqual(path.read_text(),self.patched)
        corrupt = self.original.replace('objectHandles_.pop_back();','unrecognisedRetirement();')
        path.write_text(corrupt)
        with self.assertRaises(ValueError):
            PATCH.patch(self.root)
        self.assertEqual(path.read_text(),corrupt)
        path.write_text(self.patched)


if __name__ == '__main__':
    unittest.main()
