"""Execute the original split-bias arithmetic and its R32F receiver adapter.

Host/instruction evidence does not establish Windows GPU image acceptance.
"""
import importlib.util
import io
import math
import os
from pathlib import Path
import random
import re
import shutil
import struct
import subprocess
import tempfile
import tarfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[1]


class DirectionalShadowTests(unittest.TestCase):
    def test_original_bias_and_receiver_depth_contract(self):
        client = os.environ.get('WOT_0922_CLIENT')
        if not client:
            self.skipTest('WOT_0922_CLIENT is required for the instruction oracle')
        try:
            import pefile
            from unicorn import Uc, UC_ARCH_X86, UC_MODE_32
            from unicorn.x86_const import UC_X86_REG_ESP, UC_X86_REG_FPCW, UC_X86_REG_MXCSR
        except ImportError:
            self.skipTest('pefile and Unicorn are required for the instruction oracle')
        pe = pefile.PE(str(Path(client) / 'WorldOfTanks.exe'))
        image = pe.get_memory_mapped_image()
        base = pe.OPTIONAL_HEADER.ImageBase
        # Pin the actual DX9 caller coefficients and precision branch, not
        # merely the inputs supplied to the arithmetic oracle below.
        setup = image[0x9cc42a - base:0x9cc444 - base]
        self.assertEqual(setup.count(struct.pack('<f', .7)), 2)
        self.assertEqual(setup.count(struct.pack('<f', 1.)), 1)
        self.assertTrue(setup.endswith(b'\x6a\x18'))
        uc = Uc(UC_ARCH_X86, UC_MODE_32)
        uc.mem_map(base, (len(image) + 4095) & ~4095)
        uc.mem_write(base, image)
        memory, stack = 0x30000000, 0x40000000
        uc.mem_map(memory, 0x10000)
        uc.mem_map(stack, 0x10000)
        # Store the native x87 return value at the same float boundary as 9cc475.
        uc.mem_write(memory + 0x1000, b'\xd9\x1d' + struct.pack('<I', memory + 0x2000))
        generator = random.Random(1513)
        cases = [(x, y, z, width, height) for width, height in ((512, 512), (1024, 1024), (2048, 1024))
                 for x, y, z in ((.01, .02, .001), (.1, .03, .004), (1., 1., 1.e-8))]
        cases.extend((10 ** generator.uniform(-4, 0), 10 ** generator.uniform(-4, 0),
                      10 ** generator.uniform(-8, -1), generator.choice((256, 512, 1024, 2048)),
                      generator.choice((256, 512, 1024, 2048))) for _ in range(256))
        cases = [(struct.unpack('<3f', struct.pack('<3f', *row[:3])) + row[3:]) for row in cases]
        expected = []
        for x, y, z, width, height in cases:
            projection = [0.] * 16
            projection[0], projection[5], projection[10] = x, y, z
            uc.mem_write(memory, struct.pack('<16f', *projection))
            uc.mem_write(memory + 0x100, struct.pack('<3f', .7, .7, 1.))
            uc.mem_write(stack + 0x8000, struct.pack('<6I', 0, memory, memory + 0x100, width, height, 24))
            uc.reg_write(UC_X86_REG_ESP, stack + 0x8000)
            uc.reg_write(UC_X86_REG_FPCW, 0x37f)
            uc.reg_write(UC_X86_REG_MXCSR, 0x1f80)
            uc.emu_start(0x9ceba0, 0x9cec63, count=100)
            uc.emu_start(memory + 0x1000, memory + 0x1006, count=1)
            expected.append(struct.unpack('<f', uc.mem_read(memory + 0x2000, 4))[0])
        host = r'''
#include "wot/stock_directional_shadow.hpp"
#include <cassert>
#include <iostream>
int main(){float x,y,z;unsigned width,height;
while(std::cin>>x>>y>>z>>width>>height){
float bias=wot::stock_directional_shadow_depth_bias(x,y,z,width,height);
std::cout.write(reinterpret_cast<char*>(&bias),sizeof(bias));
// One orthographic plane spans a shadow texel. The nearest sample may lie
// up to .7 texels closer than the receiver. Zero bias creates moving bands.
for(unsigned i=0;i<17;++i){
const double receiver=.4+double(i)*.0001;
const float texel=std::max(std::fabs(2.f/(width*x)),std::fabs(2.f/(height*y)))*z;
const double atlas=receiver-double(texel)*.6;
assert(receiver>atlas); // Negative control: unbiased front-face COLOR depth.
assert(!(receiver-bias>atlas));
assert(receiver-bias>receiver-double(bias)*2.); // A real blocker still casts a shadow.
// Original raster depth and the adapted COLOR comparison have the same sign.
assert((receiver>atlas+bias)==(receiver-bias>atlas));
}
}}
'''
        with tempfile.TemporaryDirectory(prefix='directional-shadow-oracle-') as temporary:
            root = Path(temporary)
            (root / 'probe.cpp').write_text(host)
            result = subprocess.run(['c++', '-std=c++14', '-ffp-contract=off', '-Wall', '-Wextra', '-Werror',
                                     '-I', str(ROOT / 'include'), str(root / 'probe.cpp'), '-o', str(root / 'probe')],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            result = subprocess.run([str(root / 'probe')], input=''.join(' '.join(map(str, row)) + '\n' for row in cases).encode(),
                                    capture_output=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            actual = struct.unpack('<%df' % len(cases), result.stdout)
            for row, original, ported in zip(cases, expected, actual):
                self.assertEqual(original, ported, (row, original, ported))
        self.assertGreater(expected[0], expected[3])
        self.assertTrue(all(math.isfinite(value) and value > 0 for value in actual))

    def test_actual_receiver_filter_tracks_sloped_planes_and_atlas_tiles(self):
        spec = importlib.util.spec_from_file_location('receiver_patch', ROOT / 'bigworld/patch_stock_deferred_buffers.py')
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        resources = Path(os.environ.get('WOT_PUBLIC_RESOURCES',
            '/Users/peng/.cache/wot-independent-client/builds/c4009598/client/engine-res'))
        if not resources.exists():
            self.skipTest('Pinned public shader resources are required')
        with tempfile.TemporaryDirectory(prefix='directional-plane-filter-') as temporary:
            root = Path(temporary)
            shutil.copytree(resources / 'shaders', root / 'shaders')
            module.patch_resources(root)
            shader = (root / 'shaders/shadow/dynamic_resolve.fx').read_text(encoding='latin1')
            helper = (root / 'shaders/shadow/stock_directional_shadow.fxh').read_text()
            self.assertEqual(helper, (ROOT / 'bigworld/stock_directional_shadow.fxh').read_text())
            self.assertLess(shader.index('float3 worldDx = ddx(wPos);'), shader.index('clip( g_splitCount - split );'))
            self.assertIn('mul(float4(worldDx, 0.f), g_textureMatrices[split])', shader)
            self.assertIn('mul(float4(worldDy, 0.f), g_textureMatrices[split])', shader)
            self.assertIn('depthGradient,\n\t\tsplit,', shader)
            begin = shader.index('float rotated_PCF_filter(')
            end = shader.index('//-- Sampling appropriate split.', begin)
            # Execute the real filter/helper arithmetic. Only sampler calls,
            # HLSL annotations and type syntax are replaced by a host fixture.
            kernel = shader[begin:end].replace('[unroll]', '').replace('sampler s', 'int s')
            kernel = kernel.replace('float2x2 rotation = {noise.r, -noise.g, noise.g, noise.r};',
                                    'float2x2 rotation = {noise.x, -noise.y, noise.y, noise.x};')
            kernel = kernel.replace('g_screen.xy', 'g_screen')
            kernel = kernel.replace('tex2D(noiseMapSampler, noiseUV).rg', 'sampleNoise(noiseUV)')
            kernel = kernel.replace('tex2D(s, uv).x', 'sampleAtlas(s, uv)')
            points = shader[shader.index('const static float2 poissonDisk[]'):shader.index('//-- do PCF filter')]
            host = r"""
#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>
using std::min;using std::max;
struct float2 {float x,y;float2(float a=0):x(a),y(a){}float2(float a,float b):x(a),y(b){}};
struct float3 {float x,y,z;};
float2 operator+(float2 a,float2 b){return {a.x+b.x,a.y+b.y};}
float2 operator-(float2 a,float2 b){return {a.x-b.x,a.y-b.y};}
float2 operator*(float2 a,float2 b){return {a.x*b.x,a.y*b.y};}
float2 operator/(float2 a,float2 b){return {a.x/b.x,a.y/b.y};}
float2 floor(float2 a){return {std::floor(a.x),std::floor(a.y)};}
float2 clamp(float2 a,float2 lo,float2 hi){return {std::max(lo.x,std::min(hi.x,a.x)),std::max(lo.y,std::min(hi.y,a.y))};}
float lerp(float a,float b,float x){return a+(b-a)*x;}
struct float2x2{float a,b,c,d;};
float2 mul(float2 v,float2x2 m){return {v.x*m.a+v.y*m.c,v.x*m.b+v.y*m.d};}
float g_dynamicShadowMapResolution,g_splitCount;float2 g_screen(1920,1080);
float2 currentUV,currentGradient;float currentDepth,bias=.0001f,blocker=0;
unsigned calls,negativeShadows;int currentSplit;float angle;bool checker=false;
float2 sampleNoise(float2){return {std::cos(angle),std::sin(angle)};}
float sampleAtlas(int,float2 uv){
++calls;const float width=g_dynamicShadowMapResolution*g_splitCount;
const float2 pixel= floor(uv*float2(width,g_dynamicShadowMapResolution));
assert(pixel.x>=currentSplit*g_dynamicShadowMapResolution);
assert(pixel.x<(currentSplit+1)*g_dynamicShadowMapResolution);
assert(pixel.y>=0&&pixel.y<g_dynamicShadowMapResolution);
const float2 centre=pixel/float2(width,g_dynamicShadowMapResolution);
const float2 delta=centre-currentUV;
const float occlusion=checker && (int(pixel.x+pixel.y)&1)?0.f:blocker;
const float depth=currentDepth+delta.x*currentGradient.x+delta.y*currentGradient.y-occlusion;
negativeShadows += currentDepth-bias>depth;
return depth;
}
@HELPER@
@POINTS@
@KERNEL@
int main(){
unsigned cases=0,negativeFailures=0;
for(unsigned resolution:{512u,1024u,2048u})for(unsigned count:{1u,3u,4u}){
g_dynamicShadowMapResolution=float(resolution);g_splitCount=float(count);
for(int split=0;split<int(count);++split)for(int samples:{1,4,8})
for(float sx:{-.8f,-.12f,0.f,.12f,.8f})for(float sy:{-.7f,0.f,.7f}){
currentSplit=split;
// Different, non-axis-aligned screen-space bases must recover the same plane.
currentGradient={sx*count,sy};
const float3 dx={.003f,-.001f,currentGradient.x*.003f-currentGradient.y*.001f};
const float3 dy={.002f,.004f,currentGradient.x*.002f+currentGradient.y*.004f};
const auto gradient=stockShadowDepthGradient(dx,dy);
assert(std::fabs(gradient.x-currentGradient.x)<.00001f);
assert(std::fabs(gradient.y-currentGradient.y)<.00001f);
for(unsigned phase=0;phase<29;++phase){
angle=float(phase)*.37f;
// Include the actual atlas borders as well as continuous subtexel motion.
const float local=phase==0?0.f:phase==28?1.f:.4f+phase/(29.f*resolution);
currentUV={(split+local)/count,phase==0?0.f:phase==28?1.f:.6f+phase/(31.f*resolution)};
currentDepth=.5f;calls=negativeShadows=0;blocker=0;
float coverage=rotated_PCF_filter(0,{.3f,.4f},currentUV,currentDepth-bias,gradient,split,samples);
assert(calls==unsigned(samples));assert(coverage==0.f);negativeFailures+=negativeShadows>0;
// A distinct closer caster remains a shadow at every slope and movement phase.
blocker=.01f;calls=0;
coverage=rotated_PCF_filter(0,{.3f,.4f},currentUV,currentDepth-bias,gradient,split,samples);
assert(calls==unsigned(samples));assert(coverage==1.f);
checker=true;calls=0;
coverage=rotated_PCF_filter(0,{.3f,.4f},currentUV,currentDepth-bias,gradient,split,samples);
assert(calls==unsigned(samples));assert(coverage>=0.f&&coverage<=1.f);
assert(coverage*samples==std::floor(coverage*samples));checker=false;
++cases;
}
}
}
assert(negativeFailures>1000);
const auto degenerate=stockShadowDepthGradient({0,0,1},{0,0,2});assert(degenerate.x==0&&degenerate.y==0);
std::cout<<cases<<" receiver plane/tile/sample cases; old fixed-depth comparison failed "<<negativeFailures<<" cases\n";
}
""".replace('@HELPER@', helper).replace('@POINTS@', points).replace('@KERNEL@', kernel)
            (root / 'probe.cpp').write_text(host)
            result = subprocess.run(['c++', '-std=c++14', '-Wall', '-Wextra', '-Werror',
                                     str(root / 'probe.cpp'), '-o', str(root / 'probe')], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            result = subprocess.run([str(root / 'probe')], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn('old fixed-depth comparison failed', result.stdout)

    def test_static_receiver_unions_depths_into_original_blue_channel(self):
        spec = importlib.util.spec_from_file_location('static_patch', ROOT / 'bigworld/patch_stock_deferred_buffers.py')
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        resources = Path(os.environ.get('WOT_PUBLIC_RESOURCES',
            '/Users/peng/.cache/wot-independent-client/builds/c4009598/client/engine-res'))
        if not resources.exists():self.skipTest('Pinned public shader resources required')
        with tempfile.TemporaryDirectory(prefix='static-shadow-channels-') as temporary:
            root=Path(temporary);shutil.copytree(resources/'shaders',root/'shaders');module.patch_resources(root)
            semi=(root/'shaders/shadow/semi_resolve.fx').read_text(encoding='latin1')
            helper=(root/'shaders/shadow/stock_directional_shadow.fxh').read_text()
            self.assertIn('COLORWRITEENABLE = BLUE;',semi)
            self.assertIn('return float4(0, 0, t.x, 0);',semi)
            self.assertNotIn('z > tex2D(',semi)
            legacy=(root/'shaders/std_effects/shadow_helpers.fxh').read_text(encoding='latin1')
            self.assertIn('stockShadowCombinedCoverage(shadows.r, shadows.b)',legacy)
            self.assertIn('#include "shaders/shadow/stock_directional_shadow.fxh"',legacy)
            self.assertNotIn('shadows.g',legacy)
            start=semi.index('float2 sampleShadowMapLinearPCF(')
            end=semi.index('//--------------------------------------------------------------------------------------------------',start)
            kernel=semi[start:end].split('#if 1',1)
            kernel=kernel[0]+kernel[1].split('#else',1)[0]+'\n}\n'
            kernel=kernel.replace('.rg','').replace('tc.xy','tc')
            host=r"""
#include <algorithm>
#include <cassert>
#include <cmath>
using std::min;using std::max;
struct float2{float x,y;float2(float a=0):x(a),y(a){}float2(float a,float b):x(a),y(b){}};
struct float3{float x,y,z;};
float2 operator+(float2 a,float2 b){return {a.x+b.x,a.y+b.y};}
float2 operator-(float2 a,float2 b){return {a.x-b.x,a.y-b.y};}
float2 operator*(float2 a,float2 b){return {a.x*b.x,a.y*b.y};}
float2 operator/(float2 a,float2 b){return {a.x/b.x,a.y/b.y};}
float2 floor(float2 a){return {std::floor(a.x),std::floor(a.y)};}
float2 frac(float2 a){return a-floor(a);}
float lerp(float a,float b,float t){return a+(b-a)*t;}
float2 lerp(float2 a,float2 b,float t){return {lerp(a.x,b.x,t),lerp(a.y,b.y,t)};}
float2 g_shadowMapSizeInv(1.f/16,1.f/16);
int g_shadowMapSmlLow=0,g_shadowMapSmlHeight=1;unsigned calls;
float2 depths[4];float2 tex2D(int,float2){return depths[calls++%4];}
@HELPER@
@KERNEL@
int main(){
for(int quality:{0,1})for(unsigned mask=0;mask<256;++mask){
for(unsigned i=0;i<4;++i)depths[i]=float2(mask&(1u<<(i*2))?.2f:.8f,mask&(1u<<(i*2+1))?.2f:.8f);
calls=0;const auto coverage=sampleShadowMapLinearPCF({.35f,.4f},.5f,quality);assert(calls==4);
const auto blend=frac(float2(-16,-16)*float2(.35f,.4f));
float expected[4];for(unsigned i=0;i<4;++i)expected[i]=.5f>min(depths[i].x,depths[i].y);
const float expectedCoverage=lerp(lerp(expected[0],expected[1],blend.x),lerp(expected[2],expected[3],blend.x),blend.y);
assert(std::fabs(coverage.x-expectedCoverage)<.000001f&&coverage.x==coverage.y);
assert(coverage.x>=0&&coverage.x<=1);
for(float dynamic:{0.f,.25f,.5f,1.f})assert(stockShadowCombinedCoverage(dynamic,coverage.x)==max(dynamic,expectedCoverage));
}
// Negative control: terrain-only occlusion previously went to GREEN while
// original SUN samples BLUE, so its illumination incorrectly remained one.
for(auto& value:depths)value={.2f,.8f};calls=0;
const auto terrain=sampleShadowMapLinearPCF({.3f,.4f},.5f,0);assert(terrain.x==1);
const float oldBlue=.5f>depths[0].y;assert(oldBlue==0);
assert(stockShadowCombinedCoverage(0,terrain.x)==1);
// Overlapping terrain/object occlusion is one shadow, never counted twice.
for(auto& value:depths)value={.2f,.2f};calls=0;
assert(sampleShadowMapLinearPCF({.3f,.4f},.5f,0).x==1);
}
""".replace('@HELPER@',helper).replace('@KERNEL@',kernel)
            (root/'probe.cpp').write_text(host)
            result=subprocess.run(['c++','-std=c++14','-Wall','-Wextra','-Werror',str(root/'probe.cpp'),'-o',str(root/'probe')],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stderr)
            result=subprocess.run([str(root/'probe')],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stderr)
            before={p.relative_to(root):p.read_bytes() for p in (root/'shaders').rglob('*') if p.is_file()}
            module.patch_resources(root)
            self.assertEqual(before,{p.relative_to(root):p.read_bytes() for p in (root/'shaders').rglob('*') if p.is_file()})

    def test_original_shadow_caster_writes_unbiased_colour(self):
        client = os.environ.get('WOT_0922_CLIENT')
        if not client:
            self.skipTest('WOT_0922_CLIENT is required for original caster bytecode')
        source = r'''
#include "wot/stock_effect_binding.hpp"
#include "wot/stock_shader_runtime.hpp"
#include <cassert>
#include <iostream>
#include <iterator>
int main(int argc,char**argv){std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(std::cin)),{});
auto effect=std::make_shared<wot::StockEffectBinary>();std::string error;
assert(wot::decode_stock_dx9_effect(bytes.data(),bytes.size(),*effect,error));
wot::StockEffectUniformPool pool;wot::StockEffectUniformContext context(pool);
wot::StockEffectBinding binding(effect,context);
for(unsigned i=0;i<effect->techniques.size();++i)if(effect->techniques[i].name==std::string(argc>1?argv[1]:"SHADOW")){
wot::StockDx9PassSelection selected;assert(binding.select_pass(i,0,selected,error));
if(argc>1){bool taa=false;for(const auto& u:effect->shaders[*selected.vertex_shader].uniforms)
if(u.first_register==4&&u.source_offset==32)taa=true;assert(taa);
bool semantic=false;for(const auto& u:effect->uniform_buffers[0].uniforms)
if(u.semantic=="TAAParams"&&u.byte_offset==32)semantic=true;assert(semantic);}
if(argc>2){std::vector<wot::StockDx9StateValue> states;
assert(wot::stock_dx9_render_states(effect->render_states[*selected.render_state],states,error));
bool blue=false;for(const auto& state:states)if(state.state==168)blue=state.value==4;assert(blue);}
if(argc>1&&std::string(argv[1])=="SUN_ST4__FL_ORIG"){
bool shadow=false;for(const auto& r:effect->shaders[*selected.pixel_shader].resources)if(r.kind==2&&r.shader_slot==4){
std::optional<unsigned> index;assert(wot::resolve_stock_effect_reference(*effect,binding.selection(),wot::StockEffectResourceTable::resource_view,r.resource_index,index,error));
shadow=index&&effect->resource_views[*index].name=="g_ssShadowMap";}assert(shadow);}
for(auto index:{selected.vertex_shader,selected.pixel_shader}){
assert(index);const auto& code=effect->shaders[*index].bytecode;unsigned count=code.size();
std::cout.write(reinterpret_cast<char*>(&count),4);
std::cout.write(reinterpret_cast<const char*>(code.data()),count*4);
}return 0;}return 2;}
'''
        def instructions(code):
            words = struct.unpack('<%dI' % (len(code) // 4), code)
            result, index = [], 1
            while index < len(words):
                opcode = words[index] & 65535
                count = ((words[index] >> 16) & 32767) if opcode == 65534 else ((words[index] >> 24) & 15)
                if opcode != 65534:
                    result.append((opcode, words[index + 1:index + count + 1]))
                index += count + 1
            return result
        with tempfile.TemporaryDirectory(prefix='original-shadow-caster-') as temporary:
            root = Path(temporary)
            (root / 'probe.cpp').write_text(source)
            command = ['c++', '-std=c++17', '-I', str(ROOT / 'include'), str(root / 'probe.cpp')]
            command += [str(ROOT / 'src' / name) for name in ('stock_shader_binary.cpp', 'stock_shader_runtime.cpp', 'stock_effect_binding.cpp')]
            subprocess.run(command + ['-o', str(root / 'probe')], check=True, capture_output=True)
            with zipfile.ZipFile(Path(client) / 'res/packages/shaders.pkg') as package:
                for name in ('PBS_tank', 'PBS_tank_old', 'normalmap'):
                    with zipfile.ZipFile(io.BytesIO(package.read('shaders/std_effects/' + name + '.10.dx9.fxo'))) as effect:
                        result = subprocess.run([str(root / 'probe')], input=effect.read('effect'), capture_output=True)
                    self.assertEqual(result.returncode, 0, result.stderr)
                    size = struct.unpack_from('<I', result.stdout)[0] * 4
                    vertex = instructions(result.stdout[4:4 + size])
                    pixel = instructions(result.stdout[8 + size:])
                    # MAD oPos.z, bias.x, clip.w, clip.z changes only hardware
                    # raster depth; TEXCOORD1 still carries the unbiased z.
                    self.assertIn((4, (0xe0040000, 0xa0000008, 0x80ff0000, 0x80aa0000)), vertex)
                    self.assertIn((5, (0xe00f0002, 0x80aa0000, 0xa0400009)), vertex)
                    # The original PS returns the unbiased TEXCOORD.w to COLOR0.
                    self.assertIn((1, (0x800f0800, 0x90ff0000)), pixel)
                with zipfile.ZipFile(io.BytesIO(package.read('shaders/shadow/dynamic_shadows.10.dx9.fxo'))) as effect:
                    result = subprocess.run([str(root / 'probe'), 'receive'], input=effect.read('effect'), capture_output=True)
                self.assertEqual(result.returncode, 0, result.stderr)
                size = struct.unpack_from('<I', result.stdout)[0] * 4
                vertex = instructions(result.stdout[4:4 + size])
                # Original VS0 removes the two corner differences times
                # TAAParams.xy from the ray; sampling UV stays independent.
                self.assertIn((5, (0x80070001, 0x80e40001, 0xa0550004)), vertex)
                self.assertIn((4, (0x80070000, 0x80e40000, 0xa0000004, 0x80e40001)), vertex)
                self.assertTrue(any(op == 2 and operands[0] == 0xe0070001 and operands[1] == 0x81e40000
                                    for op, operands in vertex))
                with zipfile.ZipFile(io.BytesIO(package.read('shaders/shadow/asm.10.dx9.fxo'))) as effect:
                    result = subprocess.run([str(root / 'probe'), 'receive', 'blue'], input=effect.read('effect'), capture_output=True)
                self.assertEqual(result.returncode, 0, result.stderr)
                with zipfile.ZipFile(io.BytesIO(package.read('shaders/std_effects/resolve_lighting.10.dx9.fxo'))) as effect:
                    result = subprocess.run([str(root / 'probe'), 'SUN_ST4__FL_ORIG'], input=effect.read('effect'), capture_output=True)
                self.assertEqual(result.returncode, 0, result.stderr)
                size = struct.unpack_from('<I', result.stdout)[0] * 4
                pixel = instructions(result.stdout[8 + size:])
                # Original SUN copies only R (dynamic) and B (static) after
                # sampling ShadowScreenSpaceMap at s4. GREEN is not terrain.
                self.assertIn((1, (0x80190001, 0x80a40005)), pixel)

    def test_actual_sdk_split_state_and_receiver_upload(self):
        spec = importlib.util.spec_from_file_location('split_sdk_patch', ROOT / 'bigworld/patch_stock_deferred_buffers.py')
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        archive = Path(os.environ.get('WOT_PUBLIC_ENGINE_ARCHIVE', '/tmp/wot-bigworld-engine-14.4.1.tar.gz'))
        if not archive.is_file():
            self.skipTest('Pinned public SDK archive is required')
        with tempfile.TemporaryDirectory(prefix='directional-split-owner-') as temporary:
            root = Path(temporary)
            files = set(re.findall(r"['\"]((?:lib|client)/[^'\"]+\.(?:cpp|hpp|ipp))['\"]",
                                   (ROOT / 'bigworld/patch_stock_deferred_buffers.py').read_text()))
            with tarfile.open(archive) as source:
                prefix = source.getmembers()[0].name.split('/')[0] + '/programming/bigworld/'
                members = {entry.name: entry for entry in source.getmembers()}
                for relative in files:
                    if prefix + relative not in members:
                        continue
                    path = root / relative
                    path.parent.mkdir(parents=True, exist_ok=True)
                    path.write_bytes(source.extractfile(members[prefix + relative]).read())
            module.apply(root)
            source = (root / 'lib/moo/dynamic_shadow.cpp').read_text()
            self.assertIn('newViewport.Width = m_dynamicShadowMapSize;', source)
            self.assertIn('newViewport.Height = m_dynamicShadowMapSize;', source)
            self.assertIn('m_dynamicShadowMapSize * m_shadowQuality.m_numSplit,', source)
            self.assertIn('m_stockShadowDepthBiases.clear();', source)
            self.assertIn('if (numSplit == 0 || m_shadowQuality.m_numSplit == numSplit', source)
            # Compile the actual producer/storage/upload statements, preserving
            # indexing and count instead of reproducing them in a test fake.
            assign = re.search(r'm_stockShadowDepthBiases\.assign[^;]+;', source).group()
            update = re.search(r'm_stockShadowDepthBiases\[i\] =[^;]+;', source).group()
            upload = re.search(r'effect->SetFloatArray\("stockShadowDepthBiases",[^;]+;', source).group()
            host = r'''
#include "wot/stock_directional_shadow.hpp"
#include <cassert>
#include <vector>
struct Projection{float p[3];float operator()(unsigned x,unsigned y)const{assert(x==y);return p[x];}};
struct Sink{std::vector<float> values;void SetFloatArray(const char*,const float* first,unsigned count){values.assign(first,first+count);}};
int main(){
struct{unsigned m_numSplit;}m_shadowQuality;std::vector<float> m_stockShadowDepthBiases;
unsigned m_dynamicShadowMapSize=1024;Sink sink;auto effect=&sink;
for(unsigned count:{4u,3u,1u,4u}){
m_shadowQuality.m_numSplit=count;@ASSIGN@
for(unsigned i=0;i<count;++i){assert(m_stockShadowDepthBiases[i]==0.f);
Projection proj={{.02f*(i+1),.03f*(i+1),.001f}};@UPDATE@}
@UPLOAD@
assert(sink.values.size()==count);
for(unsigned i=0;i<count;++i){const float expected=wot::stock_directional_shadow_depth_bias(.02f*(i+1),.03f*(i+1),.001f,1024,1024);
assert(sink.values[i]==expected);if(i)assert(sink.values[i]<sink.values[i-1]);}
// A single updated split must retain every other split's value.
const auto before=sink.values;unsigned i=count-1;Projection proj={{.013f,.071f,.004f}};
m_dynamicShadowMapSize=512;@UPDATE@ @UPLOAD@
for(unsigned j=0;j<i;++j)assert(sink.values[j]==before[j]);assert(sink.values[i]!=before[i]);
m_dynamicShadowMapSize=1024;
}
m_stockShadowDepthBiases.clear();assert(m_stockShadowDepthBiases.empty());
}
'''.replace('@ASSIGN@', assign).replace('@UPDATE@', update).replace('@UPLOAD@', upload)
            (root / 'probe.cpp').write_text(host)
            result = subprocess.run(['c++', '-std=c++14', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT / 'include'),
                                     str(root / 'probe.cpp'), '-o', str(root / 'probe')], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            result = subprocess.run([str(root / 'probe')], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)

    def test_patch_transfers_independent_split_biases_and_preserves_shaders(self):
        spec = importlib.util.spec_from_file_location('directional_patch', ROOT / 'bigworld/patch_stock_deferred_buffers.py')
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        resources = Path(os.environ.get('WOT_PUBLIC_RESOURCES',
            '/Users/peng/.cache/wot-independent-client/builds/c4009598/client/engine-res'))
        if not resources.exists():
            self.skipTest('Pinned public shader resources are required')
        with tempfile.TemporaryDirectory(prefix='directional-shadow-resources-') as temporary:
            root = Path(temporary)
            shutil.copytree(resources / 'shaders', root / 'shaders')
            module.patch_resources(root)
            shader = (root / 'shaders/shadow/dynamic_resolve.fx').read_text(encoding='latin1')
            self.assertEqual(shader.count('float stockShadowDepthBiases[MaxNumSplits];'), 1)
            self.assertIn('smTC.z - stockShadowDepthBiases[split],', shader)
            self.assertIn('g_textureMatrices[split]', shader)
            before = (root / 'shaders/shadow/dynamic_resolve.fx').read_bytes()
            module.patch_resources(root)
            self.assertEqual(before, (root / 'shaders/shadow/dynamic_resolve.fx').read_bytes())
        # Actual producer lines are exercised by the combined SDK patch-order
        # test; each value uses the current split's projection and tile size.
        patch = (ROOT / 'bigworld/patch_stock_deferred_buffers.py').read_text()
        self.assertIn('m_stockShadowDepthBiases[i] = wot::stock_directional_shadow_depth_bias(', patch)
        self.assertIn('proj(0, 0), proj(1, 1), proj(2, 2),', patch)
        self.assertIn('&m_stockShadowDepthBiases[0], m_shadowQuality.m_numSplit);', patch)


if __name__ == '__main__':
    unittest.main()
