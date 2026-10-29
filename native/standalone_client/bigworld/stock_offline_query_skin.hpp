#pragma once

#include "offline_destructible_geometry.h"

#include <cstdint>
#include <map>
#include <set>
#include <tuple>
#include <utility>
#include <vector>

namespace stock_offline_query
{
using Point = offline_destructible::Point;
using Box = offline_destructible::Box;
using Key = std::pair<std::uint32_t, std::uint32_t>; // chunk, item
using Cell = std::pair<int, int>;
// The final component is -1 for Python None, otherwise an exact material.
using MaterialKey = std::tuple<std::uint32_t, std::uint32_t, std::int64_t>;

struct Surface
{
    std::uint32_t material, flags, item, chunk;
    Key key() const { return Key(chunk, item); }
    bool operator<(const Surface & other) const
    {
        return std::tie(material, flags, item, chunk) <
            std::tie(other.material, other.flags, other.item, other.chunk);
    }
    bool operator==(const Surface & other) const
    {
        return material == other.material && flags == other.flags &&
            item == other.item && chunk == other.chunk;
    }
};

enum class SkinKind { Structure, Fragile, Falling, Other };
struct SkinInstance
{
    SkinKind kind = SkinKind::Other;
    std::vector<Box> boxes;
};

// A synchronous, canonical data view. Its owner refreshes it after every
// Python effect barrier. No native geometry or Python reference is retained.
struct SkinView
{
    std::map<Key, SkinInstance> instances, baked_instances;
    std::set<Key> tree_instances, isolated_slots, excluded_instances;
    std::set<std::uint32_t> isolated_chunks, layout_repairs;
    std::map<std::uint32_t, std::int64_t> layout_generations;
    std::map<Cell, std::set<Key>> contact_bins, baked_shot_bins;
    std::set<MaterialKey> destroyed, speculative;
};

struct SkinTuning
{
    double ray_epsilon, point_padding, bin_metres;
};

// These are C++ native Vector3 operations, never Python callbacks. Their
// owner retains the float32 intermediate stores of subtraction/length and
// start + (end-start).scale(factor), including the float32 scalar conversion.
struct SkinMath
{
    void * context;
    double (*distance)(void *, const Point &, const Point &);
    Point (*bound)(void *, const Point &, const Point &, double);
};

struct SkinResult
{
    bool found = false;
    double distance = 0.;
    std::set<Surface> excluded;
};

// The anonymous-original law does not alias any real registered/baked/tree
// identity. Native callers supply exact unsigned integer surface fields.
bool anonymousOriginal(const SkinView &, const Surface &);

// Pure control-law port of the pinned _owned_component_skin_1513 and
// _compiled_motion_skin_1513. Input points have already crossed the same
// native Vector3 float32 boundary as the original Python capabilities.
SkinResult motionSkin(const SkinView &, const Point & point,
    const Point & start, const Point & end, const Point * normal,
    const std::vector<Surface> &, const SkinTuning &, const SkinMath &);
}
