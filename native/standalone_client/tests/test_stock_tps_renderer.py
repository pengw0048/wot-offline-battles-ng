"""Bind the production tPS parameters through the real adapter and #1513 FXO."""
import io
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest
import zipfile

from test_stock_effect_material_handles import D3DX, DX9

ROOT = Path(__file__).resolve().parents[1]

HOST = r'''
#define WOT_STOCK_TPS_RENDERER_PORTABLE
#include "stock_tps_renderer.hpp"
#include "stock_effect_adapter.hpp"
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
void check(bool value,const std::string& message){if(!value)throw std::runtime_error(message);}
struct Effect {ID3DXEffect* value;ID3DXEffect* pEffect(){return value;}};
struct Material {Effect value;Effect* pEffect(){return &value;}};
struct Texture {IDirect3DBaseTexture9* value;IDirect3DBaseTexture9* pTexture(){return value;}};
bool bind(ID3DXEffect* value,IDirect3DBaseTexture9* texture,
          const wot::StockTPSDrawParameters& parameters,std::string& error){
    Material material{{value}};Material* material_=&material;
    Texture diffuse{texture};Texture* texture_=&diffuse;
@BIND@
    return true;
}
int main(int argc,char** argv){try{
    check(argc==3,"Both original pipeline variants are required");
    unsigned bindings=0;
    for(int variant=1;variant<argc;++variant){
        std::ifstream input(argv[variant],std::ios::binary);
        std::vector<char> bytes((std::istreambuf_iterator<char>(input)),{});
        auto* device=new IDirect3DDevice9;auto* states=new ID3DXEffectStateManager;
        auto context=std::make_shared<wot::StockEffectSharedContext>(wot::StockEffectConstantProvider{});
        ID3DXEffect* definition=nullptr;ID3DXEffect* effect=nullptr;std::string error;
        check(SUCCEEDED(wot::create_stock_effect(device,states,bytes.data(),bytes.size(),context,&definition,error)),error);
        check(SUCCEEDED(wot::create_stock_effect_material(definition,&effect)),"Material binding");
        for(const char* name:{"particleQuality","particleTransformMode","particleDebugMode","particleTransparencyMode"}){
            D3DXPARAMETER_DESC desc{};
            check(SUCCEEDED(effect->GetParameterDesc(effect->GetParameterByName(nullptr,name),&desc))&&
                  desc.Type==D3DXPT_INT,std::string("Original integer selector: ")+name);
        }
        D3DXPARAMETER_DESC desc{};
        check(SUCCEEDED(effect->GetParameterDesc(effect->GetParameterByName(nullptr,"g_offscreenMode"),&desc))&&
              desc.Type==D3DXPT_BOOL,"Original offscreen selector is BOOL");
        // Reproduce the live failure without a permissive D3DX fake: the
        // production ARIE binding rejects an integer write to this bool.
        check(effect->SetInt("g_offscreenMode",0)==E_INVALIDARG,"Old integer setter must fail");
        check(effect->SetBool("g_offscreenMode",FALSE)==S_OK,"Original boolean setter");
        auto* texture=new IDirect3DBaseTexture9;
        for(int quality=0;quality<=4;++quality)for(int mode=0;mode<3;++mode)
            for(int offscreen=0;offscreen<2;++offscreen)for(int transparency=0;transparency<2;++transparency){
                const wot::StockTPSDrawParameters parameters{quality,mode,0,offscreen,transparency,.75f,1.f};
                check(bind(effect,texture,parameters,error),error);
                UINT passes=0;
                check(SUCCEEDED(effect->SetTechnique(effect->GetTechniqueByName("Render")))&&
                      SUCCEEDED(effect->Begin(&passes,D3DXFX_DONOTSAVESTATE))&&passes!=0,"Begin original tPS");
                for(UINT pass=0;pass<passes;++pass){
                    check(SUCCEEDED(effect->BeginPass(pass)),"Begin original tPS pass");
                    bool sampled=false;for(const auto& slot:states->textures)sampled|=slot.second==texture;
                    check(sampled,"Original tPS diffuse texture reaches the selected shader");
                    check(SUCCEEDED(effect->EndPass()),"End original tPS pass");
                }
                check(SUCCEEDED(effect->End()),"End original tPS");++bindings;
            }
        texture->Release();effect->Release();definition->Release();device->Release();states->Release();
    }
    std::cout<<bindings<<" original tPS parameter bindings and shader passes passed\n";
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
'''


class StockTPSRendererTests(unittest.TestCase):
    def test_original_effect_parameter_types_and_render_passes(self):
        compiler = shutil.which('c++')
        client = os.environ.get('WOT_0922_CLIENT')
        if not compiler or not client:
            self.skipTest('Set WOT_0922_CLIENT and provide a host C++ compiler')
        source = (ROOT / 'bigworld/stock_tps_renderer.cpp').read_text()
        begin = source.index('        ID3DXEffect * effect =')
        end = source.index('        IDirect3DDevice9 * device =', begin)
        binding = source[begin:end]
        adapter = (ROOT / 'bigworld/stock_effect_adapter.cpp').read_text()
        body = adapter[adapter.index('class StockEffectAdapter final : public ID3DXEffect {'):]
        methods = re.findall(r'^\s+([A-Z][^;]*?)\s+override;', body[:body.index('private:')], re.M | re.S)
        with tempfile.TemporaryDirectory(prefix='tps-render-binding-') as temp:
            folder = Path(temp)
            (folder / 'd3d9.h').write_text(DX9.DEVICE)
            (folder / 'd3dx9effect.h').write_text(D3DX.replace('METHODS', '\n'.join(
                '    virtual ' + ' '.join(method.split()) + ' = 0;' for method in methods)))
            effects = []
            with zipfile.ZipFile(Path(client) / 'res/packages/shaders.pkg') as archive:
                for variant in ('10', '11'):
                    member = 'shaders/wg_particles/ps.%s.dx9.fxo' % variant
                    path = folder / ('ps.' + variant + '.effect')
                    path.write_bytes(zipfile.ZipFile(io.BytesIO(archive.read(member))).read('effect'))
                    effects.append(str(path))
            for name, block in (('production', binding), ('old-integer', binding.replace(
                    'SetBool( "g_offscreenMode", parameters.offscreenMode )',
                    'SetInt( "g_offscreenMode", parameters.offscreenMode )'))):
                path = folder / (name + '.cpp')
                path.write_text(HOST.replace('@BIND@', block))
                binary = folder / name
                subprocess.run([compiler, '-std=c++17', '-Wall', '-Wextra', '-Werror',
                                '-Wno-unused-parameter', '-I' + str(folder), '-I' + str(ROOT / 'include'),
                                '-I' + str(ROOT / 'bigworld'), str(path),
                                *[str(ROOT / 'bigworld' / cpp) for cpp in ('stock_effect_adapter.cpp', 'stock_effect_dx9.cpp')],
                                *[str(ROOT / 'src' / cpp) for cpp in ('stock_effect_binding.cpp', 'stock_effect_reflection.cpp',
                                                                   'stock_shader_binary.cpp', 'stock_shader_runtime.cpp')],
                                '-o', str(binary)], check=True)
                result = subprocess.run([str(binary)] + effects, capture_output=True, text=True)
                self.assertEqual(result.returncode == 0, name == 'production', (name, result.stdout, result.stderr))
                if name == 'production':
                    self.assertIn('120 original tPS parameter bindings and shader passes passed', result.stdout)


if __name__ == '__main__':
    unittest.main()
