"""Run the patched SDK frame blocks with the production timing scope and bounded owners."""
import importlib.util
import os
from pathlib import Path
import shutil
import subprocess
import tarfile
import tempfile
import unittest
from unittest import mock


ROOT = Path(__file__).resolve().parents[1]


def module(name):
    spec = importlib.util.spec_from_file_location(name, ROOT / 'bigworld' / (name + '.py'))
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


PATCH = module('patch_stock_render_thread')
WORKER = module('patch_stock_worker_runtime')


def block_at(source, start):
    depth = 0
    for end in range(start, len(source)):
        depth += {'{': 1, '}': -1}.get(source[end], 0)
        if depth == 0:
            return source[start:end + 1]
    raise AssertionError('Unterminated SDK block')


def profile_block(source, name):
    return block_at(source, source.rindex('{', 0, source.index('PROFILER_SCOPED( ' + name)))


def engine(test):
    archive = Path(os.environ.get('WOT_PUBLIC_ENGINE_ARCHIVE', '/tmp/wot-bigworld-engine-14.4.1.tar.gz'))
    if not archive.is_file():
        if 'WOT_PUBLIC_ENGINE_ARCHIVE' in os.environ:
            test.fail('Configured pinned SDK archive is missing')
        test.skipTest('Pinned public engine archive required')
    wanted = set(PATCH.EDITS)
    result = {}
    with tarfile.open(archive) as original:
        for member in original:
            for name in wanted:
                if member.name.endswith('/programming/bigworld/' + name):
                    result[name] = original.extractfile(member).read().decode('utf-8-sig')
    test.assertEqual(set(result), wanted)
    # The render owner already follows display/screenshot setup in the build.
    old, new = PATCH.EDITS['lib/moo/render_context.cpp'][0]
    result['lib/moo/render_context.cpp'] = result['lib/moo/render_context.cpp'].replace(
        '#include "render_context.hpp"\n', old, 1)
    return result


def changed(source):
    return {name: apply_edits(text, PATCH.EDITS[name], name) for name, text in source.items()}


def apply_edits(text, edits, name):
    for old, new in edits:
        text = PATCH.replace_once(text, old, new, name)
    return text


def worker_app(source):
    # Exercise the real worker state validator and patch for this shared owner.
    edits = tuple(row for row in WORKER.EDITS if row[0] == 'client/app.cpp')
    shared = (ROOT/'bigworld/stock_worker_runtime.hpp').read_bytes()
    header = shared if WORKER.INCLUDE in source else None
    with mock.patch.object(WORKER, 'EDITS', edits), mock.patch.object(
            WORKER, 'ROLE_COUNTS', {'client/app.cpp': WORKER.ROLE_COUNTS['client/app.cpp']}):
        return WORKER.patch_sources({'client/app.cpp': source}, header, shared)['client/app.cpp']


HOST = r'''
#include <cassert>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include "stock_d3d9_render_thread.hpp"
#define GPU_PROFILER_SCOPE(x)
#define PROFILER_SCOPED(x)
#define MF_FREEZE_DATA_WATCHER(...)
#define D3DRS_FILLMODE 1
#define D3DFILL_WIREFRAME 2
#define D3DFILL_SOLID 3
#define UMBRA_ENABLE 0
static std::vector<std::uint64_t> stack;
static std::vector<std::string> trace;
static unsigned counts[5], serial;
static int fail=-1;
static bool validDevice=true, worker=false;
static void work(int stage) {
    trace.push_back("work"+std::to_string(stage));
    if(fail==stage)throw std::runtime_error("bounded owner failure");
}
namespace wot { namespace d3d9rt {
std::uint64_t begin_frame_stage(){stack.push_back(++serial);return serial;}
void end_frame_stage(unsigned stage,std::uint64_t begin){
    assert(!stack.empty() && stack.back()==begin && stage<5);
    stack.pop_back();++counts[stage];trace.push_back("end"+std::to_string(stage));
}
} bool stockSimulationWorker(){return worker;} }
struct DogWatch {void start(){}void stop(){}explicit DogWatch(const char* = "") {}};
struct ScopedDogWatch {explicit ScopedDogWatch(DogWatch&) {}};
static DogWatch g_watchTick,g_watchUpdate,g_watchOutput,sceneWatch;
struct Matrix{};
namespace Moo {
struct DrawContext {
    enum {ALL_CHANNELS_MASK,OPAQUE_CHANNEL_MASK};
    void end(int){}void flush(int){trace.push_back("opaque_flush");}
};
struct LightContainer{};
using LightContainerPtr=std::shared_ptr<LightContainer>;
enum {RENDERING_PASS_COLOR};
struct Context {
    bool checkDevice(){return validDevice;}void setRenderState(int,int){}
    Matrix viewProjection(){return {};}
    void preloadDeviceResources(int){trace.push_back("preload");}
};
static Context context;
Context&rc(){return context;}
}
struct Scene{};
struct Space {Scene data;Scene&scene(){return data;}};
static Space space;
static Space* pSpace=&space;
using ChunkSpacePtr=void*;
struct ClientChunkSpaceAdapter {static ChunkSpacePtr getChunkSpace(Space*){return nullptr;}};
struct SceneDrawContext {SceneDrawContext(Scene&,Moo::DrawContext&) {}};
struct IntersectionSet{};
struct FrustumHull {explicit FrustumHull(Matrix){}int hull(){return 0;}};
struct SceneIntersectContext {explicit SceneIntersectContext(Scene&) {}};
struct IRendererPipeline {enum {TYPE_DEFERRED_SHADING};};
struct Renderer {static Renderer&instance(){static Renderer r;return r;}int pipelineType(){return 1;}};
struct LightsManager {static LightsManager&instance(){static LightsManager m;return m;}
    void gatherVisibleLights(Moo::LightContainerPtr) {}};
namespace DrawHelpers {
void addIntersectingSceneLightsToContainer(Scene&,int,Moo::LightContainer&){}
void cull(SceneIntersectContext&,Scene&,Matrix&,IntersectionSet&){}
void drawSceneVisibilitySet(SceneDrawContext&,Scene&,IntersectionSet&){work(3);}
}
struct ChunkManager {static ChunkManager&instance(){static ChunkManager m;return m;}
    void draw(Moo::DrawContext&) {}};
namespace Terrain {
struct BaseTerrainRenderer {static BaseTerrainRenderer*instance(){static BaseTerrainRenderer r;return &r;}
    bool canSeeTerrain(){return true;}void drawAll(int){work(4);}};
}
static void drawWorld(){
    Moo::DrawContext colourDrawContext;
    int wireFrameStatus_=0;bool canSeeTerrain_=false;
    @MAIN_SCENE@
    @TERRAIN@
    (void)canSeeTerrain_;
}
struct Root {
    void tick(float,float){work(0);}void updateAnimations(float){work(1);}
    void draw(){work(2);drawWorld();}
};
struct MainLoopTasks {static Root&root(){static Root r;return r;}};
struct Camera {int invView(){return 0;}};
struct ClientCamera {Camera*camera(){static Camera c;return &c;}};
struct CameraApp {static CameraApp&instance(){static CameraApp a;return a;}
    ClientCamera&clientCamera(){static ClientCamera c;return c;}};
void tickStockAudio(int,float){trace.push_back("audio");}
struct SimpleGUI {static SimpleGUI&instance(){static SimpleGUI g;return g;}
    void draw(){trace.push_back("gui");}};
static void frame(){float dGameTime_=0.01f,dRenderTime_=0.01f;
@FRAME@
}
static void reset(){assert(stack.empty());trace.clear();for(auto&n:counts)n=0;fail=-1;worker=false;validDevice=true;}
int main(){
    frame();assert(stack.empty());
    assert(trace==std::vector<std::string>({"work0","end0","work1","end1","work2",
        "work3","opaque_flush","end3","work4","end4","end2"}));
    for(unsigned n:counts)assert(n==1);
    for(int stage=0;stage<5;++stage){
        reset();fail=stage;bool thrown=false;try{frame();}catch(const std::runtime_error&){thrown=true;}
        assert(thrown && stack.empty() && counts[stage]==1);
        if(stage>=2)assert(counts[2]==1);
    }
    reset();validDevice=false;frame();assert(counts[0]==1);
    for(unsigned i=1;i<5;++i)assert(counts[i]==0);
    reset();worker=true;frame();assert(stack.empty());
    @WORKER_CHECK@
}
'''


class StockFrameStagesTest(unittest.TestCase):
    def test_pristine_cached_and_worker_patch_orders(self):
        source = engine(self)
        for worker_first in (False, True):
            with self.subTest(worker_first=worker_first):
                before = dict(source)
                if worker_first:
                    before['client/app.cpp'] = worker_app(before['client/app.cpp'])
                final = changed(before)
                self.assertEqual(changed(final), final)
                final['client/app.cpp'] = worker_app(final['client/app.cpp'])
                self.assertEqual(changed(final), final)
                self.assertEqual(worker_app(final['client/app.cpp']), final['client/app.cpp'])
                self.assertEqual(final['client/app.cpp'].count('::wot::d3d9rt::FrameStage '), 3)
                self.assertEqual(final['client/world_app.cpp'].count('::wot::d3d9rt::FrameStage '), 2)
                world = final['client/world_app.cpp']
                main = profile_block(world, 'MainScene_Draw')
                terrain = profile_block(world, 'Terrain_Draw')
                self.assertIn('colourDrawContext.flush( Moo::DrawContext::OPAQUE_CHANNEL_MASK );', main)
                self.assertNotIn('Terrain_Draw', main)
                self.assertIn('Moo::RENDERING_PASS_COLOR', terrain)

    def test_unknown_or_duplicate_seam_leaves_all_files_unchanged(self):
        source = engine(self)
        for target, edits in PATCH.EDITS.items():
            for old, unused_new in edits:
                for duplicate in (False, True):
                    with self.subTest(target=target, duplicate=duplicate), tempfile.TemporaryDirectory() as temp:
                        root = Path(temp)
                        for name, text in source.items():
                            if name == target:
                                text = text.replace(old, old + old if duplicate else 'changed source seam\n', 1)
                            path = root / name
                            path.parent.mkdir(parents=True, exist_ok=True)
                            path.write_text(text)
                        before = {name: (root/name).read_bytes() for name in source}
                        with self.assertRaisesRegex(RuntimeError, 'Unexpected public engine source'):
                            PATCH.patch(root)
                        self.assertEqual(before, {name: (root/name).read_bytes() for name in source})

    def test_actual_sdk_blocks_order_device_guard_and_exception_balance(self):
        compiler = os.environ.get('CXX', 'c++')
        if not shutil.which(compiler):
            self.skipTest('C++ compiler required')
        source = engine(self)
        for has_worker in (False, True):
            original = dict(source)
            if has_worker:
                original['client/app.cpp'] = worker_app(original['client/app.cpp'])
            final = changed(original)
            app = final['client/app.cpp']
            start = app.index('\t\t\t// Now tick (and input)\n')
            frame = app[start:app.index('\n\t\t}\n\t\telse', start)]
            cpp = (HOST.replace('@FRAME@', frame)
                   .replace('@MAIN_SCENE@', profile_block(final['client/world_app.cpp'], 'MainScene_Draw'))
                   .replace('@TERRAIN@', profile_block(final['client/world_app.cpp'], 'Terrain_Draw'))
                   .replace('@WORKER_CHECK@',
                            'assert(counts[0]==1 && counts[1]==1 && counts[2]==1 && counts[3]==0 && counts[4]==0);'
                            if has_worker else 'for(unsigned n:counts)assert(n==1);'))
            with self.subTest(worker=has_worker), tempfile.TemporaryDirectory(prefix='frame-stage-host-') as temp:
                root = Path(temp)
                (root/'d3d9.h').write_text('struct IDirect3DDevice9;typedef unsigned long DWORD;typedef int HRESULT;\n')
                (root/'host.cpp').write_text(cpp)
                result = subprocess.run([compiler, '-std=c++17', '-O1', '-UNDEBUG',
                                         '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                                         '-I', str(root), '-I', str(ROOT/'bigworld'),
                                         str(root/'host.cpp'), '-o', str(root/'host')], capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                result = subprocess.run([str(root/'host')], capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == '__main__':
    unittest.main()
