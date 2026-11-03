"""The generated swatch protocol preserves original native pixel semantics."""
import importlib.util
import os
from pathlib import Path
import random
import shutil
import struct
import subprocess
import tempfile
import unittest

from native_test_compile_flags import native_test_compile_flags
from stock_battle_gui_support import client_executable, client_package, python27

ROOT = Path(__file__).resolve().parents[1]


class FlashCamouflageTests(unittest.TestCase):
    def build(self, root):
        compiler = shutil.which(os.environ.get('CXX', 'c++'))
        if not compiler:
            self.skipTest('Host C++ compiler required')
        path = root / 'host.cpp'
        path.write_text(HOST)
        binary = root / 'host'
        subprocess.run([compiler, '-std=c++14', '-Wall', '-Wextra', '-Werror', '-ffp-contract=off',
                        *native_test_compile_flags(compiler), '-I', str(ROOT / 'bigworld'),
                        str(path), str(ROOT / 'bigworld/stock_flash_camouflage.cpp'), '-o', str(binary)], check=True)
        return binary

    def test_schema_and_observed_live_uri_are_general(self):
        with tempfile.TemporaryDirectory(prefix='flash-camouflage-schema-') as folder:
            binary = self.build(Path(folder))
            result = subprocess.run([str(binary)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_original_camo_icon_producer_reaches_the_parser(self):
        script = r'''
import marshal,types,struct,zipfile,sys
code=marshal.loads(zipfile.ZipFile(sys.argv[1]).read('scripts/client/gui/shared/gui_items/customization/c11n_items.pyc')[8:])
template=next(v for v in code.co_consts if isinstance(v,str) and v.startswith('img://camouflage,'))
producer=next(v for v in code.co_consts if isinstance(v,types.CodeType) and v.co_name=='camoIconTemplate')
class Math:
    @staticmethod
    def Vector4(*values):return tuple(struct.unpack('<f',struct.pack('<f',v))[0] for v in values)
function=types.FunctionType(producer,{'Math':Math,'_CAMO_ICON_TEMPLATE':template,'__builtins__':__builtins__},argdefs=('gui/maps/vehicles/camouflages/camo_back.dds',))
for colors in ([0,0xffffffff,0x80000000,0xff123456],[4280297506,4283005561,4281883201,0]):
    print(function('vehicles/chinese/Camouflage/china_historical_08.dds',128,128,colors))
'''
        result = subprocess.run([python27(self), '-c', script, str(client_package(self))],
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        with tempfile.TemporaryDirectory(prefix='flash-camouflage-consumer-') as folder:
            binary = self.build(Path(folder))
            parsed = subprocess.run([str(binary), 'uris'], input=result.stdout,
                                    capture_output=True, text=True, check=True)
        palettes = ([0, 0xffffffff, 0x80000000, 0xff123456],
                    [4280297506, 4283005561, 4281883201, 0])
        expected = []
        for colors in palettes:
            weights = [struct.unpack('<I', struct.pack('<f', (color >> 24) / 255.))[0]
                       for color in colors]
            expected.append(' '.join(str(value) for value in [128, 128, *colors, *weights]))
        self.assertEqual(parsed.stdout.splitlines(), expected)

    def test_pixels_match_the_original_x86_formula(self):
        if importlib.util.find_spec('unicorn') is None:
            self.skipTest('Pinned Unicorn audit dependency required')
        from stock_flash_camouflage_oracle import OriginalCamouflagePixel
        oracle = OriginalCamouflagePixel(client_executable(self))
        rng = random.Random(1513)
        cases = [
            (0, 0x12345678, [0xffffffff] * 4, [1.] * 4),
            (0xffffffff, 0xff000000, [0xffff0000, 0xff00ff00, 0xff0000ff, 0xff112233], [1.] * 4),
            (0x00ff0000, 0xff000000, [0xffff0000, 0xff00ff00, 0xff0000ff, 0], [1., 0., 0., 0.]),
        ]
        for unused in range(600):
            colors = [rng.getrandbits(32) for i in range(4)]
            weights = [struct.unpack('<f', struct.pack('<f', ((color >> 24) & 255) / 255.))[0] for color in colors]
            cases.append((rng.getrandbits(32), rng.getrandbits(32), colors, weights))
        inputs = b''.join(struct.pack('<6I4f', mask, background, *colors, *weights)
                          for mask, background, colors, weights in cases)
        with tempfile.TemporaryDirectory(prefix='flash-camouflage-oracle-') as folder:
            binary = self.build(Path(folder))
            result = subprocess.run([str(binary), 'pixels'], input=inputs, capture_output=True, check=True)
        actual = struct.unpack('<%dI' % len(cases), result.stdout)
        expected = [oracle.pixel(*case) for case in cases]
        self.assertEqual(actual, tuple(expected))

    def test_native_row_loop_preserves_decoded_buffer_order(self):
        if importlib.util.find_spec('unicorn') is None:
            self.skipTest('Pinned Unicorn audit dependency required')
        from stock_flash_camouflage_oracle import OriginalCamouflagePixel
        oracle = OriginalCamouflagePixel(client_executable(self))
        mask = [0x00ff0000, 0x0000ff00, 0x000000ff, 0xff000000, 0, 0x80808080]
        background = [0xff102030 + value * 0x010101 for value in range(6)]
        colors = [0xffff0000, 0xff00ff00, 0xff0000ff, 0xfff0e0d0]
        weights = [1., 1., 1., .5]
        expected = oracle.image(struct.pack('<6I', *mask), struct.pack('<6I', *background),
                                2, 3, colors, weights)
        inputs = b''.join(struct.pack('<6I4f', a, b, *colors, *weights)
                          for a, b in zip(mask, background))
        with tempfile.TemporaryDirectory(prefix='flash-camouflage-rows-') as folder:
            binary = self.build(Path(folder))
            result = subprocess.run([str(binary), 'pixels'], input=inputs, capture_output=True, check=True)
        self.assertEqual(result.stdout, expected)
        self.assertNotEqual(result.stdout, b''.join(expected[y * 8:(y + 1) * 8]
                                                    for y in reversed(range(3))))


HOST = r'''
#include "stock_flash_camouflage.hpp"
#include <cstdio>
#include <cstring>
#include <iostream>
#define CHECK(value) do{if(!(value))return 41;}while(0)
int main(int argc,char**argv){
 using namespace wot::flash_camouflage;
 if(argc>1&&std::string(argv[1])=="uris"){
  std::string uri;while(std::getline(std::cin,uri)){Parameters p;CHECK(parse(uri.c_str(),p));std::cout<<p.width<<' '<<p.height;
   for(auto color:p.colors)std::cout<<' '<<color;
   for(auto weight:p.weights){std::uint32_t bits;std::memcpy(&bits,&weight,4);std::cout<<' '<<bits;}
   std::cout<<'\n';}return 0;
 }
 if(argc>1){
  (void)argv;std::uint32_t words[6];float weights[4];
  while(fread(words,sizeof(words),1,stdin)==1){
   if(fread(weights,sizeof(weights),1,stdin)!=1)return 42;
   Parameters p;for(unsigned i=0;i<4;++i){p.colors[i]=words[i+2];p.weights[i]=weights[i];}
   const auto result=pixel(words[0],words[1],p);fwrite(&result,4,1,stdout);
  }return 0;
 }
 Parameters p;
 const char*live="img://camouflage,128,128,\"vehicles/chinese/Camouflage/china_historical_08.dds\",\"gui/maps/vehicles/camouflages/camo_back.dds\",4280297506,4283005561,4281883201,0,1.0,1.0,1.0,0.0";
 CHECK(parse(live,p));CHECK(p.width==128&&p.height==128&&p.colors[0]==4280297506u);
 CHECK(p.texture=="vehicles/chinese/Camouflage/china_historical_08.dds"&&p.weights[3]==0.f);
 CHECK(parse("IMG://CAMOUFLAGE,3,5,\"a,b.dds\",\"background.png\",-1,2147483648,0,4294967295,0,.5,1,0.25\n",p));
 CHECK(p.colors[0]==0xffffffffu&&p.colors[1]==0x80000000u&&p.width==3&&p.height==5);
 const auto previous=p;
 for(const char*bad:{"", "img://x", "img://camouflage,0,1,\"a\",\"b\",0,0,0,0,0,0,0,0",
  "img://camouflage,-1,1,\"a\",\"b\",0,0,0,0,0,0,0,0",
  "img://camouflage,4294967295,1,\"a\",\"b\",0,0,0,0,0,0,0,0",
  "img://camouflage,1,1,\"a\",\"\",0,0,0,0,0,0,0,0",
  "img://camouflage,1,1,\"a\",\"b\",4294967296,0,0,0,0,0,0,0",
  "img://camouflage,1,1,\"a\",\"b\",0,0,0,0,nan,0,0,0",
  "img://camouflage,1,1,\"a\",\"b\",0,0,0,0,inf,0,0,0",
  "img://camouflage,1,1,\"a\",\"b\",0,0,0,0,1.1,0,0,0",
  "img://camouflage,1,1,\"a\",\"b\",0,0,0,0,1,0,0",
  "img://camouflage,1,1,\"a\",\"b\",0,0,0,0,1,0,0,0,trailing"}){
  CHECK(!parse(bad,p));CHECK(p.width==previous.width&&p.texture==previous.texture);
 }
 CHECK(!parse(nullptr,p));
 Parameters solid;solid.colors[0]=0xff0000ffu;solid.weights[0]=1.f;
 CHECK(pixel(0x00ff0000u,0xff000000u,solid)==0xff0000ffu);
}
'''


if __name__ == '__main__':
    unittest.main()
