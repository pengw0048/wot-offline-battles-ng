#include "wot/stock_effect_binding.hpp"
#include "wot/stock_effect_reflection.hpp"
#include <cassert>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>

int main(int argc, char** argv)
{
    assert(argc == 3);
    std::ifstream input(argv[1], std::ios::binary);
    std::vector<char> bytes((std::istreambuf_iterator<char>(input)), {});
    auto definition = std::make_shared<wot::StockEffectBinary>();
    std::string error;
    assert(wot::decode_stock_dx9_effect(bytes.data(), bytes.size(), *definition, error));
    wot::StockEffectReflection reflection(definition, {});
    wot::StockEffectUniformPool pool;
    wot::StockEffectUniformContext context(pool);
    wot::StockEffectBinding first(definition, context), second(definition, context);
    const bool radius = std::string(argv[2]) == "radius";
    const bool flag = !radius && std::string(argv[2]) != "staff";
    if (flag)
    {
        for (const char* name : {"g_flagColor", "g_emblemTC", "flagEmblemMap", "flagBackgroundMap"})
        {
            const auto handle = reflection.parameter_by_name(name);
            assert(handle && reflection.parameter_used(handle));
            const auto* type = reflection.parameter_type(handle);
            assert(type && type->basic_type == (std::string(name).find("Map") == std::string::npos ? 3u : 5u));
        }
        for (const char* name : {"g_flagColor", "g_emblemTC"})
        {
            const auto* parameter = reflection.parameter_reference(reflection.parameter_by_name(name));
            assert(parameter && parameter->kind == wot::StockEffectParameterKind::uniform);
            const float value[] = {.125f, .375f, .625f, .875f};
            const auto original = second.uniform_buffer(parameter->owner)->bytes();
            assert(first.set_uniform(parameter->owner, parameter->member, value, 16, 3, error));
            assert(first.uniform_buffer(parameter->owner) != second.uniform_buffer(parameter->owner));
            assert(second.uniform_buffer(parameter->owner)->bytes() == original);
            const auto offset = definition->uniform_buffers[parameter->owner].uniforms[parameter->member].byte_offset;
            assert(std::memcmp(first.uniform_buffer(parameter->owner)->bytes().data() + offset, value, 16) == 0);
        }
    }
    if (radius)
    {
        const auto handle = reflection.parameter_by_name("g_color");
        assert(handle && reflection.parameter_used(handle) && reflection.parameter_type(handle)->basic_type == 3);
    }
    if (!radius && std::string(argv[2]) != "flag")
    {
        const auto handle = reflection.parameter_by_name("g_alpha");
        assert(handle && reflection.parameter_used(handle) && reflection.parameter_type(handle)->basic_type == 3);
    }
    unsigned selected = 0;
    for (size_t t = 0; t != definition->techniques.size(); ++t)
        for (size_t p = 0; p != definition->techniques[t].passes.size(); ++p)
        {
            wot::StockDx9PassSelection pass;
            assert(first.select_pass(t, p, pass, error));
            if (pass.vertex_shader && pass.pixel_shader) ++selected;
        }
    assert(selected);
    std::cout << "Original control-point shader private parameters and real passes passed\n";
}
