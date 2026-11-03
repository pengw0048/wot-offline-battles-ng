"""Exercise the real HDR history initializer and generated resource lifecycle."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from native_test_compile_flags import native_test_compile_flags
from test_stock_hdr import FILES, ROOT, patch, public_sources


HOST = r'''
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>
using uint = unsigned; using uint32 = unsigned; using HRESULT = int;
using std::max;
#define BW_GUARD
#define BW_SCOPED_DOG_WATCHER(x)
#define MF_ASSERT(x) ((void)0)
#define SUCCEEDED(x) ((x)>=0)
#define D3DCLEAR_TARGET 1
#define D3DFMT_A16B16G16R16F 1
#define D3DFMT_R32F 2
#define D3DPOOL_SYSTEMMEM 3
namespace BW { template<class T> using vector = std::vector<T>; using string = std::string; }
struct D3DSURFACE_DESC { uint Width=1920, Height=1080; };
namespace DX { struct Viewport { uint Width=1920, Height=1080; }; }
struct Texture { float value=std::numeric_limits<float>::quiet_NaN(); };
struct RenderTarget;
RenderTarget* active=nullptr;
Texture backbuffer{.75f};
std::string failPush, failClear, failAllocation;
int clearAttempts=0, popCount=0;
struct Device {
 int Clear(int count, void* rect, int flags, int colour, float depth, int stencil);
};
struct ComTexture { bool valid=true; bool hasComObject() const { return valid; } };
namespace Moo {
struct Context {
 D3DSURFACE_DESC desc; Device dev; DX::Viewport viewport;
 const D3DSURFACE_DESC& backBufferDesc() { return desc; }
 void getViewport(DX::Viewport* out) { *out=viewport; }
 void setViewport(DX::Viewport*) {}
 ComTexture createTexture(int,int,int,int,int,int,const char*) { return {failAllocation != "system"}; }
 Device* device() { return &dev; }
};
Context& rc() { static Context context; return context; }
using RenderTarget = ::RenderTarget;
}
struct RenderTarget {
 std::string name; Texture texture;
 explicit RenderTarget(const char* n):name(n) {}
 bool push() { if (name==failPush) return false; assert(active==nullptr); active=this; return true; }
 void pop() { assert(active==this); active=nullptr; ++popCount; }
 Texture* pTexture() { return &texture; }
};
int Device::Clear(int count, void* rect, int flags, int colour, float depth, int stencil) {
 assert(active && count==0 && !rect && flags==D3DCLEAR_TARGET && colour==0 && depth==1.f && stencil==0);
 ++clearAttempts; if (active->name==failClear) return -1; active->texture.value=0; return 0;
}
@HELPER@
using Target = std::shared_ptr<RenderTarget>;
bool createRenderTarget(Target& target,uint,uint,int,const char* name,bool,bool) {
 target=std::make_shared<RenderTarget>(name); return failAllocation!=name;
}
std::string makeStr(const char* format, unsigned n) { char text[128]; std::snprintf(text,sizeof(text),format,n); return text; }
const uint g_minBloomTargetSize=4;
struct HDRSupport {
 Target m_hdrRT,m_avgLumRT,m_finalAvgLumRT;
 std::vector<Target> m_avgLumRTs;
 std::vector<std::pair<Target,Target>> m_bloomMaps;
 std::vector<DX::Viewport> m_viewPorts;
 ComTexture m_inmemTexture;
 bool m_valid=false;
 void createUnmanagedObjects();
 void destroy() { m_valid=false; m_viewPorts.clear(); m_avgLumRTs.clear(); m_bloomMaps.clear(); }
 void downsampleAndSwap() {
   // A valid downsample writes the last mip before the real public swap sequence.
   m_avgLumRTs.back()->texture.value=.4f;
   @SWAP@
 }
 float firstGpuHistory() { return m_avgLumRT->texture.value; }
};
@CREATE@
void verify_success(HDRSupport& hdr) {
 assert(hdr.m_valid && !active && backbuffer.value==.75f);
 assert(hdr.m_avgLumRT->texture.value==0 && hdr.m_finalAvgLumRT->texture.value==0);
 hdr.downsampleAndSwap();
 assert(std::isfinite(hdr.firstGpuHistory()) && hdr.firstGpuHistory()==.4f);
 assert(hdr.m_avgLumRTs.back()->texture.value==0 && hdr.m_finalAvgLumRT->texture.value==0);
}
int main() {
 HDRSupport hdr;
 hdr.createUnmanagedObjects(); verify_success(hdr);
 hdr.destroy(); Moo::rc().desc.Width=1280; Moo::rc().desc.Height=720;
 hdr.createUnmanagedObjects(); verify_success(hdr);
 for (int failure=0; failure<4; ++failure) {
   hdr.destroy(); failPush.clear(); failClear.clear();
   const char* first="texture/HDR average luminance RT";
   const char* second="texture/HDR final average luminance RT";
   if (failure==0) failPush=first; if (failure==1) failClear=first;
   if (failure==2) failPush=second; if (failure==3) failClear=second;
   int before=clearAttempts, pops=popCount;
   hdr.createUnmanagedObjects();
   assert(!hdr.m_valid && !active && backbuffer.value==.75f);
   assert(clearAttempts-before==(failure==0 || failure==2 ? 1 : 2));
   assert(popCount-pops==clearAttempts-before);
   if (failure<2) assert(hdr.m_finalAvgLumRT->texture.value==0);
 }
 failPush.clear(); failClear.clear();
 for (const char* allocation : {"texture/HDR average luminance RT", "system"}) {
   hdr.destroy(); failAllocation=allocation; int before=clearAttempts;
   hdr.createUnmanagedObjects(); assert(!hdr.m_valid && !active && clearAttempts==before && backbuffer.value==.75f);
 }
}
'''


class HdrLuminanceHistoryTests(unittest.TestCase):
    def test_lifecycle_and_negative_control(self):
        compiler = shutil.which(os.environ.get('CXX', 'c++'))
        if not compiler:
            self.skipTest('Host C++ compiler is required')
        with tempfile.TemporaryDirectory(prefix='hdr-history-') as directory:
            root = Path(directory)
            public_sources(self, root)
            patch.patch(root)
            first = {name: (root / name).read_text() for name in FILES}
            patch.patch(root)
            self.assertEqual(first, {name: (root / name).read_text() for name in FILES})
            support = first['lib/romp/hdr_support.cpp']
            create = patch.function(support, 'void HDRSupport::createUnmanagedObjects(')
            helper = patch.function((ROOT / 'bigworld/stock_hdr.cpp').read_text(),
                                    'bool clearStockHdrLuminanceHistory(')
            start = support.index('    std::swap(m_avgLumRTs.back(), m_avgLumRT);')
            swap = support[start:support.index('\n}', start)].strip()
            self.assertEqual(swap.count('std::swap('), 2)
            begin = create.index('    // #1513 a72669..a726b3')
            end = create.index('    MF_ASSERT(success', begin)
            legacy = create[:begin] + create[end:]
            cached = support.replace(create, legacy, 1)
            (root / 'lib/romp/hdr_support.cpp').write_text(cached)
            patch.patch(root)
            self.assertEqual(first, {name: (root / name).read_text() for name in FILES})
            patch.patch(root)
            self.assertEqual(first, {name: (root / name).read_text() for name in FILES})
            for label, body in [('patched', create), ('legacy', legacy)]:
                source = HOST.replace('@HELPER@', helper).replace('@SWAP@', swap).replace('@CREATE@', body)
                path = root / (label + '.cpp')
                path.write_text(source)
                binary = root / label
                subprocess.run([compiler, *native_test_compile_flags(compiler), '-std=c++17',
                                str(path), '-o', str(binary)], check=True)
                result = subprocess.run([str(binary)], capture_output=True)
                if label == 'patched':
                    self.assertEqual(result.returncode, 0, result.stderr.decode(errors='replace'))
                else:
                    self.assertNotEqual(result.returncode, 0, 'legacy uninitialized history passed')
                    self.assertIn('m_avgLumRT->texture.value==0', result.stderr.decode(errors='replace'))


if __name__ == '__main__':
    unittest.main()
