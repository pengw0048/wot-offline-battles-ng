#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace wot
{
// #1513 006c93e0/006cc380. These are the complete BWT2 version-2 records,
// including the settings, resource mappings and spatial-query tail. Own the
// decoded arrays: the uint16 parent array does not align the following spans.
struct StockTerrainBinary
{
    struct Header
    {
        float blockSize;
        std::int32_t minX, maxX, minZ, maxZ;
        std::uint32_t resourceKeys[3];
    } header{};
    struct Block { std::uint32_t resourceKey; std::int16_t x, z; };
    struct Mapping { std::uint32_t resourceKey, value; };
    struct Config
    {
        std::uint32_t maxDepth;
        float center[3], size;
        std::uint16_t numDataEntries, padding;
    } config{};
    struct Bounds { float minimum[3], maximum[3]; };
    struct Center { float xyz[3]; };
    struct Node { std::uint16_t child[8]; };
    struct Span { std::uint16_t first, last; };

    std::vector<Block> blocks;
    std::vector<std::uint32_t> grid;
    std::array<std::uint32_t, 37> settings{};
    std::vector<float> lodDistances;
    std::vector<Mapping> mappings;
    std::vector<Bounds> bounds;
    std::vector<Center> centers;
    std::vector<Node> nodes;
    std::vector<std::uint32_t> references;
    std::vector<std::uint16_t> parents;
    std::vector<Span> spans;
    std::vector<std::uint32_t> contents;

    float settingFloat(std::size_t word) const
    {
        float value;
        std::memcpy(&value, &settings[word], sizeof(value));
        return value;
    }

    // #1513 ac9de0, renderer version 200. Keep the original bit meanings
    // here; the public renderer has different Blend/Bump bit positions.
    static unsigned textureFlags(float minimum, float maximum, bool useLod,
        bool bump, float lodStart, float lodDistance,
        float bumpStart, float bumpDistance)
    {
        const bool drawBump = bump && minimum <= bumpStart + bumpDistance;
        if (!useLod) return 4u | (drawBump ? 8u : 0u);
        unsigned result = maximum >= lodStart ? 1u : 0u;
        if (minimum <= lodStart + lodDistance)
            result |= 4u | (drawBump ? 8u : 0u);
        return result;
    }

    static std::array<float, 4> lodParameters(float start, float end)
    {
        // #1513 adf650 publishes the actual morph interval as scale/offset.
        const float inverse = 1.f / (end - start);
        return {{inverse, -(inverse * start), 0.f, 0.f}};
    }

    struct BorderParameters
    {
        std::array<float, 4> outer, inverseDifference;
        float blend;
    };
    BorderParameters borderParameters() const
    {
        // #1513 acc0f0, preserving the individual float operations. The
        // author's narrow border can lose precision against large bounds.
        const float width = settingFloat(24), halfWidth = width * .5f;
        const float blend = settingFloat(25) + settingFloat(25);
        const float inner[4] = {settingFloat(20) + halfWidth,
            settingFloat(21) + halfWidth, settingFloat(22) - halfWidth,
            settingFloat(23) - halfWidth};
        BorderParameters result;
        result.outer = {{inner[0] - width, inner[1] - width,
            inner[2] + width, inner[3] + width}};
        for (std::size_t i = 0; i < 4; ++i)
            result.inverseDifference[i] = blend / (result.outer[i] - inner[i]);
        result.blend = blend;
        return result;
    }

    bool read(const void* bytes, std::size_t length, std::string& error)
    {
        StockTerrainBinary next;
        Cursor cursor(bytes, length);
        if (!cursor.record(next.header) || !cursor.array(next.blocks) ||
            !cursor.array(next.grid) || !cursor.record(next.settings) ||
            !cursor.array(next.lodDistances) || !cursor.array(next.mappings) ||
            !cursor.record(next.config) || !cursor.array(next.bounds) ||
            !cursor.array(next.centers) || !cursor.array(next.nodes) ||
            !cursor.array(next.references) || !cursor.array(next.parents) ||
            !cursor.array(next.spans) || !cursor.array(next.contents) ||
            !cursor.finished())
        {
            *this = StockTerrainBinary();
            error = "Incomplete or invalid BWT2 version-2 record layout";
            return false;
        }
        if (!next.validate(error))
        {
            *this = StockTerrainBinary();
            return false;
        }
        *this = std::move(next);
        error.clear();
        return true;
    }

private:
    class Cursor
    {
        const unsigned char* data_;
        std::size_t length_, offset_ = 0;
        bool raw(void* out, std::size_t count)
        {
            if (!data_ || count > length_ - offset_) return false;
            if (count) std::memcpy(out, data_ + offset_, count);
            offset_ += count;
            return true;
        }
    public:
        Cursor(const void* data, std::size_t length) :
            data_(static_cast<const unsigned char*>(data)), length_(length) {}
        template<class T> bool record(T& out)
        {
            std::uint32_t stride = 0;
            return raw(&stride, 4) && stride == sizeof(T) && raw(&out, sizeof(T));
        }
        template<class T> bool array(std::vector<T>& out)
        {
            std::uint32_t stride = 0, count = 0;
            if (!raw(&stride, 4) || stride != sizeof(T) || !raw(&count, 4) ||
                count > (length_ - offset_) / sizeof(T)) return false;
            out.resize(count);
            return raw(count ? &out[0] : nullptr, std::size_t(count) * sizeof(T));
        }
        bool finished() const { return offset_ == length_; }
    };

    bool validate(std::string& error) const
    {
        const auto fail = [&error](const char* text) { error = text; return false; };
        if (!std::isfinite(header.blockSize) || header.blockSize <= 0 ||
            header.minX > header.maxX || header.minZ > header.maxZ)
            return fail("Invalid BWT2 terrain grid bounds");
        const std::uint64_t width = std::int64_t(header.maxX) - header.minX + 1;
        const std::uint64_t height = std::int64_t(header.maxZ) - header.minZ + 1;
        if (width > std::numeric_limits<std::uint32_t>::max() ||
            height > std::numeric_limits<std::uint32_t>::max() ||
            width * height != grid.size()) return fail("Invalid BWT2 terrain grid size");
        for (std::size_t i = 0; i < blocks.size(); ++i)
        {
            const Block& block = blocks[i];
            if (block.x < header.minX || block.x > header.maxX ||
                block.z < header.minZ || block.z > header.maxZ)
                return fail("Invalid BWT2 block-to-grid reference");
            // The grid-size check above bounds this index by native size_t.
            // Subtract in 64 bits before narrowing for the 32-bit client.
            const std::uint64_t column = std::int64_t(block.x) - header.minX;
            const std::uint64_t row = std::int64_t(block.z) - header.minZ;
            if (grid[static_cast<std::size_t>(column + row * width)] != i)
                return fail("Invalid BWT2 block-to-grid reference");
        }
        for (std::uint32_t ref : grid)
            if (ref != 0xffffffffu && ref >= blocks.size())
                return fail("Invalid BWT2 grid-to-block reference");
        if (settings[0] != 200) return fail("Unsupported BWT2 terrain renderer version");
        if (!settings[27] || settings[27] >= 32 || settings[5] != (1u << settings[27]) ||
            settings[30] >= settings[27] ||
            !std::isfinite(settingFloat(28)) || settingFloat(28) <= 0 ||
            !std::isfinite(settingFloat(29)) ||
            !std::isfinite(settingFloat(24)) || settingFloat(24) <= 0)
            return fail("Invalid BWT2 terrain LOD or border settings");
        for (std::size_t word = 14; word <= 26; ++word)
            if (!std::isfinite(settingFloat(word)))
                return fail("Invalid BWT2 terrain distance or border parameter");
        for (float distance : lodDistances)
            if (!std::isfinite(distance)) return fail("Invalid BWT2 vertex LOD distance");
        if (nodes.empty() || nodes.size() > 0x8000 || bounds.size() != nodes.size() ||
            centers.size() != nodes.size() || references.size() != nodes.size() ||
            parents.size() != nodes.size() || config.numDataEntries != spans.size())
            return fail("Invalid BWT2 spatial array sizes");
        if (!std::isfinite(config.size) || config.size <= 0)
            return fail("Invalid BWT2 spatial extent");
        for (std::size_t axis = 0; axis < 3; ++axis)
        {
            if (!std::isfinite(config.center[axis])) return fail("Invalid BWT2 spatial center");
            for (std::size_t i = 0; i < nodes.size(); ++i)
                if (!std::isfinite(bounds[i].minimum[axis]) ||
                    !std::isfinite(bounds[i].maximum[axis]) ||
                    bounds[i].minimum[axis] > bounds[i].maximum[axis] ||
                    !std::isfinite(centers[i].xyz[axis]))
                    return fail("Invalid BWT2 spatial node bounds");
        }
        // Verify the actual rooted tree, including back-links. A cycle would
        // otherwise recurse indefinitely in the public visibility/collision view.
        std::vector<unsigned char> visited(nodes.size(), 0);
        std::vector<std::uint16_t> pending(1, 0);
        while (!pending.empty())
        {
            const std::uint16_t index = pending.back();
            pending.pop_back();
            if (visited[index]++) return fail("Cyclic BWT2 spatial hierarchy");
            const auto ref = references[index];
            if (ref != 0xffffffffu && ref >= spans.size())
                return fail("Invalid BWT2 spatial data reference");
            for (auto child : nodes[index].child)
            {
                if (child == 0xffffu) continue;
                const auto childIndex = std::uint16_t(child & 0x7fffu);
                if (childIndex >= nodes.size() || parents[childIndex] != index)
                    return fail("Invalid BWT2 spatial child reference");
                pending.push_back(childIndex);
            }
        }
        for (unsigned char reached : visited)
            if (!reached) return fail("Unreachable BWT2 spatial node");
        for (const Span& span : spans)
            if (span.first > span.last || span.last >= contents.size())
                return fail("Invalid BWT2 spatial content span");
        for (auto block : contents)
            if (block >= blocks.size()) return fail("Invalid BWT2 spatial block reference");
        return true;
    }
};
static_assert(sizeof(StockTerrainBinary::Header) == 32, "BWT2 header ABI");
static_assert(sizeof(StockTerrainBinary::Block) == 8, "BWT2 block ABI");
static_assert(sizeof(StockTerrainBinary::Config) == 24, "BWT2 octree ABI");
}
