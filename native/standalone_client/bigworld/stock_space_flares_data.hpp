#pragma once
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

namespace wot
{
struct StockFlareData
{
    struct Definition
    {
        std::uint32_t resource;
        float maxDistance, area, fadeSpeed, position[3], colour[3];
        std::uint32_t flags, visibilityMask;
    };
    std::vector<Definition> definitions;
    bool read(const void* bytes, std::size_t size, std::string& error)
    {
        definitions.clear(); error.clear();
        if (!bytes || size < 8) { error="Truncated BWfr array header"; return false; }
        std::uint32_t stride, count;
        std::memcpy(&stride,bytes,4); std::memcpy(&count,static_cast<const char*>(bytes)+4,4);
        if (stride!=48 || count>(size-8)/48 || size-8!=std::size_t(count)*48)
        { error="Invalid BWfr array extent"; return false; }
        std::vector<Definition> next(count);
        if (count) std::memcpy(next.data(),static_cast<const char*>(bytes)+8,std::size_t(count)*48);
        for (const auto& value:next)
        {
            if (!std::isfinite(value.maxDistance)||!std::isfinite(value.area)||!std::isfinite(value.fadeSpeed))
            { error="Non-finite BWfr effect scalar"; return false; }
            for (float component:value.position) if (!std::isfinite(component))
            { error="Non-finite BWfr placement"; return false; }
            for (float component:value.colour) if (!std::isfinite(component))
            { error="Non-finite BWfr tint"; return false; }
        }
        definitions.swap(next); return true;
    }
    static unsigned channel(double value)
    {
        // 6be3b0 uses CVTTSD2SI, then signed clamp. Indefinite converts to
        // INT_MIN, including finite values outside the signed 32-bit range.
        const int converted = !std::isfinite(value) || value>=2147483648.0 || value< -2147483648.0 ?
            (std::numeric_limits<int>::min)() : static_cast<int>(value);
        return converted<0?0u:converted>255?255u:static_cast<unsigned>(converted);
    }
    static std::uint32_t modulate(std::uint32_t colour, const float (&tint)[3])
    {
        // Native SSE performs the two multiplications in float, then widens
        // before multiplying by the double constant 255 and truncating.
        unsigned rgb[3];
        for (unsigned i=0;i<3;++i)
        {
            const float normalised=float((colour>>(16-i*8))&255)*1.5378702300949953e-05f;
            const float product=normalised*tint[i];
            rgb[i]=channel(double(product)*255.0);
        }
        const float alpha=float(colour>>24)*0.003921568859368563f;
        return (channel(double(alpha)*255.0)<<24)|(rgb[0]<<16)|(rgb[1]<<8)|rgb[2];
    }
};
static_assert(sizeof(StockFlareData::Definition)==48,"Original BWfr2 stride");
}
