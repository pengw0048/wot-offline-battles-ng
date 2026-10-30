"""Check only timing scopes are added around the installed scene-owner calls."""
import importlib.util
import os
from pathlib import Path
import shutil
import subprocess
import tarfile
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    'scene_profiler', ROOT / 'bigworld/patch_stock_scene_profiler.py')
PATCH = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PATCH)


class StockSceneProfilerTests(unittest.TestCase):
    def setUp(self):
        archive = Path(os.environ.get(
            'WOT_PUBLIC_ENGINE_ARCHIVE', '/tmp/wot-bigworld-engine-14.4.1.tar.gz'))
        if not archive.is_file():
            self.skipTest('Pinned public engine archive is required')
        with tarfile.open(archive) as original:
            member = next(name for name in original.getnames()
                          if name.endswith('/programming/bigworld/' + PATCH.PATH))
            self.source = original.extractfile(member).read().decode('utf-8-sig')
            member = next(name for name in original.getnames()
                          if name.endswith('/programming/bigworld/' + PATCH.DYNAMIC_PATH))
            self.dynamic = original.extractfile(member).read().decode('utf-8-sig')
        spec = importlib.util.spec_from_file_location(
            'dynamic_lifecycle', ROOT / 'bigworld/patch_stock_dynamic_scene_ownership.py')
        lifecycle = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(lifecycle)
        self.dynamic = lifecycle.replace_once(self.dynamic, lifecycle.REMOVE_BEFORE, lifecycle.REMOVE_AFTER)
        self.dynamic = lifecycle.replace_once(self.dynamic, lifecycle.TAIL_BEFORE, lifecycle.TAIL_AFTER)
        # Current producer order: vehicle visuals, TPS, ground effects,
        # battleground elements, vehicle traces, flock manager. Keep all SDK
        # calls and comments; install only those reviewed WorldApp additions.
        global_tick = '        GlobalEmbodiments::tick( dGameTime );\n'
        installed = global_tick + ''.join('        ' + call + '\n'
                                         for scope, call in PATCH.STAGES[4:9])
        self.assertEqual(self.source.count(global_tick), 1)
        self.source = self.source.replace(global_tick, installed)
        end = '    }\n}\n\n\nvoid WorldApp::inactiveTick('
        additions = ''.join('        ' + call + '\n' for scope, call in PATCH.STAGES[9:])
        self.assertEqual(self.source.count(end), 1)
        self.source = self.source.replace(end, additions + end)

    def install_dynamic(self, directory):
        path = Path(directory) / PATCH.DYNAMIC_PATH
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(self.dynamic)
        return path

    def test_scopes_are_disjoint_reversible_and_idempotent(self):
        final = PATCH.patch_source(self.source)
        self.assertEqual(PATCH.patch_source(final), final)
        restored = final
        for before, after in PATCH.SEAMS:
            self.assertEqual(final.count(after), 1)
            restored = restored.replace(after, before, 1)
        self.assertEqual(restored, self.source)
        original_positions = [self.source.index(call) for scope, call in PATCH.STAGES]
        final_positions = [final.index(call) for scope, call in PATCH.STAGES]
        self.assertEqual(sorted(original_positions), original_positions)
        self.assertEqual(sorted(final_positions), final_positions)
        for scope, call in PATCH.STAGES:
            self.assertEqual(final.count(call), self.source.count(call))
            self.assertEqual(final.count(scope), 1)

    def test_changed_missing_partial_or_duplicate_source_is_not_written(self):
        final = PATCH.patch_source(self.source)
        with tempfile.TemporaryDirectory(prefix='wot-scene-profiler-') as directory:
            self.install_dynamic(directory)
            path = Path(directory) / PATCH.PATH
            path.parent.mkdir(parents=True)
            damaged = []
            for before, after in PATCH.SEAMS:
                damaged.extend((self.source.replace(before, '', 1),
                                self.source.replace(before, before + before, 1),
                                final.replace(after, before, 1), final + after))
            for source in damaged:
                path.write_text(source)
                before = path.read_bytes()
                with self.assertRaises(ValueError):
                    PATCH.patch(directory)
                self.assertEqual(path.read_bytes(), before)

    def test_missing_lifecycle_install_is_rejected(self):
        # A profiler must not silently omit an owner because it ran too early.
        for scope, call in PATCH.STAGES[4:]:
            with self.assertRaises(ValueError):
                PATCH.patch_source(self.source.replace('        ' + call + '\n', '', 1))

    def test_patch_preserves_bom_crlf_and_cached_source(self):
        with tempfile.TemporaryDirectory(prefix='wot-scene-profiler-') as directory:
            self.install_dynamic(directory)
            path = Path(directory) / PATCH.PATH
            path.parent.mkdir(parents=True)
            path.write_bytes(b'\xef\xbb\xbf' + self.source.replace('\n', '\r\n').encode('utf-8'))
            PATCH.patch(directory)
            first = path.read_bytes()
            self.assertTrue(first.startswith(b'\xef\xbb\xbf'))
            self.assertNotIn(b'\n', first.replace(b'\r\n', b''))
            PATCH.patch(directory)
            self.assertEqual(path.read_bytes(), first)

    def test_dynamic_scopes_are_reversible_and_preserve_removal_fencing(self):
        final = PATCH.patch_dynamic_source(self.dynamic)
        self.assertEqual(PATCH.patch_dynamic_source(final), final)
        restored = final
        for before, after in PATCH.DYNAMIC_SEAMS:
            self.assertEqual(final.count(after), 1)
            restored = restored.replace(after, before, 1)
        self.assertEqual(restored, self.dynamic)
        self.assertEqual(final.count('PROFILER_SCOPED(') - self.dynamic.count('PROFILER_SCOPED('), 2)
        self.assertEqual(final.count('PROFILER_COUNTER(') - self.dynamic.count('PROFILER_COUNTER('), 7)

    def test_invalid_dynamic_source_does_not_partially_write_world(self):
        with tempfile.TemporaryDirectory(prefix='wot-dynamic-profiler-') as directory:
            world = Path(directory) / PATCH.PATH
            world.parent.mkdir(parents=True)
            dynamic = self.install_dynamic(directory)
            final = PATCH.patch_dynamic_source(self.dynamic)
            for before, after in PATCH.DYNAMIC_SEAMS:
                for damaged in (self.dynamic.replace(before, '', 1), self.dynamic + before,
                                final.replace(after, before, 1), final + after):
                    world.write_text(self.source)
                    dynamic.write_text(damaged)
                    saved = world.read_bytes(), dynamic.read_bytes()
                    with self.assertRaises(ValueError):
                        PATCH.patch(directory)
                    self.assertEqual(saved, (world.read_bytes(), dynamic.read_bytes()))

    def test_actual_provider_methods_keep_results_and_report_bounded_work(self):
        compiler = shutil.which('c++')
        if not compiler:
            self.skipTest('C++ compiler is required')
        def function(source, signature):
            start = source.index(signature)
            end = source.index('{', start)
            depth = 1
            while depth:
                end += 1
                depth += (source[end] == '{') - (source[end] == '}')
            return source[start:end + 1]
        methods = '\n'.join(function(self.dynamic, name) for name in (
            'void DynamicSceneProvider::tick(', 'void DynamicSceneProvider::onPostTick()'))
        patched = '\n'.join(function(PATCH.patch_dynamic_source(self.dynamic), name) for name in (
            'void DynamicSceneProvider::tick(', 'void DynamicSceneProvider::onPostTick()'))
        original = methods.replace('DynamicSceneProvider::tick(', 'DynamicSceneProvider::originalTick(').replace(
            'DynamicSceneProvider::onPostTick()', 'DynamicSceneProvider::originalPostTick()')
        harness = r'''
#include <array>
#include <cassert>
#include <cstdint>
#include <map>
#include <string>
#include <vector>
using int32=std::int32_t;
std::map<std::string,int> scopes,counterCalls;
std::map<std::string,int32> counters;
struct Scope{explicit Scope(const char*s){++scopes[s];}};
#define PROFILER_SCOPED(n) Scope scope_##n(#n)
#define PROFILER_COUNTER(n,v) (++counterCalls[#n],counters[#n]=(v))
#define MF_ASSERT assert
#define ENABLE_SPACE_DEBUG_DRAW 0
namespace BW {template<class T>using vector=std::vector<T>;}
struct SceneObject{int value=0;};
struct Handle{int index_;};
struct TickOperation{int calls=0,objects=0;void tick(SceneObject*,unsigned n,float){++calls;objects+=n;}};
struct Scene{TickOperation operation;template<class T>T*getObjectOperation(){return &operation;}};
struct Tree{int resets=0,updates=0,inserts=0;void reinitialise(){++resets;}void updateHeirarchy(){++updates;}};
struct Changes{int calls=0,objects=0;void notifyObjectsChanged(void*,SceneObject*,unsigned n){++calls;objects+=n;}};
class DynamicSceneProvider{
public:
 struct DynamicObjectStorageInfo{int collisionNode_=0,visibilityNode_=0;};
 using DynamicObjectHandle=Handle;using ObjectHandleCollection=std::vector<Handle>;
 struct Detail{
  static void clearTreeContents(std::vector<int>&v){v.clear();}
  static void insertObjectNode(Handle,std::vector<int>&,Tree&t,std::vector<int>&,int*p,bool){++t.inserts;*p=9;}
 };
 Scene owner;Changes changes;Changes*pObjectChangeView_=&changes;
 std::vector<SceneObject>objects_;ObjectHandleCollection objectHandles_;
 std::vector<int>objectLookup_,objectCollisionBBs_,objectVisibilityBBs_,objectWorldXform_;
 std::vector<int>latestObjectCollisionBBs_,latestObjectVisibilityBBs_,latestObjectWorldXform_;
 std::vector<bool>objectDirtyFlags_;std::vector<DynamicObjectStorageInfo>objectStorageInfo_;
 Tree collisionTree_,visibilityTree_;std::vector<int>collisionTreeContents_,visibilityTreeContents_;
 unsigned numDirtyObjects_=0;Scene*scene(){return &owner;}
 void tick(float);void onPostTick();void originalTick(float);void originalPostTick();
 void setup(unsigned n,unsigned dirty){
  objects_.resize(n);objectDirtyFlags_.resize(n);objectStorageInfo_.resize(n);
  for(unsigned i=0;i<n;++i){objectHandles_.push_back({int(i)});objectLookup_.push_back(i);objectDirtyFlags_[i]=i<dirty;}
  for(auto*p:{&objectCollisionBBs_,&objectVisibilityBBs_,&objectWorldXform_})p->resize(n,1);
  for(auto*p:{&latestObjectCollisionBBs_,&latestObjectVisibilityBBs_,&latestObjectWorldXform_})p->resize(n,2);
  numDirtyObjects_=dirty;
 }
};
@ORIGINAL@
@PATCHED@
int main(){
 for(unsigned n:{0u,1u,30u})for(unsigned dirty:{0u,1u,30u}){
  if(dirty>n)continue;
  DynamicSceneProvider a,b;a.setup(n,dirty);b.setup(n,dirty);
  a.originalTick(.03f);a.originalPostTick();scopes.clear();counterCalls.clear();counters.clear();b.tick(.03f);b.onPostTick();
  assert(a.owner.operation.calls==b.owner.operation.calls&&a.owner.operation.objects==b.owner.operation.objects);
  assert(a.objectCollisionBBs_==b.objectCollisionBBs_&&a.objectVisibilityBBs_==b.objectVisibilityBBs_&&a.objectWorldXform_==b.objectWorldXform_);
  assert(a.objectDirtyFlags_==b.objectDirtyFlags_&&a.numDirtyObjects_==b.numDirtyObjects_);
  assert(a.collisionTree_.resets==b.collisionTree_.resets&&a.collisionTree_.inserts==b.collisionTree_.inserts&&a.visibilityTree_.inserts==b.visibilityTree_.inserts);
  assert(a.changes.calls==b.changes.calls&&a.changes.objects==b.changes.objects);
  assert(scopes["DynamicScene_Tick"]==1&&scopes["DynamicScene_PostTick"]==1);
  assert(counters["DynamicScene_TickObjects"]==int(n)&&counters["DynamicScene_PostObjects"]==int(n)&&counters["DynamicScene_DirtyObjects"]==int(dirty));
  assert(counters["DynamicScene_Rebuilds"]==(dirty?1:0));
  assert(counters["DynamicScene_ReinsertAttempts"]==(dirty?int(2*n):0));
  assert(counters["DynamicScene_NotifyCalls"]==(dirty?1:0)&&counters["DynamicScene_NotifiedObjects"]==int(dirty));
  for(auto&entry:counterCalls)assert(entry.second==1);
 }
}
'''.replace('@ORIGINAL@', original).replace('@PATCHED@', patched)
        with tempfile.TemporaryDirectory(prefix='wot-dynamic-methods-') as directory:
            source = Path(directory) / 'host.cpp'
            binary = Path(directory) / 'host'
            source.write_text(harness)
            subprocess.run([compiler, '-std=c++14', '-Wall', '-Wextra', '-Werror',
                            str(source), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)


if __name__ == '__main__':
    unittest.main()
