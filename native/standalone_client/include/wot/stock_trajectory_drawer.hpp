#pragma once

#include "wot/gun_ballistics.hpp"
#include <array>
#include <cstdint>
#include <functional>

namespace wot {
namespace stock_trajectory {

// #1513 TrajectoryDrawer: 006f7130 computes collision fractions, 006f6d90
// interpolates them along the camera's aim ray. These are three coloured
// straight segments, not a polyline through the projectile's world positions.
struct Fractions {
    float first = 0.f;
    float second = 0.f;
    float firstHeight = 0.f;
    float secondHeight = 0.f;
};
struct Vertex {
    Vector3 position;
    std::uint32_t colour;
};
using SegmentQuery = std::function<bool(Vector3, Vector3, Vector3&)>;
using BoundaryQuery = std::function<bool(Vector3, Vector3, Vector3&)>;

std::uint32_t colour(const std::array<float, 4>& rgba);
// 006f6700: leave an already-outside segment alone; stop at the nearest box
// face only when it starts inside and ends outside.
bool leave_bounds(Vector3 minimum, Vector3 maximum, Vector3 start, Vector3 end, Vector3& hit);
Fractions calculate(Vector3 target, Vector3 start, Vector3 velocity, Vector3 gravity,
    float maxDistance, const SegmentQuery& scene, const BoundaryQuery& bounds);

struct SimulatedHit {
    float time;
    Vector3 point;
    Vector3 velocity;
};
// f5db50 -> f5bf40: first camera-space surface over five seconds.
// The scene callback implements the caller's skip/required collision masks.
bool simulate(Vector3 start, Vector3 velocity, Vector3 gravity, float step,
    float epsilon, const SegmentQuery& scene, SimulatedHit& hit);

class State {
public:
    bool configured = false;
    bool visible = true;
    bool strategic = true;
    float maxDistance = 0.f;
    Vector3 gravity{0.f, 0.f, 0.f};
    std::array<float, 2> aim{{0.f, 0.f}};
    Fractions previous, current;
    Vector3 previousStart{0.f, 0.f, 0.f}, currentStart{0.f, 0.f, 0.f};
    float elapsed = 0.f, rate = 1.f;
    std::array<Vertex, 6> vertices{};

    State();
    void set_colours(const std::array<float, 4>& good, const std::array<float, 4>& blocked,
        const std::array<float, 4>& missing);
    void update(Vector3 target, Vector3 start, Vector3 velocity, float interval,
        const SegmentQuery& scene, const BoundaryQuery& bounds);
    // Camera ray is camera-origin + 10000 * transformed near-plane direction.
    // Its scene hit is required, as in 006f6ff3. A miss preserves the old mesh.
    void advance(float dt, const SegmentQuery& cameraRay, Vector3 rayStart, Vector3 rayEnd);
};

} // namespace stock_trajectory
} // namespace wot
