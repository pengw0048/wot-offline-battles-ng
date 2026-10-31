#pragma once
// Spotting, team observation, target outline, Expert, radio, Sixth Sense and map space visibility.
#include "stock_native_battle.hpp"
#include <array>

namespace native_battle {
struct VisibilityCore;

// Round inputs the startup subsystem supplies once the local loadout is known.
struct LocalVisibilityInputs {
    Object crew;                // garage crew rows, or None
    Object equipments;          // garage equipment rows
    Object factors;             // the player's #1513 attribute factors, or None
    Object accepted_spotting;   // replay: the accepted effective spotting profile, or None
    Object arena_type;          // ArenaType for the gameplay visibility mask
    bool sixth_sense = false;   // crew owns commander_sixthsense
    bool expert = false;        // crew owns commander_expert
    double sixth_sense_delay = 3.;
};

struct Visibility {
    Visibility();
    ~Visibility();
    LocalVisibilityInputs local;
    std::optional<std::int64_t> outlined_engine_id;
    std::optional<std::int64_t> spectated_engine_id;   // postmortem presentation origin
    bool outline_blocked = false, expert_visibility_enabled = false, vision_feed_failed = false;
    std::unique_ptr<VisibilityCore> core;
};

namespace visibility {
// Round lifetime. bind runs at the running edge on the visible client, after the vehicle table and native scene exist.
void configureLocal(Battle& b, LocalVisibilityInputs inputs);
void bind(Battle& b);
// Installs changed vehicle resources and spotting profiles; call after replica/query ingress.
void ingest(Battle& b);
void retire(Battle& b);

// Per frame on the visible client; hud_only before the battle goes live.
void spotting(Battle& b, double now, bool hud_only);
void outline(Battle& b, double now);
void expert(Battle& b, double now);
void validateTargetLock(Battle& b);

// The LAN bot_observation message: team radio memory and the Sixth Sense edge.
bool observation(Battle& b, const Value& message);

// Stock edges.
bool monitorExpert(Battle& b, PyObject* vehicle_id, const std::string& reason = "target_clear");
bool enableExpertVisibility(Battle& b);
void disableExpert(Battle& b);
void disableOutline(Battle& b);
// Drops the outline when its vehicle leaves presentation; false when a stock edge re-entered.
bool clearOutline(Battle& b);
bool releaseTargetLock(Battle& b, std::int64_t engine_id);
// Postmortem: the vehicle whose position is the presentation origin, or none for the player's own.
void spectate(Battle& b, std::optional<std::int64_t> engine_id);

// Map space visibility: latch the gameplay bit before mapping, apply it once the space exists, keep it applied.
std::uint32_t prepareMappingVisibility(Battle& b, std::int64_t space_id);
void configureSpaceVisibility(Battle& b, std::optional<std::int64_t> space_id = {});
void maintainSpaceVisibility(Battle& b, double now);
void disableSpaceVisibility(Battle& b);

// Spotting inputs for diagnostics and other owners.
p::Ref spottingProfile(Battle& b, PyObject* descriptor, bool local);
double visionRadius(Battle& b, PyObject* descriptor, const Actor* vehicle, double still_seconds, bool local);
std::array<double, 2> baseInvisibility(PyObject* descriptor, PyObject* profile, PyObject* camouflage_id);
double shotInvisibilityFactor(PyObject* descriptor);

// The cursor/projectile query owner (an offline.client.query capsule), or null outside a visible round.
PyObject* queryOwner(const Battle& b);
}
}
