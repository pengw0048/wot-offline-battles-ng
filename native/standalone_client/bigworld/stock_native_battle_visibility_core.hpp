#pragma once
// Private state shared by the visibility sources.
#include "stock_native_battle_visibility.hpp"
#include "stock_native_client_drive_python.hpp"
#include "stock_native_client_environment.hpp"
#include "stock_native_client_query.hpp"
#include "stock_native_client_targeting.hpp"

namespace native_battle {
// Calls into other subsystems.
namespace local {
// The visible native scene: world registry, pipeline and destructible backend.
stock_native_client_drive::SceneAccess scene(Battle& b);
// Refreshes the native critical status before vision factors are read.
void synchronizeStatus(Battle& b, double now);
}
namespace world {
// Current fallen/standing foliage for camouflage, or null without foliage.
const native_visibility::FoliageSnapshot* foliageSnapshot(const Battle& b);
}
namespace projectiles {
// Server time in milliseconds at local clock `now`, once the server anchor exists.
std::optional<double> serverTimeMs(const Battle& b, double now);
}
namespace params {
// The spotting inputs loadout.spotting_profile returns for one descriptor (local: the player's crew, devices and factors;
// replay: the accepted effective profile).
p::Ref spottingProfile(Battle& b, PyObject* descriptor, bool local);
}
namespace gun {
// Living crew members holding a skill (the pinned _local_skill_count).
int livingSkillCount(Battle& b, const std::string& skill);
}

struct VisibilityCore {
    Object query;               // offline.client.query capsule
    std::uint64_t epoch = 0;
    // Spotting.
    stock_native_client_environment::Spotting spotting;
    struct Installed {
        std::shared_ptr<Actor> actor;
        Object descriptor;
        std::uint64_t revision = 0;
        Value effective;
    };
    std::map<std::string, Installed> installed;
    std::map<std::string, stock_native_client_environment::Profile> profiles;
    bool spg = false;
    std::string strategic, arty;
    std::map<std::string, std::string> sight_failures;
    // Outline, Expert and target lock.
    stock_native_client_targeting::Expert expert{4.};
    stock_native_client_targeting::OutlineClock outline_clock{.05};
    Object outlined_vehicle, outlined_visual, outlined_model;
    bool clearing = false, expert_disabled = false;
    std::uint64_t outline_revision = 0;
    struct Extras {
        std::shared_ptr<Actor> actor;
        std::uint64_t revision = 0;
        Object descriptor;
        std::map<std::string, int> values;
    };
    std::map<std::int64_t, Extras> extras;
    std::map<std::string, std::string> query_failures;
    std::shared_ptr<void> mouse;    // the round's MouseTargetingMatrix
    // Sixth Sense.
    double observed_until = 0.;
    Object sixth_callback;
    std::map<std::string, int> sixth_counts;
    // Map space visibility.
    struct Space { std::int64_t id = 0; std::uint64_t selected = 0, client = 0, server = 0; };
    std::optional<Space> space;
    double next_space_check = 0.;
    bool space_warning = false;
};

namespace visibility {
// The battle still owns this visibility core (no stock edge retired it meanwhile).
bool current(const Battle& b, const VisibilityCore* core);
VisibilityCore* core(Battle& b);
std::int64_t localTeam(const Battle& b);
}
}

namespace stock_native_client_query {
PyObject* createFor(native_battle::Battle& b);
std::vector<OutlineFailure> ingestFor(PyObject* capsule, native_battle::Battle& b);
}
