// Integration glue: small adapters where one subsystem's call and another's definition were written apart.
#if !defined(WOT_NATIVE_BATTLE_HOST_BUILD)
#include "pch.hpp"
#endif
#include "stock_native_battle_subsystems.hpp"
#include <cmath>
#include "stock_native_battle_startup_needs.hpp"
#include "stock_native_battle_vehicles_internal.hpp"

namespace native_battle {
// The local vehicle state is created by startup::start; its owners attach on the first drive slice.
void local::start(Battle& b) {
    if (!b.local) b.local = std::make_unique<LocalVehicle>();
}
// The local gun's reload sound and timeline end with the round.
void gun::retire(Battle& b) {
    if (!b.gun) return;
    gun::retireSound(b);
    b.gun.reset();
}

namespace startup { void teardownAfterFailure(Battle& b); }
// The core's failure path ends the round through startup's teardown.
void teardownAfterFailure(Battle& b) { startup::teardownAfterFailure(b); }

// Vehicle creation reads startup's descriptors, placement and outfit season.
p::Ref vehicles::descriptorFor(Battle& b, const std::string& name) { return startup::descriptorFor(b, name); }
p::Ref vehicles::playerDescriptor(Battle& b, const Value& state) { return startup::playerDescriptor(b, state); }
std::pair<Point, double> vehicles::worldPose(Battle& b, const Value& state) { return startup::worldPose(b, state); }
std::optional<std::int64_t> vehicles::outfitSeason(Battle& b) { return startup::outfitSeason(b); }

// BattleRuntime._bot_authority_is_local
bool vehicles::botAuthorityIsLocal(Battle& b, const Value& player_id) {
    const auto& id = player_id.resolved();
    if (id.kind == Value::Kind::Null || !b.local_player) return false;
    if (id.kind == Value::Kind::Text) {
        try { return std::stoll(id.text) == b.local_player; }
        catch (...) { throw p::Error("bot authority identity is invalid"); }
    }
    if (!std::isfinite(id.numeric(NAN))) throw p::Error("bot authority identity is invalid");
    return std::int64_t(id.numeric()) == b.local_player;
}

// ai.planner.bot_initially_visible(team, own team, spotting enabled): an enemy model starts hidden.
bool vehicles::botInitiallyVisible(Battle& b, std::int64_t team) {
    auto own = py::optional(b.engine.client.get(), "team");
    if (own.get() == Py_None) return team == 1;
    return team == p::wide(own.get());
}

namespace vehicles {
bool showDamageInfo(Battle& b, std::int64_t engine_id, int damage_index, int extra_index, std::int64_t attacker);
}
// The local critical owner publishes with int indices.
bool vehicles::showDamageInfo(Battle& b, std::int64_t engine_id, int damage_index, int extra_index, std::int64_t attacker) {
    return showDamageInfo(b, engine_id, std::int64_t(damage_index), std::int64_t(extra_index), attacker);
}
}
