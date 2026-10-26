#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>

namespace wot
{
inline float stockDecalAdd(float a, float b) { volatile float result = a + b; return result; }
inline float stockDecalSubtract(float a, float b) { volatile float result = a - b; return result; }
inline float stockDecalMultiply(float a, float b) { volatile float result = a * b; return result; }
inline float stockDecalDivide(float a, float b) { volatile float result = a / b; return result; }

inline std::uint32_t stockSpaceDecalMipLevels(std::uint32_t anisotropy, unsigned skip)
{
    // Original 13f5450 preserves unsigned subtraction and its max(1) branch.
    std::uint32_t levels = 5;
    if (anisotropy > 1)
    {
        const std::uint32_t divisor = (anisotropy >> 1) > 2 ? (anisotropy >> 1) : 2;
        std::uint32_t remaining = divisor << 4;
        levels = 0;
        while (remaining >= divisor) { ++levels; remaining /= divisor; }
    }
    const std::uint32_t remaining = levels - skip;
    return remaining > 1 ? remaining : 1;
}

inline float stockSpaceDecalDistance(const float (&position)[3], const float (&camera)[3], float zoom)
{
    // a4c810 scales each delta before squaring, then adds y*y, x*x and z*z.
    const float x = stockDecalMultiply(stockDecalSubtract(position[0], camera[0]), zoom);
    const float y = stockDecalMultiply(stockDecalSubtract(position[1], camera[1]), zoom);
    const float z = stockDecalMultiply(stockDecalSubtract(position[2], camera[2]), zoom);
    return stockDecalAdd(stockDecalAdd(stockDecalMultiply(y, y), stockDecalMultiply(x, x)), stockDecalMultiply(z, z));
}

inline bool stockSpaceDecalCameraOutside(const float (&scale)[3], const float (&position)[3],
    const float (&camera)[3], float nearPlane, float cameraFov)
{
    // a91199..a91239. Camera+8 is passed directly to cosf. COMISS/CMOVBE
    // selects outside on unordered input too; it is not a half-FOV formula.
    const float radius = stockDecalMultiply(std::sqrt(stockDecalAdd(stockDecalAdd(
        stockDecalMultiply(scale[0], scale[0]), stockDecalMultiply(scale[1], scale[1])),
        stockDecalMultiply(scale[2], scale[2]))), .5f);
    const float x = stockDecalSubtract(position[0], camera[0]);
    const float y = stockDecalSubtract(position[1], camera[1]);
    const float z = stockDecalSubtract(position[2], camera[2]);
    const float distance = std::sqrt(stockDecalAdd(stockDecalAdd(stockDecalMultiply(x, x),
        stockDecalMultiply(y, y)), stockDecalMultiply(z, z)));
    return !(stockDecalAdd(radius, stockDecalDivide(nearPlane, std::cos(cameraFov))) > distance);
}

// Original a8fd60, normal rendering branch. The 124-byte intermediate record
// and 92-byte material record come from the real provider, not the 116-byte
// serialized record. Debug visualisation has a separate native branch.
struct StockSpaceDecalMaterialInput
{
    std::array<float, 16> textureSizes;
    std::array<std::uint32_t, 7> controls;
};
static_assert(sizeof(StockSpaceDecalMaterialInput) == 92, "Original decal material input is 92 bytes");
inline std::array<float, 24> packStockSpaceDecal(
    const StockSpaceDecalMaterialInput& material, const std::array<std::uint8_t, 124>& source)
{
    auto word = [&](std::size_t offset)
    {
        return std::uint32_t(source[offset]) | (std::uint32_t(source[offset + 1]) << 8) |
            (std::uint32_t(source[offset + 2]) << 16) | (std::uint32_t(source[offset + 3]) << 24);
    };
    auto scalar = [&](std::size_t offset)
    {
        const std::uint32_t bits = word(offset);
        float result; std::memcpy(&result, &bits, 4); return result;
    };
    auto half = [&](std::size_t offset)
    {
        return unsigned(source[offset]) | (unsigned(source[offset + 1]) << 8);
    };
    unsigned channel = 0;
    const std::uint32_t type = word(0);
    if (type != 0 && type != 1 && type != 4 && type != 5 && type != 7) channel = 12;
    const float width = material.textureSizes[channel], height = material.textureSizes[channel + 1];
    const float invWidth = material.textureSizes[channel + 2], invHeight = material.textureSizes[channel + 3];
    // Material +0x54 is an integer pixel border, despite adjacent float4s.
    const std::int32_t border = static_cast<std::int32_t>(material.controls[5]);
    const int left = static_cast<int>(half(116)), top = static_cast<int>(half(118));
    const int right = static_cast<int>(half(120)), bottom = static_cast<int>(half(122));
    std::array<float, 24> output;
    for (unsigned index = 0; index != 3; ++index)
    {
        output[index] = scalar(32 + index * 4);
        output[4 + index] = scalar(48 + index * 4);
        output[8 + index] = scalar(60 + index * 4);
    }
    output[3] = stockDecalMultiply(stockDecalAdd(float(word(8) & 0xffffff), 0.f), scalar(44) >= 0.f ? 1.f : -1.f);
    output[7] = scalar(16);
    output[11] = scalar(20);
    output[12] = stockDecalMultiply(float(left + border), invWidth);
    output[13] = stockDecalMultiply(float(top + border), invHeight);
    output[14] = stockDecalMultiply(float(right - left - border * 2 + 1), invWidth);
    output[15] = stockDecalMultiply(float(bottom - top - border * 2 + 1), invHeight);
    for (unsigned index = 0; index != 4; ++index)
    {
        const float alpha = stockDecalMultiply(scalar(88 + index * 4), 255.f);
        output[16 + index] = stockDecalAdd(std::floor(alpha), stockDecalMultiply(scalar(72 + index * 4), .499f));
    }
    output[20] = stockDecalDivide(float(right - left), width);
    output[21] = stockDecalDivide(float(bottom - top), height);
    output[22] = stockDecalAdd(stockDecalAdd(float(word(104)), .0001f), scalar(24));
    output[23] = scalar(108);
    return output;
}

// Exact WGSD2 records. Native 6bcfd0 consumes the authored world transform
// directly; group bounds and inclusive ranges are consumed by 6bcf20.
struct StockSpaceDecalRecord
{
    std::array<std::uint8_t, 116> bytes;
    std::uint32_t word(std::size_t offset) const
    {
        return std::uint32_t(bytes[offset]) | (std::uint32_t(bytes[offset + 1]) << 8) |
            (std::uint32_t(bytes[offset + 2]) << 16) | (std::uint32_t(bytes[offset + 3]) << 24);
    }
    float scalar(std::size_t offset) const
    {
        const std::uint32_t bits = word(offset);
        float result;
        std::memcpy(&result, &bits, sizeof(result));
        return result;
    }
    std::uint8_t type() const { return bytes[0]; }
    std::uint32_t texture(unsigned slot) const { return word(0x44 + slot * 4); }
    std::uint32_t visibilityMask() const { return word(0x70); }
};

struct StockSpaceDecalGroup
{
    std::array<std::uint8_t, 32> bytes;
    std::uint32_t word(std::size_t offset) const
    {
        return std::uint32_t(bytes[offset]) | (std::uint32_t(bytes[offset + 1]) << 8) |
            (std::uint32_t(bytes[offset + 2]) << 16) | (std::uint32_t(bytes[offset + 3]) << 24);
    }
    float scalar(std::size_t offset) const
    {
        const std::uint32_t bits = word(offset);
        float result;
        std::memcpy(&result, &bits, sizeof(result));
        return result;
    }
    std::uint32_t first() const { return word(24); }
    std::uint32_t last() const { return word(28); }
};

struct StockSpaceDecalsData
{
    std::vector<StockSpaceDecalRecord> decals;
    std::vector<StockSpaceDecalGroup> groups;

    bool read(const void* data, std::size_t size, std::string& error)
    {
        const auto* bytes = static_cast<const std::uint8_t*>(data);
        std::size_t cursor = 0;
        StockSpaceDecalsData next;
        auto word = [&](std::uint32_t& result)
        {
            if (size - cursor < 4) return false;
            result = std::uint32_t(bytes[cursor]) | (std::uint32_t(bytes[cursor + 1]) << 8) |
                (std::uint32_t(bytes[cursor + 2]) << 16) | (std::uint32_t(bytes[cursor + 3]) << 24);
            cursor += 4;
            return true;
        };
        auto array = [&](auto& output, std::uint32_t stride)
        {
            std::uint32_t storedStride, count;
            if (!word(storedStride) || storedStride != stride || !word(count) || count > (size - cursor) / stride)
                return false;
            output.resize(count);
            for (auto& record : output)
            {
                std::memcpy(record.bytes.data(), bytes + cursor, stride);
                cursor += stride;
            }
            return true;
        };
        if ((!bytes && size) || !array(next.decals, 116) || !array(next.groups, 32) || cursor != size)
        {
            error = "WGSD2 requires complete 116-byte decals and 32-byte groups with exact EOF";
            return false;
        }
        for (const auto& group : next.groups)
        {
            if (group.first() > group.last() || group.last() >= next.decals.size())
            {
                error = "WGSD2 group has an invalid inclusive decal range";
                return false;
            }
        }
        decals.swap(next.decals);
        groups.swap(next.groups);
        error.clear();
        return true;
    }
};
}
