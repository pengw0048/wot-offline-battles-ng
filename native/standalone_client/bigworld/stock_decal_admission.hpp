#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <vector>

namespace wot
{
// a545b0 and a4be05: this is the static provider's emission rule, before
// the shared admission sort/cap. Preserve each binary32 store and MINSS's
// second-operand result for unordered input. Other providers own their fade.
inline float stockStaticDecalFade(float distanceSquared, float startSquared,
    float inverseSquaredRange)
{
    volatile float delta = distanceSquared - startSquared;
    volatile float product = delta * inverseSquaredRange;
    volatile float remaining = 1.f - product;
    if (0.f > remaining) return 0.f;
    return remaining < 1.f ? remaining : 1.f;
}
inline bool stockStaticDecalFadeSurvives(float fade)
{ return fade > 0.003921569f; }

// a8e1c0 compares the provider's raw 124-byte record, before camera-kind
// reclassification and GPU packing. Do not substitute distance-only ordering.
struct StockDecalAdmissionKey
{
    std::uint32_t materialGroup;
    std::int32_t material;
    std::int8_t projection;
    std::uint32_t priority;
    float distanceSquared;
};
inline bool stockDecalAdmissionLess(const StockDecalAdmissionKey& a,
    const StockDecalAdmissionKey& b)
{
    if (a.materialGroup != b.materialGroup) return a.materialGroup < b.materialGroup;
    if (a.material != b.material) return a.material < b.material;
    if (a.projection != b.projection) return a.projection < b.projection;
    if (a.priority != b.priority) return a.priority < b.priority;
    return a.distanceSquared < b.distanceSquared;
}

// Input is one provider's emitted survivors: its fade/resource rejection has
// already happened. a49990 repeats this operation independently for all five
// providers. a49a50 then draws the same prepared groups in each phase; it does
// not grant another budget. Equivalent sort keys have no specified tie order.
inline std::vector<std::vector<std::uint32_t>> admitStockDecalProvider(
    const std::vector<StockDecalAdmissionKey>& emitted, std::uint32_t materialGroups,
    std::uint32_t cap)
{
    std::vector<std::vector<std::uint32_t>> groups(materialGroups);
    if (materialGroups == 0 || emitted.empty()) return groups;
    std::vector<std::uint32_t> order(emitted.size());
    std::iota(order.begin(), order.end(), std::uint32_t(0));
    std::sort(order.begin(), order.end(), [&](std::uint32_t a, std::uint32_t b)
        { return stockDecalAdmissionLess(emitted[a], emitted[b]); });
    order.resize(std::min<std::size_t>(cap, order.size()));
    for (std::uint32_t index : order)
    {
        // a9108d checks the group only after a9106d capped the sorted prefix.
        const std::uint32_t group = emitted[index].materialGroup;
        if (group < materialGroups) groups[group].push_back(index);
    }
    return groups;
}
}
