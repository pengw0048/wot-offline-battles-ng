// Decode exact effect schemas through the production decoder, without D3D.
#include "wot/stock_effect_binding.hpp"

#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <map>
#include <set>

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    std::ifstream stream(argv[1], std::ios::binary);
    if (!stream) return 2;
    std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(stream)), {});
    auto effect = std::make_shared<wot::StockEffectBinary>();
    std::string error;
    if (!wot::decode_stock_dx9_effect(bytes.data(), bytes.size(), *effect, error)) {
        std::cerr << error;
        return 1;
    }
    wot::StockEffectUniformPool pool;
    wot::StockEffectUniformContext context(pool);
    wot::StockEffectBinding binding(effect, context);
    std::map<std::pair<unsigned, unsigned>, std::set<unsigned>> usage;
    for (unsigned si = 0; si < effect->shaders.size(); ++si) {
        const auto& shader = effect->shaders[si];
        std::map<unsigned, unsigned> slots;
        for (const auto& resource : shader.resources) {
            if (resource.kind != 3) continue;
            std::optional<unsigned> index;
            if (!wot::resolve_stock_effect_reference(*effect, binding.selection(),
                    wot::StockEffectResourceTable::uniform_buffer,
                    resource.resource_index, index, error)) {
                std::cerr << error;
                return 1;
            }
            if (index) slots[resource.shader_slot] = *index;
        }
        for (const auto& uniform : shader.uniforms) {
            const auto slot = slots.find(uniform.uniform_slot);
            if (slot == slots.end() || slot->second >= effect->uniform_buffers.size()) continue;
            const unsigned bi = slot->second;
            const auto& buffer = effect->uniform_buffers[bi];
            for (unsigned ui = 0; ui < buffer.uniforms.size(); ++ui) {
                const auto& field = buffer.uniforms[ui];
                const auto& type = effect->types[field.type_index];
                const unsigned size = type.byte_size * (field.elements ? field.elements : 1);
                if (field.byte_offset < uniform.source_offset + uniform.source_size &&
                        uniform.source_offset < field.byte_offset + size) {
                    usage[{bi, ui}].insert(si);
                }
            }
        }
    }
    unsigned conditional_srv = 0;
    for (const auto& shader : effect->shaders) {
        for (const auto& resource : shader.resources) {
            if (resource.kind == 2 && resource.resource_index < effect->resource_indirections[1].size() &&
                    effect->resource_indirections[1][resource.resource_index] >= 0x10000) {
                ++conditional_srv;
            }
        }
    }
    std::cout << "{\"conditional_srv_references\":" << conditional_srv
              << ",\"shader_count\":" << effect->shaders.size() << ",\"techniques\":[";
    for (unsigned i = 0; i < effect->techniques.size(); ++i) {
        if (i) std::cout << ',';
        std::cout << std::quoted(effect->techniques[i].name);
    }
    std::cout << "],\"uniforms\":[";
    bool comma = false;
    for (unsigned bi = 0; bi < effect->uniform_buffers.size(); ++bi) {
        const auto& buffer = effect->uniform_buffers[bi];
        for (unsigned ui = 0; ui < buffer.uniforms.size(); ++ui) {
            const auto& uniform = buffer.uniforms[ui];
            const auto& type = effect->types[uniform.type_index];
            if (comma) std::cout << ',';
            comma = true;
            std::cout << "{\"buffer\":" << std::quoted(buffer.name)
                      << ",\"shared\":" << ((buffer.flags & 1) ? "true" : "false")
                      << ",\"name\":" << std::quoted(uniform.name)
                      << ",\"semantic\":" << std::quoted(uniform.semantic)
                      << ",\"type\":" << std::quoted(type.name)
                      << ",\"basic_type\":" << type.basic_type
                      << ",\"class\":" << type.type_class
                      << ",\"bytes\":" << type.byte_size
                      << ",\"elements\":" << uniform.elements
                      << ",\"offset\":" << uniform.byte_offset
                      << ",\"used_by_shaders\":" << usage[{bi, ui}].size() << '}';
        }
    }
    std::cout << "],\"shadow_consumers\":[";
    comma = false;
    for (unsigned si = 0; si < effect->shaders.size(); ++si) {
        const auto& shader = effect->shaders[si];
        for (const auto& resource : shader.resources) {
            if (resource.kind != 2) continue;
            std::optional<unsigned> index;
            if (!wot::resolve_stock_effect_reference(*effect, binding.selection(),
                    wot::StockEffectResourceTable::resource_view,
                    resource.resource_index, index, error)) {
                std::cerr << error;
                return 1;
            }
            if (!index) continue;
            const auto& view = effect->resource_views[*index];
            if (view.semantic != "ShadowScreenSpaceMap") continue;
            if (comma) std::cout << ',';
            comma = true;
            std::cout << "{\"shader\":" << si << ",\"sampler\":" << resource.shader_slot
                      << ",\"stage\":" << shader.stage << ",\"bytecode\":[";
            for (unsigned wi = 0; wi < shader.bytecode.size(); ++wi) {
                if (wi) std::cout << ',';
                std::cout << shader.bytecode[wi];
            }
            std::cout << "]}";
        }
    }
    std::cout << "],\"resources\":[";
    comma = false;
    for (const auto& view : effect->resource_views) {
        if (comma) std::cout << ',';
        comma = true;
        std::cout << "{\"name\":" << std::quoted(view.name)
                  << ",\"semantic\":" << std::quoted(view.semantic)
                  << ",\"elements\":" << view.elements << '}';
    }
    std::cout << "]}";
}
