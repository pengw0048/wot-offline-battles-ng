#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace wot
{
// #1513 6c7510/6c7830. Keep the complete 244-byte native state: the public
// WaterState is 224 bytes and cannot describe this version by layout casting.
class StockSpaceWaterData
{
public:
    using U32 = std::uint32_t;
    struct Definition
    {
        std::array<U32,61> state;
        std::array<U32,4> resourceKeys; // foam, waves, reflection, transparency
        U32 firstCell, endCell;
        float scalar(std::size_t offset) const;
        U32 word(std::size_t offset) const;
    };
    struct Bounds { std::array<float,3> minimum, maximum; };
    struct Cell
    {
        Bounds bounds;
        U32 surface;
        std::uint8_t first;
        std::array<std::uint8_t,3> padding{};
    };
    struct Grid
    {
        U32 x, z;
        std::uint64_t vertices;
    };
    struct Instance
    {
        U32 definition;
        Grid grid;
        std::array<std::string,4> resources;
    };
    using Resolver = std::function<const char*(U32)>;

    bool read(const void* bytes, std::size_t size, std::string& error);
    bool resolve(const Resolver& lookup, std::string& error);
    void clear();
    const std::vector<Definition>& definitions() const { return definitions_; }
    const std::vector<Bounds>& bounds() const { return bounds_; }
    const std::vector<Cell>& cells() const { return cells_; }
    const std::vector<Instance>& instances() const { return instances_; }
    bool resourcesResolved() const { return resolved_; }
    static bool grid(const Definition& definition, Grid& result);
private:
    std::vector<Definition> definitions_;
    std::vector<Bounds> bounds_;
    std::vector<Cell> cells_;
    std::vector<Instance> instances_;
    bool resolved_ = false;
};
static_assert(sizeof(StockSpaceWaterData::Definition)==268,"Original BWWa2 definition stride");
static_assert(sizeof(StockSpaceWaterData::Bounds)==24,"Original BWWa2 bounds stride");
static_assert(sizeof(StockSpaceWaterData::Cell)==32,"Original BWWa2 CPU cell footprint");
}
