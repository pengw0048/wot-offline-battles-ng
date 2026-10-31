#pragma once
// Cross-subsystem calls the vehicle and event code makes before their owners' headers declare them.
// Integration moves each declaration into its owner's header and deletes this file.
#include "stock_native_battle.hpp"

namespace native_battle {
// One render callback's stage timing for the frame diagnostics owner.
struct FrameTiming {
    double start = 0., raw_dt = 0., dt = 0.;
    bool transitioned = false;
    std::size_t bot_count = 0;
    std::vector<std::pair<const char*, double>> stages;
};
void tick(Battle& b);
void scheduleFrame(Battle& b);
namespace local {
void fallbackPostmortemViewpoint(Battle& b, std::int64_t excluded_engine_id);
void siegeAcknowledged(Battle& b, std::int64_t siege, std::int64_t left_ms, const Value& input_seq, bool switching);
void siegeReported(Battle& b, const char* stage, std::int64_t siege, std::int64_t left_ms, const Value& input_seq);
void damageReported(Battle& b);
}
namespace gun {
void descriptorChanged(Battle& b, PyObject* entity);
void stunChanged(Battle& b, PyObject* entity);
}
namespace projectiles {
// The projectile collision pose of a vehicle sample (the pinned _projectile_boundary_pose).
Value boundaryPose(Battle& b, const Point& position, const Value& state);
std::optional<double> estimatedServerTimeMs(Battle& b, double now);
bool inLineage(Battle& b, const Value& projectile_id);
void rememberLineage(Battle& b, const Value& shot_event);
bool missingAttackerAllowed(Battle& b, const Value& event);
std::int64_t maxDamageSticker(Battle& b);
std::optional<double> presentationDue(Battle& b, const Value& terminal_event);
void setEpoch(Battle& b, const Value& epoch, double now);
void observeMessage(Battle& b, const Value& message);
void ricochet(Battle& b, const Value& event);
void terminal(Battle& b, const Value& event);
}
namespace startup {
double serverClock(Battle& b);
void ensureRecording(Battle& b);
void presentReplayLocal(Battle& b, double dt);
void frameDiagnostics(Battle& b, const FrameTiming& timing);
}
namespace remote {
void siegePose(Battle& b, std::int64_t engine_id);
void setInterpolateMotion(Battle& b, std::int64_t engine_id, bool interpolate);
bool nativeEntities(const Battle& b);
}
namespace world {
void refreshDetachedWreck(Battle& b, PyObject* entity);
void vehicleChanged(Battle& b, Actor& vehicle);
bool destructibleEvent(Battle& b, const Value& event);
void observeDisabled(Battle& b, const Value& message);
}
namespace visibility {
void maintainSpace(Battle& b, double now);
void vehicleChanged(Battle& b, Actor& vehicle);
double shotCamouflageSeconds();
}
namespace critical {
void prewarmLayout(Battle& b);
std::optional<bool> reconcile(Battle& b, Actor& vehicle, const Value& source, Value* canonical);
}
namespace bots {
void frameStarted(Battle& b, double rule_dt, double raw_dt);
void playerEnvironment(Battle& b, double dt, double now);
void prewarm(Battle& b, double now);
void projectileEvent(Battle& b, const Value& event);
void reconcileAuthority(Battle& b, const Value& player_id);
}
}
