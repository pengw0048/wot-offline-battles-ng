"""Exercise actual background-loader publication without racing scene containers."""
import importlib.util
from pathlib import Path
import os
import subprocess
import tarfile
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


def body(source, signature):
    begin = source.index(signature)
    pos = source.index('{', begin)
    end, depth = pos + 1, 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[begin:end]


class LoadingTests(unittest.TestCase):
    def fresh(self, root):
        archive = Path(os.environ.get('WOT_PUBLIC_ENGINE_ARCHIVE', '/tmp/wot-bigworld-engine-14.4.1.tar.gz'))
        if not archive.exists():
            self.skipTest('Pinned engine archive required')
        with tarfile.open(archive) as source:
            for name in ('compiled_space.hpp', 'compiled_space.cpp', 'compiled_space_mapping.hpp', 'compiled_space_mapping.cpp', 'loader.hpp', 'loader.cpp'):
                member = next(m for m in source.getmembers() if m.name.endswith('/programming/bigworld/lib/compiled_space/' + name))
                path = root / 'lib/compiled_space' / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(source.extractfile(member).read())
        spec = importlib.util.spec_from_file_location('mapping_patch', ROOT/'bigworld/patch_stock_compiled_space_mapping.py')
        patch = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(patch)
        patch.patch(root)
        before = {p: p.read_bytes() for p in root.rglob('*') if p.is_file()}
        patch.patch(root)
        self.assertEqual(before, {p: p.read_bytes() for p in root.rglob('*') if p.is_file()})

    def run_cpp(self, root, source):
        (root/'probe.cpp').write_text(source)
        result = subprocess.run(['c++', '-std=c++14', '-pthread', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined', '-I', str(root), str(root/'probe.cpp'), '-o', str(root/'probe')], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        result = subprocess.run([str(root/'probe')], capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_producer_completion_and_failed_bind(self):
        with tempfile.TemporaryDirectory(prefix='space-load-publication-') as directory:
            root = Path(directory)
            self.fresh(root)
            for name in ('compiled_space/forward_declarations.hpp', 'cstdmf/smartpointer.hpp', 'space/forward_declarations.hpp', 'pch.hpp'):
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text('')
            self.run_cpp(root, r'''
#include <atomic>
#include <cassert>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#define BW_BEGIN_NAMESPACE namespace BW {
#define BW_END_NAMESPACE }
#define MF_ASSERT(x) assert(x)
namespace BW {template<class T>using SmartPointer=std::shared_ptr<T>;class ClientSpace{};class DataSection{};class Matrix{};namespace CompiledSpace{class BinaryFormat{};class StringTable{};}}
#include "lib/compiled_space/loader.hpp"
#include "lib/compiled_space/loader.cpp"
using namespace BW;using namespace BW::CompiledSpace;
struct Loader:ILoader{
 std::mutex mutex;std::condition_variable cv;bool entered=false,released=false,success=true;int payload=0,bindings=0;
 bool doLoadFromSpace(ClientSpace*,BinaryFormat&,const DataSectionPtr&,const Matrix&,const StringTable&)override{
  std::unique_lock<std::mutex> lock(mutex);entered=true;cv.notify_all();cv.wait(lock,[&]{return released;});payload=1513;return success;
 }
 bool doBind()override{assert(payload==1513);++bindings;return true;}
 void doUnload()override{payload=0;}
 float percentLoaded()const override{assert(false&&"Progress must not inspect background-owned state");return 1.f;}
 void wait(){std::unique_lock<std::mutex> lock(mutex);cv.wait(lock,[&]{return entered;});}
 void release(){std::lock_guard<std::mutex> lock(mutex);released=true;cv.notify_all();}
};
int main(){ClientSpace space;BinaryFormat file;Matrix matrix;StringTable strings;
 for(bool success:{true,false}){Loader loader;loader.success=success;assert(loader.loadStatus()==0&&!loader.bind());
 std::thread worker([&]{assert(loader.loadFromSpace(&space,file,{},matrix,strings)==success);});loader.wait();
 for(unsigned i=0;i<10000;++i){assert(loader.loadStatus()==0);assert(!loader.bind());}
 loader.release();while(loader.loadStatus()!=1.f)std::this_thread::yield();assert(loader.payload==1513);worker.join();
 assert(loader.bind()==success);assert(loader.bindings==int(success));loader.unload();loader.unload();assert(loader.loadStatus()==0&&!loader.bind());}
}
''')

    def test_mapping_progress_tracks_completed_stages_and_reserves_binding(self):
        with tempfile.TemporaryDirectory(prefix='space-load-stages-') as directory:
            root = Path(directory)
            self.fresh(root)
            source = (root/'lib/compiled_space/compiled_space_mapping.cpp').read_text()
            production = '\n'.join(body(source, signature) for signature in ('void CompiledSpaceMapping::loadBG()', 'float CompiledSpaceMapping::loadStatus() const'))
            self.run_cpp(root, r'''
#include <atomic>
#include <cassert>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#define PROFILER_SCOPED(...)
#define MF_ASSERT(x) assert(x)
#define ASSET_MSG(...) ((void)0)
#define BW_MAX_PATH 256
struct StringBuilder{std::string s;StringBuilder(char*,size_t){}void append(const std::string& v){s+=v;}const char* string()const{return s.c_str();}};
struct Gate{std::mutex mutex;std::condition_variable cv;int entered=0,released=0;void hit(int n){std::unique_lock<std::mutex> lock(mutex);entered=n;cv.notify_all();cv.wait(lock,[&]{return released>=n;});}void wait(int n){std::unique_lock<std::mutex> lock(mutex);cv.wait(lock,[&]{return entered>=n;});}void release(int n){std::lock_guard<std::mutex> lock(mutex);released=n;cv.notify_all();}};
struct Reader{bool open(const char*){return true;}};
struct Part{std::function<void()> visit;bool ok=true;bool read(Reader&){if(visit)visit();return ok;}bool read(Reader&,int){return true;}void preloadAssets(Part&){if(visit)visit();}void releasePreloadedAssets(){}float percentLoaded()const{assert(false);return 1;}};
struct Space{void mappingLoadedBG(void*){}};
struct EntityListTypes{enum{CLIENT_ENTITIES_MAGIC,UDOS_MAGIC};};
struct ILoader{std::function<void()> visit;bool ok=true;bool loadFromSpace(Space*,Reader&,int,int,Part&){if(visit)visit();return ok;}bool loadMustSucceed()const{return true;}float loadStatus()const{assert(false);return 1;}};
struct CompiledSpaceMapping{enum State{STATE_UNLOADED,STATE_LOADING,STATE_FAILED_TO_LOAD,STATE_LOADED};State state_=STATE_LOADING;std::atomic<size_t> completedLoadStages_{0};using Loaders=std::vector<ILoader*>;Loaders loaders_;std::string path_="original";Reader reader_;Part settings_,stringTable_,staticGeometry_,assetList_,entities_,userDataObjects_;Space space_;int pSettings_=0,transform_=0,numFailures_=0;void ensureSpaceCompiled(){}void loadBG();float loadStatus()const;};
@PRODUCTION@
int main(){for(bool fail:{false,true}){Gate gate;CompiledSpaceMapping mapping;ILoader first,second;mapping.loaders_={&first,&second};
 mapping.staticGeometry_.visit=[&]{gate.hit(1);};mapping.assetList_.visit=[&]{gate.hit(2);};first.visit=[&]{gate.hit(3);};second.visit=[&]{gate.hit(4);};second.ok=!fail;
 std::thread worker([&]{mapping.loadBG();});for(int stage=1;stage<=4;++stage){gate.wait(stage);for(int i=0;i<1000;++i)assert(mapping.loadStatus()==float(stage-1)/5.f);gate.release(stage);}worker.join();
 assert(mapping.numFailures_==int(fail));assert(mapping.loadStatus()==4.f/5.f);mapping.state_=fail?CompiledSpaceMapping::STATE_FAILED_TO_LOAD:CompiledSpaceMapping::STATE_LOADED;assert(mapping.loadStatus()==1.f);mapping.state_=CompiledSpaceMapping::STATE_UNLOADED;assert(mapping.loadStatus()==0.f);}
 CompiledSpaceMapping failed;failed.staticGeometry_.ok=false;failed.loadBG();assert(failed.numFailures_==1&&failed.loadStatus()==0.f);
}
'''.replace('@PRODUCTION@', production))


if __name__ == '__main__':
    unittest.main()
