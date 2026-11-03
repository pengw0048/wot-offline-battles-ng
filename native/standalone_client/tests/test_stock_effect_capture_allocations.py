"""Execute production capture and storage without copying descriptive strings."""
from pathlib import Path
import os
import re
import shutil
import subprocess
import tempfile
import unittest

from native_test_compile_flags import native_test_compile_flags
from test_stock_vector4_basic import block

ROOT = Path(__file__).resolve().parents[1]


HOST = r'''
#include "wot/stock_effect_binding.hpp"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <stdexcept>
using UINT = unsigned;
using HRESULT = int;
constexpr HRESULT S_OK = 0, E_INVALIDARG = -1;
bool counting = false;
unsigned allocations = 0;
void* operator new(std::size_t size) {
    if (counting) ++allocations;
    if (void* value = std::malloc(size ? size : 1)) return value;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* value) noexcept { std::free(value); }
void operator delete[](void* value) noexcept { std::free(value); }
void operator delete(void* value, std::size_t) noexcept { std::free(value); }
void operator delete[](void* value, std::size_t) noexcept { std::free(value); }
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
namespace wot {
// Only the adapter's enclosing owner is reduced. Its captured method and both
// UniformContext/UniformStorage implementations are production code.
struct CaptureContext {
    struct Impl {
        StockEffectUniformPool pool;
        StockEffectUniformContext uniforms{pool};
    };
    std::unique_ptr<Impl> impl_{new Impl};
};
class StockEffectAdapter {
public:
    struct Destination { bool resource = false; std::size_t owner = 0, member = 0, declaration = 0; };
    std::shared_ptr<StockEffectBinary> definition_ = std::make_shared<StockEffectBinary>();
    CaptureContext context_;
    std::string capture_error_;
    HRESULT write_capture(const Destination&, const void*, UINT);
};
@CAPTURE@
}
int main() {
    try {
        wot::StockEffectAdapter adapter;
        auto& definition = *adapter.definition_;
        wot::StockEffectType scalar; scalar.byte_size = 4;
        definition.types.push_back(scalar);
        wot::StockEffectUniform uniform;
        uniform.flags = 0x82; // Preserve non-readonly bits; producer bypasses bit 1.
        uniform.name.assign(256, 'n'); uniform.semantic.assign(256, 's');
        uniform.semantic_present = true; uniform.type_index = 0;
        uniform.byte_offset = 8; uniform.elements = 1; uniform.annotations = 19;
        wot::StockEffectUniformBuffer buffer;
        buffer.name = "PerFrame"; buffer.flags = 1; buffer.defaults.assign(128, 0xcc);
        buffer.uniforms.push_back(uniform); definition.uniform_buffers.push_back(buffer);
        const auto declaration = adapter.context_.impl_->pool.register_buffer(buffer);
        wot::StockEffectAdapter::Destination destination{false, 0, 0, declaration};
        auto storage = adapter.context_.impl_->uniforms.shared(declaration);
        float input = 1.25f;
        counting = true;
        for (unsigned i = 0; i < 256; ++i) {
            input = static_cast<float>(i);
            if (adapter.write_capture(destination, &input, sizeof(input)) != S_OK) {
                counting = false; throw std::runtime_error("capture failed");
            }
        }
        counting = false;
        check(allocations == 0, "capture copied immutable descriptive strings");
        float output = 0;
        std::memcpy(&output, storage->bytes().data() + 8, sizeof(output));
        check(output == input && definition.uniform_buffers[0].uniforms[0].flags == 0x82,
              "producer readonly bypass or source flags changed");
        std::string error;
        input = 999.f;
        check(storage->write_uniform(uniform, scalar, &input, sizeof(input), error), "readonly storage write failed");
        std::memcpy(&output, storage->bytes().data() + 8, sizeof(output));
        check(output == 255.f, "ordinary storage readonly guard changed");
        // Scalar writes still clamp to the declared width.
        const float oversized[] = {17.f, 18.f};
        check(adapter.write_capture(destination, oversized, sizeof(oversized)) == S_OK,
              "scalar capture rejected caller width");
        check(storage->bytes()[12] == 0xcc, "scalar capture overwrote following bytes");
        auto& field = definition.uniform_buffers[0].uniforms[0];
        field.elements = 3; field.element_padding = 12;
        const float padded[] = {7.f, 8.f, 9.f};
        check(adapter.write_capture(destination, padded, sizeof(padded)) == S_OK, "padded capture failed");
        for (unsigned i = 0; i < 3; ++i) {
            std::memcpy(&output, storage->bytes().data() + 8 + 16*i, 4);
            check(output == padded[i] && storage->bytes()[12 + 16*i] == 0xcc,
                  "padded capture changed layout");
        }
        const auto before = storage->bytes();
        const auto revision = storage->revision();
        field.byte_offset = 120;
        check(adapter.write_capture(destination, padded, sizeof(padded)) == E_INVALIDARG &&
              storage->bytes() == before && storage->revision() == revision,
              "out-of-bounds capture was not atomic");
        field.byte_offset = 8;
        check(adapter.write_capture(destination, nullptr, 4) == E_INVALIDARG,
              "null source capture was admitted");
        destination.resource = true;
        check(adapter.write_capture(destination, padded, sizeof(padded)) == E_INVALIDARG,
              "resource destination admitted numeric capture");
        destination.resource = false;
        // Keep the original struct projection and zero-filled padding.
        wot::StockEffectType sun; sun.name = "SunLight"; sun.type_class = 5; sun.byte_size = 48;
        for (const auto& item : std::array<std::pair<const char*, unsigned>, 3>{{
                {"m_ambient", 0}, {"m_dir", 16}, {"m_color", 32}}}) {
            wot::StockEffectUniform member; member.name = item.first; member.byte_offset = item.second;
            sun.members.push_back(member);
        }
        definition.types.push_back(sun);
        field.type_index = 1; field.semantic = "SunLight"; field.byte_offset = 0;
        field.elements = 1; field.element_padding = 0;
        const float source[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
        check(adapter.write_capture(destination, source, sizeof(source)) == S_OK, "SunLight projection failed");
        std::array<unsigned char, 48> expected{};
        std::memcpy(expected.data(), source + 7, 16);
        std::memcpy(expected.data() + 16, source, 12);
        std::memcpy(expected.data() + 32, source + 3, 16);
        check(std::memcmp(storage->bytes().data(), expected.data(), expected.size()) == 0,
              "SunLight capture changed its projected layout");
        field.semantic = "OtherStruct";
        check(adapter.write_capture(destination, source, sizeof(source)) == E_INVALIDARG,
              "mismatched struct size was admitted");
        std::puts("Production capture preserves storage semantics without descriptor allocations");
    } catch (const std::exception& error) {
        counting = false; std::fprintf(stderr, "%s\n", error.what()); return 1;
    }
}
'''


class EffectCaptureAllocationTests(unittest.TestCase):
    def test_actual_capture_storage_and_old_copy_negative_control(self):
        compiler = shutil.which(os.environ.get('CXX', 'c++'))
        if not compiler:
            self.skipTest('Host C++ compiler required')
        source = (ROOT / 'bigworld/stock_effect_adapter.cpp').read_text()
        capture = block(source, 'HRESULT StockEffectAdapter::write_capture(')
        layout = re.search(r'  const StockEffectUniform writable\{.*?uniform.annotations\};', capture, re.S)
        self.assertIsNotNone(layout)
        old = capture.replace(layout.group(), '  auto writable = uniform;\n  writable.flags &= ~2u;')
        # Guard this optimisation's field-consumer boundary against future edits.
        storage = block((ROOT / 'src/stock_effect_binding.cpp').read_text(),
                        'bool StockEffectUniformStorage::write_uniform(')
        self.assertEqual(set(re.findall(r'uniform\.(\w+)', storage)),
                         {'flags', 'byte_offset', 'elements', 'element_padding'})
        with tempfile.TemporaryDirectory(prefix='effect-capture-allocations-') as directory:
            root = Path(directory)
            for label, method in (('current', capture), ('old-copy', old)):
                path, binary = root / (label + '.cpp'), root / label
                path.write_text(HOST.replace('@CAPTURE@', method))
                compiled = subprocess.run([compiler, '-std=c++17', '-Wall', '-Wextra', '-Werror',
                    '-UNDEBUG', *native_test_compile_flags(compiler), '-I', str(ROOT / 'include'),
                    str(path), str(ROOT / 'src/stock_effect_binding.cpp'), '-o', str(binary)],
                    capture_output=True, text=True, timeout=60)
                self.assertEqual(compiled.returncode, 0, compiled.stdout + compiled.stderr)
                result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=10)
                if label == 'current':
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                else:
                    self.assertNotEqual(result.returncode, 0)
                    self.assertIn('capture copied immutable descriptive strings', result.stderr)


if __name__ == '__main__':
    unittest.main()
