#include "stock_space_decals_data.hpp"
#include <cassert>
#include <fstream>
#include <iterator>

namespace
{
void word(std::vector<std::uint8_t>& data, std::size_t offset, std::uint32_t value)
{
    for (unsigned index = 0; index != 4; ++index) data[offset + index] = std::uint8_t(value >> (index * 8));
}
void write(std::ostream& stream, std::uint32_t value)
{
    for (unsigned index = 0; index != 4; ++index) stream.put(char(value >> (index * 8)));
}
std::uint32_t read(const std::vector<std::uint8_t>& data, std::size_t offset)
{
    return std::uint32_t(data[offset]) | (std::uint32_t(data[offset + 1]) << 8) |
        (std::uint32_t(data[offset + 2]) << 16) | (std::uint32_t(data[offset + 3]) << 24);
}
void emit(std::ostream& output, const wot::StockSpaceDecalsData& data)
{
    write(output, 116); write(output, std::uint32_t(data.decals.size()));
    for (const auto& record : data.decals) output.write(reinterpret_cast<const char*>(record.bytes.data()), record.bytes.size());
    write(output, 32); write(output, std::uint32_t(data.groups.size()));
    for (const auto& group : data.groups) output.write(reinterpret_cast<const char*>(group.bytes.data()), group.bytes.size());
}
}

int main(int argc, char** argv)
{
    wot::StockSpaceDecalsData data;
    std::string error;
    if (argc == 4 && std::string(argv[3]) == "--levels")
    {
        std::ifstream input(argv[1], std::ios::binary);
        std::ofstream output(argv[2], std::ios::binary);
        std::uint32_t value[2];
        while (input.read(reinterpret_cast<char*>(value), sizeof(value)))
            write(output, wot::stockSpaceDecalMipLevels(value[0], value[1]));
        assert(input.eof() && output.good());
        return 0;
    }
    if (argc == 4 && std::string(argv[3]) == "--distance")
    {
        std::ifstream input(argv[1], std::ios::binary);
        std::ofstream output(argv[2], std::ios::binary);
        struct Input {float position[3], camera[3], zoom;} value;
        while (input.read(reinterpret_cast<char*>(&value), sizeof(value)))
        {
            const float distance = wot::stockSpaceDecalDistance(value.position, value.camera, value.zoom);
            output.write(reinterpret_cast<const char*>(&distance), sizeof(distance));
        }
        assert(input.eof() && output.good());
        return 0;
    }
    if (argc == 4 && std::string(argv[3]) == "--pack")
    {
        std::ifstream input(argv[1], std::ios::binary);
        std::ofstream output(argv[2], std::ios::binary);
        wot::StockSpaceDecalMaterialInput material;
        std::array<std::uint8_t, 124> record;
        while (input.read(reinterpret_cast<char*>(&material), sizeof(material)))
        {
            assert(input.read(reinterpret_cast<char*>(record.data()), record.size()));
            const auto gpu = wot::packStockSpaceDecal(material, record);
            output.write(reinterpret_cast<const char*>(gpu.data()), sizeof(gpu));
        }
        assert(input.eof() && output.good());
        return 0;
    }
    if (argc == 3)
    {
        std::ifstream input(argv[1], std::ios::binary);
        std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(input)), {});
        std::ofstream output(argv[2], std::ios::binary);
        std::size_t cursor = 0;
        while (cursor < bytes.size())
        {
            assert(bytes.size() - cursor >= 4);
            const std::uint32_t size = read(bytes, cursor); cursor += 4;
            assert(size <= bytes.size() - cursor);
            assert(data.read(bytes.data() + cursor, size, error)); cursor += size;
            write(output, size); emit(output, data);
        }
        assert(cursor == bytes.size() && output.good());
        return 0;
    }
    assert(argc == 1);
    std::vector<std::uint8_t> valid(16 + 2 * 116 + 32);
    word(valid, 0, 116); word(valid, 4, 2);
    // Preserve every authored byte, including fields which the public two-map
    // descriptor cannot represent. This reader never fabricates material data.
    for (unsigned index = 0; index < 232; ++index) valid[8 + index] = std::uint8_t(index * 7);
    word(valid, 240, 32); word(valid, 244, 1);
    word(valid, 272, 0); word(valid, 276, 1);
    assert(data.read(valid.data(), valid.size(), error));
    assert(data.decals.size() == 2 && data.groups.size() == 1);
    assert(data.groups[0].first() == 0 && data.groups[0].last() == 1);
    const auto saved = data.decals[0].bytes;
    auto fails = [&](std::vector<std::uint8_t> input)
    {
        assert(!data.read(input.data(), input.size(), error));
        assert(!error.empty() && data.decals.size() == 2 && data.decals[0].bytes == saved);
    };
    for (std::size_t size = 0; size != valid.size(); ++size) fails({valid.begin(), valid.begin() + size});
    auto bad = valid; bad.push_back(0); fails(bad);
    bad = valid; word(bad, 0, 115); fails(bad);
    bad = valid; word(bad, 4, 0xffffffff); fails(bad);
    bad = valid; word(bad, 240, 31); fails(bad);
    bad = valid; word(bad, 244, 0xffffffff); fails(bad);
    bad = valid; word(bad, 272, 1); word(bad, 276, 0); fails(bad);
    bad = valid; word(bad, 276, 2); fails(bad);
    assert(!data.read(NULL, 10, error));
    std::vector<std::uint8_t> empty(16);
    word(empty, 0, 116); word(empty, 8, 32);
    assert(data.read(empty.data(), empty.size(), error));
    assert(data.decals.empty() && data.groups.empty());
}
