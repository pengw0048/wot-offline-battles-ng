"""Execute the production Influx matrix consumer without changing its phase."""
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import unittest

from test_stock_vector4_basic import block

ROOT = Path(__file__).resolve().parents[1]
HOST = r'''
#include <atomic>
#include <cassert>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>
namespace BW { using string=std::string; }
std::vector<std::string> events;
std::map<std::string,unsigned> scopes;
struct ProfilerScope { explicit ProfilerScope(const char*name){++scopes[name];} };
#define PROFILER_SCOPED(name) ProfilerScope profilerScope(#name)
#define ERROR_MSG(...) ((void)0)
struct Matrix {int position=0;};
struct ParticleSystem {
    bool local=false,attached=true;unsigned reads=0,warnings=0;
    Matrix committed{17};std::function<void()> duringRead;
    Matrix worldTransform(){++reads;if(attached)++warnings;events.push_back("matrix");if(duringRead)duringRead();return attached?committed:Matrix{};}
};
struct DataSection {std::string filename;bool local=false;
    std::string readString(const char*key,const std::string&fallback){return std::string(key)=="fn_influx_effect"?filename:fallback;}
    bool readBool(const char*,bool){return local;}
    void writeString(const char*,const std::string&){}void writeBool(const char*,bool){}
};
using DataSectionPtr=std::shared_ptr<DataSection>;
struct Renderer {static Renderer&instance(){static Renderer r;return r;}int pipelineType(){return 0;}};
struct IRendererPipeline {enum {TYPE_FORWARD_SHADING=0};};
struct ParticleSystemAction;
struct ParticleSystemActionPtr {std::shared_ptr<ParticleSystemAction>value;ParticleSystemActionPtr(ParticleSystemAction*p):value(p){}};
struct ParticleSystemAction {
    float delay_=0.f,age_=0.f;bool enabled_=true;
    virtual ~ParticleSystemAction()=default;
    virtual ParticleSystemActionPtr clone()const=0;
    void clone(ParticleSystemAction*value)const{value->delay_=delay_;value->age_=age_;value->enabled_=enabled_;}
    virtual std::size_t sizeInBytes()const=0;virtual int typeID()const=0;virtual const BW::string&nameID()const=0;
    virtual void execute(ParticleSystem&,float)=0;virtual void lateExecute(ParticleSystem&,float)=0;
    virtual void loadInternal(DataSectionPtr)=0;virtual void saveInternal(DataSectionPtr)const=0;
};
const BW::string&stockInfluxActionName(){static const BW::string name="Influx";return name;}
struct Resource {};
struct SceneEffect;
struct Runtime {
    bool closed=false,fail=false;std::atomic<float>time{1.f};std::vector<std::shared_ptr<SceneEffect>>pendingEffects;
    std::function<void()> duringPreload;unsigned ticks=0;std::vector<int>chunks,visuals;unsigned submitted=0,generation=1;
    std::shared_ptr<const Resource>preload(const std::string&,std::string&){events.push_back("preload");if(duringPreload)duringPreload();return fail?nullptr:std::make_shared<const Resource>();}
    void tick(float){++ticks;}
};
std::shared_ptr<Runtime> runtime;
std::shared_ptr<Runtime>currentRuntime(){return runtime;}
float currentRuntimeTime(){return runtime?runtime->time.load():0.f;}
struct SceneEffect {
    Matrix world;unsigned updates=0,stops=0,resets=0,warms=0;float rate=0.,gap=0.;
    SceneEffect(Runtime&,std::shared_ptr<const Resource>,const Matrix&m):world(m){events.push_back("create");}
    void setWorld(const Matrix&m){world=m;++updates;events.push_back("world");}
    void multRate(float r){rate=r;events.push_back("rate");}
    void warmup(float g){gap=g;++warms;events.push_back("warmup");}
    void resetMotion(){++resets;events.push_back("reset");}
    void stop(){++stops;events.push_back("stop");}
};
@INFLUX@
struct Action:InfluxPSA {void load(const std::string&name,bool local=false){auto d=std::make_shared<DataSection>();d->filename=name;d->local=local;loadInternal(d);}};
std::atomic<bool>drawEnabled{true};
namespace Moo {namespace DrawContext {enum {TRANSPARENT_CHANNEL_MASK=1};}}
struct DrawItem {virtual ~DrawItem()=default;};
struct TPSDrawItem:DrawItem {explicit TPSDrawItem(std::shared_ptr<Runtime>){}};
struct TPSVisualDrawItem:DrawItem {explicit TPSVisualDrawItem(std::shared_ptr<Runtime>){}};
struct Context {unsigned draws=0;void drawUserItem(DrawItem*p,int){++draws;delete p;}};
struct App {enum {COLOUR_DRAW_CONTEXT=0};Context context;static App&instance(){static App app;return app;}Context&drawContext(int){return context;}};
@TICK@
@DRAW@
int main(){
    for(bool local:{false,true}){
        runtime=std::make_shared<Runtime>();ParticleSystem system;system.local=local;Action action;
        events.clear();const auto noEffect=scopes["StockInflux_NoEffect"];
        action.execute(system,0.f);action.delay_=2.f;action.execute(system,1.f);assert(system.reads==0);
        action.delay_=0.f;
        for(int i=0;i<2000;++i)action.execute(system,.01f);
        assert(system.reads==0&&runtime->pendingEffects.empty());
        assert(scopes["StockInflux_NoEffect"]==noEffect+2000);
        action.resetMotion();action.execute(system,.01f);assert(system.reads==0);
        action.load("valid",local);events.clear();
        // Resource loading precedes the first demanded matrix sample. The
        // same sample is reused by construction and both setWorld consumers.
        runtime->duringPreload=[&]{system.committed.position=23;};
        const auto active=scopes["StockInflux_Active"];action.execute(system,.01f);
        assert(system.reads==1&&system.warnings==1&&runtime->pendingEffects.size()==1);
        auto effect=runtime->pendingEffects.back();assert(effect->world.position==23&&effect->updates==2&&effect->resets==0);
        assert((events==std::vector<std::string>{"preload","matrix","create","world","rate","warmup","world"}));
        assert(scopes["StockInflux_Active"]==active+1);
        action.multRate(.4f);assert(effect->rate==.4f);
        for(int i=0;i<100;++i){system.committed.position=i;action.execute(system,.01f);assert(effect->world.position==i);}
        assert(system.reads==101&&system.warnings==101); // Active diagnostics remain.
        action.resetMotion();action.execute(system,.01f);action.execute(system,.01f);assert(effect->resets==1);
        // Late execution remains an explicit call, with no new queue or
        // invented local/world-space scheduling branch.
        action.lateExecute(system,0.f);assert(system.reads==104);
        system.attached=false;action.execute(system,.01f);assert(effect->world.position==0);
        system.attached=true;
        // Resource failure preserves stop/dirty/reset and never queries a
        // transform that cannot be consumed by a replacement effect.
        action.load("missing",local);runtime->fail=true;action.resetMotion();events.clear();
        const auto before=system.reads;action.execute(system,.01f);
        assert(system.reads==before&&effect->stops==1);
        assert((events==std::vector<std::string>{"stop","preload"}));
        runtime->fail=false;action.execute(system,.01f);assert(system.reads==before);
        // A new resource version reactivates the same action, with no stale
        // reset flag carried over from the failed version.
        action.load("reenter",local);runtime->time=5.f;action.execute(system,.01f);
        auto current=runtime->pendingEffects.back();assert(current!=effect&&current->resets==0&&current->gap==4.f);
        // A native getter may re-enter the action. Its new flags are observed
        // in the same sync; retaining the runtime keeps its storage alive.
        system.duringRead=[&]{action.resetMotion();action.multRate(.7f);};
        action.execute(system,.01f);assert(current->resets==1&&current->rate==.7f);
        system.duringRead=[&]{runtime->pendingEffects.clear();current.reset();};
        action.execute(system,.01f); // No retained effect survives getter reentry.
        system.duringRead={};runtime->closed=true;const auto closed=system.reads;action.execute(system,.01f);assert(system.reads==closed);
        runtime->closed=false;runtime->pendingEffects.clear();current.reset();effect.reset();
        action.execute(system,.01f);assert(system.reads==closed);
    }
    runtime=std::make_shared<Runtime>();tickStockTPSRuntime(.1f);assert(runtime->ticks==1&&scopes["StockTPS_tick"]==1);
    assert(drawStockTPSRuntime()&&scopes["StockTPS_draw"]==1&&App::instance().context.draws==0);
    runtime->chunks.push_back(1);assert(drawStockTPSRuntime()&&App::instance().context.draws==1);
    assert(drawStockTPSRuntime()&&scopes["StockTPS_draw"]==3&&App::instance().context.draws==1);
    runtime.reset();return 0;
}
'''


class InfluxSyncTests(unittest.TestCase):
    def test_real_owner_matrix_consumption(self):
        compiler = shutil.which('c++')
        if not compiler:
            self.skipTest('C++ compiler required')
        source = (ROOT / 'bigworld/stock_tps_scene.cpp').read_text()
        action = source[source.index('class InfluxPSA :'):source.index('\nclass PyInfluxPSA')]
        host = HOST.replace('@INFLUX@', action)
        host = host.replace('@TICK@', block(source, 'void tickStockTPSRuntime('))
        host = host.replace('@DRAW@', block(source, 'bool drawStockTPSRuntime('))
        with tempfile.TemporaryDirectory(prefix='influx-sync-') as directory:
            folder = Path(directory)
            (folder / 'probe.cpp').write_text(host)
            subprocess.run([compiler, '-std=c++17', '-O2', '-Wall', '-Wextra',
                            '-Werror', '-fsanitize=address,undefined',
                            str(folder / 'probe.cpp'), '-o', str(folder / 'probe')], check=True)
            subprocess.run([str(folder / 'probe')], check=True)

    def test_exact_client_synchronous_dispatch(self):
        client = os.environ.get('WOT_0922_CLIENT')
        if not client:
            self.skipTest('exact client executable required')
        data = (Path(client) / 'WorldOfTanks.exe').read_bytes()
        pe = struct.unpack_from('<I', data, 0x3c)[0]
        self.assertEqual(data[pe:pe + 4], b'PE\0\0')
        machine, count = struct.unpack_from('<HH', data, pe + 4)
        self.assertEqual(machine, 0x14c)
        optional_size = struct.unpack_from('<H', data, pe + 20)[0]
        base = struct.unpack_from('<I', data, pe + 24 + 28)[0]
        sections = []
        for index in range(count):
            header = pe + 24 + optional_size + index * 40
            size, rva, raw_size, raw = struct.unpack_from('<IIII', data, header + 8)
            sections.append((rva, max(size, raw_size), raw))
        def at(address, size):
            rva = address - base
            for begin, length, raw in sections:
                if begin <= rva and rva + size <= begin + length:
                    return data[raw + rva - begin:raw + rva - begin + size]
            self.fail('Reviewed code address is outside exact client sections')
        self.assertEqual(struct.unpack('<II', at(0x174ece0, 8)), (0xfab560, 0xfab860))
        for address, target in ((0xfab57a, 0x9ea370), (0xfab5a4, 0xfab680),
                                (0xfab885, 0x9ea370), (0xfab88d, 0xfab680)):
            instruction = at(address, 5)
            self.assertEqual(instruction[0], 0xe8)
            self.assertEqual(address + 5 + struct.unpack('<i', instruction[1:])[0], target)


if __name__ == '__main__':
    unittest.main()
