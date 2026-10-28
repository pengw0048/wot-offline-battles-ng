"""Compile the actual GPU producer against a lifecycle-aware D3D/SDK fake."""
import shutil
import subprocess
import tempfile
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]


class GpuProducerTests(unittest.TestCase):
    def test_producer_resources_and_failures(self):
        compiler = shutil.which('c++')
        if not compiler:
            self.skipTest('C++ compiler unavailable')
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            names = ('pch.hpp', 'cstdmf/stdmf.hpp', 'moo/effect_material.hpp',
                     'moo/fullscreen_quad.hpp', 'moo/init.hpp', 'moo/moo_utilities.hpp',
                     'moo/render_context.hpp', 'moo/render_target.hpp', 'moo/renderer.hpp', 'dxgi.h')
            for name in names:
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text('#pragma once\n')
            source = (ROOT / 'tests/stock_gpu_detection_host.cpp').read_text()
            production = (ROOT / 'bigworld/stock_gpu_detection.cpp').read_text()
            production = '\n'.join(line for line in production.splitlines() if not line.startswith('#include'))
            source = source.replace('#include "stock_gpu_detection.cpp"', production)
            source = '#include "wot/stock_gpu_performancer.hpp"\n#include "wot/stock_gpu_sampling.hpp"\n' + source
            (root / 'host.cpp').write_text(source)
            subprocess.run([compiler, '-std=c++14', '-Wall', '-Wextra', '-Werror',
                            '-I', str(root), '-I', str(ROOT / 'include'), '-I', str(ROOT / 'bigworld'),
                            str(root / 'host.cpp'), '-o', str(root / 'probe')], check=True)
            subprocess.run([str(root / 'probe')], check=True)

    def test_exact_client_effect_techniques_and_texture_bindings(self):
        import io
        import os
        import zipfile
        client = os.environ.get('WOT_0922_CLIENT')
        compiler = shutil.which('c++')
        if not client or not compiler:
            self.skipTest('Exact #1513 client and C++ compiler are required')
        source = r'''
#include "wot/stock_shader_binary.hpp"
#include <cassert>
#include <fstream>
#include <iterator>
int main(int argc,char** argv){assert(argc==2);std::ifstream f(argv[1],std::ios::binary);
std::vector<unsigned char> b((std::istreambuf_iterator<char>(f)),{});
wot::StockEffectBinary e;std::string error;assert(wot::decode_stock_dx9_effect(b.data(),b.size(),e,error));
for(const char* name:{"GenRandTex","Julia","RandTex"}){bool found=false;for(const auto& t:e.techniques)if(t.name==name){assert(t.passes.size()==1);found=true;}assert(found);}
for(const char* name:{"g_texture0","g_texture1"}){bool found=false;for(const auto& r:e.resource_views)found|=r.name==name;assert(found);}}
'''
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            (root / 'probe.cpp').write_text(source)
            subprocess.run([compiler, '-std=c++17', '-I', str(ROOT / 'include'),
                            str(root / 'probe.cpp'), str(ROOT / 'src/stock_shader_binary.cpp'),
                            '-o', str(root / 'probe')], check=True)
            with zipfile.ZipFile(Path(client) / 'res/packages/shaders.pkg') as shaders:
                for variant in ('10', '11'):
                    name = 'shaders/std_effects/wg_gpu_performancer.%s.dx9.fxo' % variant
                    with zipfile.ZipFile(io.BytesIO(shaders.read(name))) as effect:
                        (root / 'effect').write_bytes(effect.read('effect'))
                    subprocess.run([str(root / 'probe'), str(root / 'effect')], check=True)

    def test_original_display_format_names(self):
        import os
        import struct
        client = os.environ.get('WOT_0922_CLIENT')
        if not client:
            self.skipTest('Exact #1513 client is required')
        try:
            import pefile
        except ImportError:
            self.skipTest('pefile is required')
        pe = pefile.PE(str(Path(client) / 'WorldOfTanks.exe'))
        data = pe.get_memory_mapped_image()
        base = pe.OPTIONAL_HEADER.ImageBase
        formats = {}
        for address in range(0x1851608, 0x1851660, 8):
            index, pointer = struct.unpack_from('<II', data, address - base)
            formats[index] = data[pointer - base:].split(b'\0', 1)[0].decode('ascii')
        self.assertEqual(formats[3], 'X8R8G8B8')
        self.assertEqual(formats[9], 'A8B8G8R8')
