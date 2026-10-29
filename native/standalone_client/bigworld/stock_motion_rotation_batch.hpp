#pragma once

#include "world_stage.h"

#include <array>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <vector>

namespace stock_motion_rotation {
using Point = std::array<double, 3>;
struct Bounds { Point minimum, maximum; };
struct Contact { Point point, normal; };

// The caller has completed canonical entry guards and supplied valid body
// bounds. Missing-bbox compatibility and replacement early exits before that
// preparation remain caller policy, outside this experimental primitive.
// A complete canonical rotation request. Geometry and pose calculations stay
// double; only WorldSnapshot.position and returned contacts cross Vector3's
// float32 boundary. No descriptor or Python object enters the native loop.
struct Snapshot {
    Point position;
    Bounds body;
    double start_yaw, end_yaw, pitch, roll, pivot_offset;
    std::array<double, 2> translation;
    double max_angle_step, drivable_gradient;
    std::uint32_t max_steps;
    bool include_static, replacement_reader_available, replacement_active;
};

// Preserve the actual hard-contact record rather than reconstructing it from
// another ray. The future world adapter fills these directly at WORLD_TRACE.
struct Trace {
    // This is an actual retained hit, including one written before a failed
    // dispatch. Query failure itself never manufactures a physical contact.
    bool present = false;
    WorldSnapshot snapshot{};
    WorldRequest request{};
    WorldResponse response{};
};
struct WorldResult {
    int status = 0; // wot_world_run: 0 clear, 1 hard, 2 kinetic; <0 local failure.
    Trace trace;
};
class DeparturePolicy {
public:
    virtual ~DeparturePolicy() = default;
    virtual bool accepts(const Contact &) = 0;
};
class WorldEngine {
public:
    virtual ~WorldEngine() = default;
    // Run the existing world_stage law synchronously, retaining its ordered
    // rays/filter/recast/trace. A supplied policy is called only in C++ for
    // WORLD_HORIZONTAL with flags&1. It may synchronously run the two lazy
    // previous-footprint queries through this same owner. No Python hot call
    // or physical destruction is permitted here (commit_enabled is false).
    virtual WorldResult run(const WorldSnapshot &, DeparturePolicy *) = 0;
};
// A typed batch-owned frontier may retain geometry and ledger views across
// worlds, but each reentrant world gets its own hit/filter/trace frame.
class WorldFrame {
public:
    virtual ~WorldFrame() = default;
    virtual int dispatch(std::uint32_t, const WorldRequest *, std::uint32_t,
        WorldResponse *) = 0;
    virtual Trace trace() const = 0;
};
class WorldFrameFactory {
public:
    virtual ~WorldFrameFactory() = default;
    virtual std::unique_ptr<WorldFrame> begin(const WorldSnapshot &,
        DeparturePolicy *) = 0;
};
class WorldStageEngine final: public WorldEngine {
    WorldFrameFactory & frames_;
public:
    explicit WorldStageEngine(WorldFrameFactory & frames): frames_(frames) {}
    WorldResult run(const WorldSnapshot &, DeparturePolicy *) override;
};
struct Result {
    bool clear = true, skipped = false;
    std::uint32_t slices = 0, world_calls = 0, previous_world_calls = 0;
    std::uint32_t departure_tests = 0, query_failures = 0;
    bool blocking_failure = false; // Query failure, distinct from physical hard/kinetic.
    Trace blocking_trace;
};
struct DepartureProof { bool improves, inside; };

// The current fixed-release laws, exposed as typed primitives. The older
// extension only exposes these numerical bodies inside Python bindings.
void validateSnapshot(const Snapshot &);
double angleDelta(double current, double target);
Bounds intervalEnvelope(const Bounds &, double half_angle, double pivot);
DepartureProof departureProof(const Snapshot &, const Contact &);
Result run(const Snapshot &, WorldEngine &);
}
