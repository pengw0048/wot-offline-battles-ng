"""Production dynamic-decal retirement and pinned renderer ownership seams."""
from pathlib import Path
import importlib.util
import os
import re
import subprocess
import sys
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


class DynamicDecalOwnerTests(unittest.TestCase):
    def test_actual_tick_swap_retirement_and_camera_space_lifetime(self):
        source = (ROOT / 'bigworld/stock_dynamic_decals.cpp').read_text()
        production = '\n'.join(method(source, name) for name in ('void syncSpace()', 'void tick(float elapsed)'))
        host = r'''
#include "wot/stock_dynamic_decals.hpp"
#include "stock_space_decals_data.hpp"
#include <cassert>
#include <map>
#include <memory>
#include <vector>
struct Vector3 { float x,y,z; };
struct Space { bool dead=false; bool cleared()const{return dead;} };
using ClientSpacePtr=std::shared_ptr<Space>;
ClientSpacePtr current;
namespace DeprecatedSpaceHelpers {ClientSpacePtr cameraSpace(){return current;}}
namespace Moo {struct View {Vector3 applyToOrigin(){return {1,2,3};}};
struct Context {View invView(){return {};} float lodZoomFactor(){return 2.f;}};
Context& rc(){static Context c;return c;}}
struct DynamicTextureSet {};
struct DynamicDecal {wot::StockDynamicDecalInstance data;std::shared_ptr<DynamicTextureSet> textures;};
struct Owner {
ClientSpacePtr space;float time=0;
std::vector<DynamicDecal> decals;
std::map<unsigned,std::weak_ptr<DynamicTextureSet>> textures;
@PRODUCTION@
};
int main(){
Owner owner;current=std::make_shared<Space>();owner.syncSpace();assert(owner.space==current);
auto texture=std::make_shared<DynamicTextureSet>();std::weak_ptr<DynamicTextureSet> resource=texture;
for(unsigned i=0;i<3;++i){DynamicDecal d;d.data.priority=i;d.data.lifetime=i+1;d.textures=texture;owner.decals.push_back(d);}
owner.textures[1]=texture;texture.reset();
owner.tick(1.f);assert(owner.time==1&&owner.decals.size()==2&&owner.decals[0].data.priority==2);
assert(owner.decals[0].data.distanceSquared==56.f&&owner.decals[1].data.distanceSquared==56.f);
owner.tick(1.f);assert(owner.decals.size()==1&&owner.decals[0].data.priority==2&&!resource.expired());
owner.tick(1.f);assert(owner.decals.empty()&&resource.expired()&&owner.textures.empty());
texture=std::make_shared<DynamicTextureSet>();resource=texture;
DynamicDecal d;d.data.lifetime=100;d.textures=texture;owner.decals.push_back(std::move(d));
owner.textures[2]=texture;texture.reset();
std::weak_ptr<Space> previous=current;current=std::make_shared<Space>();owner.tick(0.f);
assert(owner.space==current&&previous.expired()&&owner.decals.empty()&&resource.expired());
current->dead=true;owner.tick(0.f);assert(!owner.space&&owner.time==3.f);
current.reset();owner.tick(0.f);assert(!owner.space&&owner.decals.empty());
}
'''.replace('@PRODUCTION@', production)
        with tempfile.TemporaryDirectory(prefix='actual-dynamic-decal-owner-') as temporary:
            directory = Path(temporary)
            (directory / 'test.cpp').write_text(host)
            command = ['c++', '-std=c++14', '-Wall', '-Wextra', '-Werror', '-UNDEBUG']
            if sys.platform != 'win32':
                command += ['-fsanitize=address,undefined']
            subprocess.run(command + ['-I'+str(ROOT / 'include'), '-I'+str(ROOT / 'bigworld'),
                                      str(directory / 'test.cpp'), '-o', str(directory / 'test')], check=True)
            subprocess.run([str(directory / 'test')], check=True)

    def test_original_owner_patch_order_idempotence_and_atomic_failure(self):
        names = ('stock_space_decals', 'stock_dynamic_decals')
        modules, paths = {}, set()
        for name in names:
            path = ROOT / 'bigworld' / ('patch_' + name + '.py')
            spec = importlib.util.spec_from_file_location(name, path)
            module = importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
            modules[name] = module
            paths.update(re.findall(r"['\"]((?:lib|client)/[^'\"]+\.(?:cpp|hpp))['\"]", path.read_text()))
        archive = Path(os.environ.get('WOT_PUBLIC_ENGINE_ARCHIVE', '/tmp/wot-bigworld-engine-14.4.1.tar.gz'))
        if not archive.exists(): self.skipTest('The pristine public-engine archive is required')
        with tempfile.TemporaryDirectory(prefix='dynamic-decal-patch-') as temporary, tarfile.open(archive) as source:
            root = Path(temporary)
            members = {entry.name: entry for entry in source.getmembers()}
            prefix = next(iter(members)).split('/')[0] + '/programming/bigworld/'
            for path in paths:
                if prefix + path not in members: continue
                target = root / path; target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(source.extractfile(members[prefix + path]).read())
            def snapshot(): return {str(p.relative_to(root)):p.read_bytes() for p in root.rglob('*') if p.is_file()}
            before = snapshot()
            with self.assertRaises(ValueError): modules['stock_dynamic_decals'].patch(root)
            self.assertEqual(before, snapshot())
            modules['stock_space_decals'].patch(root)
            modules['stock_dynamic_decals'].patch(root)
            before = snapshot(); modules['stock_dynamic_decals'].patch(root); self.assertEqual(before, snapshot())
            manager = (root / 'lib/moo/deferred_decals_manager.cpp').read_text()
            self.assertLess(manager.index('finaliseStockDynamicDecals()'), manager.index('finaliseStockSpaceDecals()'))
            self.assertIn('initialiseStockSpaceDecals() && initialiseStockDynamicDecals()', manager)
            pipeline = root / 'lib/moo/deferred_pipeline.cpp'
            pipeline.write_text(pipeline.read_text().replace('    m_decalsManager->tick(dt);', '    unknownOwner->tick(dt);')
                                .replace('    tickStockDynamicDecals(dt);', ''))
            before = snapshot()
            with self.assertRaises(ValueError): modules['stock_dynamic_decals'].patch(root)
            self.assertEqual(before, snapshot())


if __name__ == '__main__': unittest.main()
