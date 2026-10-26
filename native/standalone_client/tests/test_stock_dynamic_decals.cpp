#include "wot/stock_dynamic_decals.hpp"
#include <cassert>
#include <fstream>

int main(int argc, char** argv)
{
    using namespace wot;
    StockDynamicDecalInstance value;
    value.created = 1.f; value.lifetime = 3.f;
    assert(stockDynamicDecalAlive(value, 3.999f));
    assert(!stockDynamicDecalAlive(value, 4.f));
    value.lifetime = 0.f;
    assert(!stockDynamicDecalAlive(value, 1.f));
    if (argc == 1) return 0;
    assert(argc == 3);
    std::ifstream input(argv[1], std::ios::binary);
    std::ofstream output(argv[2], std::ios::binary);
    std::array<std::uint8_t, 92> bytes;
    while (input.read(reinterpret_cast<char*>(bytes.data()), bytes.size()))
    {
        static_assert(sizeof(value) == 64, "Fixture instance has no native pointers");
        std::memcpy(&value, bytes.data(), 64);
        float time, start, range;
        std::memcpy(&time, bytes.data() + 64, 4);
        std::memcpy(&start, bytes.data() + 68, 4);
        std::memcpy(&range, bytes.data() + 72, 4);
        std::array<std::uint16_t, 4> rectangle;
        std::memcpy(rectangle.data(), bytes.data() + 76, 8);
        std::uint32_t group; int downsample;
        std::memcpy(&group, bytes.data() + 84, 4);
        std::memcpy(&downsample, bytes.data() + 88, 4);
        const float alpha = stockDynamicDecalAlpha(value, time, start, range);
        const std::uint32_t accepted = !(alpha <= .003921569f);
        auto record = accepted ? stockDynamicDecalRecord(value, alpha, group, rectangle, downsample) : std::array<std::uint8_t, 124>{};
        output.write(reinterpret_cast<const char*>(&accepted), 4);
        output.write(reinterpret_cast<const char*>(&alpha), 4);
        output.write(reinterpret_cast<const char*>(record.data()), record.size());
    }
    assert(input.eof() && input.gcount() == 0);
}
