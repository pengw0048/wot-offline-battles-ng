"""Run both public GUI filter sequences through the production ARIE bindings."""

import importlib.util
import io
import os
from pathlib import Path
import re
import shutil
import subprocess
import tarfile
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    value = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(value)
    return value


HANDLES = module('gui_filter_handles', Path(__file__).with_name('test_stock_effect_material_handles.py'))
PATCH = module('gui_filter_patch', ROOT / 'bigworld/patch_stock_effect_owners.py')


def sequence(source, owner):
    method = source.index('void ' + owner + '::drawSelf(')
    write = source.index('material_->pEffect()->pEffect()->SetInt( "filterType",', method)
    select = source.rfind('\tif (!material_->properties().select(', method, write)
    start = select if select != -1 else source.rfind('\n', method, write) + 1
    end = source.index('{', source.index('if ( material_->begin() )', write))
    return source[start:end]


HOST = r'''
#include "stock_effect_adapter.hpp"
#include <cassert>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
struct Managed {
    ID3DXEffect* definition;
    ID3DXEffect* active = nullptr;
    ID3DXEffect* pEffect() { return active ? active : definition; }
};
struct Properties {
    ID3DXEffect* binding = nullptr;
    ID3DXEffect* definition = nullptr;
    bool fail = false;
    ~Properties() { if (binding) binding->Release(); }
    bool select(Managed& effect) {
        if (fail) return false;
        if (!binding || definition != effect.definition) {
            if (binding) binding->Release();
            binding = nullptr;
            assert(wot::create_stock_effect_material(effect.definition, &binding) == S_OK);
            definition = effect.definition;
        }
        effect.active = binding; return true;
    }
};
struct Material {
    Managed managed;
    Properties values;
    ID3DXEffectStateManager* states;
    unsigned begins = 0;
    Material(ID3DXEffect* definition, ID3DXEffectStateManager* output) : managed{definition}, states(output) {}
    Managed* pEffect() { return &managed; }
    Properties& properties() { return values; }
    bool begin() {
        ++begins;
        if (!values.select(managed)) return false;
        ID3DXEffect* effect = managed.pEffect();
        UINT passes = 0;
        assert(effect->SetTechnique(effect->GetTechniqueByName("FX_SOLID")) == S_OK);
        assert(effect->Begin(&passes, D3DXFX_DONOTSAVESTATE) == S_OK && passes == 1);
        states->samplers.clear();
        assert(effect->BeginPass(0) == S_OK);
        return true;
    }
    void end() {
        assert(managed.pEffect()->EndPass() == S_OK && managed.pEffect()->End() == S_OK);
        managed.active = nullptr;
    }
};
struct Consumer {
    Material* material_;
    int filterType_;
    std::map<std::pair<DWORD, int>, DWORD> observed;
    int filterType() const { return filterType_; }
    void oldGUI() { @OLD_GUI@ { consume(); } }
    void newGUI() { @NEW_GUI@ { consume(); } }
    void oldMap() { @OLD_MAP@ { consume(); } }
    void newMap() { @NEW_MAP@ { consume(); } }
    void consume() {
        observed = material_->states->samplers;
        material_->end();
    }
};
std::map<std::pair<DWORD, int>, DWORD> reference(ID3DXEffect* definition,
    ID3DXEffectStateManager* states, int filter) {
    ID3DXEffect* binding = nullptr;
    assert(wot::create_stock_effect_material(definition, &binding) == S_OK);
    assert(binding->SetInt("filterType", filter) == S_OK);
    assert(binding->SetTechnique(binding->GetTechniqueByName("FX_SOLID")) == S_OK);
    UINT passes = 0;
    assert(binding->Begin(&passes, D3DXFX_DONOTSAVESTATE) == S_OK && passes == 1);
    states->samplers.clear();
    assert(binding->BeginPass(0) == S_OK);
    const auto result = states->samplers;
    assert(binding->EndPass() == S_OK && binding->End() == S_OK);
    binding->Release();
    return result;
}
int main(int argc, char** argv) {
    for (int n = 1; n < argc; ++n) {
        std::ifstream stream(argv[n], std::ios::binary);
        std::vector<char> bytes{std::istreambuf_iterator<char>(stream), {}};
        auto context = std::make_shared<wot::StockEffectSharedContext>(wot::StockEffectConstantProvider{});
        auto* device = new IDirect3DDevice9;
        auto* states = new ID3DXEffectStateManager;
        ID3DXEffect* definition = nullptr;
        std::string error;
        assert(wot::create_stock_effect(device, states, bytes.data(), bytes.size(), context, &definition, error) == S_OK);
        int initial = -1;
        assert(definition->GetInt("filterType", &initial) == S_OK);
        const int desired = initial == 0 ? 1 : 0;
        const auto defaultSampler = reference(definition, states, initial);
        const auto changedSampler = reference(definition, states, desired);
        assert(defaultSampler != changedSampler); // The selected live shader actually consumes this filter.
        {
            Material first(definition, states), second(definition, states);
            Consumer a{&first, desired, {}}, b{&second, desired, {}};
            a.oldGUI(); b.oldMap();
            assert(a.observed == defaultSampler && b.observed == defaultSampler); // Negative controls: Definition writes miss draw bindings.
            a.newGUI(); b.newMap();
            assert(a.observed == changedSampler && b.observed == changedSampler);
            a.filterType_ = initial; a.newGUI();
            b.newMap();
            assert(a.observed == defaultSampler && b.observed == changedSampler); // Alternating materials stay isolated.
            for (bool map : {false, true}) {
                const unsigned before = first.begins;
                first.values.fail = true; a.observed.clear();
                if (map) a.newMap(); else a.newGUI();
                assert(a.observed.empty() && first.begins == before);
                first.values.fail = false;
            }
            // A reloaded Definition must select a fresh Binding before the write.
            ID3DXEffect* replacement = nullptr;
            assert(wot::create_stock_effect(device, states, bytes.data(), bytes.size(), context, &replacement, error) == S_OK);
            first.managed.definition = replacement;
            a.filterType_ = desired; a.newGUI();
            assert(a.observed == changedSampler && first.values.definition == replacement);
            a.newMap(); assert(a.observed == changedSampler);
            replacement->Release();
        }
        definition->Release(); states->Release(); device->Release();
    }
}
'''


class StockGUIFilterBindingTests(unittest.TestCase):
    def test_original_and_generated_gui_minimap_sequences_use_the_draw_binding(self):
        archive = Path(os.environ.get('WOT_PUBLIC_ENGINE_ARCHIVE', '/tmp/wot-bigworld-engine-14.4.1.tar.gz'))
        client = os.environ.get('WOT_0922_CLIENT')
        compiler = shutil.which('c++')
        if not archive.is_file() or not client or not compiler:
            self.skipTest('Pinned public source, WOT_0922_CLIENT and a C++ compiler are required')
        with tempfile.TemporaryDirectory(prefix='stock-gui-filter-') as directory:
            root = Path(directory)
            relatives = ['lib/ashes/' + name for name in ('simple_gui_component.hpp', 'simple_gui_component.cpp',
                         'simple_gui.cpp', 'text_gui_component.cpp')] + ['client/minimap.cpp']
            with tarfile.open(archive) as source:
                for relative in relatives:
                    member = next(name for name in source.getnames() if name.endswith('/programming/bigworld/' + relative))
                    path = root / relative
                    path.parent.mkdir(parents=True, exist_ok=True)
                    path.write_bytes(source.extractfile(member).read())
            simple, minimap = root / relatives[1], root / 'client/minimap.cpp'
            old = (sequence(simple.read_text(), 'SimpleGUIComponent'), sequence(minimap.read_text(), 'Minimap'))
            PATCH.patch_gui_materials(root)
            new = (sequence(simple.read_text(), 'SimpleGUIComponent'), sequence(minimap.read_text(), 'Minimap'))
            after = (simple.read_bytes(), minimap.read_bytes())
            PATCH.patch_gui_materials(root)
            self.assertEqual(after, (simple.read_bytes(), minimap.read_bytes()))
            host = HOST
            for key, value in zip(('@OLD_GUI@', '@OLD_MAP@', '@NEW_GUI@', '@NEW_MAP@'), old + new):
                host = host.replace(key, value)
            adapter = (ROOT / 'bigworld/stock_effect_adapter.cpp').read_text()
            declaration = adapter[adapter.index('class StockEffectAdapter final : public ID3DXEffect {'):]
            declaration = declaration[:declaration.index('private:')]
            methods = re.findall(r'^\s+([A-Z][^;]*?)\s+override;', declaration, re.M | re.S)
            (root / 'd3d9.h').write_text(HANDLES.DX9.DEVICE)
            (root / 'd3dx9effect.h').write_text(HANDLES.D3DX.replace('METHODS', '\n'.join(
                '    virtual ' + ' '.join(method.split()) + ' = 0;' for method in methods)))
            (root / 'host.cpp').write_text(host)
            effects = []
            with zipfile.ZipFile(Path(client) / 'res/packages/misc.pkg') as assets:
                for variant in ('10', '11'):
                    with zipfile.ZipFile(io.BytesIO(assets.read('system/materials/ashes.%s.dx9.fxo' % variant))) as nested:
                        path = root / ('ashes-%s.effect' % variant)
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
