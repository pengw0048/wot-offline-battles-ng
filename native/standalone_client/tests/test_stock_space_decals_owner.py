"""Production WGSD2 wrapper ownership and pristine deferred patch composition.

Scene notification delivery is a synchronous boundary recorder. GPU validation
belongs to the separate real D3D9Ex texture probe and Windows client.
"""
from pathlib import Path
import importlib.util
import itertools
import os
import re
import subprocess
import tarfile
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


def method(source, signature):
    start = source.index(signature)
    end = source.index('{', start)
    depth = 1
    while depth:
        end += 1
        depth += (source[end] == '{') - (source[end] == '}')
    return source[start:end + 1]


class SpaceDecalOwnerTests(unittest.TestCase):
    def test_actual_wrapper_add_remove_observer_reentry_and_frame_lease(self):
        source = (ROOT / 'bigworld/stock_space_decals.cpp').read_text()
        production = '\n'.join(method(source, signature) for signature in (
            'void removeDecalMembership(', 'bool StockSpaceDecalsLoader::doBind()',
            'void StockSpaceDecalsLoader::doUnload()', 'float StockSpaceDecalsLoader::percentLoaded()'))
        production = production.replace('bool StockSpaceDecalsLoader::doBind()',
                                        'namespace CompiledSpace {\nbool StockSpaceDecalsLoader::doBind()') + '\n}'
        host = r'''
#include <algorithm>
#include <atomic>
#include <cassert>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <vector>
using uint32=unsigned;
using DynamicObjectHandle=unsigned;
struct Matrix{};
struct BoundingBox{static BoundingBox s_insideOut_;};BoundingBox BoundingBox::s_insideOut_;
struct SceneObject{};
struct DecalGroup{Matrix world;BoundingBox visibility;bool available=true;};
struct DecalDrawHandler{};struct DecalSpatialHandler{};
struct Operation{
bool registered=false;unsigned registrations=0;
template<class T>void* getHandler(){return registered?this:nullptr;}
template<class T,class H>void addHandler(){assert(!registered);registered=true;++registrations;}
};
struct DrawOperation:Operation{};struct SpatialQueryOperation:Operation{};
struct Scene{
DrawOperation draw;SpatialQueryOperation spatial;
template<class T>T* getObjectOperation();
};
template<>DrawOperation* Scene::getObjectOperation<DrawOperation>(){return &draw;}
template<>SpatialQueryOperation* Scene::getObjectOperation<SpatialQueryOperation>(){return &spatial;}
struct DynamicScene{
std::set<unsigned> live;unsigned next=0,removes=0;
std::function<void()> added,removed;
unsigned addObject(const SceneObject&,const Matrix&,const BoundingBox&,const BoundingBox&){
const unsigned handle=++next;assert(live.insert(handle).second);auto event=std::move(added);added=nullptr;if(event)event();return handle;}
void removeObject(unsigned handle){assert(live.erase(handle)==1);++removes;auto event=std::move(removed);removed=nullptr;if(event)event();}
};
namespace CompiledSpace{
struct CompiledSpace{
Scene scene_;DynamicScene dynamic;Scene& scene(){return scene_;}DynamicScene& dynamicScene(){return dynamic;}
};
struct StockSpaceDecalState{
std::vector<DecalGroup> groups;std::vector<SceneObject> objects;std::vector<unsigned> membership;
CompiledSpace* space=nullptr;bool active=false,bound=false;
};
struct StockSpaceDecalsLoader{
std::shared_ptr<StockSpaceDecalState> state_;uint32 unloadDepth_=0;std::atomic<bool> loaded_{false};
bool doBind();void doUnload();float percentLoaded()const;
~StockSpaceDecalsLoader(){doUnload();}
};
}
@PRODUCTION@
int main(){
using namespace CompiledSpace;
::CompiledSpace::CompiledSpace space;StockSpaceDecalsLoader loader;
auto make=[&](unsigned count){auto state=std::make_shared<StockSpaceDecalState>();state->space=&space;
state->groups.resize(count);state->objects.resize(count);return state;};
auto publish=[&](unsigned count){assert(!loader.state_&&!loader.unloadDepth_);loader.state_=make(count);loader.loaded_.store(true,std::memory_order_release);};
assert(!loader.doBind()&&loader.percentLoaded()==0.f);
publish(4);loader.state_->groups[1].available=false;
assert(loader.doBind()&&space.dynamic.live.size()==3&&loader.percentLoaded()==1.f);
assert(loader.doBind()&&space.dynamic.live.size()==3);
assert(space.scene_.draw.registrations==1&&space.scene_.spatial.registrations==1);
std::weak_ptr<StockSpaceDecalState> retired=loader.state_;
space.dynamic.removed=[&]{assert(!loader.state_&&loader.unloadDepth_==1&&loader.percentLoaded()==0.f);
assert(!retired.expired()&&!retired.lock()->active);loader.doUnload();assert(!loader.doBind());};
loader.doUnload();assert(space.dynamic.live.empty()&&retired.expired());
const auto removes=space.dynamic.removes;loader.doUnload();assert(space.dynamic.removes==removes);
publish(1);retired=loader.state_;
space.dynamic.added=[&]{loader.doUnload();assert(!retired.expired());assert(space.dynamic.live.size()==1);};
assert(!loader.doBind()&&space.dynamic.live.empty()&&retired.expired());
publish(2);retired=loader.state_;
space.dynamic.added=[&]{loader.doUnload();publish(1);assert(loader.doBind());};
assert(!loader.doBind()&&space.dynamic.live.size()==1&&loader.state_->active&&retired.expired());
auto frameLease=loader.state_;loader.doUnload();assert(!frameLease->active&&space.dynamic.live.empty());
retired=frameLease;frameLease.reset();assert(retired.expired());
assert(space.scene_.draw.registrations==1&&space.scene_.spatial.registrations==1);
}
'''.replace('@PRODUCTION@', production)
        with tempfile.TemporaryDirectory(prefix='actual-wgsd-owner-') as directory:
            directory = Path(directory)
            (directory / 'probe.cpp').write_text(host)
            subprocess.run(['c++', '-std=c++14', '-Wall', '-Wextra', '-Werror', '-UNDEBUG',
                            str(directory / 'probe.cpp'), '-o', str(directory / 'probe')], check=True)
            subprocess.run([str(directory / 'probe')], check=True)

    def test_pristine_patch_orders_idempotence_and_failed_transaction(self):
        names = ('stock_space_decals', 'stock_deferred_buffers', 'stock_decals', 'stock_vehicle_traces')
        modules, paths = {}, set()
        for name in names:
            path = ROOT / 'bigworld' / ('patch_' + name + '.py')
            spec = importlib.util.spec_from_file_location('decal_' + name, path)
            module = importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
            modules[name] = module
            paths.update(re.findall(r"['\"]((?:lib|client)/[^'\"]+\.(?:cpp|hpp|ipp))['\"]", path.read_text()))
        paths.update(('lib/moo/renderer.hpp', 'lib/moo/renderer.cpp', 'lib/moo/sticker_decal.cpp'))
        archive = Path(os.environ.get('WOT_PUBLIC_ENGINE_ARCHIVE', '/tmp/wot-bigworld-engine-14.4.1.tar.gz'))
        if not archive.exists(): self.skipTest('Pristine public-engine archive is required')
        contents = {}
        with tarfile.open(archive) as source:
            members = {member.name: member for member in source.getmembers()}
            prefix = next(iter(members)).split('/')[0] + '/programming/bigworld/'
            for path in paths:
                if prefix + path in members: contents[path] = source.extractfile(members[prefix + path]).read()
        for order in itertools.permutations(names):
            with self.subTest(order=order), tempfile.TemporaryDirectory(prefix='actual-wgsd-patch-') as directory:
                root = Path(directory)
                for path, data in contents.items():
                    target = root / path; target.parent.mkdir(parents=True, exist_ok=True); target.write_bytes(data)
                def apply(name):
                    module = modules[name]
                    (getattr(module, 'apply', None) or getattr(module, 'patch', None) or module.patch_engine)(root)
                for name in order: apply(name)
                snapshot = {str(path.relative_to(root)): path.read_bytes() for path in root.rglob('*') if path.is_file()}
                for name in order: apply(name)
                self.assertEqual(snapshot, {str(path.relative_to(root)): path.read_bytes() for path in root.rglob('*') if path.is_file()})
                pipeline = (root / 'lib/moo/deferred_pipeline.cpp').read_text()
                self.assertLess(pipeline.index('m_stockDeferredBuffers->drawSpaceDecals()'), pipeline.index('m_decalsManager->draw();'))
                self.assertLess(pipeline.index('m_decalsManager->draw();'), pipeline.index('drawStockVehicleTraces()'))
                self.assertLess(pipeline.index('m_lightsManager->draw();'), pipeline.index('drawStockSpaceDecalsPhase(3)'))
                shadows = (root / 'lib/moo/shadow_manager.cpp').read_text()
                self.assertEqual(shadows.count('drawStockSpaceDecalsPhase(2)'), 1)
                self.assertLess(shadows.index('drawStockSpaceDecalsPhase(2)'), shadows.index('m_screenSpaceShadowMap->pop();'))
                manager = (root / 'lib/moo/deferred_decals_manager.cpp').read_text()
                self.assertEqual(manager.count('initialiseStockSpaceDecals()'), 1)
                self.assertEqual(manager.count('finaliseStockSpaceDecals()'), 1)
                (root / 'lib/moo/shadow_manager.cpp').write_text(shadows.replace('drawStockSpaceDecalsPhase(2)', 'unrecognizedPhase(2)')
                    .replace('                m_screenSpaceShadowMap->pop();', '                unknownTarget->pop();'))
                before = {str(path.relative_to(root)): path.read_bytes() for path in root.rglob('*') if path.is_file()}
                with self.assertRaises(ValueError): modules['stock_space_decals'].patch(root)
                self.assertEqual(before, {str(path.relative_to(root)): path.read_bytes() for path in root.rglob('*') if path.is_file()})


if __name__ == '__main__':
    unittest.main()
