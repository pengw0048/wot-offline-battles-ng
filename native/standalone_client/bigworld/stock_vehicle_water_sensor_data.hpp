#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace wot {

// #1513 WaterSensor::Config, copied by 00794890 to component+0x48.
struct StockWaterSensorConfig {
    std::array<float, 3> turretOffset{};
    std::array<float, 2> carryingPoint{};
    float lightVelocity = 0.f, heavyVelocity = 0.f, heavyDepth = 0.f;
};

// Bytes +0xbc/+0xbd/+0xbe hold one sample per update; Python reads bit 0.
struct StockWaterSensorState {
    float waterHeight = 0.f;
    std::uint8_t splash = 0, inWater = 0, underWater = 0;
};

struct StockWaterSensorSample {
    bool splashEdge = false;
    bool underWaterChanged = false;
    std::array<float, 3> hitPoint{};
};

// LodProcessorHandlerMixin 00781f70: process unless either limit is exceeded.
inline bool stockWaterSensorLodAllows(float maxDistance, std::uint32_t maxPriority,
    float distance, std::uint32_t priority) {
    return !(maxDistance < distance) && !(maxPriority < priority);
}

// 00794cb0 after the LOD gate. `matrix` is the sampled sensor plane (row-major,
// translation in row 3). `surface(start, end)` is the Waters scan 00f645e0 with
// the alpha test disabled; `transform` is D3DXVec3Transform.
template <class Surface, class Transform>
StockWaterSensorSample measureStockWaterSensor(StockWaterSensorState& state,
    const StockWaterSensorConfig& config, const std::array<float, 16>& matrix,
    Surface&& surface, Transform&& transform) {
    const std::array<float, 3> position{{matrix[12], matrix[13], matrix[14]}};
    const std::array<float, 3> above{{position[0], position[1] + 1.f, position[2]}};
    const float height = surface(position, above);
    state.waterHeight = height;
    const bool inWater = height != -1.f;
    state.inWater = static_cast<std::uint8_t>(state.inWater << 1 | (inWater ? 1 : 0));
    bool underWater = false;
    if (inWater) {
        const std::array<float, 4> turret = transform(config.turretOffset, matrix);
        underWater = height > turret[1] - position[1];
    }
    state.underWater = static_cast<std::uint8_t>(state.underWater << 1 | (underWater ? 1 : 0));
    StockWaterSensorSample sample;
    if (inWater) {
        state.splash = static_cast<std::uint8_t>(state.splash << 1 | 1);
        sample.hitPoint = {{position[0], position[1] + height, position[2]}};
    } else {
        // The four carrying-point probes are shifted out again at 00794f41:
        // their hits never reach bit 0, the splash edge or the hit point.
        state.splash = static_cast<std::uint8_t>(state.splash << 1);
    }
    sample.splashEdge = (state.splash & 3) == 1;
    const unsigned change = state.underWater & 3;
    sample.underWaterChanged = change == 1 || change == 2;
    return sample;
}

// 0079501d probes from the water hit down by Config.heavyDepth.
inline std::array<float, 3> stockWaterSensorHeavyProbeEnd(const std::array<float, 3>& hit,
    const StockWaterSensorConfig& config) {
    return {{hit[0], hit[1] + -config.heavyDepth, hit[2]}};
}

// LowFrequencyUpdater<MultiBlockPool<WaterSensor>>: 3 per frame, 0.25 s (005af540).
struct StockLowFrequencyEntry {
    float remaining, reset;
    std::uint32_t stamp;
    std::uint64_t token;
};

namespace stock_low_frequency_detail {
inline bool due(const StockLowFrequencyEntry& entry) { return entry.remaining <= 0.f; }
inline bool before(const StockLowFrequencyEntry& left, const StockLowFrequencyEntry& right) {
    return (due(left) && !due(right)) || (due(left) == due(right) && left.stamp < right.stamp);
}
// MSVC _Pop_heap_hole_by_index as called by 013ef910.
inline void adjust(StockLowFrequencyEntry* first, std::ptrdiff_t hole, std::ptrdiff_t bottom,
    StockLowFrequencyEntry value) {
    const std::ptrdiff_t top = hole;
    std::ptrdiff_t index = hole;
    const std::ptrdiff_t maxNonLeaf = (bottom - 1) / 2;
    while (index < maxNonLeaf) {
        index = 2 * index + 2;
        if (before(first[index], first[index - 1])) --index;
        first[hole] = first[index];
        hole = index;
    }
    if (index == maxNonLeaf && bottom % 2 == 0) {
        first[hole] = first[bottom - 1];
        hole = bottom - 1;
    }
    for (std::ptrdiff_t parent = (hole - 1) / 2; top < hole && before(first[parent], value);
         parent = (hole - 1) / 2) {
        first[hole] = first[parent];
        hole = parent;
    }
    first[hole] = value;
}
}

// 013efb40: age every entry, then MSVC partial_sort the first `limit` entries.
inline void advanceStockLowFrequency(std::vector<StockLowFrequencyEntry>& entries,
    float elapsed, std::uint32_t frame, std::size_t limit) {
    using namespace stock_low_frequency_detail;
    for (StockLowFrequencyEntry& entry : entries) {
        const float previous = entry.remaining;
        entry.remaining = previous - elapsed;
        if (entry.remaining <= 0.f && previous > 0.f) entry.stamp = frame;
    }
    const std::ptrdiff_t size = static_cast<std::ptrdiff_t>(entries.size());
    const std::ptrdiff_t middle = static_cast<std::ptrdiff_t>(limit) < size ?
        static_cast<std::ptrdiff_t>(limit) : size;
    if (middle == 0) return;
    StockLowFrequencyEntry* first = entries.data();
    for (std::ptrdiff_t hole = middle / 2; 0 < hole;) {
        --hole;
        adjust(first, hole, middle, first[hole]);
    }
    for (std::ptrdiff_t next = middle; next < size; ++next) {
        if (before(first[next], first[0])) {
            const StockLowFrequencyEntry value = first[next];
            first[next] = first[0];
            adjust(first, 0, middle, value);
        }
    }
    for (std::ptrdiff_t last = middle; 1 < last; --last) {
        const StockLowFrequencyEntry value = first[last - 1];
        first[last - 1] = first[0];
        adjust(first, 0, last - 1, value);
    }
}

}
