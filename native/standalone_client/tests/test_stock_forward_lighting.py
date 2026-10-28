"""Compare forward SH consumption with #1513 x86 and original terrain shaders."""
import io
import os
from pathlib import Path
import random
import shutil
import struct
import subprocess
import tempfile
import unittest
import zipfile

from native_test_compile_flags import native_test_compile_flags

ROOT = Path(__file__).resolve().parents[1]
CORE = r'''
#define WOT_STOCK_DEFERRED_LIGHTING_PORTABLE
#define WOT_STOCK_DEFERRED_LIGHTING_CORE_ONLY
#define BW_BEGIN_NAMESPACE
#define BW_END_NAMESPACE
struct Vector3 {float values[3];float operator[](unsigned i) const{return values[i];}};
#include "@PRODUCTION@"
'''
SCALE = r'''
#include <iostream>
int main(){float source[28],out[28],multiplier;Vector3 ambient;
while(std::cin.read(reinterpret_cast<char*>(source),sizeof(source))){
std::cin.read(reinterpret_cast<char*>(&ambient),sizeof(ambient));
std::cin.read(reinterpret_cast<char*>(&multiplier),sizeof(multiplier));
if(!std::cin)return 2;
scaleStockForwardSphericalHarmonics(source,ambient,multiplier,out);
std::cout.write(reinterpret_cast<const char*>(out),sizeof(out));}
return std::cin.eof()&&std::cout?0:2;}
'''
CONSUMER = r'''
#include "wot/stock_shader_binary.hpp"
#include "wot/stock_effect_binding.hpp"
#include "wot/stock_shader_runtime.hpp"
#include <cassert>
#include <cstring>
#include <fstream>
#include <iterator>
int main(int argc,char**argv){assert(argc==3);
for(int arg=1;arg<argc;++arg){
std::ifstream input(argv[arg],std::ios::binary);std::vector<unsigned char> raw((std::istreambuf_iterator<char>(input)),{});
auto definition=std::make_shared<wot::StockEffectBinary>();std::string error;
assert(wot::decode_stock_dx9_effect(raw.data(),raw.size(),*definition,error));
wot::StockEffectUniformPool pool;wot::StockEffectUniformContext context(pool);wot::StockEffectBinding binding(definition,context);
float packed[28];for(unsigned i=0;i<28;++i)packed[i]=float(i+1)*.03125f;
Vector3 ambient{{.25f,.5f,.75f}};float consumed[28];
scaleStockForwardSphericalHarmonics(packed,ambient,2.29f,consumed);
bool wrote=false;
for(unsigned b=0;b<definition->uniform_buffers.size();++b)
 for(unsigned u=0;u<definition->uniform_buffers[b].uniforms.size();++u)
  if(definition->uniform_buffers[b].uniforms[u].semantic=="REM_SH9"){
   // Auto constants use shared capture storage, not the private material setter.
   auto field=definition->uniform_buffers[b].uniforms[u];field.flags&=~2u;
   assert(binding.uniform_buffer(b)->write_uniform(field,definition->types[field.type_index],consumed,sizeof(consumed),error));wrote=true;}
assert(wrote);
for(unsigned t=0;t<definition->techniques.size();++t){
 if(definition->techniques[t].name!="Color"&&definition->techniques[t].name!="ColorGridOnly")continue;
 wot::StockDx9PassSelection pass;assert(binding.select_pass(t,0,pass,error)&&pass.pixel_shader);
 const auto& shader=definition->shaders[*pass.pixel_shader];std::vector<wot::StockShaderUniformView> views;
 for(const auto& resource:shader.resources)if(resource.kind==3){
  std::optional<unsigned> resolved;
  assert(wot::resolve_stock_effect_reference(*definition,binding.selection(),wot::StockEffectResourceTable::uniform_buffer,resource.resource_index,resolved,error)&&resolved);
  if(resource.shader_slot>=views.size())views.resize(resource.shader_slot+1);
  const auto& bytes=binding.uniform_buffer(*resolved)->bytes();views[resource.shader_slot]={bytes.data(),bytes.size()};}
 wot::StockShaderRegisters registers;assert(wot::stage_stock_shader_registers(shader,views,registers,error));
 // Both original forward terrain pixel programs read linear SH RGB at c3..c5.
 assert(registers[2].words.size()>=24);
 assert(std::memcmp(registers[2].words.data()+12,consumed,48)==0);
}
}
}
'''


class ForwardLightingTests(unittest.TestCase):
    def client(self):
        configured = os.environ.get('WOT_0922_CLIENT')
        if not configured:
            self.skipTest('WOT_0922_CLIENT required for original forward evidence')
        client = Path(configured)
        for relative in ('WorldOfTanks.exe', 'res/packages/shaders.pkg', 'res/packages/33_fjord.pkg'):
            self.assertTrue((client / relative).is_file(), 'Configured client missing: ' + relative)
        return client

    def compile(self, root, source, extra=()):
        configured = os.environ.get('CXX')
        compiler = shutil.which(configured or 'c++')
        if not compiler:
            if configured:
                raise AssertionError('Configured compiler missing: ' + configured)
            self.skipTest('C++ compiler required')
        target = root / 'probe.cpp'
        target.write_text(CORE.replace('@PRODUCTION@', str(ROOT / 'bigworld/stock_deferred_lighting.cpp')) + source)
        binary = root / ('probe.exe' if os.name == 'nt' else 'probe')
        subprocess.run([compiler, *native_test_compile_flags(compiler), '-std=c++17', '-Wall', '-Wextra',
                        '-Werror', '-UNDEBUG', '-ffp-contract=off', '-I', str(ROOT / 'include'),
                        str(target), *(str(p) for p in extra), '-o', str(binary)], check=True)
        return binary

    def test_original_forward_callback_instructions(self):
        client = self.client()
        try:
            import pefile
            from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
            from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_ESP, UC_X86_REG_EIP, UC_X86_REG_MXCSR
        except ImportError as error:
            self.skipTest('Optional original PE oracle dependency missing: ' + str(error))
        pe = pefile.PE(str(client / 'WorldOfTanks.exe'))
        self.assertEqual(pe.FILE_HEADER.Machine, 0x14c)
        image, base = pe.get_memory_mapped_image(), pe.OPTIONAL_HEADER.ImageBase
        self.assertEqual(image[0xa746f0-base:0xa746f6-base], bytes.fromhex('55 8b ec 83 ec 78'))
        machine = Uc(UC_ARCH_X86, UC_MODE_32)
        machine.mem_map(base, (len(image)+4095)&~4095);machine.mem_write(base,image)
        memory = 0x30000000;machine.mem_map(memory,0x10000)
        wrapper,owner,rc,sun,stack = (memory+x for x in (0,0x100,0x1000,0x2000,0x8000))
        machine.mem_write(wrapper+4,struct.pack('<I',owner));machine.mem_write(rc+0x79c,struct.pack('<I',sun))
        captured = []
        def code(uc,address,unused_size,unused):
            if address in (0xa49a80,0x9288d0):
                sp=uc.reg_read(UC_X86_REG_ESP);ret=struct.unpack('<I',uc.mem_read(sp,4))[0]
                uc.reg_write(UC_X86_REG_EAX,1 if address==0xa49a80 else rc)
                uc.reg_write(UC_X86_REG_ESP,sp+4);uc.reg_write(UC_X86_REG_EIP,ret)
            elif address==0x74b870:
                sp=uc.reg_read(UC_X86_REG_ESP);start,end=struct.unpack('<II',uc.mem_read(sp+4,8))
                self.assertEqual(end-start,112);captured.append(bytes(uc.mem_read(start,112)));uc.emu_stop()
        machine.hook_add(UC_HOOK_CODE,code)
        generator=random.Random(1513)
        rows=[([float(i-14) for i in range(28)],[.2,.3,.4],2.29),([1.]*28,[0.,0.,0.],1.),([1.]*28,[1.,1.,1.],0.)]
        rows += [([generator.uniform(-10,10) for _ in range(28)],
                  [generator.uniform(0,2) for _ in range(3)],generator.uniform(0,4)) for _ in range(256)]
        inputs=bytearray()
        for packed,ambient,multiplier in rows:
            encoded=struct.pack('<32f',*(packed+ambient+[multiplier]));inputs.extend(encoded)
            machine.mem_write(owner+0x5c,encoded[:112]);machine.mem_write(sun+0x18,encoded[112:124]);machine.mem_write(owner+0x34,encoded[124:])
            machine.mem_write(stack,struct.pack('<III',0,0,memory+0x3000))
            machine.reg_write(UC_X86_REG_ECX,wrapper);machine.reg_write(UC_X86_REG_ESP,stack);machine.reg_write(UC_X86_REG_MXCSR,0x1f80)
            machine.emu_start(0xa746f0,0xa74825,count=1000)
        self.assertEqual(len(captured),len(rows))
        with tempfile.TemporaryDirectory(prefix='forward-sh-native-') as folder:
            binary=self.compile(Path(folder),SCALE)
            actual=subprocess.check_output([str(binary)],input=inputs)
        self.assertEqual(actual,b''.join(captured))

    def test_original_terrain_forward_shader_register_consumption(self):
        client = self.client()
        with tempfile.TemporaryDirectory(prefix='forward-sh-shader-') as folder:
            root=Path(folder)
            inputs=[]
            with zipfile.ZipFile(client/'res/packages/shaders.pkg') as package:
                for name in ('terrain2','terrain2_lod_texture'):
                    raw=package.read('shaders/terrain/'+name+'.11.dx9.fxo')
                    target=root/(name+'.arie')
                    target.write_bytes(zipfile.ZipFile(io.BytesIO(raw)).read('effect'));inputs.append(target)
            sources=[ROOT/'src'/name for name in ('stock_shader_binary.cpp','stock_effect_binding.cpp','stock_shader_runtime.cpp')]
            binary=self.compile(root,CONSUMER,sources)
            subprocess.run([str(binary),*(str(p) for p in inputs)],check=True)


if __name__ == '__main__':
    unittest.main()
