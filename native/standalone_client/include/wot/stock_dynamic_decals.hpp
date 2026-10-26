#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace wot
{
inline float stockDynamicDecalAdd(float a, float b) { volatile float r = a + b; return r; }
inline float stockDynamicDecalSub(float a, float b) { volatile float r = a - b; return r; }
inline float stockDynamicDecalMul(float a, float b) { volatile float r = a * b; return r; }
inline float stockDynamicDecalDiv(float a, float b) { volatile float r = a / b; return r; }

struct StockDynamicDecalInstance
{
    std::array<float, 3> position = {};
    std::array<float, 3> scale = {};
    std::array<float, 4> rotation = {};
    std::uint32_t priority = 0;
    std::uint32_t material = 1;
    std::uint32_t influence = 0x12;
    float created = 0.f;
    float lifetime = 0.f;
    float distanceSquared = 0.f;
};

inline bool stockDynamicDecalAlive(const StockDynamicDecalInstance& value, float time)
{
    // a5f680: expiration is exclusive and precedes visibility admission.
    return time < stockDynamicDecalAdd(value.created, value.lifetime);
}

inline float stockDynamicDecalAlpha(const StockDynamicDecalInstance& value,
    float time, float startSquared, float inverseRange)
{
    // a545b0 and a5f080 preserve their scalar float stores, including NaN.
    float fade = stockDynamicDecalSub(1.f, stockDynamicDecalMul(
        stockDynamicDecalSub(value.distanceSquared, startSquared), inverseRange));
    if (fade < 0.f) fade = 0.f;
    if (1.f <= fade) fade = 1.f;
    return stockDynamicDecalMul(stockDynamicDecalSub(1.f, stockDynamicDecalDiv(
        stockDynamicDecalSub(time, value.created), value.lifetime)), fade);
}

inline std::array<std::uint8_t, 124> stockDynamicDecalRecord(
    const StockDynamicDecalInstance& value, float alpha, std::uint32_t group,
    const std::array<std::uint16_t, 4>& rectangle, int downsample)
{
    // a5f080 publishes this record to the shared original decal renderer.
    std::array<std::uint8_t, 124> output = {};
    auto word = [&](unsigned offset, std::uint32_t v) { std::memcpy(output.data() + offset, &v, 4); };
    auto scalar = [&](unsigned offset, float v) { std::memcpy(output.data() + offset, &v, 4); };
    const std::uint32_t reduced = 0u - std::uint32_t(downsample);
    word(0, value.material == 0 ? (std::int32_t(reduced) > 0 ? reduced : 0u) : value.material);
    word(4, value.priority);
    word(8, value.influence & 0xffffffu);
    scalar(12, value.distanceSquared);
    scalar(16, 1.f); scalar(20, 1.f);
    std::memcpy(output.data() + 32, value.rotation.data(), 16);
    std::memcpy(output.data() + 48, value.position.data(), 12);
    std::memcpy(output.data() + 60, value.scale.data(), 12);
    for (unsigned i = 0; i != 4; ++i) scalar(88 + i * 4, alpha);
    word(112, group);
    std::memcpy(output.data() + 116, rectangle.data(), 8);
    return output;
}
}
