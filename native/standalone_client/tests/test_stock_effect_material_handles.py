"""Run the production ARIE adapter with original GUI effects and Moo's cached handles."""
import importlib.util
import io
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest
import zipfile


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('stock_effect_dx9_test', Path(__file__).with_name('test_stock_effect_dx9.py'))
DX9 = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(DX9)

# The DX9 runtime's device fake, recording what BeginPass actually uploads.
RECORDING = {
    'HRESULT SetVertexShader(IDirect3DVertexShader9*) { return 0; }':
        'IDirect3DVertexShader9* vertexShader = nullptr;\n'
        '    HRESULT SetVertexShader(IDirect3DVertexShader9* shader) { vertexShader = shader; return 0; }',
    'HRESULT SetVertexShaderConstantF(DWORD, const float*, DWORD) { return 0; }':
        'std::map<DWORD, std::array<float, 4>> vertexFloats, pixelFloats;\n'
        '    HRESULT SetVertexShaderConstantF(DWORD first, const float* data, DWORD count) {\n'
        '        for (DWORD i = 0; i < count; ++i) std::copy(data+4*i, data+4*i+4, vertexFloats[first+i].begin());\n'
        '        return 0; }',
    'HRESULT SetPixelShaderConstantF(DWORD, const float*, DWORD) { return 0; }':
        'HRESULT SetPixelShaderConstantF(DWORD first, const float* data, DWORD count) {\n'
        '        for (DWORD i = 0; i < count; ++i) std::copy(data+4*i, data+4*i+4, pixelFloats[first+i].begin());\n'
        '        return 0; }',
}

D3DX = r'''
#pragma once
#include "d3d9.h"
#include <array>
#include <cstring>
#define STDMETHODCALLTYPE
using UINT = unsigned; using INT = int; using FLOAT = float; using LONG = long; using ULONG = unsigned long;
using LPCSTR = const char*; using D3DXHANDLE = const char*;
struct GUID { std::uint32_t Data1; std::uint16_t Data2, Data3; std::uint8_t Data4[8]; };
using REFIID = const GUID&;
inline bool IsEqualGUID(REFIID a, REFIID b) { return std::memcmp(&a, &b, sizeof(GUID)) == 0; }
const GUID IID_IUnknown = {1, 0, 0, {0}}, IID_ID3DXEffect = {2, 0, 0, {0}}, IID_ID3DXBaseEffect = {3, 0, 0, {0}};
inline LONG InterlockedIncrement(LONG* value) { return ++*value; }
inline LONG InterlockedDecrement(LONG* value) { return --*value; }
constexpr HRESULT S_OK = 0, S_FALSE = 1, E_FAIL = -2, E_INVALIDARG = -3, E_NOTIMPL = -4,
    E_POINTER = -5, E_NOINTERFACE = -6, D3DERR_INVALIDCALL = -7;
constexpr DWORD D3DXFX_DONOTSAVESTATE = 4;
constexpr BOOL TRUE = 1, FALSE = 0;
enum D3DXPARAMETER_CLASS { D3DXPC_SCALAR, D3DXPC_VECTOR, D3DXPC_MATRIX_ROWS, D3DXPC_MATRIX_COLUMNS,
    D3DXPC_OBJECT, D3DXPC_STRUCT };
enum D3DXPARAMETER_TYPE { D3DXPT_VOID, D3DXPT_BOOL, D3DXPT_INT, D3DXPT_FLOAT, D3DXPT_STRING,
    D3DXPT_TEXTURE, D3DXPT_UNSUPPORTED = 100 };
struct D3DXVECTOR4 { float x, y, z, w; };
struct D3DXMATRIX { float m[4][4]; };
struct D3DXEFFECT_DESC { LPCSTR Creator; UINT Parameters, Techniques, Functions; };
struct D3DXPARAMETER_DESC { LPCSTR Name, Semantic; D3DXPARAMETER_CLASS Class; D3DXPARAMETER_TYPE Type;
    UINT Rows, Columns, Elements, Annotations, StructMembers; DWORD Flags; UINT Bytes; };
struct D3DXTECHNIQUE_DESC { LPCSTR Name; UINT Passes, Annotations; };
struct D3DXPASS_DESC; struct D3DXFUNCTION_DESC; struct ID3DXEffectPool;
struct ID3DXEffect {
    virtual ~ID3DXEffect() = default;
METHODS
};
'''

HARNESS = r'''
#include "stock_effect_adapter.hpp"
#include "wot/stock_effect_reflection.hpp"
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <stdexcept>
#include <string>
void check(bool value, const std::string& message) { if (!value) throw std::runtime_error(message); }
void exercise(const char* path) {
    std::ifstream input(path, std::ios::binary);
    std::vector<char> bytes((std::istreambuf_iterator<char>(input)), {});
    auto decoded = std::make_shared<wot::StockEffectBinary>(); std::string error;
    check(wot::decode_stock_dx9_effect(bytes.data(), bytes.size(), *decoded, error), error);
    std::vector<std::string> resources;
    for (const auto& resource : decoded->resource_views)
        if (resource.semantic_present) resources.push_back(resource.semantic);
    // Precondition: registering resource producers changes the handle namespace.
    check(wot::StockEffectReflection(decoded, {}).parameter_count() !=
          wot::StockEffectReflection(decoded, resources).parameter_count(), "Original effect hides no resources");
    bool registered = false;
    auto context = std::make_shared<wot::StockEffectSharedContext>(
        [&](const std::string& semantic, ID3DXEffect*, D3DXHANDLE) {
            return registered && std::find(resources.begin(), resources.end(), semantic) != resources.end();
        });
    auto* device = new IDirect3DDevice9;
    auto* states = new ID3DXEffectStateManager;
    // SimpleGUI::init loads ashes and FontManager loads font before the
    // render pipeline registers its automatic resource producers.
    ID3DXEffect* definition = nullptr;
    check(SUCCEEDED(wot::create_stock_effect(device, states, bytes.data(), bytes.size(), context,
        &definition, error)), error);
    // ManagedEffect::load caches each semantic parameter handle from the Definition.
    std::map<std::string, D3DXHANDLE> handles;
    D3DXEFFECT_DESC description;
    check(SUCCEEDED(definition->GetDesc(&description)), "GetDesc");
    for (UINT i = 0; i < description.Parameters; ++i) {
        D3DXHANDLE handle = definition->GetParameter(nullptr, i);
        D3DXPARAMETER_DESC parameter;
        check(SUCCEEDED(definition->GetParameterDesc(handle, &parameter)), "GetParameterDesc");
        handles[parameter.Name] = handle;
    }
    registered = true;
    ID3DXEffect* binding = nullptr;
    check(SUCCEEDED(wot::create_stock_effect_material(definition, &binding)), "material binding");
    const bool font = handles.count("uvDivisor") != 0;
    D3DXMATRIX matrix;
    for (int r = 0; r < 4; ++r) for (int c = 0; c < 4; ++c) matrix.m[r][c] = float(1 + 4*r + c);
    const D3DXVECTOR4 colour = {0.25f, 0.5f, 0.75f, 0.875f}, screen = {1024, 768, 1.f/1024, 1.f/768};
    // AshesProjSetter, TFactorSetter/FontColourSetter, Screen, pixel-snap and UV-divisor producers.
    check(binding->SetMatrix(handles.at("worldViewProj"), &matrix) == S_OK, "worldViewProj handle");
    check(binding->SetVector(handles.at("factor"), &colour) == S_OK, "factor handle");
    check(binding->SetVector(handles.at("resolution"), &screen) == S_OK, "resolution handle");
    check(binding->SetBool(handles.at("pixelSnapEnabled"), TRUE) == S_OK, "pixelSnapEnabled handle");
    if (font) check(binding->SetFloat(handles.at("uvDivisor"), 2.f) == S_OK, "uvDivisor handle");
    UINT passes = 0;
    check(binding->SetTechnique(binding->GetTechniqueByName("FX_SOLID")) == S_OK &&
          binding->Begin(&passes, D3DXFX_DONOTSAVESTATE) == S_OK && passes == 1 &&
          binding->BeginPass(0) == S_OK, "FX_SOLID pass");
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c)
            check(states->vertexFloats.at(r)[c] == matrix.m[r][c], "VS c0-c3 must hold worldViewProj");
    // The pixel-snap VS (selected only by pixelSnapEnabled) reads resolution at c4.
    for (int c = 0; c < 4; ++c)
        check(states->vertexFloats.at(4)[c] == (&screen.x)[c], "VS c4 must be the pixel-snap resolution");
    if (font) check(states->vertexFloats.at(5)[0] == 2.f, "font VS c5.x must be uvDivisor");
    for (int c = 0; c < 4; ++c)
        check(states->pixelFloats.at(0)[c] == (&colour.x)[c], "PS c0 must hold factor");
    check(binding->EndPass() == S_OK && binding->End() == S_OK, "pass close");
    binding->Release(); definition->Release(); context->detach_providers();
    states->Release(); device->Release();
}
int main(int argc, char** argv) {
    try {
        for (int i = 1; i < argc; ++i) exercise(argv[i]);
        std::cout << "Material bindings use the Definition handles of all original GUI effects\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
'''


class StockEffectMaterialHandleTests(unittest.TestCase):
    def test_original_gui_bindings_accept_definition_handles(self):
        compiler = shutil.which('c++')
        client = Path(os.environ.get('WOT_0922_CLIENT',
            '/Users/peng/Downloads/World_of_Tanks_0.09.22.00.01_CH_1513_HD'))
        package = client / 'res/packages/misc.pkg'
        if not compiler or not package.is_file():
            self.skipTest('C++ compiler and exact #1513 misc.pkg are required')
        adapter = (ROOT / 'bigworld/stock_effect_adapter.cpp').read_text()
        body = adapter[adapter.index('class StockEffectAdapter final : public ID3DXEffect {'):]
        body = body[:body.index('private:')]
        methods = re.findall(r'^\s+([A-Z][^;]*?)\s+override;', body, re.M | re.S)
        self.assertGreater(len(methods), 60)
        device = DX9.DEVICE
        for old, new in RECORDING.items():
            self.assertEqual(device.count(old), 1, old)
            device = device.replace(old, new)
        with tempfile.TemporaryDirectory(prefix='wot-material-handles-') as directory:
            root = Path(directory)
            (root / 'd3d9.h').write_text(device.replace('#include <map>', '#include <map>\n#include <array>\n#include <algorithm>'))
            (root / 'd3dx9effect.h').write_text(D3DX.replace('METHODS', '\n'.join(
                '    virtual ' + ' '.join(method.split()) + ' = 0;' for method in methods)))
            effects = []
            with zipfile.ZipFile(package) as original:
                for material in ('ashes', 'font'):
                    for variant in ('10', '11'):
                        name = 'system/materials/%s.%s.dx9.fxo' % (material, variant)
                        with zipfile.ZipFile(io.BytesIO(original.read(name))) as nested:
                            path = root / ('%s-%s.effect' % (material, variant))
                            path.write_bytes(nested.read('effect'))
                            effects.append(str(path))
            (root / 'probe.cpp').write_text(HARNESS)
            binary = root / 'probe'
            subprocess.run([compiler, '-std=c++17', '-Wall', '-Wextra', '-Werror',
                            '-Wno-unused-parameter', '-I', str(root), '-I', str(ROOT / 'include'),
                            '-I', str(ROOT / 'bigworld'), str(root / 'probe.cpp'),
                            str(ROOT / 'bigworld/stock_effect_adapter.cpp'),
                            str(ROOT / 'bigworld/stock_effect_dx9.cpp'),
                            str(ROOT / 'src/stock_effect_binding.cpp'),
                            str(ROOT / 'src/stock_effect_reflection.cpp'),
                            str(ROOT / 'src/stock_shader_binary.cpp'),
                            str(ROOT / 'src/stock_shader_runtime.cpp'),
                            '-o', str(binary)], check=True)
            result = subprocess.run([str(binary)] + effects, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == '__main__':
    unittest.main()
