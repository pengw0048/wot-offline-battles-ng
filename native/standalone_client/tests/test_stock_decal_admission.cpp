#include "stock_decal_admission.hpp"
#include <array>
#include <cassert>
#include <cstring>
#include <fstream>
#include <limits>

static std::uint32_t read(std::istream& input)
{
    std::uint32_t result = 0; input.read(reinterpret_cast<char*>(&result), 4);
    if (!input) throw 1;
    return result;
}
static void write(std::ostream& output, std::uint32_t value)
{ output.write(reinterpret_cast<const char*>(&value), 4); }
int main(int argc, char** argv)
{
    using namespace wot;
    const std::vector<StockDecalAdmissionKey> input = {
        {0, 7, 0, 0, 1}, {0, 0, 0, 0, 100}, {0, 2, 0, 0, 2}, {1, 0, 0, 0, 0}};
    assert(admitStockDecalProvider(input, 1, 1)[0] == std::vector<std::uint32_t>{1});
    assert(admitStockDecalProvider(input, 0, 1).empty());
    assert(admitStockDecalProvider(input, 1, 0)[0].empty());
    assert(admitStockDecalProvider(input, 1, 4)[0].size() == 3);
    assert(stockDecalAdmissionLess({0, -1, 0, 0, 0}, {0, 0, 0, 0, 0}));
    assert(stockDecalAdmissionLess({0, 0, -128, 0, 0}, {0, 0, 127, 0, 0}));
    assert(stockDecalAdmissionLess({0, 0, 0, 0x7fffffff, 1}, {0, 0, 0, 0x80000000, 0}));
    if (argc == 4 && std::strcmp(argv[1], "--fade") == 0)
    {
        std::ifstream source(argv[2], std::ios::binary);
        std::ofstream output(argv[3], std::ios::binary);
        const std::uint32_t count = read(source);
        for (std::uint32_t i = 0; i < count; ++i)
        {
            float inputValues[3] = {};
            source.read(reinterpret_cast<char*>(inputValues), sizeof(inputValues));
            if (!source) return 3;
            const float fade = stockStaticDecalFade(inputValues[0], inputValues[1], inputValues[2]);
            output.write(reinterpret_cast<const char*>(&fade), 4);
            write(output, stockStaticDecalFadeSurvives(fade) ? 1 : 0);
        }
        return output ? 0 : 4;
    }
    if (argc == 1) return 0;
    if (argc != 3) return 2;
    std::ifstream source(argv[1], std::ios::binary);
    std::ofstream output(argv[2], std::ios::binary);
    const std::uint32_t cases = read(source);
    for (std::uint32_t c = 0; c < cases; ++c)
    {
        const std::uint32_t cap = read(source), groups = read(source), count = read(source);
        std::vector<StockDecalAdmissionKey> keys;
        for (std::uint32_t i = 0; i < count; ++i)
        {
            std::array<unsigned char, 124> record = {};
            source.read(reinterpret_cast<char*>(record.data()), record.size());
            if (!source) return 3;
            StockDecalAdmissionKey key = {};
            std::memcpy(&key.materialGroup, record.data() + 112, 4);
            std::memcpy(&key.material, record.data(), 4);
            std::memcpy(&key.projection, record.data() + 11, 1);
            std::memcpy(&key.priority, record.data() + 4, 4);
            std::memcpy(&key.distanceSquared, record.data() + 12, 4);
            keys.push_back(key);
        }
        const auto admitted = admitStockDecalProvider(keys, groups, cap);
        for (const auto& group : admitted)
        {
            write(output, static_cast<std::uint32_t>(group.size()));
            for (std::uint32_t index : group) write(output, index);
        }
    }
    return output ? 0 : 4;
}
