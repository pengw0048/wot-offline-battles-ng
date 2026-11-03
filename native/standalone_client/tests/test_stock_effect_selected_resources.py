"""Run selected-resource admission against the real adapter and effect runtime."""
import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path
from native_test_compile_flags import native_test_compile_flags
from test_stock_effect_adapter_reads import FIXTURE, HOST as READ_HOST
ROOT=Path(__file__).resolve().parents[1]
HOST=READ_HOST[:READ_HOST.index('int main()')]+r'''
int main(){try{
 auto e=definition();e->resource_views.clear();e->shaders[0].resources={{2,0,0}};
 e->shaders[1].resources={{2,1,0}};
 for(unsigned i=0;i<6;++i){wot::StockEffectResource r;r.name="texture"+std::to_string(i);r.semantic="R"+std::to_string(i);r.semantic_present=true;r.type_index=6;e->resource_views.push_back(r);}
 e->resource_indirections[1]={0,0x10000,2,4};
 auto append=[](std::vector<unsigned char>&v,unsigned x){for(unsigned i=0;i<4;++i)v.push_back((x>>(8*i))&255);};
 e->selection_domains={{0,4,0,{0,0,0,0,1,0,0,0}}};
 for(unsigned x:{16u,0u,0u,1u,0u})append(e->selector_code,x);
 for(unsigned x:{1u,3u})append(e->selector_data,x);
 auto shader=e->shaders[1];shader.resources={{2,2,0}};e->shaders.push_back(shader);
 shader.resources={{2,3,0}};e->shaders.push_back(shader);
 e->resource_indirections[5]={0,1,2,3};
 auto pass=e->techniques[0].passes[0];pass.descriptor[1]=2;e->techniques.push_back({"ALT",0xffffffff,{pass}});
 pass.descriptor[1]=3;e->techniques.push_back({"FAIL",0xffffffff,{pass}});
 auto* device=new IDirect3DDevice9;auto* states=new ID3DXEffectStateManager;
 std::shared_ptr<wot::StockDx9EffectRuntime> runtime;std::string error;
 check(wot::StockDx9EffectRuntime::create(device,states,e,runtime,error),"runtime");
 std::array<unsigned,6> calls{};std::array<D3DXHANDLE,6> captures{};
 std::array<IDirect3DTexture9*,6> textures{};for(auto& p:textures)p=new IDirect3DTexture9;
 bool failUsed=false;unsigned reports=0,frameWrites=0,screenWrites=0;
 auto context=std::make_shared<wot::StockEffectSharedContext>([&](const std::string& semantic,ID3DXEffect*target,D3DXHANDLE handle){
  if(semantic=="TestFrame"||semantic=="TestScreen"){
   if(!target)return true;
   if(semantic=="TestFrame")++frameWrites;else ++screenWrites;
   D3DXVECTOR4 value={1,2,3,4};return target->SetVector(handle,&value)==S_OK;
  }
  if(semantic.size()!=2||semantic[0]!='R')return false;
  unsigned i=unsigned(semantic[1]-'0');if(i>=calls.size())return false;
  if(!target)return true;
  ++calls[i];captures[i]=handle;
  if(i==4||(i==1&&failUsed))return false;
  return target->SetTexture(handle,textures[i])==S_OK;
 },[&](const std::string&){++reports;});
 {
  wot::StockEffectAdapter material(runtime,context);UINT passes=0;
  auto draw=[&](const char* technique,bool success){
   check(material.SetTechnique(technique)==S_OK,"technique");
   check(material.Begin(&passes,D3DXFX_DONOTSAVESTATE)==S_OK,"begin");
   check((material.BeginPass(0)==S_OK)==success,"selected resource result");
   check(material.EndPass()==(success?S_OK:D3DERR_INVALIDCALL)&&material.End()==S_OK,"end");
  };
  draw("COLOR",true);check(calls[0]==1&&calls[1]==1&&calls[2]==0&&calls[3]==0&&calls[4]==0&&calls[5]==0,"unused providers skipped");
  check(frameWrites==1,"initial shared producer publication");
  check(wot::update_stock_effect_shared_constants(*context,1,error)&&frameWrites==2,"partial frame update");
  auto newDefinition=std::make_shared<wot::StockEffectBinary>(*e);
  auto screen=newDefinition->uniform_buffers[2];screen.name="PerScreen";
  screen.uniforms[0].name="screenValue";screen.uniforms[0].semantic="TestScreen";
  newDefinition->uniform_buffers.push_back(screen);
  std::shared_ptr<wot::StockDx9EffectRuntime> newRuntime;
  check(wot::StockDx9EffectRuntime::create(device,states,newDefinition,newRuntime,error),"late runtime");
  wot::StockEffectAdapter late(newRuntime,context);
  check(wot::update_stock_effect_shared_constants(*context,1,error)&&screenWrites==0,"screen remains pending after frame-only update");
  draw("COLOR",true);check(screenWrites==1&&frameWrites==4,"late schema initialised on old material first use");
  calls.fill(0);draw("COLOR",true);check(frameWrites==4&&screenWrites==1,"steady draws do not republish all shared values");
  calls.fill(0);draw("COLOR",true);
  auto* fresh=new IDirect3DTexture9;auto* old=textures[0];textures[0]=fresh;
  draw("COLOR",true);check(calls[0]==2&&calls[1]==2,"used resources refreshed on unchanged plan");
  IDirect3DBaseTexture9* value=nullptr;
  check(material.GetTexture(captures[0],&value)==S_OK&&value==fresh,"fresh provider value");value->Release();old->Release();
  check(material.SetInt("choose",1)==S_OK,"selection write");
  draw("COLOR",true);check(calls[0]==3&&calls[1]==2&&calls[3]==1,"resource selection invalidates plan");
  check(material.SetInt("choose",0)==S_OK,"selection restore");
  failUsed=true;draw("COLOR",false);check(reports==1&&calls[1]==3,"selected failure preserved");
  failUsed=false;draw("COLOR",true);check(calls[1]==4,"failed used provider retries");
  draw("ALT",true);check(calls[2]==1&&calls[5]==0,"shader change invalidates plan");
  draw("FAIL",false);check(calls[4]==1&&reports==2,"formerly unused provider fails when selected");
  draw("ALT",true);check(calls[2]==2,"unused failure does not poison another pass");
  auto capturesDefinition=std::make_shared<wot::StockEffectBinary>(*e);
  for(unsigned group=0;group<3;++group){
   wot::StockEffectUniformBuffer b;b.name="Capture"+std::to_string(group);b.flags=group==1?0:1;
   for(unsigned j=0;j<(group==2?0u:7u);++j){auto u=capturesDefinition->uniform_buffers[2].uniforms[0];u.name="capture"+std::to_string(group)+"_"+std::to_string(j);u.byte_offset=j*16;b.uniforms.push_back(u);}
   b.defaults.resize(b.uniforms.size()*16);capturesDefinition->uniform_buffers.push_back(b);
  }
  std::shared_ptr<wot::StockDx9EffectRuntime> capturesRuntime;
  check(wot::StockDx9EffectRuntime::create(device,states,capturesDefinition,capturesRuntime,error),"capture runtime");
  wot::StockEffectAdapter capturesProducer(capturesDefinition,*context);
  wot::StockEffectReflection capturesSchema(capturesDefinition,{});
  wot::StockEffectAdapter capturesMaterial(capturesRuntime,context,capturesSchema);
  std::vector<D3DXHANDLE> retained;
  for(unsigned i=0;i<capturesDefinition->uniform_buffers.size();++i){
   const auto& b=capturesDefinition->uniform_buffers[i];
   check(!capturesProducer.capture(i,b.uniforms.size()),"capture member bounds");
   for(unsigned j=0;j<b.uniforms.size();++j){auto h=capturesProducer.capture(i,j);
    if(!(b.flags&1)){check(!h,"private uniform has no producer capture");continue;}
    D3DXVECTOR4 written={float(i+1),float(j+1),3,4},read{};
    check(h&&capturesProducer.SetVector(h,&written)==S_OK,"capture destination write");retained.push_back(h);
    std::vector<unsigned char> bytes(b.defaults.size());
    check(capturesMaterial.GetValue(b.name.c_str(),bytes.data(),UINT(bytes.size()))==S_OK,"capture buffer read");
    std::memcpy(&read,bytes.data()+b.uniforms[j].byte_offset,16);
    check(std::memcmp(&written,&read,16)==0,"capture buffer/member address equivalence");
   }
  }
  check(!capturesProducer.capture(capturesDefinition->uniform_buffers.size(),0),"capture buffer bounds");
  D3DXVECTOR4 retainedValue={7,8,9,10};check(capturesProducer.SetVector(retained.back(),&retainedValue)==S_OK,"retained capture stable");
  context->detach_providers();check(material.Begin(&passes,D3DXFX_DONOTSAVESTATE)==S_OK,"detached begin");check(material.BeginPass(0)!=S_OK,"detached providers guarded");material.EndPass();material.End();
 }
 for(auto* texture:textures){check(texture->refs==1,"resource lifetime");texture->Release();}
 std::cout<<"Selected resources refresh per draw; unused providers, variant transitions and failure isolation passed\n";
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
'''

class SelectedResourceTests(unittest.TestCase):
    def test_real_selected_inputs_and_old_all_resources_negative_control(self):
        compiler=shutil.which('c++')
        if not compiler:self.skipTest('Host C++ compiler required')
        source=(ROOT/'bigworld/stock_effect_adapter.cpp').read_text()
        methods=re.findall(r'^\s+([A-Z][^;]*?)\s+override;',source[source.index('class StockEffectAdapter final : public ID3DXEffect {'):source.index('private:')],re.M|re.S)
        before='    if (!required_resources_[target->owner]) continue;'
        self.assertEqual(source.count(before),1)
        with tempfile.TemporaryDirectory(prefix='effect-selected-resources-') as temp:
            root=Path(temp)
            (root/'d3d9.h').write_text(FIXTURE.DX9.DEVICE)
            (root/'d3dx9effect.h').write_text(FIXTURE.D3DX.replace('METHODS','\n'.join('    virtual '+' '.join(m.split())+' = 0;' for m in methods)))
            (root/'probe.cpp').write_text(HOST)
            for label,code,reason in (('fixed',source,''),('all-resources',source.replace(before,''),'selected resource result'),('stale-selection',source.replace('resource_plan_version_ != version ||','false ||'),'resource selection invalidates plan'),('stale-shader',source.replace('resource_plan_selection_.pixel_shader != selected.pixel_shader','false'),'shader change invalidates plan')):
                (root/'adapter_under_test.cpp').write_text(code)
                binary=root/label
                args=[compiler,'-std=c++17',*native_test_compile_flags(),'-I',str(root),'-I',str(ROOT/'include'),'-I',str(ROOT/'bigworld'),str(root/'probe.cpp'),str(ROOT/'bigworld/stock_effect_dx9.cpp')]
                args.extend(str(ROOT/'src'/n) for n in ('stock_effect_binding.cpp','stock_effect_reflection.cpp','stock_shader_binary.cpp','stock_shader_runtime.cpp'))
                subprocess.run(args+['-o',str(binary)],check=True,capture_output=True,text=True)
                run=subprocess.run([str(binary)],capture_output=True,text=True)
                if label=='fixed':self.assertEqual(run.returncode,0,run.stdout+run.stderr)
                else:
                    self.assertNotEqual(run.returncode,0)
                    self.assertIn(reason,run.stderr)

if __name__=='__main__':unittest.main()
