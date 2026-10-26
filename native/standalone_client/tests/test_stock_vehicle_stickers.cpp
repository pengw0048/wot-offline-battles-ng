#include "stock_vehicle_stickers_data.hpp"
#include <cassert>
#include <cstring>
#include <iostream>

int main()
{
    wot::StockDamageStickerRegistry registry;
    wot::StockDamageStickerVariant first = {"a.dds", "a_n.dds", {{.4f, .7f}}, .15f, true};
    wot::StockDamageStickerVariant second = {"b.dds", "", {{.8f, .5f}}, -.25f, false};
    assert(!registry.find(-1) && !registry.find(0));
    assert(registry.add("penetration", first) == 1);
    assert(registry.add("penetration", second) == 0);
    assert(registry.add("ricochet", second) == 2);
    assert(registry.add("ricochet", first) == 1);
    assert(registry.size() == 2 && !registry.find(2));
    const auto* style = registry.find(0);
    assert(style && style->name == "penetration" && style->variants.size() == 2);
    assert(style->variants[0].texture == "a.dds" && style->variants[0].bumpTexture == "a_n.dds");
    assert(style->variants[1].texture == "b.dds" && style->variants[1].bumpTexture.empty());
    assert(style->variants[0].size == first.size && style->variants[1].size == second.size);
    assert(style->variants[0].randomYaw && !style->variants[1].randomYaw);
    assert(std::memcmp(&style->variants[1].variation, &second.variation, sizeof(float)) == 0);
    // Names are byte strings, with the same length-aware identity as BW::string.
    assert(registry.add(std::string("impact\0A", 8), first) == 3);
    assert(registry.add(std::string("impact\0B", 8), first) == 4);
    assert(registry.add(std::string("impact\0A", 8), second) == 2);
    assert(registry.add("Penetration", first) == 5);
    for (unsigned width : {64u, 128u, 256u, 512u})
        for (unsigned height : {64u, 128u, 256u, 512u})
        {
            const auto uv = wot::stockVehicleStickerTextureUV(width, height);
            assert(uv[0] * width == float(width) - 1.5f && uv[1] * height == float(height) - 1.5f);
            assert(uv[2] * width == .5f && uv[3] * height == .5f);
        }
    std::cout << "Damage sticker registry passed: exact name identity, native IDs, ordered variants and unchanged producer parameters" << std::endl;
}
