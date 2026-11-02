"""Exercise real SDK collection guards with the production tPS frame owners."""
import importlib.util
import os
from pathlib import Path
import re
import shutil
import subprocess
import tarfile
import tempfile
import unittest

from native_test_compile_flags import native_test_compile_flags
from test_stock_tps_owners import extract_method

ROOT = Path(__file__).resolve().parents[1]


def patcher(name):
    spec = importlib.util.spec_from_file_location(name, ROOT / 'bigworld' / (name + '.py'))
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


HOST = r'''
#include <atomic>
#include <cassert>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>
#define MF_ASSERT(value) assert(value)
#define BW_GUARD_PROFILER(...)
#define PROFILER_SCOPED_DYNAMIC_STRING(...)
#define PROFILER_SCOPED(...)
using uint32=unsigned;
std::vector<unsigned> events;
unsigned destroyed=0;
namespace Moo {
class DrawContext {
public:
    enum {OPAQUE_CHANNEL_MASK=1,TRANSPARENT_CHANNEL_MASK=2,SHIMMER_CHANNEL_MASK=4,ALL_CHANNELS_MASK=7};
    using ChannelMask=unsigned;
    struct UserDrawItem {virtual ~UserDrawItem()=default;virtual void draw()=0;virtual void fini(){};};
    struct WrapperOp {enum {USER_DRAW_ITEM};int type_=0;void* payload_=nullptr;};
    struct DistanceSortedOp:WrapperOp {float cameraDistance_=0.f;};
    template<class T>struct Ops:std::vector<T> {T& push_back(){this->emplace_back();return this->back();}};
    unsigned collectChannelMask_=0;bool autoResetStatistics_=false;
    Ops<WrapperOp> opaqueOps_,shimmerOps_;Ops<DistanceSortedOp> transparentOps_;
    struct Allocator {void releaseAll(){}} instanceDataAllocator_;
    struct Recorder {void reset(){}} recorder;
    Recorder* globalStateRecorder_=&recorder;
    void resetStatistics(){}
    void begin(uint32);void end(uint32);void flush(uint32,bool=true);
    void drawUserItem(UserDrawItem*,ChannelMask,float=0.f);
    void flushOpaqueChannel(){}
    void flushTransparentChannel(bool clear){
        for(auto& op:transparentOps_){auto* p=static_cast<UserDrawItem*>(op.payload_);p->draw();if(clear)p->fini();}
    }
    void flushShimmerChannel(bool){assert(false);}
};
@SDK_METHODS@
}
struct Runtime {
    bool closed=false;unsigned generation=1,submitted=0,visualGeneration=0;
    std::vector<int> chunks{1},visuals{1};
    ~Runtime(){++destroyed;}
    bool draw(unsigned bucket){events.push_back(bucket);return true;}
    bool drawVisuals(){if(visualGeneration!=generation){visualGeneration=generation;events.push_back(9);}return true;}
};
std::shared_ptr<Runtime> runtime;
std::shared_ptr<Runtime> currentRuntime(){return runtime;}
std::atomic<bool> drawEnabled{true};
struct App {
    enum {COLOUR_DRAW_CONTEXT};Moo::DrawContext context;
    static App& instance(){static App value;return value;}
    Moo::DrawContext& drawContext(int){return context;}
};
@DRAW_TYPES@
@SUBMIT@
@FINISH@
void world(){auto& colourDrawContext=App::instance().context;
@WORLD@
}
void facade(){
@FACADE@
}
struct Rejected:Moo::DrawContext::UserDrawItem {
    unsigned& finished;explicit Rejected(unsigned& n):finished(n){}
    void draw()override{assert(false);}void fini()override{++finished;delete this;}
};
int main(){
    auto& context=App::instance().context;
    runtime=std::make_shared<Runtime>();std::weak_ptr<Runtime> weak=runtime;
    // These are the actual World/Facade call sites, not an always-open fake.
    world();assert(context.collectChannelMask_==0);
    assert(context.transparentOps_.size()==2&&events.empty());
    facade();assert((events==std::vector<unsigned>{0,9,1}));
    assert(context.transparentOps_.empty());
    facade();assert(events.size()==3); // A clearing flush consumes each item once.
    runtime.reset();assert(weak.expired()&&destroyed==1);
    // Rejected items must also release both production native owner leases.
    auto owner=std::make_shared<Runtime>();weak=owner;
    context.drawUserItem(new TPSDrawItem(owner),Moo::DrawContext::TRANSPARENT_CHANNEL_MASK);
    context.drawUserItem(new TPSVisualDrawItem(owner),Moo::DrawContext::TRANSPARENT_CHANNEL_MASK);
    owner.reset();assert(weak.expired()&&destroyed==2);
    // Colour collection must continue to reject distortion-only sprites.
    unsigned finished=0;context.begin(3);
    context.drawUserItem(new Rejected(finished),Moo::DrawContext::SHIMMER_CHANNEL_MASK);
    context.end(3);assert(finished==1&&context.shimmerOps_.empty());
    // A queued frame may be retired before its flush: skip it and release it.
    runtime=std::make_shared<Runtime>();weak=runtime;world();runtime->closed=true;runtime.reset();
    facade();assert(weak.expired()&&destroyed==3&&events.size()==3);
}
'''


class StockTPSCollectionTests(unittest.TestCase):
    def test_real_sdk_guards_and_submission_phase(self):
        archive = Path(os.environ.get('WOT_PUBLIC_ENGINE_ARCHIVE', '/tmp/wot-bigworld-engine-14.4.1.tar.gz'))
        if not archive.is_file():
            if 'WOT_PUBLIC_ENGINE_ARCHIVE' in os.environ:
                self.fail('Invalid explicit WOT_PUBLIC_ENGINE_ARCHIVE')
            self.skipTest('Pinned public SDK archive required')
        compiler = os.environ.get('CXX') or shutil.which('c++')
        if not compiler:
            self.skipTest('C++ compiler required')
        names = ('lib/particle/meta_particle_system.cpp', 'lib/particle/meta_particle_system.hpp',
                 'lib/particle/actions/particle_system_action.hpp', 'lib/particle/actions/particle_system_action.cpp',
                 'lib/particle/particle_system.cpp', 'client/world_app.cpp', 'client/facade_app.cpp',
                 'client/script_app.cpp', 'lib/moo/draw_context.cpp',
                 'lib/particle/renderers/sprite_particle_renderer.cpp',
                 'lib/particle/renderers/sprite_particle_renderer.ipp')
        with tempfile.TemporaryDirectory(prefix='tps-collection-') as temp:
            folder = Path(temp)
            with tarfile.open(archive) as tar:
                members = tar.getmembers()
                for name in names:
                    member = next(m for m in members if m.name.endswith('/' + name))
                    path = folder / name
                    path.parent.mkdir(parents=True, exist_ok=True)
                    path.write_bytes(tar.extractfile(member).read())
            tps = patcher('patch_stock_tps')
            tps.patch(folder)
            patcher('patch_stock_sprite_shimmer').patch(folder)
            # Other real patchers insert headers after pch and shutdown owners
            # before Python teardown. Reapplying tPS must preserve that order.
            for name in names:
                path = folder / name
                text = path.read_text()
                for header in ('stock_tps_scene.hpp', 'stock_tps_quality.hpp'):
                    line = '#include "' + header + '"\n'
                    if line in text:
                        text = text.replace(line, '')
                        text = text.replace('#include "pch.hpp"\n',
                                            '#include "pch.hpp"\n#include "other_stock_owner.hpp"\n' + line)
                if name == 'client/script_app.cpp':
                    text = text.replace('\tfiniStockTPSRuntime();\n\tBigWorldClientScript::fini();',
                                        '\tfiniStockTPSRuntime();\n\tfiniStockVehicleStickers();\n'
                                        '\tfiniStockBattlegroundElements();\n\tBigWorldClientScript::fini();')
                path.write_text(text)
            interleaved = {p: (p.read_bytes(), p.stat().st_mtime_ns) for p in folder.rglob('*') if p.is_file()}
            tps.patch(folder)
            self.assertEqual(interleaved, {p: (p.read_bytes(), p.stat().st_mtime_ns) for p in interleaved})
            script = folder / 'client/script_app.cpp'
            valid_script = script.read_text()
            for duplicate in ('#include "stock_tps_scene.hpp"\n', '\tfiniStockTPSRuntime();\n'):
                script.write_text(valid_script + duplicate)
                unchanged = {p: p.read_bytes() for p in interleaved}
                with self.assertRaises(RuntimeError):
                    tps.patch(folder)
                self.assertEqual(unchanged, {p: p.read_bytes() for p in unchanged})
            script.write_text(valid_script)
            # Incremental upgrades remove the old Facade insertion, including
            # when another patch has inserted work before its flush.
            facade_path = folder / 'client/facade_app.cpp'
            text = facade_path.read_text()
            text = text.replace('        App::instance().drawContext( App::COLOUR_DRAW_CONTEXT ).flush(',
                                '        drawStockTPSRuntime();\n        App::instance().drawContext( App::COLOUR_DRAW_CONTEXT ).flush(')
            facade_path.write_text(text)
            tps.patch(folder)
            before = {p: (p.read_bytes(), p.stat().st_mtime_ns) for p in folder.rglob('*') if p.is_file()}
            tps.patch(folder)
            self.assertEqual(before, {p: (p.read_bytes(), p.stat().st_mtime_ns) for p in before})
            world = (folder / 'client/world_app.cpp').read_text()
            facade = facade_path.read_text()
            self.assertEqual(world.count('drawStockTPSRuntime();'), 1)
            self.assertNotIn('drawStockTPSRuntime();', facade)
            # Keep the real frame ordering and actual SDK guard implementations.
            world_calls = re.findall(r'(?:colourDrawContext\.(?:begin|end)\([^;]+\)|drawStockTPSRuntime\(\));', world)
            facade_calls = re.findall(r'App::instance\(\)\.drawContext\( App::COLOUR_DRAW_CONTEXT \)\.flush\( Moo::DrawContext::TRANSPARENT_CHANNEL_MASK \);', facade)
            self.assertEqual(len(world_calls), 3)
            self.assertEqual(len(facade_calls), 1)
            sdk = (folder / 'lib/moo/draw_context.cpp').read_text()
            methods = '\n'.join(extract_method(sdk, signature) for signature in
                                ('void\tDrawContext::begin(', 'void\tDrawContext::end(',
                                 'void\tDrawContext::flush(', 'void\tDrawContext::drawUserItem('))
            scene = (ROOT / 'bigworld/stock_tps_scene.cpp').read_text()
            types = scene[scene.index('struct DeferredTPSDraw'):scene.index('\n}\n\nconst BW::string & stockInfluxActionName')]
            host = HOST.replace('@SDK_METHODS@', methods).replace('@DRAW_TYPES@', types)
            host = host.replace('@SUBMIT@', extract_method(scene, 'bool drawStockTPSRuntime('))
            host = host.replace('@FINISH@', extract_method(scene, 'void finishStockTPSParticleItems('))
            host = host.replace('@WORLD@', '\n'.join(world_calls)).replace('@FACADE@', '\n'.join(facade_calls))
            for name, text in (
                    ('production', host),
                    ('late-submission', host.replace('drawStockTPSRuntime();', '', 1)),
                    ('missing-fini', host.replace('void fini() override { delete this; }', ''))):
                if name == 'late-submission':
                    text = text.replace('void facade(){', 'void facade(){drawStockTPSRuntime();')
                path = folder / (name + '.cpp')
                path.write_text(text)
                binary = folder / name
                subprocess.run([compiler, '-std=c++17', '-Wall', '-Wextra', '-Werror',
                                *native_test_compile_flags(compiler), str(path), '-o', str(binary)], check=True)
                result = subprocess.run([str(binary)], capture_output=True)
                self.assertEqual(result.returncode == 0, name == 'production', (name, result.stderr))


if __name__ == '__main__':
    unittest.main()
