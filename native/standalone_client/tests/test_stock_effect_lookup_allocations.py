"""Execute reflection and adapter string lookups without owning temporary keys."""
from pathlib import Path
import os
import re
import shutil
import subprocess
import tempfile
import unittest

from native_test_compile_flags import native_test_compile_flags
from test_stock_effect_adapter_reads import FIXTURE
from test_stock_effect_capture_allocations import HOST as CAPTURE_HOST

ROOT = Path(__file__).resolve().parents[1]
COUNTER = CAPTURE_HOST[CAPTURE_HOST.index('bool counting ='):CAPTURE_HOST.index('namespace wot {')]

HOST = r'''
#include "adapter_under_test.cpp"
#include <cstdio>
#include <cstdlib>
#include <new>
#include <stdexcept>
@COUNTER@
int main() {
    try {
        auto effect = std::make_shared<wot::StockEffectBinary>();
        wot::StockEffectType type; type.basic_type = 3; type.byte_size = 4;
        effect->types.push_back(type);
        const std::string name(128, 'n'), semantic(128, 's'), technique(128, 't'), annotation(128, 'a');
        wot::StockEffectUniform uniform;
        uniform.name = name; uniform.semantic = semantic; uniform.semantic_present = true;
        uniform.elements = 1; uniform.annotations = 0;
        wot::StockEffectUniformBuffer buffer; buffer.name = "$Global";
        buffer.defaults.resize(4); buffer.uniforms.push_back(uniform);
        effect->uniform_buffers.push_back(buffer);
        effect->annotations = {{{annotation, 4, {1, 0, 0, 0}}}};
        effect->techniques = {{technique, 0, {}}};
        auto* device = new IDirect3DDevice9;
        auto* states = new ID3DXEffectStateManager;
        std::shared_ptr<wot::StockDx9EffectRuntime> runtime;
        std::string error;
        check(wot::StockDx9EffectRuntime::create(device, states, effect, runtime, error), "runtime creation failed");
        auto context = std::make_shared<wot::StockEffectSharedContext>(wot::StockEffectConstantProvider{});
        wot::StockEffectReflection reflection(effect, {});
        {
            wot::StockEffectAdapter adapter(runtime, context, reflection);
            const auto parameter = adapter.GetParameterByName(nullptr, name.c_str());
            const auto selected = adapter.GetTechniqueByName(technique.c_str());
            const auto metadata = adapter.GetAnnotationByName(parameter, annotation.c_str());
            check(parameter && selected && metadata, "long lookup fixture was not reflected");
            check(!adapter.GetParameterByName(nullptr, nullptr) &&
                  !adapter.GetParameterByName(parameter, name.c_str()) &&
                  !adapter.GetParameterBySemantic(nullptr, nullptr) &&
                  !adapter.GetTechniqueByName(nullptr) &&
                  !adapter.GetAnnotationByName(parameter, nullptr) &&
                  adapter.SetFloat(nullptr, 1.f) == E_INVALIDARG,
                  "adapter null/object query boundaries changed");
            bool correct = true;
            counting = true;
            for (unsigned i=0; i<128; ++i) {
                correct &= adapter.GetParameterByName(nullptr, name.c_str()) == parameter;
                correct &= adapter.GetParameterBySemantic(nullptr, semantic.c_str()) == parameter;
                correct &= adapter.GetTechniqueByName(technique.c_str()) == selected;
                correct &= adapter.GetAnnotationByName(parameter, annotation.c_str()) == metadata;
                correct &= adapter.SetFloat(name.c_str(), float(i)) == S_OK;
                float value = -1.f;
                correct &= adapter.GetFloat(name.c_str(), &value) == S_OK && value == float(i);
            }
            counting = false;
            check(correct, "actual adapter lookup or named storage access changed");
            check(allocations == 0, "lookup allocated an owning temporary string");
        }
        runtime.reset(); states->Release(); device->Release();
        std::puts("Actual adapter/reflection long-key lookups allocate no temporary strings");
    } catch (const std::exception& error) {
        counting = false; std::fprintf(stderr, "%s\n", error.what()); return 1;
    }
}
'''


class EffectLookupAllocationTests(unittest.TestCase):
    def test_actual_adapter_lookup_and_old_owning_key_negative_control(self):
        compiler = shutil.which(os.environ.get('CXX', 'c++'))
        if not compiler:
            self.skipTest('Host C++ compiler required')
        adapter = (ROOT / 'bigworld/stock_effect_adapter.cpp').read_text()
        header = (ROOT / 'include/wot/stock_effect_reflection.hpp').read_text()
        reflection = (ROOT / 'src/stock_effect_reflection.cpp').read_text()
        body = adapter[adapter.index('class StockEffectAdapter final : public ID3DXEffect {'):]
        methods = re.findall(r'^\s+([A-Z][^;]*?)\s+override;', body[:body.index('private:')], re.M | re.S)
        self.assertGreater(len(methods), 60)
        # Recreate the actual previous API's implicit char* -> string conversion,
        # while retaining the same real adapter callers and lookup algorithms.
        old_header = header.replace('std::string_view name)', 'const std::string& name)').replace(
            'std::string_view semantic)', 'const std::string& semantic)')
        old_reflection = reflection.replace('std::string_view name_view(const std::string& name) {\n'
            '    return {name.data(), name.size()};\n}\n', '')
        old_reflection = old_reflection.replace('std::string_view name)', 'const std::string& name)').replace(
            'std::string_view semantic)', 'const std::string& semantic)')
        old_reflection = re.sub(r'name_view\(([^()]*)\)', r'\1', old_reflection)
        with tempfile.TemporaryDirectory(prefix='effect-lookup-allocations-') as directory:
            root = Path(directory)
            (root / 'wot').mkdir()
            (root / 'd3d9.h').write_text(FIXTURE.DX9.DEVICE)
            (root / 'd3dx9effect.h').write_text(FIXTURE.D3DX.replace('METHODS', '\n'.join(
                '    virtual ' + ' '.join(method.split()) + ' = 0;' for method in methods)))
            (root / 'adapter_under_test.cpp').write_text(adapter)
            (root / 'probe.cpp').write_text(HOST.replace('@COUNTER@', COUNTER))
            for label, schema, source in (('current', header, reflection), ('old-owning-key', old_header, old_reflection)):
                (root / 'wot/stock_effect_reflection.hpp').write_text(schema)
                (root / 'reflection.cpp').write_text(source)
                binary = root / label
                command = [compiler, '-std=c++17', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter',
                    '-UNDEBUG', *native_test_compile_flags(compiler), '-I', str(root), '-I', str(ROOT / 'include'),
                    '-I', str(ROOT / 'bigworld'), str(root / 'probe.cpp'), str(root / 'reflection.cpp'),
                    str(ROOT / 'bigworld/stock_effect_dx9.cpp')]
                command += [str(ROOT / 'src' / name) for name in (
                    'stock_effect_binding.cpp', 'stock_shader_binary.cpp', 'stock_shader_runtime.cpp')]
                command += ['-o', str(binary)]
                built = subprocess.run(command, capture_output=True, text=True, timeout=60)
                self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
                result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=10)
                if label == 'current':
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                else:
                    self.assertNotEqual(result.returncode, 0)
                    self.assertIn('lookup allocated an owning temporary string', result.stderr)

    def test_existing_reflection_and_binary_name_boundaries(self):
        compiler = shutil.which(os.environ.get('CXX', 'c++'))
        if not compiler:
            self.skipTest('Host C++ compiler required')
        with tempfile.TemporaryDirectory(prefix='effect-reflection-boundaries-') as directory:
            binary = Path(directory) / 'reflection'
            built = subprocess.run([compiler, '-std=c++17', '-Wall', '-Wextra', '-Werror', '-UNDEBUG',
                *native_test_compile_flags(compiler), '-I', str(ROOT / 'include'),
                str(ROOT / 'tests/test_stock_effect_reflection.cpp'),
                str(ROOT / 'src/stock_effect_reflection.cpp'), '-o', str(binary)],
                capture_output=True, text=True, timeout=60)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == '__main__':
    unittest.main()
