// The production ARIE material owner (StockDx9EffectRuntime/Material/Binding and
// reflection) over the original background, transfer and z_pass effects. A
// ManagedEffect's definition and each EffectMaterial Properties binding are
// separate StockDx9EffectMaterial instances from one runtime/context, as
// create_stock_effect_material builds them. The writes below follow the adapter
// (SetTexture -> set_texture, SetVector[Array] type 3, SetInt 1, SetBool 4).
#include "stock_effect_dx9.hpp"
#include "wot/stock_effect_reflection.hpp"
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

namespace {
void check(bool condition, const std::string & message)
{
    if (!condition) throw std::runtime_error(message);
}

struct Probe
{
    std::shared_ptr<wot::StockEffectBinary> effect;
    std::shared_ptr<wot::StockDx9EffectRuntime> runtime;
    wot::StockEffectUniformPool pool;
    wot::StockEffectUniformContext context{pool};
    std::unique_ptr<wot::StockEffectReflection> reflection;

    const wot::StockEffectParameterReference * parameter(const char * name) const
    {
        const auto handle = reflection->parameter_by_name(name);
        return handle ? reflection->parameter_reference(handle) : nullptr;
    }
    std::vector<unsigned char> bytes(wot::StockDx9EffectMaterial & owner, const char * name, std::size_t size)
    {
        const auto * p = parameter(name);
        const auto & uniform = effect->uniform_buffers[p->owner].uniforms[p->member];
        // depthFunc and edgeDetectOccluded live in $Selection, which chooses the pass.
        const auto buffer = owner.binding().uniform_buffer(p->owner);
        const auto & storage = buffer ? buffer->bytes() : owner.binding().selection();
        return std::vector<unsigned char>(storage.begin() + uniform.byte_offset,
                                          storage.begin() + uniform.byte_offset + size);
    }
    void write(wot::StockDx9EffectMaterial & owner, const char * name, const void * data, std::size_t size,
               std::uint32_t type)
    {
        const auto * p = parameter(name);
        std::string error;
        check(owner.binding().set_uniform(p->owner, p->member, data, size, type, error), name + (": " + error));
    }
};

std::vector<unsigned char> raw(const void * data, std::size_t size)
{
    const auto * begin = static_cast<const unsigned char *>(data);
    return std::vector<unsigned char>(begin, begin + size);
}

// GUIBackground::draw and the edge transfer: the selected binding owns the
// texture; the definition and other bindings never see it; a lost device does
// not release a manual texture, clearing the existing binding does.
void textures(Probe & probe, wot::StockDx9EffectMaterial & definition, const char * name)
{
    const auto * p = probe.parameter(name);
    if (!p) return;
    std::string error;
    auto * texture = new IDirect3DTexture9;
    wot::StockDx9EffectMaterial binding(probe.runtime, probe.context);
    check(binding.set_texture(p->owner, texture, error) && texture->refs == 2, name);
    check(binding.texture(p->owner) == texture, std::string(name) + ": the draw binding holds the texture");
    check(!definition.texture(p->owner), std::string(name) + ": the definition stays unbound");
    {
        wot::StockDx9EffectMaterial other(probe.runtime, probe.context);
        check(!other.texture(p->owner), std::string(name) + ": another material's binding stays unbound");
    }
    check(probe.runtime->on_lost_device(error) && texture->refs == 2,
          std::string(name) + ": a lost device keeps the manual binding reference");
    check(binding.set_texture(p->owner, nullptr, error) && texture->refs == 1,
          std::string(name) + ": clearing the existing binding releases the target");
    texture->Release();
    std::cout << name << ": binding owns, reset clears\n";
}

// EdgeDetector::parameters/transfer: successive members overwrite the same
// binding, and each immediate draw reads that member's values.
void uniforms(Probe & probe, wot::StockDx9EffectMaterial & definition)
{
    wot::StockDx9EffectMaterial binding(probe.runtime, probe.context);
    wot::StockDx9EffectMaterial other(probe.runtime, probe.context);
    if (probe.parameter("g_colorMask"))
    {
        const auto initial = probe.bytes(definition, "g_colorMask", 16);
        for (unsigned index = 0; index < 4; ++index)
        {
            float mask[4] = {0.f, 0.f, 0.f, 0.f};
            mask[index] = 1.f;
            const int depth = index == 1 ? 3 : index ? 1 : 0;
            const int occluded = index == 1;
            probe.write(binding, "g_colorMask", mask, sizeof(mask), 3);
            probe.write(binding, "depthFunc", &depth, sizeof(depth), 1);
            probe.write(binding, "edgeDetectOccluded", &occluded, sizeof(occluded), 4);
            check(probe.bytes(binding, "g_colorMask", 16) == raw(mask, sizeof(mask)), "member mask reaches the draw");
            check(probe.bytes(binding, "depthFunc", 4) == raw(&depth, 4), "member depthFunc reaches the draw");
            check(probe.bytes(binding, "edgeDetectOccluded", 4) == raw(&occluded, 4),
                  "member occlusion reaches the draw");
            if (occluded)
            {
                wot::StockDx9PassSelection pass;
                std::vector<wot::StockDx9StateValue> states;
                std::string error;
                check(binding.binding().select_pass(0, 0, pass, error) && pass.render_state &&
                      wot::stock_dx9_render_states(probe.effect->render_states[*pass.render_state], states, error), error);
                unsigned function = 0, biasBits = 0;
                for (const auto & state : states)
                {
                    if (state.state == 23) function = state.value;
                    if (state.state == 195) biasBits = state.value;
                }
                float bias;std::memcpy(&bias, &biasBits, 4);
                check(function == 5 && bias == -.002f, "Original own edge uses GREATER and negative bias");
                // The old order lets the rear of a vehicle fail against its
                // own front depth. Deferring its colour leaves only scenery
                // in this test: an open view has no edge, an occluder has one.
                const float rear = .7f, ownFront = .65f, openScene = 1.f, occluder = .6f;
                check(rear + bias > ownFront && !(rear + bias > openScene) && rear + bias > occluder,
                      "Own geometry must not become its own occluder");
            }
        }
        check(probe.bytes(definition, "g_colorMask", 16) == initial, "g_colorMask: the definition is unchanged");
        check(probe.bytes(other, "g_colorMask", 16) == initial, "g_colorMask: another binding is unchanged");
        std::cout << "g_colorMask/depthFunc/edgeDetectOccluded: successive members reach the binding\n";
    }
    if (probe.parameter("g_edgeColors"))
    {
        const auto initial = probe.bytes(definition, "g_edgeColors", 64);
        const float colours[16] = {0.1f, 0.2f, 0.3f, 1.f, 1.f, 0.f, 0.f, 1.f, 0.f, 1.f, 0.f, 1.f, 1.f, 1.f, 0.f, 1.f};
        probe.write(binding, "g_edgeColors", colours, sizeof(colours), 3);
        check(probe.bytes(binding, "g_edgeColors", 64) == raw(colours, sizeof(colours)), "palette reaches the draw");
        check(probe.bytes(definition, "g_edgeColors", 64) == initial, "g_edgeColors: the definition is unchanged");
        check(probe.bytes(other, "g_edgeColors", 64) == initial, "g_edgeColors: another binding is unchanged");
        std::cout << "g_edgeColors: four colours reach the binding\n";
    }
}
}

int main(int argc, char ** argv)
{
    try
    {
        for (int a = 1; a < argc; ++a)
        {
            std::ifstream stream(argv[a], std::ios::binary);
            check(bool(stream), argv[a]);
            std::vector<unsigned char> data((std::istreambuf_iterator<char>(stream)), {});
            Probe probe;
            probe.effect = std::make_shared<wot::StockEffectBinary>();
            std::string error;
            check(wot::decode_stock_dx9_effect(data.data(), data.size(), *probe.effect, error), error);
            auto * device = new IDirect3DDevice9;
            auto * states = new ID3DXEffectStateManager;
            check(wot::StockDx9EffectRuntime::create(device, states, probe.effect, probe.runtime, error), error);
            probe.reflection.reset(new wot::StockEffectReflection(probe.effect, {}));
            std::cout << argv[a] << '\n';
            {
                wot::StockDx9EffectMaterial definition(probe.runtime, probe.context);
                textures(probe, definition, "inputTexture");
                textures(probe, definition, "g_edgeDetectedMap");
                uniforms(probe, definition);
            }
            probe.reflection.reset();
            probe.runtime.reset();
            device->Release();
            states->Release();
        }
    }
    catch (const std::exception & error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
