#pragma once

#include <array>
#include <cfloat>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace wot {

// LodProcessorHandlerMixin 00781f70: comiss/jb then cmp/jbe. An unordered
// distance fails, unlike a plain `!(max < distance)` test.
inline bool stockTerrainSensorLodAllows(float maxDistance, std::uint32_t maxPriority,
    float distance, std::uint32_t priority) {
    return maxDistance >= distance && priority <= maxPriority;
}

// ClosestCcbNative::operator() 00f62400 as configured by 00796410: flags 0x12
// and an empty filter. 00f63970 keeps the triangle's material-kind byte.
struct StockTerrainMatKindHit {
    static const std::uint32_t COLLISION_MASK = 0x12;
    float nearest = FLT_MAX;
    std::uint32_t kind = 0;
    bool found = false;

    // Returns the BigWorld traversal code: 3 skips the triangle, 1 narrows.
    int offer(std::uint32_t collisionFlags, std::uint32_t materialKind, float distance) {
        if (collisionFlags & COLLISION_MASK) return 3;
        if (distance > nearest) return 1;
        if (nearest > distance) {
            nearest = distance;
            kind = materialKind;
            found = true;
        }
        return 1;
    }
};

// 00796410 after its LOD gate, camera space and matrix read. `transform` is
// D3DXVec3Transform; `collide(start, end, kind)` is collideSegment 00f642b0.
// A miss stores 0, exactly as the original cmovne at 00796560.
template <class Transform, class Collide>
void sampleStockTerrainMatKinds(std::vector<std::uint32_t>& kinds,
    const std::vector<std::array<float, 3>>& points, float scanLength,
    const std::array<float, 16>& matrix, Transform&& transform, Collide&& collide) {
    for (std::size_t index = 0; index < points.size() && index < kinds.size(); ++index) {
        const std::array<float, 4> world = transform(points[index], matrix);
        const std::array<float, 3> start{{world[0], world[1], world[2]}};
        const std::array<float, 3> end{{world[0], world[1] - scanLength, world[2]}};
        std::uint32_t kind = 0;
        kinds[index] = collide(start, end, kind) ? kind : 0;
    }
}

}
