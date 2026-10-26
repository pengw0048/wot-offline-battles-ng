#pragma once

#include <array>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace wot {

inline std::array<float, 4> stockVehicleStickerTextureUV(std::uint32_t width, std::uint32_t height)
{
    // 13f7bb0 publishes inclusive right/bottom (origin+sourceSize-1).
    // 95aa70 subtracts another half texel from that coordinate difference.
    return {{(float(width) - 1.5f) / float(width),
        (float(height) - 1.5f) / float(height), .5f / float(width), .5f / float(height)}};
}

struct StockDamageStickerVariant
{
    std::string texture, bumpTexture;
    std::array<float, 2> size;
    float variation;
    bool randomYaw;
};

struct StockDamageStickerStyle
{
    std::string name;
    std::vector<StockDamageStickerVariant> variants;
};

class StockDamageStickerRegistry
{
public:
    int add(const std::string& name, const StockDamageStickerVariant& variant)
    {
        for (size_t index = 0; index < styles_.size(); ++index)
            if (styles_[index].name == name)
            {
                styles_[index].variants.push_back(variant);
                return static_cast<int>(index);
            }
        if (styles_.size() >= static_cast<size_t>(std::numeric_limits<int>::max()))
            throw std::length_error("Damage sticker style index exceeds the native int domain");
        StockDamageStickerStyle style;
        style.name = name;
        style.variants.push_back(variant);
        styles_.push_back(std::move(style));
        // Original 95ae90 returns the new count for a new name, but the zero
        // based index for each additional variant. The exact Python producer
        // overwrites its ID with each subsequent variant registration.
        return static_cast<int>(styles_.size());
    }

    const StockDamageStickerStyle* find(int index) const
    {
        return index >= 0 && static_cast<size_t>(index) < styles_.size() ?
            &styles_[static_cast<size_t>(index)] : NULL;
    }

    size_t size() const { return styles_.size(); }

private:
    std::vector<StockDamageStickerStyle> styles_;
};

} // namespace wot
