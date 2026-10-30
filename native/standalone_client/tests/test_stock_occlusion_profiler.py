"""Exercise the pinned query loop with counting scopes and a strict driver stub."""
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
    'occlusion_profiler', ROOT / 'bigworld/patch_stock_occlusion_profiler.py')
PATCH = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PATCH)


class StockOcclusionProfilerTests(unittest.TestCase):
    def setUp(self):
        archive = Path(os.environ.get(
            'WOT_PUBLIC_ENGINE_ARCHIVE', '/tmp/wot-bigworld-engine-14.4.1.tar.gz'))
        if not archive.is_file():
            self.skipTest('Pinned public engine archive is required')
        with tarfile.open(archive) as original:
            member = next(n for n in original.getnames()
                          if n.endswith('/programming/bigworld/' + PATCH.PATH))
            self.source = original.extractfile(member).read().decode('utf-8-sig').replace('\r\n', '\n')

    def test_exact_patch_is_reversible_idempotent_and_rejects_damage(self):
        final = PATCH.patch_source(self.source)
        self.assertEqual(PATCH.patch_source(final), final)
        self.assertEqual(final.replace(PATCH.AFTER, PATCH.BEFORE, 1), self.source)
        with tempfile.TemporaryDirectory(prefix='wot-occlusion-profiler-') as directory:
            path = Path(directory) / PATCH.PATH
            path.parent.mkdir(parents=True)
            for source in (self.source.replace(PATCH.BEFORE, '', 1),
                           self.source + PATCH.BEFORE, final + PATCH.AFTER,
                           final.replace('PROFILER_SCOPED( OQH_QueryReady );', '', 1),
                           self.source.replace('sizeof(DWORD), 0 );', 'sizeof(DWORD), 1 );', 1)):
                path.write_text(source)
                before = path.read_bytes()
                with self.assertRaises(ValueError):
                    PATCH.patch(directory)
                self.assertEqual(path.read_bytes(), before)
            path.write_bytes(b'\xef\xbb\xbf' + self.source.replace('\n', '\r\n').encode())
            PATCH.patch(directory)
            before = path.read_bytes()
            self.assertTrue(before.startswith(b'\xef\xbb\xbf'))
            self.assertNotIn(b'\n', before.replace(b'\r\n', b''))
            PATCH.patch(directory)
            self.assertEqual(path.read_bytes(), before)

    def test_actual_loop_has_one_read_per_pending_slot_and_exact_outcome_counts(self):
        compiler = shutil.which('c++')
        if not compiler:
            self.skipTest('C++ compiler is required')
        start = self.source.index('void OcclusionQueryHelper::getVizResults(')
        stop = self.source.index('\n\n\n/**', start)
        original = self.source[start:stop]
        patched = PATCH.patch_source(original)
        harness = r'''
#include <array>
#include <cassert>
#include <cstdint>
#include <map>
#include <string>
#include <vector>
using HRESULT=int;using DWORD=std::uint32_t;using uint16=std::uint16_t;
constexpr HRESULT D3D_OK=0,S_FALSE=1,DEVICE_LOST=-3;
std::map<std::string,int> scopes;
struct Scope{explicit Scope(const char*n){++scopes[n];}};
#define PROFILER_SCOPED(n) Scope scope_##n(#n)
#define BW_GUARD_PROFILER(n) PROFILER_SCOPED(n)
namespace HandlePool{using Handle=int;}
struct Query{
 HRESULT result=0;int calls=0;DWORD value=0;
 HRESULT GetData(void*out,unsigned size,unsigned flags){
  assert(size==sizeof(DWORD)&&flags==0);++calls;
  if(result==D3D_OK)*static_cast<DWORD*>(out)=value;
  return result;
 }
};
struct OcclusionQueryHelper{
 int numFrames_=4;std::array<DWORD,4>results_{{7,8,9,10}};
 std::array<bool,4>resultPending_{{true,true,true,false}};
 std::array<Query,4>storage;std::array<Query*,4>queries_;
 OcclusionQueryHelper(){for(int i=0;i<4;++i)queries_[i]=&storage[i];}
 uint16 resultIndex(int h,int f){assert(h==5);return f;}
 void getVizResults(int);void original(int);
};
@ORIGINAL@
@PATCHED@
void same(const OcclusionQueryHelper&a,const OcclusionQueryHelper&b){
 assert(a.results_==b.results_&&a.resultPending_==b.resultPending_);
 for(int i=0;i<4;++i)assert(a.storage[i].calls==b.storage[i].calls);
}
int main(){
 OcclusionQueryHelper a,b;
 for(auto*p:{&a,&b}){
  p->storage[0].result=D3D_OK;p->storage[0].value=123;
  p->storage[1].result=S_FALSE;p->storage[2].result=DEVICE_LOST;
 }
 a.original(5);scopes.clear();b.getVizResults(5);same(a,b);
 assert(scopes["OQH_getVizResults"]==1&&scopes["OQH_QueryGetData"]==3);
 assert(scopes["OQH_QueryReady"]==1&&scopes["OQH_QueryNotReady"]==1&&scopes["OQH_QueryOtherResult"]==1);
 assert(!b.resultPending_[0]&&b.resultPending_[1]&&b.resultPending_[2]);
 a.original(5);scopes.clear();b.getVizResults(5);same(a,b);
 assert(scopes["OQH_QueryGetData"]==2&&scopes["OQH_QueryReady"]==0);
 for(auto*p:{&a,&b})for(auto&x:p->storage)x.result=D3D_OK;
 a.original(5);scopes.clear();b.getVizResults(5);same(a,b);
 assert(scopes["OQH_QueryGetData"]==2&&scopes["OQH_QueryReady"]==2);
 a.original(5);scopes.clear();b.getVizResults(5);same(a,b);
 assert(scopes["OQH_getVizResults"]==1&&scopes["OQH_QueryGetData"]==0);
}
'''.replace('@ORIGINAL@', original.replace('::getVizResults(', '::original(', 1)).replace('@PATCHED@', patched)
        with tempfile.TemporaryDirectory(prefix='wot-occlusion-loop-') as directory:
            source = Path(directory) / 'host.cpp'
            binary = Path(directory) / 'host'
            source.write_text(harness)
            subprocess.run([compiler, '-std=c++14', '-Wall', '-Wextra', '-Werror',
                            str(source), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)


if __name__ == '__main__':
    unittest.main()
