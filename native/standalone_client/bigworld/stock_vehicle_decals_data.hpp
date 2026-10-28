#pragma once
#include "stock_space_decals_data.hpp"

namespace wot
{
// a64150: the persistent attachment provider has no lifetime fade, colour
// modulation, visibility mask or parallax. It does retain distance fading.
inline std::array<std::uint8_t, 124> stockVehicleDecalRecord(unsigned material,
    unsigned priority, unsigned influence, float distanceSquared, float alpha,
    const float* rotation, const float* position, const float* scale,
    const std::array<std::uint16_t, 4>& rectangle)
{
    std::array<std::uint8_t, 124> record = {};
    auto word = [&](unsigned offset, unsigned value) { std::memcpy(record.data()+offset,&value,4); };
    auto scalar = [&](unsigned offset, float value) { std::memcpy(record.data()+offset,&value,4); };
    word(0,material); word(4,priority); word(8,influence & 0xffffffu);
    scalar(12,distanceSquared); scalar(16,1.f); scalar(20,1.f);
    std::memcpy(record.data()+32,rotation,16);
    std::memcpy(record.data()+48,position,12);
    std::memcpy(record.data()+60,scale,12);
    for (unsigned i=0;i<4;++i) scalar(88+i*4,alpha);
    std::memcpy(record.data()+116,rectangle.data(),8);
    return record;
}
}
