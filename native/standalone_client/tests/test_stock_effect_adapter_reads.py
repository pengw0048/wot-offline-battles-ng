"""Exercise D3DX current-value reads with real Binding/default/context storage."""
import importlib.util
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('read_handle_fixture', ROOT/'tests/test_stock_effect_material_handles.py')
FIXTURE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(FIXTURE)

HOST = r'''
#include "adapter_under_test.cpp"
#include <array>
#include <stdexcept>
#include <iostream>
#include <cstring>
void check(bool value,const char* label){if(!value)throw std::runtime_error(label);}
template<class T> void initial(std::vector<unsigned char>&data,unsigned offset,const T&value){std::memcpy(data.data()+offset,&value,sizeof(value));}
std::shared_ptr<wot::StockEffectBinary> definition(){
 auto e=std::make_shared<wot::StockEffectBinary>();
 for(auto spec:{std::array<unsigned,4>{3,0,4,1},{3,1,16,4},{3,2,64,4},{4,0,4,1},{1,0,4,1},{2,0,4,1},{5,4,4,1}}){
  wot::StockEffectType t;t.basic_type=spec[0];t.type_class=spec[1];t.byte_size=spec[2];t.rows_or_members_offset=spec[3];t.columns_or_member_count=spec[1]==2?4:1;e->types.push_back(t);
 }
 wot::StockEffectUniformBuffer b;b.name="Data";b.defaults.assign(512,0xcc);
 auto field=[&](const char*name,unsigned type,unsigned offset,unsigned elements=1,unsigned padding=0){wot::StockEffectUniform u;u.name=name;u.type_index=type;u.byte_offset=offset;u.elements=elements;u.element_padding=padding;b.uniforms.push_back(u);};
 field("number",0,0);field("vector",1,16);field("matrix",2,32);field("boolean",3,96);field("integer",4,100);field("unsigned",5,104);field("array",0,112,3,12);field("vectors",1,160,2);field("matrices",2,192,2);field("bools",3,320,3,12);field("ints",4,368,3);
 initial(b.defaults,0,1.f);const D3DXVECTOR4 v={1,2,3,4};initial(b.defaults,16,v);D3DXMATRIX m{};for(int i=0;i<4;++i)m.m[i][i]=1;initial(b.defaults,32,m);
 initial(b.defaults,96,BOOL(FALSE));initial(b.defaults,100,INT(4));initial(b.defaults,104,0xffffffffu);
 for(unsigned i=0;i<3;++i) {
  initial(b.defaults,112+16*i,float(i+1));
 }
 e->uniform_buffers.push_back(b);
 wot::StockEffectUniformBuffer selection;selection.name="$Selection";selection.defaults.resize(8);wot::StockEffectUniform u;u.name="choose";u.type_index=4;u.elements=1;selection.uniforms.push_back(u);e->uniform_buffers.push_back(selection);
 wot::StockEffectUniformBuffer shared;shared.name="PerFrame";shared.flags=1;shared.defaults.resize(16);u.name="frameValue";u.semantic="TestFrame";u.semantic_present=true;u.type_index=1;shared.uniforms.push_back(u);e->uniform_buffers.push_back(shared);
 wot::StockEffectResource resource;resource.name="texture";resource.type_index=6;resource.semantic="TestTexture";resource.semantic_present=true;e->resource_views.push_back(resource);
 wot::StockShaderBinary shader;shader.stage=1;shader.bytecode={0xfffe0300,0xffff};e->shaders.push_back(shader);shader.stage=2;shader.bytecode={0xffff0300,0xffff};e->shaders.push_back(shader);wot::StockEffectPass pass;pass.descriptor={0xffffffff,1,0,0xffffffff,0xffffffff,0xffffffff,0xffffffff};e->resource_indirections[1]={0};e->shaders[1].resources.push_back({2,0,0});e->resource_indirections[5]={0,1};e->techniques.push_back({"COLOR",0xffffffff,{pass}});return e;
}
int main(){try{
 auto e=definition();auto* device=new IDirect3DDevice9;auto* states=new ID3DXEffectStateManager;
 std::shared_ptr<wot::StockDx9EffectRuntime> runtime;std::string error;
 check(wot::StockDx9EffectRuntime::create(device,states,e,runtime,error),"runtime");
 auto context=std::make_shared<wot::StockEffectSharedContext>(wot::StockEffectConstantProvider{});
 auto original=std::make_unique<wot::StockEffectAdapter>(runtime,context);
 wot::StockEffectReflection schema(e,{});
 auto material=std::make_unique<wot::StockEffectAdapter>(runtime,context,schema);
 auto sibling=std::make_unique<wot::StockEffectAdapter>(runtime,context,schema);
 auto handle=material->GetParameterByName(nullptr,"number");float number=0;
 check(material->GetFloat(handle,&number)==S_OK&&number==1.f,"initial default");
 check(original->SetFloat(handle,99.f)==S_OK&&original->GetFloat(handle,&number)==S_OK&&number==1.f,"Definition getter preserves defaults after writes");
 check(material->SetFloat(handle,1.25f)==S_OK,"set float");
 check(material->GetFloat(handle,&number)==S_OK&&number==1.25f,"post-write float");
 auto otherDefinition=std::make_shared<wot::StockEffectBinary>(*e);initial(otherDefinition->uniform_buffers[0].defaults,0,17.f);
 std::shared_ptr<wot::StockDx9EffectRuntime> otherRuntime;check(wot::StockDx9EffectRuntime::create(device,states,otherDefinition,otherRuntime,error),"second runtime");
 wot::StockEffectAdapter variantDefinition(otherRuntime,context);
 check(variantDefinition.GetFloat("number",&number)==S_OK&&number==17.f,"Definition default independent of pool template");
 wot::StockEffectReflection variantSchema(otherDefinition,{});wot::StockEffectAdapter variantBinding(otherRuntime,context,variantSchema);
 check(variantBinding.GetFloat("number",&number)==S_OK&&number==1.f,"Binding retains original first-template contract");
 check(material->GetValue("number",&number,sizeof(number))==S_OK&&number==1.25f,"raw current value");
 check(sibling->GetFloat(handle,&number)==S_OK&&number==1.f,"private isolation");
 auto clone=std::make_unique<wot::StockEffectAdapter>(*material);
 check(clone->GetFloat(handle,&number)==S_OK&&number==1.25f,"clone current value");
 check(clone->SetFloat(handle,2.5f)==S_OK&&material->GetFloat(handle,&number)==S_OK&&number==1.25f,"clone independence");
 D3DXVECTOR4 vector={.25f,.5f,.75f,1.f},got{};
 check(material->SetVector("vector",&vector)==S_OK&&material->GetVector("vector",&got)==S_OK&&std::memcmp(&vector,&got,16)==0,"vector roundtrip");
 D3DXMATRIX matrix{},matrixGot{};for(unsigned i=0;i<16;++i)(&matrix.m[0][0])[i]=float(i)*.125f;
 check(material->SetMatrix("matrix",&matrix)==S_OK&&material->GetMatrix("matrix",&matrixGot)==S_OK&&std::memcmp(&matrix,&matrixGot,64)==0,"matrix roundtrip");
 check(material->GetMatrixTranspose("matrix",&matrixGot)==S_OK&&matrixGot.m[2][1]==matrix.m[1][2],"transpose current value");
 BOOL boolean=FALSE;INT integer=0;check(material->SetBool("boolean",TRUE)==S_OK&&material->GetBool("boolean",&boolean)==S_OK&&boolean==TRUE,"bool roundtrip");
 check(material->SetInt("integer",-7)==S_OK&&material->GetInt("integer",&integer)==S_OK&&integer==-7,"int roundtrip");
 check(material->SetInt("unsigned",INT(0x81234567u))==S_OK&&material->GetInt("unsigned",&integer)==S_OK&&unsigned(integer)==0x81234567u,"uint bits");
 check(material->SetInt("choose",3)==S_OK&&material->GetInt("choose",&integer)==S_OK&&integer==3,"selection current bytes");
 float values[3]={7,8,9},array[3]={};
 check(material->SetFloatArray("array",values,3)==S_OK&&material->GetFloatArray("array",array,3)==S_OK&&std::memcmp(values,array,sizeof(values))==0,"padded array roundtrip");
 values[1]=13;check(material->SetValue("array",values,sizeof(values))==S_OK&&material->GetValue("array",array,sizeof(array))==S_OK&&std::memcmp(values,array,sizeof(values))==0,"padded raw roundtrip");
 D3DXVECTOR4 vectors[2]={vector,{3,4,5,6}},vectorsGot[2]{};
 check(material->SetVectorArray("vectors",vectors,2)==S_OK&&material->GetVectorArray("vectors",vectorsGot,2)==S_OK&&std::memcmp(vectors,vectorsGot,sizeof(vectors))==0,"vector array roundtrip");
 D3DXMATRIX matrices[2]={matrix,matrix},matricesGot[2]{};matrices[1].m[2][3]=19;
 check(material->SetMatrixArray("matrices",matrices,2)==S_OK&&material->GetMatrixArray("matrices",matricesGot,2)==S_OK&&std::memcmp(matrices,matricesGot,sizeof(matrices))==0,"matrix array roundtrip");
 BOOL bools[3]={TRUE,FALSE,TRUE},boolsGot[3]{};INT ints[3]={-7,0,19},intsGot[3]{};
 check(material->SetBoolArray("bools",bools,3)==S_OK&&material->GetBoolArray("bools",boolsGot,3)==S_OK&&std::memcmp(bools,boolsGot,sizeof(bools))==0,"bool array roundtrip");
 check(material->SetIntArray("ints",ints,3)==S_OK&&material->GetIntArray("ints",intsGot,3)==S_OK&&std::memcmp(ints,intsGot,sizeof(ints))==0,"int array roundtrip");
 std::array<unsigned char,512> raw;check(material->GetValue("Data",raw.data(),raw.size())==S_OK,"whole buffer current bytes");
 float item;std::memcpy(&item,raw.data()+128,4);check(item==13&&raw[116]==0xcc,"UBB padding preserved");
 wot::StockEffectReflection reflection(e,{});check(reflection.default_value(reflection.parameter_by_name("number"),&number,4)&&number==1.f,"immutable defaults preserved");
 wot::StockEffectAdapter producer(e,*context);auto sharedHandle=producer.capture(2,0);
 check(producer.SetVector(sharedHandle,&vector)==S_OK&&producer.GetVector(sharedHandle,&got)==S_OK&&std::memcmp(&vector,&got,16)==0,"shared capture current bytes");
 check(material->GetValue("PerFrame",&got,16)==S_OK&&std::memcmp(&vector,&got,16)==0,"material current context");
 check(sibling->GetValue("PerFrame",&got,16)==S_OK&&std::memcmp(&vector,&got,16)==0,"same context shared read");
 auto otherContext=std::make_shared<wot::StockEffectSharedContext>(wot::StockEffectConstantProvider{});wot::StockEffectAdapter other(runtime,otherContext,schema);
 check(other.GetValue("PerFrame",&got,16)==S_OK&&got.x==0&&got.y==0&&got.z==0&&got.w==0,"other context isolation");
 check(producer.GetValue("PerFrame",&got,16)==S_OK&&got.x==0&&got.y==0,"producer default reflection preserved");
 auto* texture=new IDirect3DTexture9;IDirect3DBaseTexture9* textureGot=nullptr;
 check(material->SetTexture("texture",texture)==S_OK&&texture->refs==2,"set texture retains");
 check(material->GetTexture("texture",&textureGot)==S_OK&&textureGot==texture&&texture->refs==3,"texture current AddRef");textureGot->Release();
 check(sibling->GetTexture("texture",&textureGot)==S_OK&&!textureGot,"texture private isolation");
 check(material->SetTexture("texture",nullptr)==S_OK&&texture->refs==1&&material->GetTexture("texture",&textureGot)==S_OK&&!textureGot,"texture cleared current value");texture->Release();
 auto* automaticTexture=new IDirect3DTexture9;D3DXHANDLE automaticHandle=nullptr;
 auto automaticContext=std::make_shared<wot::StockEffectSharedContext>([&](const std::string&semantic,ID3DXEffect*target,D3DXHANDLE h){
 if(semantic!="TestTexture") {
  return false;
 }
 if(!target) {
  return true;
 }
 automaticHandle=h;
 check(target->SetTexture(h,automaticTexture)==S_OK,"automatic texture capture set");IDirect3DBaseTexture9* value=nullptr;
 check(target->GetTexture(h,&value)==S_OK&&value==automaticTexture,"automatic texture capture get");value->Release();return true;},[](const std::string&failure){std::cerr<<failure<<"\n";});
 {wot::StockEffectAdapter automatic(runtime,automaticContext);UINT passes=0;
 check(automatic.Begin(&passes,D3DXFX_DONOTSAVESTATE)==S_OK&&passes==1&&automatic.BeginPass(0)==S_OK,"automatic texture begin");
 check(automaticHandle&&automatic.GetTexture(automaticHandle,&textureGot)==S_OK&&textureGot==automaticTexture,"retained capture handle get");textureGot->Release();
 check(automatic.EndPass()==S_OK&&automatic.End()==S_OK,"automatic texture end");}
 check(automaticTexture->refs==1,"automatic texture lifecycle");automaticTexture->Release();
 float invalid[4]={-1,-1,-1,-1};check(material->GetFloatArray("array",invalid,4)==E_INVALIDARG&&invalid[0]==-1,"oversize array atomic failure");
 check(material->GetFloat("unknown",&number)==E_INVALIDARG&&material->GetValue("Data",nullptr,0)==S_OK&&material->GetFloat("number",nullptr)==E_INVALIDARG,"invalid and empty reads");
 std::cout<<"Current scalar/vector/matrix/array/raw/selection/context/texture reads with immutable defaults passed\n";
}catch(const std::exception&error){std::cerr<<error.what()<<'\n';return 1;}}
'''

class EffectAdapterReadTests(unittest.TestCase):
    def test_current_binding_reads_and_old_default_only_negative_control(self):
        compiler=shutil.which('c++')
        if not compiler:self.skipTest('Host C++ compiler required')
        adapter=(ROOT/'bigworld/stock_effect_adapter.cpp').read_text()
        body=adapter[adapter.index('class StockEffectAdapter final : public ID3DXEffect {'):]
        body=body[:body.index('private:')]
        methods=re.findall(r'^\s+([A-Z][^;]*?)\s+override;',body,re.M|re.S)
        self.assertGreater(len(methods),60)
        begin=adapter.index('HRESULT StockEffectAdapter::read(')
        end=adapter.index('bool StockEffectAdapter::update_resources(',begin)
        # Exact pre-fix adapter body: keep the regression control independent of Git HEAD.
        old_read=r'''HRESULT StockEffectAdapter::read(D3DXHANDLE parameter, void *data, UINT size,
                                 unsigned annotation_type) {
  if (!data && size)
    return E_INVALIDARG;
  const auto handle = handle_number(parameter);
  if (const auto *annotation = reflection_.annotation_reference(handle)) {
    if (annotation->type != annotation_type || size > annotation->value.size())
      return E_INVALIDARG;
    if (size)
      std::memcpy(data, annotation->value.data(), size);
    return S_OK;
  }
  return reflection_.default_value(parameter_number(parameter), data, size)
             ? S_OK
             : E_INVALIDARG;
}
'''
        old_read=old_read.replace('  if (!data && size)', '  (void)definitionDefaultReads_;\n  if (!data && size)')
        with tempfile.TemporaryDirectory(prefix='effect-current-reads-') as temporary:
            root=Path(temporary)
            (root/'d3d9.h').write_text(FIXTURE.DX9.DEVICE)
            (root/'d3dx9effect.h').write_text(FIXTURE.D3DX.replace('METHODS','\n'.join('    virtual '+' '.join(m.split())+' = 0;' for m in methods)))
            (root/'probe.cpp').write_text(HOST)
            for label,source in [('fixed',adapter),('old',adapter[:begin]+old_read+adapter[end:])]:
                (root/'adapter_under_test.cpp').write_text(source)
                binary=root/label
                command=[compiler,'-std=c++17','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-I',str(root),'-I',str(ROOT/'include'),'-I',str(ROOT/'bigworld'),str(root/'probe.cpp')]
                command += [str(ROOT/'bigworld/stock_effect_dx9.cpp')]+[str(ROOT/'src'/n) for n in ('stock_effect_binding.cpp','stock_effect_reflection.cpp','stock_shader_binary.cpp','stock_shader_runtime.cpp')]+['-o',str(binary)]
                compiled=subprocess.run(command,capture_output=True,text=True)
                self.assertEqual(compiled.returncode,0,compiled.stdout+compiled.stderr)
                result=subprocess.run([str(binary)],capture_output=True,text=True)
                if label=='fixed':self.assertEqual(result.returncode,0,result.stdout+result.stderr)
                else:
                    self.assertNotEqual(result.returncode,0,result.stdout+result.stderr)
                    self.assertIn('post-write float',result.stderr)

if __name__=='__main__':unittest.main()
