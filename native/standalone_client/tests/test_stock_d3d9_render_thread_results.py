"""The actual queue templates must preserve executed HRESULTs and report rejected work."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest

from native_test_compile_flags import native_test_compile_flags
from test_stock_cursor_camera import function


SOURCE = Path(__file__).parents[1] / 'bigworld/stock_d3d9_render_thread.cpp'


class RenderThreadResultTests(unittest.TestCase):
    def test_executed_results_and_bounded_failure_records(self):
        compiler = shutil.which(os.environ.get('CXX', 'c++'))
        if not compiler:
            self.skipTest('Host C++ compiler required')
        source = SOURCE.read_text()
        implementation = function(source, 'static HRESULT reportResult(') + '\n'
        implementation += function(source, 'template<class F> HRESULT async(')
        macro = next(line for line in source.splitlines() if line.startswith('#define ASYNC('))
        wrappers = '\n'.join(function(source, signature) for signature in (
            'HRESULT WDevice::Clear(', 'HRESULT WDevice::DrawPrimitive('))
        with tempfile.TemporaryDirectory(prefix='d3d9-execution-results-') as folder:
            root = Path(folder)
            variants = {
                'production': implementation,
                'lost-sync-result': implementation.replace('return reportResult(name, f());', 'f(); return D3D_OK;'),
                'lost-queued-result': implementation.replace('reportResult(name, call());', '(void)name; call();'),
            }
            for name, body in variants.items():
                path = root / (name + '.cpp')
                path.write_text(HOST.replace('@IMPLEMENTATION@', body).replace('@MACRO@', macro).replace('@WRAPPERS@', wrappers))
                binary = root / name
                subprocess.run([compiler, '-std=c++14', '-Wall', '-Wextra', '-Werror', '-UNDEBUG',
                                *native_test_compile_flags(compiler), str(path), '-o', str(binary)], check=True)
                result = subprocess.run([str(binary)], capture_output=True, text=True)
                self.assertEqual(result.returncode, 0 if name == 'production' else 41,
                                 (name, result.returncode, result.stdout, result.stderr))

    def test_sibling_post_data_paths_record_execution_failures(self):
        source = SOURCE.read_text()
        for signature, operation in (
                ('HRESULT WDevice::DrawPrimitiveUP(', 'DrawPrimitiveUP'),
                ('HRESULT WDevice::DrawIndexedPrimitiveUP(', 'DrawIndexedPrimitiveUP')):
            body = function(source, signature)
            self.assertEqual(body.count('rt::reportResult("' + operation + '"'), 2)
        constants = source[source.index('#define CONSTS('):source.index('CONSTS(SetVertexShaderConstantF')]
        self.assertEqual(constants.count('rt::reportResult(#Name,'), 2)
        for line in source.splitlines():
            if 'return rt::async(' in line:
                self.assertIn('{ return r->UnlockRect(', line)
        restore = function(source, 'static void restoreShadowed(')
        self.assertIn('rt::reportResult("Post.RestoreRenderTarget"', restore)
        self.assertIn('rt::reportResult("Post.RestoreDepthStencilSurface"', restore)


HOST = r'''
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <utility>
#include <vector>
using HRESULT=std::int32_t;using DWORD=unsigned long;using D3DCOLOR=unsigned;using UINT=unsigned;
using D3DPRIMITIVETYPE=int;
const HRESULT D3D_OK=0;
#define FAILED(hr) ((hr)<0)
#define CHECK(value) do{if(!(value))return 41;}while(0)
unsigned mode=2;bool parked=false;unsigned syncs=0;
unsigned long GetCurrentThreadId(){return mode;}
struct D3DRECT{int x1,y1,x2,y2;};
namespace rt {
FILE* output=tmpfile();FILE*reportLog(){return output;}
bool onMain(){return mode==1;}bool mainOwnsDevice(){return onMain()&&parked;}
std::vector<std::function<void()>>queue;
template<class F>void post(F f){queue.emplace_back(std::move(f));}
template<class F>auto sync(F&& f,const char*)->decltype(f()){
 ++syncs;auto saved=mode;mode=2;auto result=f();mode=saved;return result;
}
template<class F>void postData(const void*data,unsigned length,F f){
 std::vector<unsigned char>copy(length);if(length)memcpy(copy.data(),data,length);
 post([copy,f](){f(copy.data());});
}
@IMPLEMENTATION@
void drain(){auto saved=mode;mode=2;auto pending=std::move(queue);queue.clear();for(auto&f:pending)f();mode=saved;}
unsigned records(){fflush(output);rewind(output);unsigned n=0;char line[512];while(fgets(line,sizeof(line),output))++n;fseek(output,0,SEEK_END);return n;}
}
struct Device {
 HRESULT result=0;unsigned calls=0;int x=0;
 HRESULT Clear(DWORD n,const D3DRECT*r,DWORD,D3DCOLOR,float,DWORD){++calls;if(n)x=r->x1;return result;}
 HRESULT DrawPrimitive(D3DPRIMITIVETYPE,UINT,UINT){++calls;return result;}
};
struct WDevice {
 Device*real;
 HRESULT Clear(DWORD,const D3DRECT*,DWORD,D3DCOLOR,float,DWORD);
 HRESULT DrawPrimitive(D3DPRIMITIVETYPE,UINT,UINT);
};
#define REAL (this->real)
@MACRO@
@WRAPPERS@
int main(){
 Device real;WDevice device{&real};
 real.result=-7;CHECK(device.DrawPrimitive(1,0,1)==-7);CHECK(rt::records()==1);
 mode=0;real.result=-8;CHECK(device.DrawPrimitive(1,0,1)==-8);CHECK(rt::records()==2);
 mode=1;real.result=-9;unsigned calls=real.calls;
 CHECK(device.DrawPrimitive(1,0,1)==D3D_OK);CHECK(real.calls==calls&&rt::records()==2);
 rt::drain();CHECK(real.calls==calls+1&&rt::records()==3);
 parked=true;real.result=-10;CHECK(device.DrawPrimitive(1,0,1)==-10);CHECK(rt::queue.empty());parked=false;
 D3DRECT rect{17,0,20,20};real.result=-11;
 CHECK(device.Clear(1,&rect,0,0,0,0)==D3D_OK);rect.x1=99;rt::drain();CHECK(real.x==17);
 mode=2;real.result=-12;CHECK(device.Clear(0,nullptr,0,0,0,0)==-12);CHECK(rt::records()==6);
 real.result=-7;CHECK(device.DrawPrimitive(1,0,1)==-7);CHECK(rt::records()==6);
 char sameName[]="DrawPrimitive";CHECK(rt::reportResult(sameName,-7)==-7);CHECK(rt::records()==6);
 real.result=1;CHECK(device.DrawPrimitive(1,0,1)==1);CHECK(rt::records()==6);
 for(int i=0;i<100;++i)CHECK(rt::reportResult("Bound",-100-i)==-100-i);
 CHECK(rt::records()==64);CHECK(rt::reportResult("AfterBound",-999)==-999);CHECK(rt::records()==64);
}
'''


if __name__ == '__main__':
    unittest.main()
