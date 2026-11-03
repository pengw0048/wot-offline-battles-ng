"""Execute the resource receiver against the #1513 fixed-kernel contract.

This checks host arithmetic and resource selection, not Windows GPU images.
"""
import importlib.util
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class DirectionalShadowKernelTests(unittest.TestCase):
    def test_fixed_kernel_preserves_caster_shape_under_screen_translation(self):
        resources = Path(os.environ.get('WOT_PUBLIC_RESOURCES',
            '/Users/peng/.cache/wot-independent-client/builds/c4009598/client/engine-res'))
        if not resources.is_dir():
            self.skipTest('Pinned public shader resources are required')
        spec = importlib.util.spec_from_file_location(
            'fixed_kernel_patch', ROOT / 'bigworld/patch_stock_deferred_buffers.py')
        patch = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(patch)
        with tempfile.TemporaryDirectory(prefix='directional-fixed-kernel-') as temporary:
            root = Path(temporary)
            shutil.copytree(resources / 'shaders', root / 'shaders')
            patch.patch_resources(root)
            shader = (root / 'shaders/shadow/dynamic_resolve.fx').read_text(encoding='latin1')
            begin = shader.index('float rotated_PCF_filter(')
            end = shader.index('//-- Sampling appropriate split.', begin)
            # Preserve the deployed function and helper. Replace only HLSL
            # syntax and sampler intrinsics to execute their actual arithmetic.
            kernel = shader[begin:end].replace('[unroll]', '').replace('sampler s', 'int s')
            kernel = kernel.replace('float2x2 rotation = {noise.r, -noise.g, noise.g, noise.r};',
                                    'float2x2 rotation = {noise.x, -noise.y, noise.y, noise.x};')
            kernel = kernel.replace('g_screen.xy', 'g_screen')
            kernel = kernel.replace('tex2D(noiseMapSampler, noiseUV).rg', 'sampleNoise(noiseUV)')
            kernel = re.sub(r'tex2D\(s, (uv\d*)\)\.x', r'sampleAtlas(s, \1)', kernel)
            points = shader[shader.index('const static float2 poissonDisk[]'):
                            shader.index('//-- do PCF filter')]
            helper = (root / 'shaders/shadow/stock_directional_shadow.fxh').read_text()
            host = r'''
#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>
using std::min;using std::max;
struct float2 {float x,y;float2(float a=0):x(a),y(a){}float2(float a,float b):x(a),y(b){}};
struct float3 {float x,y,z;};
struct float4 {float2 xy,zw;float4(float a=0,float b=0,float c=0,float d=0):xy(a,b),zw(c,d){}};
float2 operator+(float2 a,float2 b){return {a.x+b.x,a.y+b.y};}
float2 operator-(float2 a,float2 b){return {a.x-b.x,a.y-b.y};}
float2 operator*(float2 a,float2 b){return {a.x*b.x,a.y*b.y};}
float2 operator/(float2 a,float2 b){return {a.x/b.x,a.y/b.y};}
float2 floor(float2 a){return {std::floor(a.x),std::floor(a.y)};}
float2 clamp(float2 a,float2 lo,float2 hi){return {max(lo.x,min(hi.x,a.x)),max(lo.y,min(hi.y,a.y))};}
float lerp(float a,float b,float x){return a+(b-a)*x;}
struct float2x2 {float a,b,c,d;};
float2 mul(float2 v,float2x2 m){return {v.x*m.a+v.y*m.c,v.x*m.b+v.y*m.d};}
float g_dynamicShadowMapResolution,g_splitCount;
float2 g_screen(1920,1080),currentUV,currentGradient;
struct Tile{unsigned x,y,width,height;} tiles[4];
float2 physicalAtlas;
float4 stockShadowAtlasDimensions,stockShadowTileBounds[4];
int currentSplit,shape;unsigned calls,noiseCalls,canaryReads;bool darkCanary;
void layout(unsigned index){
const unsigned sizes[]={512,1024,2048},counts[]={1,3,4};
const unsigned resolution=index==9?1024:sizes[index/3],count=index==9?3:counts[index%3];
g_dynamicShadowMapResolution=float(resolution);g_splitCount=float(count);
physicalAtlas=index==9?float2(4096,2048):float2(resolution*count,resolution);
for(unsigned i=0;i<count;++i){
if(index==9)tiles[i]=i==0?Tile{0,0,2048,2048}:Tile{2048+(i-1)*1024,0,1024,1024};
else tiles[i]={i*resolution,0,resolution,resolution};
const auto tile=tiles[i];
stockShadowTileBounds[i]={tile.x/physicalAtlas.x,tile.y/physicalAtlas.y,
    (tile.x+tile.width)/physicalAtlas.x,(tile.y+tile.height)/physicalAtlas.y};
}
stockShadowAtlasDimensions={physicalAtlas.x,physicalAtlas.y,0,0};
}
float edgeX,edgeY;
float2 sampleNoise(float2 uv){
++noiseCalls;
const float angle=std::floor(uv.x*256)*.37f+std::floor(uv.y*256)*.71f;
return {std::cos(angle),std::sin(angle)};
}
float sampleAtlas(int,float2 uv){
++calls;
const float2 dimensions=physicalAtlas;
const auto pixel=floor(uv*dimensions);const auto tile=tiles[currentSplit];
// Adjacent cascade texels and unused lower atlas rows contain canary depth.
// They must never participate, even at the half-resolution cascade borders.
if(pixel.x<tile.x||pixel.x>=tile.x+tile.width||pixel.y<tile.y||pixel.y>=tile.y+tile.height){
++canaryReads;return darkCanary?.1f:.9f;
}
bool blocked=false;
if(shape==1)blocked=true;
if(shape==2)blocked=pixel.x>=edgeX;
// Thin aerials and a small lateral protrusion are represented by one-texel
// strips. Also cover diagonal edges rather than only symmetric straight ones.
if(shape==3)blocked=(pixel.x==edgeX&&std::fabs(pixel.y-edgeY)<4)||
    (pixel.y==edgeY&&pixel.x>=edgeX&&pixel.x<edgeX+3);
if(shape==4)blocked=pixel.x>=edgeX+.4f*(pixel.y-edgeY);
const auto raster=pixel/dimensions;
const auto delta=raster-currentUV;
return .5f+delta.x*currentGradient.x+delta.y*currentGradient.y-(blocked?.1f:0.f);
}
@HELPER@
@POINTS@
@KERNEL@
// Independent transcription of shader12's DEF constants and MAD order.
// Keep raw visibility weights: their sum is 1.0085240265. The original
// returns 1 - sum, then the UNORM destination clamps the final coverage.
float originalHighReference(){
const float offsets[9][2]={{1.2f,0},{0,0},{-1.2f,0},{0,1.2f},{0,-1.2f},
    {.84f,.84f},{-.84f,.84f},{-.84f,-.84f},{.84f,-.84f}};
const float weights[9]={.110858f,.120892f,.110858f,.110858f,.110858f,
    .11105f,.11105f,.11105f,.11105f};
const float2 dimensions=physicalAtlas;
const float2 texel=float2(1.f)/dimensions;const auto tile=tiles[currentSplit];
const float2 lower=(float2(tile.x,tile.y)+.5f)*texel;
const float2 upper=(float2(tile.x+tile.width,tile.y+tile.height)-.5f)*texel;
float sum=0;
for(int i=0;i<9;++i){
const auto uv=clamp(currentUV+float2(offsets[i][0],offsets[i][1])*(.9f*texel),lower,upper);
const auto grid=uv*dimensions-.5f;const auto base=floor(grid);const auto phase=grid-base;
float coverage[4];
for(int j=0;j<4;++j){
const auto point=clamp((base+float2(.5f+(j&1),.5f+(j>>1)))*texel,lower,upper);
// Analytical receiver plane; do not call the production depth helper.
const auto raster=floor(point*dimensions)/dimensions;
const float z=.5f-.0001f+(raster.x-currentUV.x)*currentGradient.x+
    (raster.y-currentUV.y)*currentGradient.y;
coverage[j]=z<=sampleAtlas(0,point);
}
sum+=weights[i]*lerp(lerp(coverage[0],coverage[1],phase.x),
    lerp(coverage[2],coverage[3],phase.x),phase.y);
}return 1.f-sum;
}
int main(){
unsigned cases=0,oldMovingShapes=0;float largestOldVariation=0;
for(unsigned index=0;index<10;++index){
layout(index);
for(int split=0;split<int(g_splitCount);++split)for(int mask=0;mask<5;++mask)
for(float slope:{-.7f,0.f,.7f})for(unsigned phase=0;phase<17;++phase){
currentSplit=split;shape=mask;currentGradient={slope*g_splitCount,.3f};
const auto tile=tiles[split];edgeX=float(tile.x+tile.width/2);edgeY=float(tile.y+tile.height/2);
currentUV={(edgeX-1.5f+phase*3.f/16)/physicalAtlas.x,(edgeY+.37f)/physicalAtlas.y};
const float expected=originalHighReference();float oldMin=2,oldMax=-1;
for(unsigned screen=0;screen<23;++screen){
const float2 screenUV(.1f+screen*.013f,.7f-screen*.017f);
calls=noiseCalls=canaryReads=0;
const float actual=rotated_PCF_filter(0,screenUV,currentUV,.5f-.0001f,currentGradient,split,9);
assert(calls==36);assert(noiseCalls==0);assert(canaryReads==0);
assert(std::fabs(actual-expected)<.000002f);
if(mask==0){assert(std::fabs(actual+.0085240265f)<.000001f);assert(max(actual,0.f)==0.f);}
if(mask==1)assert(actual==1.f);
// Execute the retained public receiver as the negative control, including
// its real noise lookup, eight offsets and four comparison interpolation.
const float old=rotated_PCF_filter(0,screenUV,currentUV,.5f-.0001f,currentGradient,split,8);
assert(noiseCalls>0);assert(canaryReads==0);oldMin=min(oldMin,old);oldMax=max(oldMax,old);
}
if(mask>=2&&oldMax-oldMin>.01f)++oldMovingShapes;
largestOldVariation=max(largestOldVariation,oldMax-oldMin);++cases;
}
}
// Exercise all edges/corners of the actual 4096x2048 atlas: a 2048 square
// followed by two 1024 squares, with unused lower rows behind the far tiles.
// Swapping every adjacent/unused texel from lit to shadow must not alter output.
layout(9);unsigned canaryCases=0;
for(int split=0;split<3;++split)for(int mask:{0,1,3,4})
for(float px:{0.f,.25f,.5f,1.f})for(float py:{0.f,.25f,.5f,1.f}){
currentSplit=split;shape=mask;currentGradient={-.7f,.3f};const auto tile=tiles[split];
currentUV={(tile.x+px*tile.width)/physicalAtlas.x,(tile.y+py*tile.height)/physicalAtlas.y};
edgeX=float(tile.x);edgeY=float(tile.y);float previous=0;
for(int dark=0;dark<2;++dark){
darkCanary=dark!=0;calls=noiseCalls=canaryReads=0;
const float actual=rotated_PCF_filter(0,{.5f,.5f},currentUV,.5f-.0001f,currentGradient,split,9);
assert(calls==36&&noiseCalls==0&&canaryReads==0);
assert(std::fabs(actual-originalHighReference())<.000002f);assert(canaryReads==0);
if(dark)assert(actual==previous);previous=actual;
}++canaryCases;
}
assert(oldMovingShapes>1000);assert(largestOldVariation>.1f);
std::cout<<canaryCases<<" mixed-resolution edge/corner canary cases; no adjacent tile reads\n";
std::cout<<cases<<" fixed-kernel plane/mask/atlas cases; screen-dependent public receiver changed "
    <<oldMovingShapes<<" caster samples, max variation="<<largestOldVariation<<"\n";
}
'''.replace('@HELPER@', helper).replace('@POINTS@', points).replace('@KERNEL@', kernel)
            (root / 'probe.cpp').write_text(host)
            result = subprocess.run(['c++', '-std=c++14', '-O2', '-ffp-contract=off',
                                     '-Wall', '-Wextra', '-Werror', str(root / 'probe.cpp'),
                                     '-o', str(root / 'probe')], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            result = subprocess.run([str(root / 'probe')], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn('screen-dependent public receiver changed', result.stdout)


if __name__ == '__main__':
    unittest.main()
