"""Run the real HAL scene prologue after an original scene's stencil state."""
import contextlib
import io
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from native_test_compile_flags import native_test_compile_flags
from test_stock_cursor_camera import function
from test_stock_scaleform_scene import load_patch, pinned_sdk, SDK_FILES

ROOT = Path(__file__).resolve().parents[1]
HAL = 'Src/Render/D3D9/D3D9_HAL.cpp'


class ScaleformStencilTests(unittest.TestCase):
    def test_scene_masks_ignore_external_scene_and_previous_mask_state(self):
        compiler = shutil.which(os.environ.get('CXX', 'c++'))
        if not compiler:
            self.skipTest('A host C++ compiler is required')
        files = pinned_sdk(self)
        with tempfile.TemporaryDirectory(prefix='scaleform-stencil-') as temp:
            folder = Path(temp)
            for name, data in files.items():
                path = folder / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(data)
            with contextlib.redirect_stdout(io.StringIO()):
                load_patch().patch_sdk(folder)
            snapshots = {name: (folder / name).read_bytes() for name in SDK_FILES}
            with contextlib.redirect_stdout(io.StringIO()):
                load_patch().patch_sdk(folder)
            self.assertEqual(snapshots, {name: (folder / name).read_bytes() for name in SDK_FILES})
            for label, text in (('production', (folder / HAL).read_text()),
                                ('old-owner', files[HAL].decode().replace('\r\n', '\n'))):
                source = FIXTURE.replace('@BEGIN@', function(text, 'bool HAL::BeginScene()'))
                target = folder / (label + '.cpp')
                target.write_text(source)
                binary = folder / label
                subprocess.run([compiler, '-std=c++17', '-Wall', '-Wextra', '-Werror', '-UNDEBUG',
                                *native_test_compile_flags(compiler), '-I', str(ROOT / 'include'),
                                str(target), str(ROOT / 'src/stock_shader_runtime.cpp'),
                                str(ROOT / 'src/stock_shader_binary.cpp'), '-o', str(binary)], check=True)
                result = subprocess.run([str(binary)], capture_output=True, text=True)
                self.assertEqual(result.returncode, 0 if label == 'production' else 41,
                                 (label, result.returncode, result.stdout, result.stderr))


FIXTURE = r'''
#include "wot/stock_shader_runtime.hpp"
#include <cassert>
#include <map>
#include <string>
using DWORD=unsigned;
constexpr unsigned D3DRS_STENCILENABLE=52,D3DRS_STENCILWRITEMASK=59,
 D3DRS_ALPHABLENDENABLE=27,D3DRS_ALPHATESTENABLE=15,D3DRS_SCISSORTESTENABLE=174,
 D3DRS_ZWRITEENABLE=14,D3DRS_ZENABLE=7,D3DRS_COLORWRITEENABLE=168,D3DRS_CULLMODE=22,
 D3DRS_ZFUNC=23,D3DRS_FOGENABLE=28,D3DSAMP_MIPMAPLODBIAS=8,D3DSAMP_ELEMENTINDEX=12,
 D3DTSS_TEXCOORDINDEX=11,D3DTSS_COLOROP=1,D3DTOP_DISABLE=1,
 D3DCOLORWRITEENABLE_RED=1,D3DCOLORWRITEENABLE_GREEN=2,D3DCOLORWRITEENABLE_BLUE=4,
 D3DCOLORWRITEENABLE_ALPHA=8,D3DCULL_NONE=1,D3DCMP_ALWAYS=8,HALConfig_NoSceneCalls=1;
constexpr bool TRUE=true,FALSE=false;
#define ScopedRenderEvent int
#define GPUEvent(...) unused=0;(void)unused
namespace Render {struct HAL {bool valid=true;bool BeginScene(){return valid;}};}
struct Device {
 std::map<unsigned,unsigned> states;unsigned begins=0;
 void SetRenderState(unsigned name,unsigned value){states[name]=value;}
 void SetSamplerState(unsigned,unsigned,DWORD){}void SetTextureStageState(unsigned,unsigned,unsigned){}
 void BeginScene(){++begins;}
 // D3D's stencil replacement preserves bits outside STENCILWRITEMASK.
 unsigned writeMask(unsigned previous,unsigned reference){unsigned mask=states[D3DRS_STENCILWRITEMASK];return (previous&~mask)|(reference&mask);}
};
struct ShaderState {void BeginScene(){}};
struct HAL:Render::HAL {Device*pDevice;ShaderState SManager,ShaderData;unsigned VMCFlags=1;bool BeginScene();};
@BEGIN@
int main(){
 Device device;HAL hal;hal.pDevice=&device;
 std::vector<wot::StockDx9StateValue> values;std::string error;
 assert(wot::stock_dx9_render_states(wot::stock_dx9_default_render_descriptor(),values,error));
 for(const auto&v:values)device.SetRenderState(v.state,v.value);
 assert(device.states[D3DRS_STENCILWRITEMASK]==0); // Original ARIE scene prologue.
 if(!hal.BeginScene()||device.states[D3DRS_STENCILENABLE]||device.writeMask(0,1)!=1)return 41;
 for(unsigned mask:{0u,0x80u,0x0fu,0xffu})for(bool enabled:{false,true}){
  device.SetRenderState(D3DRS_STENCILENABLE,enabled);device.SetRenderState(D3DRS_STENCILWRITEMASK,mask);
  assert(hal.BeginScene());
  assert(device.states[D3DRS_STENCILENABLE]==0);
  // First clip clears stale bits and writes its reference. Nested masks can increment it.
  assert(device.writeMask(0x80,0)==0&&device.writeMask(0,1)==1&&device.writeMask(1,2)==2);
 }
 // A rejected scene must not change the external device state.
 hal.valid=false;device.SetRenderState(D3DRS_STENCILWRITEMASK,0);assert(!hal.BeginScene());
 assert(device.states[D3DRS_STENCILWRITEMASK]==0);
}
'''


if __name__ == '__main__':
    unittest.main()
