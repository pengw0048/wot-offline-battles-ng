"""Retire real ARIE manual textures and parameter-cache references before reset."""

import importlib.util
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    value = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(value)
    return value


GUI = load('retirement_gui_fixture', Path(__file__).with_name('test_stock_gui_filter_binding.py'))


def function(source, signature):
    start = source.index(signature)
    end = source.index('{', start)
    depth = 1
    while depth:
        end += 1
        depth += (source[end] == '{') - (source[end] == '}')
    return source[start:end + 1]


HOST = r'''
#include "stock_effect_adapter.hpp"
#include <cassert>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>
#define BW_GUARD
template<class T> struct ComObjectWrap {
    T* value = nullptr;
    ComObjectWrap(T* p = nullptr) : value(p) { if (value) value->AddRef(); }
    ComObjectWrap(const ComObjectWrap& other) : ComObjectWrap(other.value) {}
    ~ComObjectWrap() { if (value) value->Release(); }
    ComObjectWrap& operator=(const ComObjectWrap& other) {
        if (other.value) other.value->AddRef();
        if (value) value->Release(); value = other.value; return *this;
    }
    T* pComObject() const { return value; }
};
struct Cache {
    ComObjectWrap<ID3DXEffect> spEffect_;
    unsigned clears = 0;
    void clear() { ++clears; }
    @CACHE@
};
namespace Moo {
struct Managed {
    ID3DXEffect* definition;
    ID3DXEffect* pEffect() { return definition; } // end() has restored Definition.
};
struct Properties {
    ID3DXEffect* binding = nullptr;
    unsigned creations = 0;
    ~Properties() { if (binding) binding->Release(); }
    ID3DXEffect* existingStockBinding() { return binding; }
    void bind(ID3DXEffect* definition) {
        if (binding) binding->Release();
        assert(wot::create_stock_effect_material(definition, &binding) == S_OK); ++creations;
    }
};
struct EffectMaterial {
    Managed managed;
    Properties values;
    explicit EffectMaterial(ID3DXEffect* definition) : managed{definition} {}
    Managed* pEffect() { return &managed; }
    Properties& properties() { return values; }
};
struct EffectMaterialPtr : std::shared_ptr<EffectMaterial> {
    using std::shared_ptr<EffectMaterial>::shared_ptr;
    bool exists() const { return bool(*this); }
};
}
@HDR_HELPER@
struct HDRSupport {
    Moo::EffectMaterialPtr m_averageLuminanceMat, m_toneMappingMat, m_bloomMat, m_gaussianBlurMat;
    std::shared_ptr<int> m_hdrRT, m_avgLumRT, m_finalAvgLumRT;
    std::vector<int> m_bloomMaps, m_avgLumRTs, m_viewPorts;
    bool m_valid = true, m_update = false;
    float m_curTime = 1;
    struct Values { void set(int, int) {} } m_avgLumValues;
    void deleteUnmanagedObjects();
};
@HDR_DELETE@
struct FloraRenderer {
    Moo::EffectMaterialPtr material_, shadowMaterial_;
    Cache standardParams_, shadowParams_;
    Cache* parameters_ = &standardParams_;
    void deactivate();
};
@FLORA_DELETE@
void populate(Moo::EffectMaterialPtr& material, ID3DXEffect* definition,
    IDirect3DTexture9* texture, const std::vector<const char*>& names) {
    if (!material) material.reset(new Moo::EffectMaterial(definition));
    material->properties().bind(definition);
    for (const char* name : names)
        assert(material->properties().binding->SetTexture(name, texture) == S_OK);
}
int main(int argc, char** argv) {
    assert(argc == 4);
    for (int asset = 1; asset < argc; ++asset) {
        std::ifstream stream(argv[asset], std::ios::binary);
        std::vector<char> bytes{std::istreambuf_iterator<char>(stream), {}};
        auto context = std::make_shared<wot::StockEffectSharedContext>(wot::StockEffectConstantProvider{});
        auto* device = new IDirect3DDevice9;
        auto* states = new ID3DXEffectStateManager;
        ID3DXEffect* definition = nullptr;
        std::string error;
        assert(wot::create_stock_effect(device, states, bytes.data(), bytes.size(), context, &definition, error) == S_OK);
        auto* texture = new IDirect3DTexture9;
        texture->pool = D3DPOOL_DEFAULT;
        if (asset != 3) {
            const std::vector<const char*> names = asset == 1 ?
                std::vector<const char*>{"g_previousMipMap", "g_avgLumMap1", "g_avgLumMap2"} :
                std::vector<const char*>{"g_avgLumMap", "g_HDRMap", "g_bloomMap"};
            HDRSupport first, second;
            auto& a = asset == 1 ? first.m_averageLuminanceMat : first.m_toneMappingMat;
            auto& b = asset == 1 ? second.m_averageLuminanceMat : second.m_toneMappingMat;
            a.reset(new Moo::EffectMaterial(definition));
            first.deleteUnmanagedObjects(); first.deleteUnmanagedObjects();
            assert(!a->properties().binding && a->properties().creations == 0 && texture->refs == 1);
            populate(a, definition, texture, names); populate(b, definition, texture, names);
            assert(texture->refs == 1 + 2 * names.size());
            // ARIE OnLostDevice leaves these manual references for their owner.
            assert(a->properties().binding->OnLostDevice() == S_OK);
            assert(texture->refs == 1 + 2 * names.size());
            first.deleteUnmanagedObjects(); first.deleteUnmanagedObjects();
            assert(texture->refs == 1 + names.size()); // A sibling material is unchanged.
            second.deleteUnmanagedObjects(); assert(texture->refs == 1);
            populate(a, definition, texture, names); assert(texture->refs == 1 + names.size());
            first.deleteUnmanagedObjects(); assert(texture->refs == 1);
        } else {
            FloraRenderer first, second;
            first.material_.reset(new Moo::EffectMaterial(definition));
            first.deactivate(); first.deactivate();
            assert(!first.material_->properties().binding && first.material_->properties().creations == 0);
            for (FloraRenderer* owner : {&first, &second}) {
                populate(owner->material_, definition, texture, {"g_tintMap"});
                owner->standardParams_.effect(owner->material_->properties().binding);
                owner->shadowParams_.effect(owner->material_->properties().binding);
            }
            assert(texture->refs == 3);
            first.deactivate(); first.deactivate();
            assert(texture->refs == 2 && !first.standardParams_.spEffect_.pComObject() &&
                !first.shadowParams_.spEffect_.pComObject() && !first.parameters_);
            second.deactivate(); assert(texture->refs == 1);
            populate(first.material_, definition, texture, {"g_tintMap"});
            first.standardParams_.effect(first.material_->properties().binding);
            first.parameters_ = &first.standardParams_;
            first.deactivate(); assert(texture->refs == 1 && !first.parameters_);
        }
        assert(texture->refs == 1);
        texture->Release(); definition->Release(); states->Release(); device->Release();
    }
}
'''


class StockManualTextureRetirementTests(unittest.TestCase):
    def test_actual_hdr_and_flora_cleanup_release_private_default_pool_references(self):
        archive = Path(os.environ.get('WOT_PUBLIC_ENGINE_ARCHIVE', '/tmp/wot-bigworld-engine-14.4.1.tar.gz'))
        client = os.environ.get('WOT_0922_CLIENT')
        compiler = shutil.which('c++')
        if not archive.is_file() or not client or not compiler:
            self.skipTest('Pinned public source, WOT_0922_CLIENT and a C++ compiler are required')
        sys.path.insert(0, str(ROOT.parents[1] / 'tools'))
        try:
            from prepare_native_client import OriginalResources
        finally:
            sys.path.pop(0)
        with tempfile.TemporaryDirectory(prefix='stock-manual-textures-') as directory:
            root = Path(directory)
            relatives = ['lib/romp/' + name for name in ('hdr_settings.hpp', 'hdr_settings.cpp', 'hdr_support.hpp',
                         'hdr_support.cpp', 'flora_renderer.cpp', 'sky_gradient_dome.cpp', 'effect_parameter_cache.hpp')]
            with tarfile.open(archive) as source:
                for relative in relatives:
                    member = next(name for name in source.getnames() if name.endswith('/programming/bigworld/' + relative))
                    path = root / relative
                    path.parent.mkdir(parents=True, exist_ok=True)
                    path.write_bytes(source.extractfile(member).read())
            hdr = load('texture_retirement_hdr_patch', ROOT / 'bigworld/patch_stock_hdr.py')
            hdr.patch(root)
            GUI.PATCH.patch_parameter_caches(root)
            paths = (root / 'lib/romp/hdr_support.cpp', root / 'lib/romp/flora_renderer.cpp')
            after = tuple(path.read_bytes() for path in paths)
            hdr.patch(root); GUI.PATCH.patch_parameter_caches(root)
            self.assertEqual(after, tuple(path.read_bytes() for path in paths))
            host = (HOST.replace('@HDR_HELPER@', function(paths[0].read_text(), 'static void clearStockHdrTexture('))
                    .replace('@HDR_DELETE@', function(paths[0].read_text(), 'void HDRSupport::deleteUnmanagedObjects()'))
                    .replace('@FLORA_DELETE@', function(paths[1].read_text(), 'void FloraRenderer::deactivate()'))
                    .replace('@CACHE@', function((root / 'lib/romp/effect_parameter_cache.hpp').read_text(),
                                                'void effect( ComObjectWrap<ID3DXEffect> spEffect )')))
            adapter = (ROOT / 'bigworld/stock_effect_adapter.cpp').read_text()
            declaration = adapter[adapter.index('class StockEffectAdapter final : public ID3DXEffect {'):]
            declaration = declaration[:declaration.index('private:')]
            methods = re.findall(r'^\s+([A-Z][^;]*?)\s+override;', declaration, re.M | re.S)
            (root / 'd3d9.h').write_text(GUI.HANDLES.DX9.DEVICE)
            (root / 'd3dx9effect.h').write_text(GUI.HANDLES.D3DX.replace('METHODS', '\n'.join(
                '    virtual ' + ' '.join(method.split()) + ' = 0;' for method in methods)))
            (root / 'host.cpp').write_text(host)
            effects = []
            with OriginalResources(Path(client)) as resources:
                import io
                import zipfile
                for name in ('shaders/hdr/average_luminance', 'shaders/hdr/tonemapping', 'shaders/flora/flora2'):
                    with zipfile.ZipFile(io.BytesIO(resources.read(name + '.10.dx9.fxo'))) as nested:
                        path = root / (Path(name).name + '.effect')
                        path.write_bytes(nested.read('effect'))
                        effects.append(str(path))
            command = [compiler, '-std=c++17', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter', '-UNDEBUG',
                       '-I', str(root), '-I', str(ROOT / 'include'), '-I', str(ROOT / 'bigworld'), str(root / 'host.cpp')]
            command += [str(ROOT / 'bigworld' / name) for name in ('stock_effect_adapter.cpp', 'stock_effect_dx9.cpp')]
            command += [str(ROOT / 'src' / name) for name in ('stock_effect_binding.cpp', 'stock_effect_reflection.cpp',
                       'stock_shader_binary.cpp', 'stock_shader_runtime.cpp')]
            command += ['-o', str(root / 'host')]
            built = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stderr)
            result = subprocess.run([str(root / 'host')] + effects, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == '__main__':
    unittest.main()
