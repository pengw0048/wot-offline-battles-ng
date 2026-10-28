"""Check the production tPS draw-queue ownership across flush and teardown."""
from pathlib import Path
import shutil
import subprocess
from native_test_compile_flags import native_test_compile_flags
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


def make_probe():
    source = (ROOT / 'bigworld/stock_tps_scene.cpp').read_text()
    draw_types = source[source.index('struct DeferredTPSDraw'):source.index('\n}\n\nconst BW::string & stockInfluxActionName')]
    finish = source[source.index('void finishStockTPSParticleItems()'):source.index('\nvoid finiStockTPSRuntime()')]
    start = source.index('std::shared_ptr<Runtime> runtime;')
    leases = source[start:source.index('\nstruct SceneSource\n', start)]
    fini = source[source.index('void finiStockTPSRuntime()'):source.index('\nBW_END_NAMESPACE')]
    return r'''
#include <cassert>
#include <atomic>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>
#include <thread>
#include "stock_tps_parameters.hpp"
#define SUCCEEDED(value) ((value)>=0)
using D3DXHANDLE=const char*;
struct ID3DXEffect {int SetValue(D3DXHANDLE,const void*,unsigned){return 0;}};
namespace Moo { namespace DrawContext {
struct UserDrawItem { virtual ~UserDrawItem() = default;virtual void draw()=0; };
}
struct EffectConstantValue {virtual ~EffectConstantValue()=default;virtual bool operator()(ID3DXEffect*,D3DXHANDLE)=0;};
struct EffectVisualContext {enum{AUTO_CONSTANT_TYPE_PER_FRAME};void unregisterAutoConstant(int,const char*){}};
struct Context {EffectVisualContext& effectVisualContext(){static EffectVisualContext value;return value;}};
Context& rc(){static Context value;return value;}
}
std::vector<unsigned> events;
unsigned destroyed=0;
struct Runtime {
    std::uint64_t generation=1;
    bool closed=false;
    std::atomic<float> time{0.f};
    float fov=1.0471975f;bool sniper=false;struct{int render_quality=4;} quality;
    ~Runtime(){++destroyed;}
    bool draw(unsigned bucket){events.push_back(bucket);return true;}
    bool drawVisuals(){events.push_back(9);return true;}
    void close(){closed=true;}
};
void finiStockTPSQuality(){}
''' + draw_types + '\n' + finish + '\n' + leases + '\n' + fini + r'''
int main() {
    auto owner=std::make_shared<Runtime>();
    auto * item=new TPSDrawItem(owner);
    item->drawSplit();
    delete item;
    assert((events==std::vector<unsigned>{0}));
    events.push_back(9); // Model transparency stays between the sprite buckets.
    finishStockTPSParticleItems();
    assert((events==std::vector<unsigned>{0,9,1}));
    finishStockTPSParticleItems();
    assert(events.size()==3);
    events.clear();
    item=new TPSDrawItem(owner);item->draw();delete item;
    assert((events==std::vector<unsigned>{0,9,1}));
    events.clear();
    item=new TPSDrawItem(owner);item->drawSplit();delete item;
    ++owner->generation;
    finishStockTPSParticleItems();
    assert((events==std::vector<unsigned>{0}));
    events.clear();
    item=new TPSDrawItem(owner);++owner->generation;
    item->draw();item->drawSplit();delete item;
    finishStockTPSParticleItems();assert(events.empty());
    item=new TPSDrawItem(owner);item->drawSplit();delete item;
    owner->closed=true;finishStockTPSParticleItems();
    assert((events==std::vector<unsigned>{0}));
    events.clear();
    item=new TPSDrawItem(owner);item->draw();item->drawSplit();delete item;
    finishStockTPSParticleItems();assert(events.empty());
    owner.reset();assert(destroyed==1);
    owner=std::make_shared<Runtime>();
    std::weak_ptr<Runtime> weak=owner;
    item=new TPSDrawItem(owner);item->drawSplit();delete item;
    owner.reset();assert(!weak.expired());
    finishStockTPSParticleItems();
    assert(weak.expired()&&destroyed==2);
    owner=std::make_shared<Runtime>();owner->time=5.f;weak=owner;
    std::atomic_store(&runtime,owner);owner.reset();
    std::atomic<bool> ready{false},release{false};
    std::thread loader([&](){
        auto lease=currentRuntime();assert(lease);
        ready.store(true,std::memory_order_release);
        while(!release.load(std::memory_order_acquire))std::this_thread::yield();
        assert(lease->closed&&lease->time.load()==5.f);
    });
    while(!ready.load(std::memory_order_acquire))std::this_thread::yield();
    finiStockTPSRuntime();
    assert(!currentRuntime()&&currentRuntimeTime()==0.f&&!weak.expired());
    release.store(true,std::memory_order_release);loader.join();
    assert(weak.expired()&&destroyed==3);
    finiStockTPSRuntime();assert(destroyed==3);
}
'''


class StockTPSSceneTests(unittest.TestCase):
    def test_production_draw_ownership(self):
        if not shutil.which('c++'):
            raise unittest.SkipTest('C++ compiler unavailable: c++')
        with tempfile.TemporaryDirectory(prefix='wot-tps-scene-') as directory:
            work = Path(directory)
            (work / 'probe.cpp').write_text(make_probe())
            subprocess.run(['c++', *native_test_compile_flags('c++'), '-std=c++17',
                            '-Wall', '-Wextra', '-Werror', str(work / 'probe.cpp'),
                            '-I'+str(ROOT/'bigworld'),
                            '-o', str(work / 'probe')], check=True)
            subprocess.run([str(work / 'probe')], check=True)


if __name__ == '__main__':
    unittest.main()
