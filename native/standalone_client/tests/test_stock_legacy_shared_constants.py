"""Execute public Moo semantic registration and shadow reconstruction on the host.

This reproduces missing legacy camera producers, not Windows GPU acceptance.
"""
import importlib.util
import os
from pathlib import Path
import re
import shutil
import subprocess
import tarfile
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('legacy_shared_patch', ROOT / 'bigworld/patch_stock_effect_owners.py')
PATCH = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PATCH)


def function(source, signature):
    start = source.index(signature)
    end = source.index('{', start)
    depth = 1
    while depth:
        end += 1
        depth += (source[end] == '{') - (source[end] == '}')
    return source[start:end + 1]


def sdk_file(archive, relative):
    suffix = '/programming/bigworld/' + relative
    member = next(name for name in archive.getnames() if name.endswith(suffix))
    return archive.extractfile(member).read().decode('utf-8-sig')


HOST = r'''
#include <array>
#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>
#include <map>
#include <string>
#include <vector>
#include "wot/stock_antialiasing.hpp"
#define BW_GUARD
#define ERROR_MSG(...) assert(false)
using UINT=unsigned;using uint32=unsigned;using BOOL=int;
constexpr int FALSE=0,TRUE=1;
namespace BW {using string=std::string;}
struct Vector2 {float x=0,y=0;Vector2()=default;Vector2(float a,float b):x(a),y(b){};};
Vector2 operator*(Vector2 a,Vector2 b){return {a.x*b.x,a.y*b.y};}
Vector2 operator*(float scale,Vector2 value){return {scale*value.x,scale*value.y};}
Vector2 operator+(Vector2 a,Vector2 b){return {a.x+b.x,a.y+b.y};}
Vector2& operator-=(Vector2& a,Vector2 b){a.x-=b.x;a.y-=b.y;return a;}
struct Vector3 {float x=0,y=0,z=0;Vector3()=default;Vector3(float a,float b,float c):x(a),y(b),z(c){};
float length()const{return std::sqrt(x*x+y*y+z*z);}};
Vector3 operator+(Vector3 a,Vector3 b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
Vector3 operator*(Vector3 a,float b){return {a.x*b,a.y*b,a.z*b};}
Vector3 operator*(float b,Vector3 a){return a*b;}
Vector3 lerp(Vector3 a,Vector3 b,float t){return a*(1-t)+b*t;}
struct Vector4 {float x=0,y=0,z=0,w=0;Vector4()=default;
Vector4(float a,float b,float c,float d):x(a),y(b),z(c),w(d){}
Vector4(Vector3 a,float b):x(a.x),y(a.y),z(a.z),w(b){}
Vector3 xyz()const{return {x,y,z};}Vector2 xy()const{return {x,y};}Vector2 zw()const{return {z,w};}};
Vector4 lerp(Vector4 a,Vector4 b,float t){return {a.x*(1-t)+b.x*t,a.y*(1-t)+b.y*t,a.z*(1-t)+b.z*t,a.w*(1-t)+b.w*t};}
struct Matrix {Vector4 rows[4];Matrix()=default;Matrix(Vector4 a,Vector4 b,Vector4 c,Vector4 d):rows{a,b,c,d}{}
Vector4 row(unsigned i)const{return rows[i];}Vector4 operator[](unsigned i)const{return rows[i];}
Vector3 applyVector(Vector3 a)const{return a;}Vector3 applyPoint(Vector4 a)const{return a.xyz();}};
struct SunLight {Vector3 m_dir;Vector4 m_color,m_ambient;};
struct Parameter {std::string name,semantic;bool shared=true,used=true,artist=false;
std::vector<unsigned char> bytes;void* texture=nullptr;unsigned writes=0;};
using D3DXHANDLE=Parameter*;
struct D3DXEFFECT_DESC {unsigned Parameters=0,Techniques=1;};
struct D3DXPARAMETER_DESC {const char* Name;const char* Semantic;};
struct ID3DXEffect {bool stock=false;std::vector<Parameter> parameters;
void GetDesc(D3DXEFFECT_DESC* d){d->Parameters=parameters.size();}
D3DXHANDLE GetParameter(void*,unsigned i){return &parameters.at(i);}
void GetParameterDesc(D3DXHANDLE p,D3DXPARAMETER_DESC* d){d->Name=p->name.c_str();d->Semantic=p->semantic.empty()?nullptr:p->semantic.c_str();}
D3DXHANDLE GetAnnotationByName(D3DXHANDLE p,const char* n){return (!std::strcmp(n,"isShared")&&p->shared)||(!std::strcmp(n,"artistEditable")&&p->artist)?p:nullptr;}
void GetBool(D3DXHANDLE,BOOL* v){*v=TRUE;}
D3DXHANDLE GetTechnique(unsigned){return parameters.data();}
bool IsParameterUsed(D3DXHANDLE p,D3DXHANDLE){return p->used;}
int SetValue(D3DXHANDLE p,const void* v,unsigned bytes){p->bytes.assign((const unsigned char*)v,(const unsigned char*)v+bytes);++p->writes;return 0;}
int SetVector(D3DXHANDLE p,const Vector4* v){return SetValue(p,v,sizeof(*v));}
int SetMatrix(D3DXHANDLE p,const Matrix* v){return SetValue(p,v,sizeof(*v));}
int SetTexture(D3DXHANDLE p,void* v){p->texture=v;++p->writes;return 0;}
Parameter& named(const char* n){for(auto& p:parameters)if(p.name==n)return p;assert(false);return parameters[0];}
};
template<class T>struct ComObjectWrap {T* p;ComObjectWrap(T* v):p(v){}T* operator->()const{return p;}T* pComObject()const{return p;}};
namespace wot {bool is_stock_effect(ID3DXEffect* effect){return effect->stock;}}
struct EffectConstantValue {virtual bool operator()(ID3DXEffect*,D3DXHANDLE)=0;virtual ~EffectConstantValue()=default;};
struct EffectConstantValuePtr {EffectConstantValue* p=nullptr;EffectConstantValue* getObject(){return p;}};
struct Camera {float far=1000,near=.5f;float farPlane()const{return far;}float nearPlane()const{return near;}
Vector3 nearPlanePoint(float x,float y)const{return {x*near,y*near,near};}};
struct Context {Matrix view;Camera cam;SunLight sun;
std::map<std::string,EffectConstantValuePtr> mappings;
EffectConstantValuePtr* getMapping(const char* name){return &mappings[name];}
const SunLight& sunLight()const{return sun;}const Matrix& invWorld()const{return view;}};
Context visual;
struct RenderContext {Context& effectVisualContext(){return visual;}const Matrix& invView(){return visual.view;}const Camera& camera(){return visual.cam;}};
RenderContext render;RenderContext& rc(){return render;}
namespace Moo {using EffectConstantValue=::EffectConstantValue;using Camera=::Camera;using ::rc;}
struct DeferredPipeline {void* texture=nullptr;void* gbufferTexture(int)const{return texture;}};
@PROVIDERS@
struct EffectPropertyFactory {static EffectPropertyFactory* findFactory(D3DXPARAMETER_DESC*){return nullptr;}int create(const char*,D3DXHANDLE,ID3DXEffect*){return 0;}};
using EffectPropertyFactoryPtr=EffectPropertyFactory*;
@HELPERS@
struct ManagedEffect {bool isEffectPool_=false;std::string resourceID_;ID3DXEffect* effect=nullptr;
std::map<std::string,int> defaultProperties_;
using MappedConstants=std::vector<std::pair<D3DXHANDLE,EffectConstantValuePtr*>>;
MappedConstants mappedConstants_;
ID3DXEffect* pEffect(){return effect;}
bool load(const BW::string&);bool reload(){ID3DXEffect* pEffect=effect;@REGISTRATION@ return true;}
void setAutoConstants();};
@LOAD@
@SET_CONSTANTS@
namespace negative_control {
struct ManagedEffect : ::ManagedEffect {bool reload(){ID3DXEffect* pEffect=effect;@ORIGINAL_REGISTRATION@ return true;}};
}
// The public shadow passes both consume this real HLSL function. Adapt only
// HLSL type/swizzle syntax; keep reconstruction arithmetic unchanged.
Vector4 g_cameraPos,g_farPlane,stockSceneDRR(1,1,0,0),stockSceneTAA;Matrix g_cameraDirs;
int stockLinearDepthSampler=0;float sampleDepth=.02f;
Vector2 sampledDepthUV;Vector4 tex2D(int,Vector2 uv){sampledDepthUV=uv;return {sampleDepth,0,0,0};}
Vector2 stereoToMonoUV(Vector2 uv,float){return uv;}
@WORLD_POSITION@
@OLD_PIXEL_POSITION@
Vector3 reconstruct(ID3DXEffect& effect,Vector2 uv){
const auto assign=[&](const char* name,void* value,unsigned size){auto& p=effect.named(name);std::memset(value,0,size);if(!p.bytes.empty()){assert(p.bytes.size()==size);std::memcpy(value,p.bytes.data(),size);}};
assign("g_cameraPos",&g_cameraPos,sizeof(g_cameraPos));assign("g_farPlane",&g_farPlane,sizeof(g_farPlane));assign("g_cameraDirs",&g_cameraDirs,sizeof(g_cameraDirs));
return g_buffer_readWorldPos(uv,false);
}
ID3DXEffect makeEffect(bool stock=false){ID3DXEffect e;e.stock=stock;
for(const auto& item:std::vector<std::pair<std::string,std::string>>{
{"g_cameraPos","CameraPos"},{"g_cameraDirs","CameraDirs"},{"g_farPlane","FarPlane"},{"g_sunLight","SunLight"},{"g_bufferTex","GBufferChannel0"}})e.parameters.push_back({item.first,item.second,true,true,false,{},nullptr,0});
e.parameters.push_back({"nonshared","CameraPos",false,true,false,{},nullptr,0});
e.parameters.push_back({"unused","CameraPos",true,false,false,{},nullptr,0});
e.parameters.push_back({"noSemantic","",false,true,false,{},nullptr,0});return e;}
bool close(float a,float b){return std::fabs(a-b)<.0001f;}
int main(){
CameraPosConstant position;CameraDirsConstant dirs;FarPlaneConstant far;SunLightConstant sun;DeferredPipeline pipeline;GBufferChannelConstant texture(0,pipeline);
visual.mappings["CameraPos"].p=&position;visual.mappings["CameraDirs"].p=&dirs;visual.mappings["FarPlane"].p=&far;visual.mappings["SunLight"].p=&sun;visual.mappings["GBufferChannel0"].p=&texture;
visual.view.rows[3]={37,9,-21,1};visual.sun={{0,-1,0},{1,.8f,.6f,1},{.1f,.2f,.3f,1}};int firstTexture=1,secondTexture=2;pipeline.texture=&firstTexture;
auto effect=makeEffect();ManagedEffect fixed;fixed.effect=&effect;assert(fixed.load("shadow/dynamic_resolve.fx"));assert(fixed.mappedConstants_.size()==6);fixed.setAutoConstants();
for(unsigned i=0;i<5;++i)assert(effect.parameters[i].writes==1);assert(effect.parameters[4].texture==&firstTexture);assert(effect.named("nonshared").writes==1);assert(effect.named("unused").writes==0);assert(effect.named("noSemantic").writes==0);
assert(effect.named("g_sunLight").bytes.size()==sizeof(SunLight));SunLight written;std::memcpy(&written,effect.named("g_sunLight").bytes.data(),sizeof(written));assert(written.m_color.x==1&&written.m_ambient.z==.3f);
const Vector3 left=reconstruct(effect,{0,0}),right=reconstruct(effect,{1,1});assert(left.x<37&&right.x>37&&left.y>9&&right.y<9);assert(close(left.z,-1)&&close(right.z,-1));
// Negative control executes the unpatched reload registration. Shared
// camera inputs remain zero and collapse every shadow lookup to one point.
auto brokenEffect=makeEffect();negative_control::ManagedEffect broken;broken.effect=&brokenEffect;assert(broken.reload());assert(broken.mappedConstants_.size()==1);broken.setAutoConstants();
const auto brokenLeft=reconstruct(brokenEffect,{0,0}),brokenRight=reconstruct(brokenEffect,{1,1});assert(brokenLeft.x==0&&brokenLeft.y==0&&brokenLeft.z==0&&brokenRight.x==0&&brokenRight.y==0&&brokenRight.z==0);assert(brokenEffect.named("g_bufferTex").texture==nullptr);
// View and resource changes use the same registered producer pointers.
visual.view.rows[3]={41,12,-18,1};visual.cam.far=500;pipeline.texture=&secondTexture;visual.sun.m_color.x=.4f;fixed.setAutoConstants();
const auto moved=reconstruct(effect,{.5f,.5f});assert(close(moved.x,41)&&close(moved.y,12)&&close(moved.z,-8));assert(effect.named("g_bufferTex").texture==&secondTexture);for(unsigned i=0;i<5;++i)assert(effect.parameters[i].writes==2);
// TAA moves the raster UV; the unchanged CameraDirs producer must recover
// the same world point at every phase. Depth keeps the jittered UV and DRR.
for(float width:{1280.f,1920.f,3840.f})for(float height:{720.f,1080.f})
for(float scale:{.5f,.75f,1.f})for(unsigned samples:{1u,2u,3u,4u,8u}){
stockSceneDRR=Vector4(scale,scale,0,0);stockSceneTAA=Vector4();
const auto fixedPoint=reconstruct(effect,{.3f,.7f});wot::StockTemporalAAFrame frame;
for(unsigned phase=0;phase<19;++phase){frame.advance(samples,true,false);
stockSceneTAA=Vector4(frame.jitterX/(width*scale),frame.jitterY/(height*scale),1.f/(width*scale),1.f/(height*scale));
const Vector2 raster(.3f+stockSceneTAA.x+.5f*stockSceneTAA.z,.7f+stockSceneTAA.y+.5f*stockSceneTAA.w);
const auto point=reconstruct(effect,raster);
assert(close(point.x,fixedPoint.x)&&close(point.y,fixedPoint.y)&&close(point.z,fixedPoint.z));
const auto wrongPixelRay=g_buffer_readWorldPosOldHalf(raster,false);
assert(std::fabs(wrongPixelRay.x-fixedPoint.x)>.00001f||std::fabs(wrongPixelRay.y-fixedPoint.y)>.00001f);
assert(close(sampledDepthUV.x,raster.x*scale)&&close(sampledDepthUV.y,raster.y*scale));
}}
stockSceneTAA=Vector4();stockSceneDRR=Vector4(1,1,0,0);
// ARIE shared constants remain solely owned by UniformContext; legacy
// pools retain their previous branch, including unused shared parameters.
auto arieEffect=makeEffect(true);ManagedEffect arie;arie.effect=&arieEffect;assert(arie.load("original.fx"));assert(arie.mappedConstants_.size()==1);arie.setAutoConstants();for(unsigned i=0;i<5;++i)assert(arieEffect.parameters[i].writes==0);assert(arieEffect.named("nonshared").writes==1);
auto poolEffect=makeEffect();ManagedEffect pool;pool.isEffectPool_=true;pool.effect=&poolEffect;assert(pool.load("legacy_pool.fx"));assert(pool.mappedConstants_.size()==7);pool.setAutoConstants();assert(poolEffect.named("unused").writes==1);
// Reload clears registrations instead of accumulating duplicate producers.
assert(fixed.reload()&&fixed.mappedConstants_.size()==6);fixed.setAutoConstants();assert(effect.named("g_cameraPos").writes==3);
std::cout<<"Legacy shared producers: live camera/sun/textures, shadow reconstruction negative control, ARIE isolation and reload passed\n";
}
'''


class LegacySharedConstantsTests(unittest.TestCase):
    def test_patch_is_idempotent_and_rejects_unknown_registration(self):
        original = '\n'.join(('\t\t\t//-- If the parameter has a Semantic, set it up as an automatic constant, but if',
            '\t\t\t//--     it is also shared skip it because shared constants are set by effect pool',
            '\t\t\t//--\t mechanism only once per frame for all effect files.',
            '\t\t\tif (!isShared(pEffect, param) || isEffectPool_)'))
        changed = PATCH.patch_legacy_shared_constants(original)
        self.assertEqual(PATCH.patch_legacy_shared_constants(changed), changed)
        for invalid in ('unknown', original + original):
            with self.assertRaises(RuntimeError):
                PATCH.patch_legacy_shared_constants(invalid)

    def test_actual_reload_producers_and_public_shadow_reconstruction(self):
        archive = Path(os.environ.get('WOT_PUBLIC_ENGINE_ARCHIVE', '/tmp/wot-bigworld-engine-14.4.1.tar.gz'))
        resources = Path(os.environ.get('WOT_PUBLIC_RESOURCES', '/Users/peng/.cache/wot-independent-client/builds/c4009598/client/engine-res'))
        if not archive.is_file() or not shutil.which('c++') or not resources.is_dir():
            self.skipTest('Pinned public engine source, resources and C++ compiler are required')
        with tarfile.open(archive) as source:
            managed = sdk_file(source, 'lib/moo/managed_effect.cpp')
            visual = sdk_file(source, 'lib/moo/effect_visual_context.cpp')
            pipeline = sdk_file(source, 'lib/moo/deferred_pipeline.cpp')
        patched = PATCH.patch_legacy_shared_constants(managed)
        # Execute the actual parameter enumeration from ManagedEffect::reload,
        # reached through its unchanged public load method.
        def registration(text):
            start = text.index('\tD3DXEFFECT_DESC effectDesc;', text.index('bool ManagedEffect::reload()'))
            end = text.index('\n\tif (pEffect_ != NULL)', start)
            return text[start:end]
        helpers = '\n'.join(function(managed, signature) for signature in (
            'static bool artistEditable(', 'static bool isShared(', 'bool isEffectParameterUsed('))
        providers = '\n'.join(function(visual, 'class ' + name) + ';' for name in (
            'CameraPosConstant', 'CameraDirsConstant', 'FarPlaneConstant', 'SunLightConstant'))
        providers += '\n' + function(pipeline, 'class GBufferChannelConstant') + ';'
        with tempfile.TemporaryDirectory(prefix='legacy-shared-producers-') as directory:
            root = Path(directory)
            resource_patch_spec = importlib.util.spec_from_file_location('legacy_depth_resources', ROOT/'bigworld/patch_stock_deferred_buffers.py')
            resource_patch = importlib.util.module_from_spec(resource_patch_spec)
            resource_patch_spec.loader.exec_module(resource_patch)
            shutil.copytree(resources/'shaders', root/'shaders')
            resource_patch.patch_resources(root)
            shared = (root/'shaders/std_effects/shared_constants.fxh').read_text(encoding='latin1')
            for name, semantic in (('g_cameraPos','CameraPos'), ('g_cameraDirs','CameraDirs'), ('g_farPlane','FarPlane'), ('g_sunLight','SunLight')):
                self.assertRegex(shared, r'shared\s+\w+\s+' + name + r'\s*:\s*' + semantic + r'\s+BW_SHARED')
            self.assertIn('#define BW_SHARED <bool isShared = true;>', shared)
            for name in ('dynamic_resolve.fx', 'semi_resolve.fx'):
                shader = (root/'shaders/shadow'/name).read_text(encoding='latin1')
                self.assertIn('#include "read_g_buffer.fxh"', shader)
                self.assertIn('g_buffer_readWorldPos(i.tc, g_nvStereoParams.w)', shader)
            reader = (root/'shaders/std_effects/read_g_buffer.fxh').read_text(encoding='latin1')
            reconstruction = function(reader, 'float3 g_buffer_readWorldPos(')
            reconstruction = reconstruction.split('#if 1', 1)[0] + reconstruction.split('#if 1', 1)[1].split('#else', 1)[0] + '\n}'
            reconstruction = re.sub(r'\bin\s+', '', reconstruction)
            reconstruction = reconstruction.replace('float3', 'Vector3').replace('float2', 'Vector2')
            reconstruction = re.sub(r'\.(xyz|xy|zw)\b', r'.\1()', reconstruction)
            reconstruction = reconstruction.replace(').r ', ').x ')
            source = HOST.replace('@PROVIDERS@', providers).replace('@HELPERS@', helpers)
            source = source.replace('@REGISTRATION@', registration(patched)).replace('@ORIGINAL_REGISTRATION@', registration(managed))
            source = source.replace('@LOAD@', function(managed, 'bool ManagedEffect::load('))
            source = source.replace('@SET_CONSTANTS@', function(managed, 'void ManagedEffect::setAutoConstants()').replace('pEffect_.pComObject()', 'pEffect()'))
            source = source.replace('@WORLD_POSITION@', reconstruction)
            old_pixel = reconstruction.replace('g_buffer_readWorldPos(', 'g_buffer_readWorldPosOldHalf(', 1)
            self.assertIn('uv -= stockSceneTAA.xy() + .5f * stockSceneTAA.zw();', old_pixel)
            old_pixel = old_pixel.replace('uv -= stockSceneTAA.xy() + .5f * stockSceneTAA.zw();',
                                          'uv -= stockSceneTAA.xy();')
            source = source.replace('@OLD_PIXEL_POSITION@', old_pixel)
            (root/'probe.cpp').write_text(source)
            result = subprocess.run(['c++','-std=c++14','-Wall','-Wextra','-Werror','-Wno-reorder','-I',str(ROOT/'include'),str(root/'probe.cpp'),'-o',str(root/'probe')], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(root/'probe')], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn('shadow reconstruction negative control', result.stdout)


if __name__ == '__main__':
    unittest.main()
