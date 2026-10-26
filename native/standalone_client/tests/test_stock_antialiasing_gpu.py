"""Exercise the actual native FXAA owner with strict target/binding lifetimes."""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


HOST = r'''
#include "wot/stock_antialiasing.hpp"
#include "wot/stock_dynamic_resolution.hpp"
#include <cassert>
#include <algorithm>
#include <map>
#include <memory>
#include <string>
#include <vector>
#define BW_BEGIN_NAMESPACE
#define BW_END_NAMESPACE
#define FAILED(x) ((x)<0)
#define SUCCEEDED(x) ((x)>=0)
using uint=unsigned;
using D3DFORMAT=unsigned;
const unsigned D3DFMT_A8R8G8B8=21,D3DFMT_G16R16F=112,D3DFMT_A16B16G16R16F=113;
struct Vector4{float x=0,y=0,z=0,w=0;Vector4(){}Vector4(float a,float b,float c,float d):x(a),y(b),z(c),w(d){}void setZero(){x=y=z=w=0;}};
struct Matrix{float v[16]={};Matrix(){setIdentity();}void setIdentity(){for(unsigned i=0;i<16;++i)v[i]=i%5==0?1.f:0.f;}
 float&operator()(unsigned row,unsigned column){return v[row*4+column];}
 void setTranslate(float x,float y,float z){setIdentity();v[12]=x;v[13]=y;v[14]=z;}
 void postMultiply(const Matrix&b){Matrix a=*this;for(unsigned i=0;i<4;++i)for(unsigned j=0;j<4;++j){v[i*4+j]=0;for(unsigned k=0;k<4;++k)v[i*4+j]+=a.v[i*4+k]*b.v[k*4+j];}}};
namespace DX{struct BaseTexture{int references=0;};struct Texture:BaseTexture{};struct Surface{BaseTexture*texture=nullptr;};struct Viewport{unsigned X,Y,Width,Height;float MinZ,MaxZ;};}
using D3DXHANDLE=const char*;
static int targets=0, selections=0, draws=0, failDraw=0, copyCalls=0;
static std::vector<std::string> paths,events;
template<class T>class Ptr{std::shared_ptr<T> value_;public:
 Ptr(){} explicit Ptr(T*p):value_(p){} Ptr&operator=(T*p){value_.reset(p);return *this;}
 explicit operator bool()const{return bool(value_);}T*operator->()const{return value_.get();}
 T*getObject()const{return value_.get();}T&operator*()const{return *value_;}};
template<class T>struct ComObjectWrap{T*pointer=nullptr;operator bool()const{return pointer!=nullptr;}T*pComObject()const{return pointer;}ComObjectWrap&operator=(T*p){pointer=p;return *this;}};
struct ID3DXEffect{bool definition=false;DX::BaseTexture* texture=nullptr;
 std::map<std::string,DX::BaseTexture*>textures;std::map<std::string,std::vector<float>> values;
 int SetTexture(const char*name,DX::BaseTexture*next){assert(!definition);auto& old=textures[name];if(old)--old->references;old=next;texture=next;if(next)++next->references;return 0;}
 int SetInt(const char*name,int value){if(std::string(name)=="g_lowQuality")return -1;values[name]={float(value)};return 0;}
 int SetBool(const char*name,bool value){values[name]={float(value)};return 0;}
 int SetVector(const char*name,const Vector4* value){values[name]={value->x,value->y,value->z,value->w};return 0;}
 int SetFloatArray(const char*name,const float*value,unsigned count){values[name]={value,value+count};return 0;}
 ~ID3DXEffect(){for(auto&item:textures)if(item.second)--item.second->references;}};
namespace wot{bool is_stock_effect(ID3DXEffect*){return true;}}
namespace Moo{
struct ManagedEffect{ID3DXEffect definition,binding;ID3DXEffect*active;
 ManagedEffect():active(&definition){definition.definition=true;}ID3DXEffect*pEffect(){return active;}};
struct Properties{ManagedEffect*owner;ID3DXEffect*existing=nullptr;
 bool select(ManagedEffect&effect){++selections;owner=&effect;existing=&effect.binding;effect.active=existing;return true;}
 ID3DXEffect*existingStockBinding(){return existing;}};
struct EffectMaterial{ManagedEffect effect;Properties props;std::string technique;
 bool setTechnique(const char*name){technique=name;return true;}
 ManagedEffect*pEffect(){return &effect;}Properties&properties(){return props;}};
using EffectMaterialPtr=Ptr<EffectMaterial>;
struct RenderTarget{DX::Texture texture;DX::Surface surface;unsigned w,h;
 RenderTarget(unsigned width,unsigned height):surface{&texture},w(width),h(height){++targets;}
 ~RenderTarget(){assert(texture.references==0);--targets;}
 DX::BaseTexture*pTexture(){return &texture;}int pSurface(ComObjectWrap<DX::Surface>&out){out.pointer=&surface;return 0;}
 unsigned width(){return w;}unsigned height(){return h;}};
using RenderTargetPtr=Ptr<RenderTarget>;
enum{CONSTANTS_PER_SCREEN=1,CONSTANTS_PER_VIEW=2,CONSTANTS_PER_FRAME=4};
struct VisualContext{int flags=0;void updateSharedConstants(int value){flags=value;}};
struct Device{int depth=7;int SetDepthStencilSurface(DX::Surface*){depth=0;return 0;}};
struct Quad{bool drawChecked(EffectMaterial&material,unsigned,unsigned){
 assert(material.effect.active==&material.effect.binding);
 if(material.technique!="fillVelocityBuffer"&&material.technique!="downsampleVelocity"&&material.technique!="approximateVelocity")assert(material.effect.binding.texture);
 ++draws;events.push_back(material.technique);material.effect.active=&material.effect.definition;
 return draws!=failDraw;}};
struct Context{unsigned width=1280,height=720;int scope=0;DX::Viewport viewport={0,0,1280,720,0,1};
 struct Saved{unsigned w,h;DX::Viewport vp;int depth;};std::vector<Saved> saved;
 struct Desc{unsigned Width=1280,Height=720;}desc;Device dev;VisualContext visual;Quad quad;
 Device*device(){return &dev;}const Desc&backBufferDesc(){return desc;}
 bool pushRenderTarget(){saved.push_back({width,height,viewport,dev.depth});++scope;return true;}
 void popRenderTarget(){assert(scope>0);auto s=saved.back();saved.pop_back();width=s.w;height=s.h;viewport=s.vp;dev.depth=s.depth;--scope;}
 int setRenderTarget(unsigned,const ComObjectWrap<DX::Surface>&){return 0;}
 int setRenderTarget(unsigned,DX::Surface*){return 0;}
 void screenWidth(unsigned v){width=v;}unsigned screenWidth(){return width;}
 void screenHeight(unsigned v){height=v;}unsigned screenHeight(){return height;}
 Matrix vp,last;int getViewport(DX::Viewport*out){*out=viewport;return 0;}
 const Matrix&viewProjection(){return vp;}void lastViewProjection(const Matrix&matrix){last=matrix;}
 VisualContext&effectVisualContext(){return visual;}Quad&fsQuad(){return quad;}};
Context&rc(){static Context context;return context;}
bool createEffect(EffectMaterialPtr&material,const std::string&path){paths.push_back(path);material=new EffectMaterial;material->technique=path.find("luminance")!=std::string::npos?"luminance":"fxaa";return true;}
bool createRenderTarget(RenderTargetPtr&target,unsigned w,unsigned h,unsigned format,const char*,bool mainDepth,bool discard){assert((format==D3DFMT_A8R8G8B8||format==D3DFMT_G16R16F||format==D3DFMT_A16B16G16R16F)&&!mainDepth&&discard);target=new RenderTarget(w,h);return true;}
bool copyBackBuffer(RenderTarget*){++copyCalls;events.push_back("copy");return true;}
bool copySurface(RenderTarget*,const ComObjectWrap<DX::Surface>&){events.push_back("copy-surface");return true;}
}
int setStockSceneViewport(DX::Viewport*viewport,bool scene){assert(scene);Moo::rc().viewport=*viewport;return 0;}
struct Resolution{wot::StockDynamicResolution value;const wot::StockDynamicResolution&state()const{return value;}}resolution;
Resolution*stockDynamicResolution(){return &resolution;}
'''

class AntialiasingOwnerTests(unittest.TestCase):
    def test_actual_temporal_velocity_history_profiles_failure_and_reset(self):
        header=(ROOT/'bigworld/stock_temporal_antialiasing.hpp').read_text()
        source=(ROOT/'bigworld/stock_temporal_antialiasing.cpp').read_text()
        header=re.sub(r'^#(?:include|pragma).*\n','',header,flags=re.M)
        source=re.sub(r'^#include.*\n','',source,flags=re.M)
        driver=r'''
int main(){
 StockTemporalAASupport owner;DX::Surface depth;
 assert(!owner.beginFrame(true));assert(owner.createManagedObjects());
 assert((paths==std::vector<std::string>{"shaders/anti_aliasing/taa.fx","shaders/post_processing/velocity_buffer.fx"}));
 owner.configure(wot::stockCustomAAMode(2,true));assert(owner.beginFrame(true));
 assert(targets==8&&owner.temporalEnabled()&&owner.scalarEnabled()&&owner.velocityTexture()&&owner.velocitySurface());
 Moo::RenderTargetPtr half(new Moo::RenderTarget(640,360));
 Moo::rc().vp.setTranslate(3,4,5);assert(owner.beginOpaque());assert(Moo::rc().last.v[12]==0.f);
 assert(Moo::rc().visual.flags==(Moo::CONSTANTS_PER_FRAME|Moo::CONSTANTS_PER_VIEW));
 assert(owner.prepareVelocity(&depth,true,true));
 assert((events==std::vector<std::string>{"fillVelocityBuffer","downsampleVelocity"}));
 auto scalar=owner.resolveScalar(half,&depth,true);assert(scalar&&scalar.getObject()!=half.getObject());
 assert(owner.resolveLDR());
 assert((events==std::vector<std::string>{"fillVelocityBuffer","downsampleVelocity","copy-surface","copy","copy-surface"}));
 owner.endFrame();assert(Moo::rc().scope==0&&Moo::rc().dev.depth==7);
 events.clear();assert(owner.beginFrame(true));Moo::rc().vp.setTranslate(6,7,8);assert(owner.beginOpaque());
 assert(Moo::rc().last.v[12]==3.f&&owner.parameters().x==.375f/1280&&owner.parameters().y==-.125f/720);
 assert(owner.prepareVelocity(&depth,true,true));assert(owner.resolveScalar(half,&depth,true));assert(owner.resolveLDR());
 assert((events==std::vector<std::string>{"fillVelocityBuffer","downsampleVelocity","taaScalar","copy","taaColor","copyResult"}));
 owner.endFrame();events.clear();assert(owner.beginFrame(true));assert(owner.beginOpaque());
 failDraw=draws+1;assert(!owner.prepareVelocity(&depth,true,true));
 assert(!owner.resolveScalar(half,&depth,true));assert(!owner.resolveLDR());
 assert(Moo::rc().scope==0&&Moo::rc().dev.depth==7);owner.endFrame();failDraw=0;
 // The next successful frame seeds fresh histories after a terminal velocity failure.
 events.clear();assert(owner.beginFrame(true)&&owner.beginOpaque()&&owner.prepareVelocity(&depth,true,true));
 assert(owner.resolveScalar(half,&depth,true)&&owner.resolveLDR());
 assert(std::find(events.begin(),events.end(),"taaColor")==events.end());owner.endFrame();
 // 16F mode uses the actual HDR source and average luminance before LDR resolve.
 scalar=nullptr;owner.configure(wot::stockCustomAAMode(3,true));assert(owner.beginFrame(true)&&owner.beginOpaque());
 assert(owner.prepareVelocity(&depth,true,true));
 Moo::RenderTargetPtr hdr(new Moo::RenderTarget(1280,720));DX::Texture averageLuminance;
 events.clear();assert(owner.resolveHDR(hdr,&averageLuminance,.18f,4.f));
 assert((events==std::vector<std::string>{"fillInputColor","copy-surface","copyResult"}));
 auto count=events.size();assert(owner.resolveLDR()&&events.size()==count);owner.endFrame();
 assert(owner.beginFrame(true)&&owner.beginOpaque()&&owner.prepareVelocity(&depth,true,true));
 events.clear();assert(owner.resolveHDR(hdr,&averageLuminance,.18f,4.f));
 assert((events==std::vector<std::string>{"fillInputColor","taaColor","copyResult"}));owner.endFrame();
 // ScalarTAA remains real while colour AA is off: the velocity path changes
 // to approximateVelocity and never allocates an unused full-resolution MRT.
 owner.configure(wot::stockCustomAAMode(0,true));assert(owner.beginFrame(true)&&owner.beginOpaque());
 assert(!owner.temporalEnabled()&&owner.scalarEnabled()&&!owner.velocityTexture());
 events.clear();assert(owner.prepareVelocity(&depth,true,true));
 assert((events==std::vector<std::string>{"approximateVelocity"}));owner.endFrame();
 assert(owner.beginFrame(false)&&!owner.temporalEnabled()&&!owner.scalarEnabled());owner.endFrame();
 assert(targets==2&&averageLuminance.references==0); // Only the external HDR/DBE inputs remain.
 auto selected=selections;owner.deleteUnmanagedObjects();owner.deleteUnmanagedObjects();
 assert(selections==selected&&targets==2);
 owner.deleteManagedObjects();owner.deleteManagedObjects();assert(!owner.beginFrame(true));
}
'''
        with tempfile.TemporaryDirectory()as folder:
            cpp=Path(folder)/'probe.cpp';binary=Path(folder)/'probe'
            cpp.write_text(HOST+header+source+driver)
            result=subprocess.run(['c++','-std=c++14','-Wall','-Wextra','-Werror',
                '-I'+str(ROOT/'include'),str(cpp),'-o',str(binary)],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stderr)
            result=subprocess.run([str(binary)],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stderr)

    def test_actual_fxaa_pass_failure_restore_and_default_texture_retirement(self):
        header = (ROOT / 'bigworld/stock_antialiasing.hpp').read_text()
        source = (ROOT / 'bigworld/stock_antialiasing.cpp').read_text()
        header = re.sub(r'^#(?:include|pragma).*\n', '', header, flags=re.M)
        source = re.sub(r'^#include.*\n', '', source, flags=re.M)
        host = HOST
        driver = r'''
int main(){
 StockFXAASupport owner;
 assert(owner.apply()&&targets==0&&paths.empty()&&selections==0);
 assert(owner.createManagedObjects());
 assert((paths==std::vector<std::string>{"shaders/anti_aliasing/wg_fxaa.fx","shaders/anti_aliasing/wg_fxaa_luminance.fx"}));
 owner.enable(true);assert(owner.apply());
 assert((events==std::vector<std::string>{"copy","luminance","fxaa"}));
 assert(targets==2&&Moo::rc().scope==0&&Moo::rc().dev.depth==7);
 assert(Moo::rc().width==1280&&Moo::rc().height==720);
 int selected=selections;owner.deleteUnmanagedObjects();
 assert(targets==0&&selections==selected); // No binding may be manufactured for cleanup.
 draws=0;failDraw=1;assert(!owner.apply());
 assert(Moo::rc().scope==0&&Moo::rc().dev.depth==7);
 draws=0;failDraw=2;assert(!owner.apply());
 assert(Moo::rc().scope==0&&Moo::rc().width==1280);
 failDraw=0;draws=0;assert(owner.apply());
 Moo::rc().desc.Width=960;Moo::rc().desc.Height=540;
 draws=0;assert(owner.apply()); // Resize must clear both retained old DEFAULT textures.
 assert(targets==2&&Moo::rc().scope==0);
 owner.deleteUnmanagedObjects();owner.deleteUnmanagedObjects();assert(targets==0);
 owner.deleteManagedObjects();owner.deleteManagedObjects();
 owner.enable(false);assert(owner.apply());
}
'''
        with tempfile.TemporaryDirectory() as folder:
            cpp = Path(folder) / 'probe.cpp'
            binary = Path(folder) / 'probe'
            cpp.write_text(host + header + source + driver)
            result = subprocess.run(['c++', '-std=c++14', '-Wall', '-Wextra', '-Werror',
                '-I'+str(ROOT/'include'), str(cpp), '-o', str(binary)], text=True, capture_output=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            result = subprocess.run([str(binary)], text=True, capture_output=True)
            self.assertEqual(result.returncode, 0, result.stderr)
