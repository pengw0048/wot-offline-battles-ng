"""Audit the exact original velocity masks and old-ST4 colour shader outputs."""
import io
import os
from pathlib import Path
import subprocess
import struct
import tempfile
import unittest
import zipfile

ROOT=Path(__file__).resolve().parents[1]

PROBE=r'''
#include "wot/stock_effect_binding.hpp"
#include "wot/stock_shader_runtime.hpp"
#include <cassert>
#include <fstream>
#include <iterator>
#include <set>
unsigned outputs(const std::vector<unsigned>&words){
 unsigned result=0;
 for(unsigned i=1;i<words.size();){
  const unsigned op=words[i]&65535;
  if(op==65535)break;
  if(op==65534){i+=1+((words[i]>>16)&32767);continue;}
  const unsigned count=(words[i]>>24)&15;
  // SM3 destination operands use COLOROUT=8; declarations and constant
  // literals are not register-writing instructions. Bit 31 marks parameters.
  if(count&&i+1<words.size()&&op!=81&&op!=48&&op!=47&&op!=31){
   const unsigned value=words[i+1];
   if((value&0x80000000)&&(((value>>28)&7)|((value>>8)&24))==8){
    assert((value&2047)<4);result|=1u<<(value&2047);
   }
  }
  i+=1+count;
 }
 return result;
}
std::set<std::string> inputs(const wot::StockEffectBinary&e,const wot::StockEffectBinding&binding,const wot::StockShaderBinary&shader){
 std::set<std::string>result;std::string error;
 for(const auto&input:shader.uniforms){
  std::optional<unsigned>index;
  for(const auto&resource:shader.resources)if(resource.kind==3&&resource.shader_slot==input.uniform_slot)
   assert(wot::resolve_stock_effect_reference(e,binding.selection(),wot::StockEffectResourceTable::uniform_buffer,resource.resource_index,index,error)&&index);
  assert(index);
  const auto&buffer=e.uniform_buffers.at(*index);
  for(const auto&uniform:buffer.uniforms){
   const unsigned begin=uniform.byte_offset,end=begin+e.types.at(uniform.type_index).byte_size*(uniform.elements?uniform.elements:1);
   if(begin<input.source_offset+input.source_size&&end>input.source_offset)result.insert(uniform.name);
  }
 }
 return result;
}
int main(int argc,char**argv){assert(argc==3);
 std::ifstream stream(argv[2],std::ios::binary);assert(stream);
 std::vector<unsigned char>bytes((std::istreambuf_iterator<char>(stream)),{});
 auto e=std::make_shared<wot::StockEffectBinary>();std::string error;
 assert(wot::decode_stock_dx9_effect(bytes.data(),bytes.size(),*e,error));
 wot::StockEffectUniformPool pool;wot::StockEffectUniformContext context(pool);wot::StockEffectBinding binding(e,context);
 const std::string mode=argv[1];
 if(mode=="velocity"){
  for(unsigned flags=0;flags<4;++flags){
   for(unsigned b=0;b<e->uniform_buffers.size();++b)for(unsigned u=0;u<e->uniform_buffers[b].uniforms.size();++u){
    const auto&uniform=e->uniform_buffers[b].uniforms[u];
    int value=uniform.name=="g_skipSpeedtree"?(flags&1)!=0:uniform.name=="g_skipFlora"?(flags&2)!=0:-1;
    if(value>=0)assert(binding.set_uniform(b,u,&value,4,4,error));
   }
   unsigned found=0;
   for(unsigned t=0;t<e->techniques.size();++t)if(e->techniques[t].name=="fillVelocityBuffer"){
    assert(e->techniques[t].passes.size()==2);
    for(unsigned p=0;p<2;++p){
     wot::StockDx9PassSelection selection;assert(binding.select_pass(t,p,selection,error)&&selection.render_state);
     std::vector<wot::StockDx9StateValue>states;assert(wot::stock_dx9_render_states(e->render_states.at(*selection.render_state),states,error));
     unsigned mask=~0u,reference=~0u,comparison=0;
     for(const auto&state:states){if(state.state==58)mask=state.value;if(state.state==57)reference=state.value;if(state.state==56)comparison=state.value;}
     assert(reference==0);
     assert(p?mask==240&&comparison==3:mask==(16u+((flags&1)?0u:32u)+((flags&2)?0u:64u))&&comparison==6);
     ++found;
    }
   }
   assert(found==2);
  }
 }else{
  const bool deferred=mode!="forward";unsigned found=0;
  for(unsigned t=0;t<e->techniques.size();++t)if(e->techniques[t].name=="COLOR"){
   assert(e->techniques[t].passes.size()==1);
   wot::StockDx9PassSelection selection;assert(binding.select_pass(t,0,selection,error)&&selection.pixel_shader&&selection.vertex_shader);
   assert(outputs(e->shaders.at(*selection.pixel_shader).bytecode)==(deferred?15u:1u));
   if(deferred){
    const auto used=inputs(*e,binding,e->shaders.at(*selection.vertex_shader));
    assert(used.count("g_reprojectionMat"));
    if(mode!="billboard")assert(used.count("g_prevWindMatrices")&&used.count("g_windMatrices"));
    if(mode=="leaf")assert(used.count("g_prevLeafAngles")&&used.count("g_leafAngles"));
   }
   ++found;
  }
  assert(found==1);
 }
}
'''


class AntialiasingResourcesTests(unittest.TestCase):
    def test_original_srt_dispatch_and_absent_new_renderer_resources(self):
        client=Path(os.environ.get('WOT_0922_CLIENT','/Users/peng/Downloads/World_of_Tanks_0.09.22.00.01_CH_1513_HD'))
        if not(client/'WorldOfTanks.exe').is_file():self.skipTest('Original #1513 executable required')
        data=(client/'WorldOfTanks.exe').read_bytes();pe=struct.unpack_from('<I',data,60)[0]
        base=struct.unpack_from('<I',data,pe+52)[0];sections=pe+24+struct.unpack_from('<H',data,pe+20)[0]
        mappings=[]
        for index in range(struct.unpack_from('<H',data,pe+6)[0]):
            at=sections+index*40;address,size,raw=struct.unpack_from('<III',data,at+12);mappings.append((base+address,size,raw))
        def offset(address):
            return next(raw+address-start for start,size,raw in mappings if start<=address<start+size)
        target=struct.unpack_from('<I',data,offset(0x1a10338))[0]
        self.assertEqual(data[offset(target):offset(target)+5],b'.srt\0')
        # 748260 lowercases and rfinds this exact native dispatch token, then
        # returns found!=npos (raw code repairs the decompiler's missing AL).
        self.assertEqual(data[offset(0x748303):offset(0x74830e)],bytes.fromhex('8d4dd8e855fdffff83f8ff'))
        srt=[];spt=[]
        for package in (client/'res/packages').glob('*.pkg'):
            with zipfile.ZipFile(package)as archive:
                srt.extend((package.name,n)for n in archive.namelist()if'.srt'in n.lower())
                spt.extend(n for n in archive.namelist()if n.lower().endswith('.spt'))
        self.assertFalse(srt,'New SRT resources require their actual renderer/submission owner')
        self.assertTrue(spt,'The native old-ST4 resource path must actually exist')

    def test_original_velocity_masks_and_actual_old_st4_shader_writers(self):
        client=Path(os.environ.get('WOT_0922_CLIENT','/Users/peng/Downloads/World_of_Tanks_0.09.22.00.01_CH_1513_HD'))
        if not(client/'res/packages/shaders.pkg').is_file():self.skipTest('Original #1513 shaders required')
        with tempfile.TemporaryDirectory(prefix='aa-original-shaders-')as folder:
            root=Path(folder);cpp=root/'probe.cpp';binary=root/'probe';cpp.write_text(PROBE)
            command=['c++','-std=c++17','-Wall','-Wextra','-Werror','-UNDEBUG','-I'+str(ROOT/'include'),str(cpp)]
            command += [str(ROOT/'src'/name)for name in ('stock_shader_binary.cpp','stock_shader_runtime.cpp','stock_effect_binding.cpp')]
            result=subprocess.run(command+['-o',str(binary)],capture_output=True,text=True);self.assertEqual(result.returncode,0,result.stderr)
            with zipfile.ZipFile(client/'res/packages/shaders.pkg')as archive:
                cases=[('velocity','shaders/post_processing/velocity_buffer.10.dx9.fxo')]
                cases += [(mode if variant==10 else 'forward','shaders/speedtree/%s.%d.dx9.fxo'%(part,variant))
                    for part,mode in [('branches','branch'),('leaves','leaf'),('billboards','billboard')]for variant in (10,11)]
                for index,(mode,resource)in enumerate(cases):
                    data=zipfile.ZipFile(io.BytesIO(archive.read(resource))).read('effect')
                    path=root/('%d.effect'%index);path.write_bytes(data)
                    result=subprocess.run([str(binary),mode,str(path)],capture_output=True,text=True)
                    self.assertEqual(result.returncode,0,resource+'\n'+result.stdout+result.stderr)
