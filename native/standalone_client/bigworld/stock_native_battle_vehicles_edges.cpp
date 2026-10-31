// Per-vehicle presentation edges: health and death, critical, stun, Siege, statistics, HUD components, spotting, pose.
#if !defined(WOT_NATIVE_BATTLE_HOST_BUILD)
#include "pch.hpp"
#endif
#include "stock_native_battle_subsystems.hpp"
#include "stock_native_battle_vehicles_internal.hpp"
#include "stock_native_battle_pending.hpp"
#include <algorithm>
#include <cmath>

namespace native_battle {
namespace vehicles {
namespace {
const Value& field(const Value& v, const char* name) { return v.resolved().get(name); }
bool has(const Value& v, const char* name) {
    const auto& r = v.resolved();
    return r.kind == Value::Kind::Object && r.object.count(name);
}
std::int64_t integer(const Value& v, std::int64_t fallback = 0) {
    const auto& r = v.resolved();
    if (r.kind == Value::Kind::Null) return fallback;
    if (r.kind == Value::Kind::Integer) return r.integer;
    return std::int64_t(r.numeric(double(fallback)));
}
bool exactInteger(const Value& v) { return v.resolved().kind == Value::Kind::Integer; }
Value& writable(Value& state) {
    if (state.reference) { Value copy = *state.reference; state = std::move(copy); }
    if (state.kind != Value::Kind::Object) state.kind = Value::Kind::Object;
    return state;
}
p::Ref id(const Actor& v) { return p::integer(v.engine_id); }
PyObject* boolean(bool v) { return v ? Py_True : Py_False; }
p::Ref provider(Battle& b) { return py::optional(b.engine.avatar.get(), "guiSessionProvider"); }
bool outlined(const Battle& b, const Actor& v) { return b.visibility && b.visibility->outlined_engine_id == v.engine_id; }
}

// ---- statistics, death attribution ----------------------------------------------------------------------------

// BattleRuntime._apply_vehicle_statistics: frags and team-killer state for the stock arena.
bool applyStatistics(Battle& b, Actor& v, const Value& state) {
    if (b.worker) return false;
    auto& d = v.presented;
    const std::int64_t frags = std::int64_t(field(state, "frags").numeric(0.));
    bool changed = false;
    if (!d.frags || *d.frags != frags) {
        startup::binding::arenaVehicleStatistics(b, v.engine_id, frags);
        d.frags = frags;
        changed = true;
    }
    if (field(state, "team_killer").truth() && !d.team_killer) {
        startup::binding::arenaTeamKiller(b, v.engine_id);
        d.team_killer = true;
        changed = true;
    }
    return changed;
}

// BattleRuntime._death_attacker_engine_id(state)
std::int64_t deathAttackerEngineId(Battle& b, const Value& state) {
    const auto& raw = field(state, "death_attacker_id");
    if (raw.resolved().kind != Value::Kind::Integer && raw.resolved().kind != Value::Kind::Number) return 0;
    const std::string kind = field(state, "death_attacker_kind").resolved().text;
    const Actor* attacker = b.vehicle(kind + ":" + std::to_string(std::int64_t(raw.numeric())));
    return attacker ? attacker->engine_id : 0;
}

// BattleRuntime._should_suppress_postmortem_killer: a local death by an unspotted enemy hides the killer.
bool suppressPostmortemKiller(Battle& b, const Actor& victim, const Value& state, std::int64_t attacker) {
    if (!victim.local || !has(state, "health")) return false;
    const bool dead = field(state, "health").numeric(0.) <= 0. || !field(state, "alive").truth(true);
    if (!dead || attacker <= 0) return false;
    const Actor* found = b.vehicleByEngine(attacker);
    if (!found) return false;
    const std::int64_t victim_team = integer(field(state, "team")), attacker_team = integer(field(found->presented.state, "team"));
    if (victim_team <= 0 || attacker_team <= 0 || victim_team == attacker_team) return false;
    return !found->presented.spot_visible;
}

// ---- fire effect and HUD components ---------------------------------------------------------------------------

// BattleRuntime._sync_fire_effect: the stock fire extra follows the burning state.
bool syncFireEffect(Battle& b, PyObject* entity, std::optional<bool> burning) {
    auto descriptor = py::optional(entity, "typeDescriptor");
    auto extras = py::optional(descriptor.get(), "extrasDict");
    if (extras.get() == Py_None) return false;
    auto extra = py::call(extras.get(), "get", p::text("fire").get());
    if (extra.get() == Py_None) return false;
    const bool on = burning ? *burning : p::flag(py::optional(entity, "is_on_fire").get());
    if (on == p::flag(py::call(extra.get(), "isRunningFor", entity).get())) return false;
    if (!on) { py::call(extra.get(), "stopFor", entity); return true; }
    auto appearance = py::optional(entity, "appearance");
    if (py::optional(appearance.get(), "compoundModel").get() == Py_None) return false;
    py::call(extra.get(), "startFor", entity);
    return true;
}

// BattleRuntime._stop_remote_visual: marker and minimap leave the HUD.
bool stopRemoteVisual(Battle& b, Actor& v) {
    if (outlined(b, v)) visibility::clearOutline(b);
    auto& d = v.presented;
    if (!d.world_marker_started && !d.minimap_started) return false;
    if (d.world_marker_started) startup::binding::stopVehicleVisual(b, v.engine_id, false);
    else startup::binding::stopVehicleMinimap(b, v.engine_id);
    d.world_marker_started = d.minimap_started = false;
    if (v.native_remote) {
        auto vehicle = remote::get(b, v.engine_id);
        if (vehicle.get() != Py_None) py::set(vehicle.get(), "_offlineNativeMarkerVisible", Py_False);
    }
    return true;
}

// BattleRuntime._present_vehicle_dead: the marker takes its dead style (immediately for an already-dead vehicle).
void presentDead(Battle& b, Actor& v, bool immediate) {
    if (v.local || !v.presented.remote) return;
    auto shared = py::optional(provider(b).get(), "shared");
    auto feedback = py::optional(shared.get(), "feedback");
    if (feedback.get() == Py_None) return;
    auto set_state = py::optional(feedback.get(), "setVehicleState");
    if (!py::callable(set_state)) throw p::Error("#1513 vehicle-marker death boundary is unavailable");
    auto event_ids = py::attr(b.engine.modules.get(), "feedback_event_id");
    py::invoke(set_state.get(), id(v).get(), py::attr(event_ids.get(), "VEHICLE_DEAD").get(), boolean(immediate));
}

// BattleRuntime._sync_remote_visual_components: the marker (world) and minimap signals, each started once.
bool syncVisualComponents(Battle& b, Actor& v, PyObject* vehicle, bool team_visible, bool world_visible) {
    auto& d = v.presented;
    bool world = d.world_marker_started, minimap = d.minimap_started;
    world_visible = world_visible && team_visible;
    if (!world_visible && outlined(b, v)) visibility::clearOutline(b);
    auto finish = [&] {
        d.world_marker_started = world;
        d.minimap_started = minimap;
        d.world_marker_visible = world;
        if (v.native_remote) py::set(vehicle, "_offlineNativeMarkerVisible", boolean(world));
    };
    auto dead = [&] { if (!alive(b, v, vehicle)) presentDead(b, v, true); };
    try {
        if (team_visible) {
            if (world_visible && !world && !minimap) {
                startup::binding::startVehicleVisual(b, v.engine_id, true);
                world = minimap = true;
                dead();
            } else {
                if (!minimap) { startup::binding::startVehicleMinimap(b, v.engine_id); minimap = true; }
                if (world_visible && !world) { startup::binding::startVehicleMarker(b, v.engine_id); world = true; dead(); }
                else if (!world_visible && world) { startup::binding::stopVehicleMarker(b, v.engine_id); world = false; }
            }
        } else if (world) {
            startup::binding::stopVehicleVisual(b, v.engine_id, false);
            world = minimap = false;
        } else if (minimap) {
            startup::binding::stopVehicleMinimap(b, v.engine_id);
            minimap = false;
        }
    } catch (...) {
        PyObject *type, *value, *trace;
        PyErr_Fetch(&type, &value, &trace);
        finish();
        PyErr_Restore(type, value, trace);
        throw;
    }
    finish();
    return world || minimap;
}

// BattleRuntime._set_record_spot_visibility: model draw gate and team-knowledge (marker) gate of one vehicle.
bool setSpotVisibility(Battle& b, Actor& v, bool visible, std::optional<bool> marker_raw) {
    auto& d = v.presented;
    const bool marker = marker_raw.value_or(visible);
    const bool changed = d.spot_visible != visible;
    d.spot_visible = visible;
    d.spot_marker_visible = marker;
    if (changed) visibility::vehicleChanged(b, v);
    if (!d.remote || !v.ready) return visible;
    auto vehicle = remote::get(b, v.engine_id);
    if (vehicle.get() == Py_None || py::attr(vehicle.get(), "model").get() == Py_None) throw p::Error("spotted remote vehicle has no model");
    py::set(vehicle.get(), "_spot_visible", boolean(visible));
    const bool is_alive = alive(b, v, vehicle.get());
    const bool draw = visible || !is_alive;
    const std::array<bool, 4> signature{visible, marker, draw, is_alive};
    if (d.spot_signature != signature) {
        if (v.native_remote) {
            py::set(vehicle.get(), "_offlineNativeDrawVisible", boolean(draw));
            remote::setDrawVisibility(b, vehicle.get(), draw);
            py::set(vehicle.get(), "targetCaps", p::list().get());
            d.track_pending = true;
            d.track_signature.reset();
            if (!draw) {
                auto stop = py::optional(vehicle.get(), "update_tracks");
                if (py::callable(stop)) {
                    auto mode = py::optional(vehicle.get(), "engineMode");
                    if (mode.get() == Py_None) replace(mode, p::Ref(Py_BuildValue("(ii)", 0, 0)));
                    d.track_pending = !b.feature("remote track animation", [&] { py::invoke(stop.get(), p::number(0.).get(), p::number(0.).get(), mode.get()); });
                }
            }
        } else {
            py::call(py::attr(vehicle.get(), "appearance").get(), "changeVisibility", boolean(draw));
        }
        syncFireEffect(b, vehicle.get(), draw ? std::optional<bool>() : std::optional<bool>(false));
        d.spot_signature = signature;
    }
    syncVisualComponents(b, v, vehicle.get(), marker, visible);
    if (marker && d.deferred_health) {
        auto present = py::optional(provider(b).get(), "setVehicleHealth");
        if (!py::callable(present)) throw p::Error("#1513 remote vehicle health presenter is unavailable");
        const auto& state = d.state;
        const std::int64_t health = std::max<std::int64_t>(0, integer(has(state, "display_health") ? field(state, "display_health") : field(state, "health")));
        py::invoke(present.get(), Py_False, id(v).get(), p::integer(health).get(), p::integer(0).get(), p::integer(0).get());
        d.deferred_health = false;
    }
    return visible;
}

// ---- stun and Siege ------------------------------------------------------------------------------------------

// BattleRuntime._apply_stun_state: the #1513 stunInfo absolute-time contract.
bool applyStun(Battle& b, Actor& v, const Value& state) {
    const auto& end_raw = field(state, "stun_end_server_time_ms");
    const std::int64_t end = end_raw.resolved().kind == Value::Kind::Null ? 0 : integer(end_raw);
    if (end_raw.resolved().kind != Value::Kind::Null && (!exactInteger(end_raw) || end < 0)) throw p::Error("LAN snapshot has an invalid stun end time");
    const std::string kind = field(state, "stun_attacker_kind").resolved().text;
    const std::int64_t attacker = integer(field(state, "stun_attacker_id"));
    const bool stunned = end > 0;
    if (stunned) {
        if ((kind != "player" && kind != "bot") || attacker <= 0) throw p::Error("LAN snapshot has an invalid stun attacker");
    } else if (!kind.empty() || attacker) {
        throw p::Error("cleared LAN stun retains an attacker");
    }
    std::string signature = std::to_string(end) + "|" + kind + "|" + std::to_string(attacker);
    const auto& factors = field(state, "stun_factors").resolved();
    if (factors.kind == Value::Kind::Object)
        for (const auto& item : factors.object) signature += "|" + item.first + "=" + std::to_string(item.second.numeric());
    auto& d = v.presented;
    if (!d.stun_signature && !stunned) { d.stun_signature = signature; return false; }
    if (d.stun_signature == signature) return false;
    double remaining = 0.;
    if (stunned) {
        const auto estimated = projectiles::estimatedServerTimeMs(b, b.clock());
        if (!estimated) throw p::Error("LAN stun has no server-time anchor");
        remaining = std::max(0., (double(end) - *estimated) / 1000.);
    }
    auto entity = b.entity(v.engine_id);
    if (entity.get() == Py_None) throw p::Error("stunned LAN vehicle is unavailable");
    const bool local_gun = v.local && !b.worker;
    if (local_gun) gun::edge(b);
    auto previous = py::optional(entity.get(), "stunInfo");
    if (previous.get() == Py_None) replace(previous, p::number(0.));
    const double server_now = startup::serverClock(b);
    py::set(entity.get(), "stunInfo", p::number(remaining > 0. ? server_now + remaining : 0.).get());
    auto copied = p::dict();
    if (remaining > 0. && factors.kind == Value::Kind::Object)
        for (const auto& item : factors.object) p::set(copied.get(), item.first.c_str(), p::number(item.second.numeric()));
    py::set(entity.get(), "_offlineStunFactors", copied.get());
    if (local_gun) gun::stunChanged(b, entity.get());
    if (!b.worker) {
        auto callback = py::optional(entity.get(), "set_stunInfo");
        auto left = p::number(remaining);
        if (py::callable(callback)) py::invoke(callback.get(), previous.get());
        else if (v.local) {
            auto view = py::attr(b.engine.modules.get(), "vehicle_view_state");
            py::call(provider(b).get(), "invalidateVehicleState", py::attr(view.get(), "STUN").get(), left.get());
        } else {
            auto feedback = py::attr(py::attr(provider(b).get(), "shared").get(), "feedback");
            auto invalidate = py::optional(feedback.get(), "invalidateStun");
            if (!py::callable(invalidate)) throw p::Error("#1513 remote stun feedback boundary is unavailable");
            py::invoke(invalidate.get(), id(v).get(), left.get());
        }
    }
    d.stun_signature = signature;
    return true;
}

// BattleRuntime._apply_siege_state: a server-owned Siege transition through #1513's callback.
bool applySiege(Battle& b, Actor& v, const Value& state) {
    auto states = py::attr(b.engine.constants.get(), "VEHICLE_SIEGE_STATE");
    const std::int64_t disabled = p::wide(py::attr(states.get(), "DISABLED").get()), on = p::wide(py::attr(states.get(), "SWITCHING_ON").get());
    const std::int64_t enabled = p::wide(py::attr(states.get(), "ENABLED").get()), off = p::wide(py::attr(states.get(), "SWITCHING_OFF").get());
    const auto& siege_raw = field(state, "siege_state");
    const auto& left_raw = field(state, "siege_time_left_ms");
    const std::int64_t siege = siege_raw.resolved().kind == Value::Kind::Null ? disabled : integer(siege_raw);
    const std::int64_t left = left_raw.resolved().kind == Value::Kind::Null ? 0 : integer(left_raw);
    if (siege_raw.resolved().kind != Value::Kind::Null && !exactInteger(siege_raw)) throw p::Error("LAN snapshot has an invalid siege state");
    if (siege != disabled && siege != on && siege != enabled && siege != off) throw p::Error("LAN snapshot has an unsupported siege state");
    if (left_raw.resolved().kind != Value::Kind::Null && (!exactInteger(left_raw) || left < 0)) throw p::Error("LAN snapshot has an invalid siege transition time");
    const bool switching = siege == on || siege == off;
    if ((switching && left <= 0) || (!switching && left != 0)) throw p::Error("LAN snapshot has an inconsistent siege transition");
    if (v.local) local::siegeAcknowledged(b, siege, left, field(state, "input_seq"), switching);
    auto& d = v.presented;
    if (d.siege_state == siege) return false;
    if (!d.siege_state && siege == disabled) { d.siege_state = disabled; return false; }
    auto entity = b.entity(v.engine_id);
    auto descriptor = py::optional(entity.get(), "typeDescriptor");
    if (descriptor.get() == Py_None) throw p::Error("Siege vehicle descriptor is unavailable");
    const bool has_siege = p::flag(py::optional(descriptor.get(), "hasSiegeMode").get());
    if (!has_siege && siege != disabled) throw p::Error("Vehicle without Siege mode received an active state");
    if (!has_siege) { d.siege_state = disabled; return false; }
    if (v.local) gun::edge(b);
    startup::binding::updateVehicleSiegeState(b, v.engine_id, siege, double(left) / 1000.);
    d.siege_state = siege;
    if (b.worker && v.native_remote) remote::updateSiegePose(b, v.engine_id);
    if (v.local) {
        local::siegeChanged(b, entity.get(), siege == enabled || siege == off);
        gun::descriptorChanged(b, entity.get());
        local::siegeReported(b, "state", siege, left, field(state, "input_seq"));
    }
    return true;
}

// ---- health and death ----------------------------------------------------------------------------------------

// BattleRuntime._apply_health: a durable health/crew transition and its #1513 presentation.
void applyHealth(Battle& b, Actor& v, Value& state, const HealthEdge& edge) {
    if (!has(state, "health")) return;
    auto& d = v.presented;
    const bool local = v.local;
    const bool requested = edge.suppress_combat && !local;
    const std::int64_t health = std::max<std::int64_t>(0, integer(field(state, "health")));
    std::int64_t reason = edge.reason ? std::max<std::int64_t>(0, *edge.reason) : std::max<std::int64_t>(0, integer(field(state, "death_reason")));
    const std::int64_t attack_reason = edge.attack_reason ? std::max<std::int64_t>(0, *edge.attack_reason) : reason;
    const std::int64_t display = std::max<std::int64_t>(0, has(state, "display_health") ? integer(field(state, "display_health")) : health);
    const bool crew_active = field(state, "alive").truth(health > 0) && health > 0;
    const bool dead = health <= 0 || !crew_active;
    const bool crew_knockout = health > 0 && !crew_active;
    const auto& critical_state = field(state, "critical");
    const bool ammo_rack_death = health <= 0 && field(critical_state, "ammo_rack_death").truth();
    if (ammo_rack_death && reason == 0) {
        if (d.ammo_death_cause == "fire") reason = 1;
        else if (d.ammo_death_cause == "world_collision") reason = 3;
    }
    std::int64_t native_health = dead && display > 0 ? display : health;
    auto special = [&](const char* name) { return p::wide(py::attr(py::attr(b.engine.constants.get(), "SPECIAL_VEHICLE_HEALTH").get(), name).get()); };
    if (ammo_rack_death) native_health = special("TURRET_DETACHED");
    const bool suppress = requested && !dead;
    const std::array<std::int64_t, 4> signature{health, display, crew_active ? 1 : 0, reason};
    const bool durable_changed = d.health != signature;
    const bool previous_dead = d.health && ((*d.health)[0] <= 0 || !(*d.health)[2]);
    auto attacker = p::integer(edge.attacker), attack = p::integer(attack_reason), reason_value = p::integer(reason);
    auto engine = id(v);
    if (previous_dead && dead) {
        if (ammo_rack_death) {
            auto entity = b.entity(v.engine_id);
            if (entity.get() == Py_None) return;
            auto previous_native = py::attr(entity.get(), "health");
            if (world::detachTurret(b, v, entity.get(), &state) && !b.worker) world::refreshDetachedWreck(b, entity.get());
            const std::int64_t bay = p::flag(py::optional(entity.get(), "isTurretMarkedForDetachment").get()) ? special("TURRET_DETACHED") : special("AMMO_BAY_DESTROYED");
            if (p::wide(previous_native.get()) != bay && !b.worker)
                py::call(provider(b).get(), "setVehicleHealth", boolean(local), engine.get(), p::integer(bay).get(), attacker.get(), attack.get());
        }
        d.health = signature;
        return;
    }
    if (!durable_changed && !edge.force) return;
    auto entity = b.entity(v.engine_id);
    if (entity.get() == Py_None) return;
    if (dead && !previous_dead && !local) {
        d.dead_marker_known = spotted(v);
        world::vehicleChanged(b, v);
    }
    d.health = signature;
    auto previous = py::optional(entity.get(), "health");
    if (previous.get() == Py_None && !PyObject_HasAttrString(entity.get(), "health")) replace(previous, p::integer(health));
    if (dead && !previous_dead && outlined(b, v)) visibility::clearOutline(b);
    if (dead && !previous_dead && !crew_knockout) {
        auto is = [&](const char* name, std::int64_t fallback) { return attackReason(b, name, fallback) == reason; };
        const char* cause = is("FIRE", 1) ? "fire" : is("RAM", 2) ? "ramming" : is("WORLD_COLLISION", 3) ? "world_collision"
                          : is("DROWNING", 5) ? "drowning" : is("OVERTURN", 7) ? "overturn" : "shot";
        const Value payload = critical::applyDeath(b, entity.get(), cause);
        if (payload.resolved().kind != Value::Kind::Null) {
            Value canonical = payload.resolved();
            writable(canonical).object["events"].kind = Value::Kind::Array;
            writable(state).object["critical"] = canonical;
            d.critical = canonical;
            d.state = state;
            if (!b.worker && !suppress) syncFireEffect(b, entity.get());
            if (local) critical::localDeath(b);
        }
    }
    p::Ref fn(p::retain(Py_None));
    auto callable = [&](PyObject* o, const char* name) { replace(fn, py::optional(o, name)); return py::callable(fn); };
    auto crewActive = p::boolean(crew_active);
    if (b.worker) {
        py::set(entity.get(), "health", p::integer(native_health).get());
        if (ammo_rack_death) world::detachTurret(b, v, entity.get(), &state);
        if (callable(entity.get(), "set_health")) py::invoke(fn.get(), previous.get());
        auto previous_crew = py::optional(entity.get(), "isCrewActive");
        if (previous_crew.get() == Py_None && !PyObject_HasAttrString(entity.get(), "isCrewActive")) replace(previous_crew, p::boolean(crew_active));
        py::set(entity.get(), "isCrewActive", crewActive.get());
        if (callable(entity.get(), "set_isCrewActive")) py::invoke(fn.get(), previous_crew.get());
        if (d.remote && dead && callable(entity.get(), "retain_wreck_model")) py::invoke(fn.get());
        return;
    }
    py::set(entity.get(), "health", p::integer(native_health).get());
    if (ammo_rack_death) world::detachTurret(b, v, entity.get(), &state);
    if (!suppress && callable(entity.get(), "onHealthChanged")) py::invoke(fn.get(), p::integer(native_health).get(), attacker.get(), attack.get());
    else if (!v.native_remote && callable(entity.get(), "set_health")) py::invoke(fn.get(), previous.get());
    auto previous_crew = py::optional(entity.get(), "isCrewActive");
    if (previous_crew.get() == Py_None && !PyObject_HasAttrString(entity.get(), "isCrewActive")) replace(previous_crew, p::boolean(crew_active));
    py::set(entity.get(), "isCrewActive", crewActive.get());
    if (p::flag(previous_crew.get()) != crew_active && (!suppress || !v.native_remote) && callable(entity.get(), "set_isCrewActive"))
        py::invoke(fn.get(), previous_crew.get());
    if (d.remote && !suppress) {
        auto present = py::optional(provider(b).get(), "setVehicleHealth");
        if (!py::callable(present)) throw p::Error("#1513 remote vehicle health presenter is unavailable");
        py::invoke(present.get(), Py_False, engine.get(), p::integer(native_health).get(), attacker.get(), attack.get());
        d.deferred_health = false;
    } else if (d.remote) {
        d.deferred_health = true;
    }
    auto alive_attr = py::optional(entity.get(), "isAlive");
    const bool entity_alive = py::callable(alive_attr) ? p::flag(py::invoke(alive_attr.get()).get()) : p::flag(alive_attr.get());
    if (!entity_alive) {
        if (outlined(b, v)) visibility::clearOutline(b);
        if (!suppress) {
            visibility::releaseTargetLock(b, v.engine_id);
            if (!v.native_remote) presentDead(b, v, false);
        }
    }
    if (local) {
        if (dead) local::died(b);
        py::call(b.engine.avatar.get(), "updateVehicleHealth", engine.get(), p::integer(ammo_rack_death ? native_health : display).get(),
                 reason_value.get(), crewActive.get(), Py_False);
    }
    if (!previous_dead && dead) {
        if (!suppress) {
            {
                startup::binding::arenaVehicleKilled(b, v.engine_id, p::wide(attacker.get()), p::wide(reason_value.get()));
                events::reportLocalKill(b, v, edge.attacker, reason);
                if (local && edge.suppress_postmortem_killer) {
                    auto clear = py::optional(b.engine.compatibility.get(), "clear_postmortem_killer");
                    if (!py::callable(clear)) throw p::Error("#1513 postmortem killer compatibility boundary is unavailable");
                    py::invoke(clear.get(), b.engine.avatar.get());
                }
            }
        }
        if (!local) feedback::fallbackPostmortemViewpoint(b, v.engine_id);
    }
    if (!previous_dead && dead && d.remote) {
        if (!b.feature("dead wreck replacement", [&] { remote::requestWreck(b, v.engine_id); }, false)) {}
        finalizeDeadWreck(b, v, entity.get());
    }
}

// BattleRuntime._finalize_dead_remote_wreck: expose one finalized corpse without re-entering spotting.
bool finalizeDeadWreck(Battle& b, Actor& v, PyObject* entity) {
    v.presented.deferred_health = false;
    auto vehicle = remote::get(b, v.engine_id);
    if (!p::flag(vehicle.get())) replace(vehicle, p::retain(entity));
    if (vehicle.get() == Py_None) return false;
    if (v.native_remote) {
        py::set(vehicle.get(), "_offlineNativeDrawVisible", Py_True);
        py::set(vehicle.get(), "targetCaps", p::list().get());
    }
    if (py::optional(vehicle.get(), "model").get() == Py_None) { v.presented.spot_signature.reset(); return false; }
    bool shown = false;
    if (!b.feature("dead wreck presentation", [&] { shown = setSpotVisibility(b, v, v.presented.spot_visible, v.presented.spot_marker_visible); }, false))
        v.presented.spot_signature.reset();
    return shown;
}

// ---- pose ----------------------------------------------------------------------------------------------------

// BattleRuntime._apply_record_pose: one remote pose sample through the matrix and aim owners.
void applyPose(Battle& b, Actor& v, const Pose& pose) {
    if (remote::owns(b, v) || v.local) return;
    auto& d = v.presented;
    const auto& state = d.state;
    const double now = b.clock();
    const std::int64_t siege = has(state, "siege_state") ? integer(field(state, "siege_state")) : 0;
    d.collision_pose = projectiles::boundaryPose(b, {pose[0], pose[1], pose[2]}, state);
    writable(d.collision_pose).object["siege_state"].kind = Value::Kind::Integer;
    writable(d.collision_pose).object["siege_state"].integer = siege;
    const bool is_alive = field(state, "alive").truth(true) && integer(field(state, "health"), 1) > 0;
    const std::array<double, 8> signature{pose[0], pose[1], pose[2], pose[3], pose[4], pose[5], pose[6], pose[7]};
    const bool pose_changed = d.pose_signature != signature || d.pose_generation != b.generation;
    auto engine = id(v);
    if (pose_changed) {
        d.pose_signature.reset();
        auto at = vector(b, {pose[0], pose[1], pose[2]});
        auto rot = rotation(pose[3], pose[4], pose[5]);
        startup::binding::setVehiclePose(b, v.engine_id, at.get(), rot.get(), Py_None, p::number(now).get());
        d.pose_signature = signature;
        d.pose_generation = b.generation;
        d.settled_signature.reset();
    } else {
        const bool intended = std::fabs(field(state, "speed").numeric()) > 0.05 || std::fabs(field(state, "movement_dir").numeric()) > .5 ||
                              std::fabs(field(state, "rotation_dir").numeric()) > .5;
        if (v.native_remote && !intended && d.settled_signature != signature) {
            auto vehicle = remote::get(b, v.engine_id);
            auto settle = py::optional(vehicle.get(), "settle_motion");
            if (py::callable(settle) && b.feature("remote motion presentation", [&] { py::invoke(settle.get(), p::number(now).get()); }))
                d.settled_signature = signature;
        }
    }
    const std::array<double, 5> aim{pose[3], pose[4], pose[6], pose[7], double(d.siege_state.value_or(-1))};
    if (pose_changed || d.aim_signature != aim) {
        d.aim_signature.reset();
        startup::binding::updateVehicleAim(b, v.engine_id, pose[3], pose[6], pose[7]);
        d.aim_signature = aim;
    }
    if (v.kind == "bot" || v.kind == "player") {
        const double turn = bots::rememberTrackTurn(v, pose[3], now);
        const std::array<double, 3> track{field(state, "speed").numeric(), is_alive ? 1. : 0., turn};
        if (pose_changed || d.track_pending || d.track_signature != track) {
            bool updated = false;
            b.feature("remote track animation", [&] { updated = bots::tracksWithTurn(b, v, turn, now); });
            d.track_pending = !updated;
            if (updated) d.track_signature = track;
        }
    }
}
}
}
