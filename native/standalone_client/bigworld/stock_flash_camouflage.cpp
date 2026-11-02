#include "stock_flash_camouflage.hpp"

#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <limits>

namespace wot { namespace flash_camouflage {

bool hasProtocol(const char* value)
{
    const char* expected = "img://camouflage,";
    if (!value) return false;
    while (*expected)
    {
        char actual = *value++;
        if (actual >= 'A' && actual <= 'Z') actual += 'a' - 'A';
        if (actual != *expected++) return false;
    }
    return true;
}

bool integer(const char*& cursor, std::int64_t& value)
{
    char* end = NULL;
    errno = 0;
    value = std::strtoll(cursor, &end, 10);
    if (end == cursor || errno == ERANGE) return false;
    cursor = end;
    return true;
}

bool comma(const char*& cursor)
{
    if (*cursor != ',') return false;
    ++cursor;
    return true;
}

bool quoted(const char*& cursor, std::string& value)
{
    if (*cursor++ != '"') return false;
    const char* begin = cursor;
    while (*cursor && *cursor != '"') ++cursor;
    if (*cursor != '"' || cursor == begin) return false;
    value.assign(begin, cursor++);
    return true;
}

bool parse(const char* value, Parameters& output)
{
    if (!hasProtocol(value)) return false;
    const char* cursor = value + 17;
    Parameters next;
    std::int64_t width, height;
    if (!integer(cursor, width) || !comma(cursor) || !integer(cursor, height) || !comma(cursor) ||
        width <= 0 || height <= 0 || width > 0xffffffffLL || height > 0xffffffffLL ||
        !quoted(cursor, next.texture) || !comma(cursor) ||
        !quoted(cursor, next.background) || !comma(cursor)) return false;
    next.width = static_cast<std::uint32_t>(width);
    next.height = static_cast<std::uint32_t>(height);
    // The x86 RawImage allocation and output plane must both fit in address space.
    if (next.width > std::numeric_limits<std::uint32_t>::max() / 4 / next.height) return false;
    for (unsigned i = 0; i < 4; ++i)
    {
        std::int64_t color;
        if (!integer(cursor, color) || color < -2147483648LL || color > 4294967295LL || !comma(cursor))
            return false;
        next.colors[i] = static_cast<std::uint32_t>(color);
    }
    for (unsigned i = 0; i < 4; ++i)
    {
        char* end = NULL;
        const double weight = std::strtod(cursor, &end);
        // camoIconTemplate derives each weight from a palette alpha byte.
        if (end == cursor || !std::isfinite(weight) || weight < 0.0 || weight > 1.0) return false;
        next.weights[i] = static_cast<float>(weight);
        if (!std::isfinite(next.weights[i])) return false;
        cursor = end;
        if (i < 3 && !comma(cursor)) return false;
    }
    while (*cursor == ' ' || *cursor == '\t' || *cursor == '\r' || *cursor == '\n') ++cursor;
    if (*cursor) return false;
    output = next;
    return true;
}

// 0xaac5c0..0xaacae7: sequential R/G/B/A mask blends, then SetPixel(ARGB).
// Input words are BGRA bytes (0xAARRGGBB), as decoded by the original ResIL.
std::uint32_t pixel(std::uint32_t mask, std::uint32_t background, const Parameters& params)
{
    const float inverse = 1.f / 255.f;
    float color[3] = { float((background >> 16) & 255) * inverse,
        float((background >> 8) & 255) * inverse, float(background & 255) * inverse };
    const unsigned channels[4] = {16, 8, 0, 24};
    for (unsigned layer = 0; layer < 4; ++layer)
    {
        const float channel = float((mask >> channels[layer]) & 255);
        const float amount = layer == 0 ? params.weights[layer] * (channel * inverse) :
            (channel * params.weights[layer]) * inverse;
        const float retained = 1.f - amount;
        for (unsigned component = 0; component < 3; ++component)
        {
            const float palette = float((params.colors[layer] >> channels[component]) & 255) * inverse;
            const float added = palette * amount;
            const float previous = color[component] * retained;
            color[component] = previous + added;
        }
    }
    std::uint32_t rgba = 0xff000000u;
    for (unsigned component = 0; component < 3; ++component)
    {
        const float clamped = color[component] < 0.f ? 0.f : color[component] > 1.f ? 1.f : color[component];
        // The native loop packs lane 0 into the SetPixel argument's low byte.
        // Format-1 SetPixel (0xb647f2) then writes its high byte first. Return
        // the resulting R,G,B,A bytes, rather than that intermediate argument.
        rgba |= static_cast<std::uint32_t>(clamped * 255.f + .5f) << ((2 - component) * 8);
    }
    return rgba;
}


}} // namespace wot::flash_camouflage
