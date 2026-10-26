"""Read the real old-ST4 FX pass annotations, constant ABI and vertex formats."""
from pathlib import Path
import io
import os
import subprocess
import sys
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[1]
PROBE = r'''
#include "wot/stock_shader_binary.hpp"
#include <cassert>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
int main(int argc,char** argv){
 for(int i=1;i+1<argc;i+=2){
  const unsigned count=static_cast<unsigned>(std::stoul(argv[i]));
  std::map<std::string,unsigned> expected{{"COLOR",0},{"DEPTH",3}};
  if(count==8){expected.emplace("REFLECTION",1);expected.emplace("SHADOW",2);}
  else assert(count==4);
  std::ifstream stream(argv[i+1],std::ios::binary);assert(stream);
  std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(stream)),{});
  wot::StockEffectBinary effect;std::string error;assert(wot::decode_stock_dx9_effect(bytes.data(),bytes.size(),effect,error));
  assert(effect.techniques.size()==count);
  for(const auto& pass:expected)for(unsigned instanced=0;instanced!=2;++instanced){
   const auto name=pass.first+(instanced?"_INSTANCED":"");bool found=false;
   for(const auto& technique:effect.techniques)if(technique.name==name){
    assert(technique.passes.size()==1);bool renderType=false,instanceType=false;
    for(const auto& annotation:effect.annotations.at(technique.annotations)){
     if(annotation.name!="renderType"&&annotation.name!="instanced")continue;
     assert(annotation.value.size()==4);unsigned value;std::memcpy(&value,annotation.value.data(),4);
     if(annotation.name=="renderType"){assert(value==pass.second);renderType=true;}
     if(annotation.name=="instanced"){assert(value==instanced);instanceType=true;}
    }
    assert(renderType&&instanceType);found=true;
   }
   assert(found);
  }
  std::map<std::string,unsigned> sizes;
  for(const auto& buffer:effect.uniform_buffers)for(const auto& uniform:buffer.uniforms)
   sizes.emplace(uniform.name,effect.types.at(uniform.type_index).byte_size*(uniform.elements?uniform.elements:1));
  assert(sizes.at("g_instance")==64&&sizes.at("g_material")==32);
  assert(sizes.at("g_windMatrices")==384&&sizes.at("g_prevWindMatrices")==384);
  assert(sizes.at("g_billboardCameraRotation")==16&&sizes.at("g_reflCorrection")==4);
 }
}
'''


class StockTreeEffectsTests(unittest.TestCase):
    def test_all_real_variants_have_exact_pass_and_vertex_constant_contracts(self):
        client = os.environ.get('WOT_0922_CLIENT')
        if not client:
            self.skipTest('WOT_0922_CLIENT is required for actual tree effects')
        sys.path.insert(0, str(ROOT.parents[1] / 'tools'))
        from packed_xml import read_packed_xml, TYPE_ELEMENT
        with tempfile.TemporaryDirectory(prefix='stock-tree-effects-') as directory:
            directory = Path(directory)
            source, binary = directory / 'probe.cpp', directory / 'probe'
            source.write_text(PROBE)
            subprocess.run(['c++', '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-UNDEBUG',
                            '-I'+str(ROOT / 'include'), str(source), str(ROOT / 'src/stock_shader_binary.cpp'),
                            '-o', str(binary)], check=True)
            effects = []
            with zipfile.ZipFile(Path(client) / 'res/packages/shaders.pkg') as archive:
                for part in ('branches', 'leaves', 'billboards'):
                    for variant in (10, 11):
                        name = 'shaders/speedtree/%s.%d.dx9.fxo' % (part, variant)
                        with zipfile.ZipFile(io.BytesIO(archive.read(name))) as outer:
                            effect = directory / ('%s-%d.effect' % (part, variant))
                            effect.write_bytes(outer.read('effect'))
                            effects.extend([str(8 if variant == 10 else 4), str(effect)])
                for part, expected_stride in (('branch', 52), ('leaf', 88), ('billboard', 68)):
                    node = read_packed_xml(archive.read('shaders/formats/speedtree_'+part+'.xml'))
                    elements = next(value.value for name, value in node.children if name == b'elements')
                    stride = 0
                    for name, element in elements.children:
                        # VertexElement defaults to FLOAT3 when type is absent.
                        children = element.value.children if element.value_type == TYPE_ELEMENT else []
                        storage = next((value.value for name, value in children if name == b'type'), b'FLOAT3')
                        self.assertIn(storage, (b'FLOAT2', b'FLOAT3', b'FLOAT4'))
                        stride += {b'FLOAT2': 8, b'FLOAT3': 12, b'FLOAT4': 16}[storage]
                    self.assertEqual(stride, expected_stride)
            subprocess.run([str(binary), *effects], check=True)
            print('Actual tree FX: deferred variants have 8 techniques, forward variants have 4; 64B instance and 52/88/68B declarations match')


if __name__ == '__main__':
    unittest.main()
