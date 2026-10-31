// Integration glue: the battle-facing parameter law (BattleRuntime's loadout caches and the local equipment ledger).
#if !defined(WOT_NATIVE_BATTLE_HOST_BUILD)
#include "pch.hpp"
#endif
#include "stock_native_battle_subsystems.hpp"
#include "stock_native_battle_startup_needs.hpp"
#include "stock_native_battle_visibility_core.hpp"
#include "stock_native_client_weapon_runtime.hpp"
#include <cmath>

namespace native_battle {
namespace {
Params& state(Battle& b) {
    if (!b.params) b.params = std::make_unique<Params>();
    return *b.params;
}
p::Ref tupleOf(const Object& value) {
    if (!value || value.get() == Py_None) return p::Ref(PyTuple_New(0));
    return p::Ref(PySequence_Tuple(value.get()));
}
// The garage's mounted equipment plus battle boosters, without the trigger-only Removed RPM Limiter.
p::Ref factorEquipments(const GarageLoadout& garage) {
    auto equipments = tupleOf(garage.equipments), boosters = tupleOf(garage.battle_boosters);
    p::Ref all(PySequence_Concat(equipments.get(), boosters.get()));
    p::Ref kept(PyList_New(0));
    for (Py_ssize_t i = 0; i < PyTuple_GET_SIZE(all.get()); ++i) {
        PyObject* equipment = PyTuple_GET_ITEM(all.get(), i);
        p::Ref single(PyTuple_Pack(1, equipment));
        bool limiter = false;
        for (const auto& name : params::equipmentNames(single.get()))
            if (name.find("removedrpmlimiter") != std::string::npos) { limiter = true; break; }
        if (!limiter) p::append(kept.get(), p::retain(equipment));
    }
    return p::Ref(PyList_AsTuple(kept.get()));
}
const params::Factors* localFactorValues(Battle& b, PyObject* descriptor) {
    auto& s = state(b);
    if (!s.local_factors_resolved) {
        const auto& garage = startup::garage(b);
        auto equipments = factorEquipments(garage);
        PyObject* crew = garage.crew && p::flag(garage.crew.get()) ? garage.crew.get() : nullptr;
        auto factors = params::attributeFactors(descriptor, crew, equipments.get());
        if (factors && factors->values.resolved().truth()) s.local_factors = std::move(factors);
        s.local_factors_resolved = true;
    }
    return s.local_factors ? &*s.local_factors : nullptr;
}
std::int64_t intOrZero(const Value& v) {
    const auto& r = v.resolved();
    if (!r.truth()) return 0;
    return r.kind == Value::Kind::Integer ? r.integer : std::int64_t(r.numeric(0.));
}
// BattleRuntime._equipment_echo: (quantity, stage, timeRemaining) for Avatar.updateVehicleAmmo.
std::array<std::int64_t, 3> equipmentEcho(Battle& b, const params::EquipmentState& e, double now) {
    auto stages = py::attr(b.engine.constants.get(), "EQUIPMENT_STAGES");
    auto stage = [&](const char* name) { return p::wide(py::attr(stages.get(), name).get()); };
    if (e.uses_left == 0) return {0, stage("EXHAUSTED"), 0};
    // #1513's trigger stays READY for native availability; a negative time denotes indefinite activity.
    if (e.contract.kind == "rpm_limiter" && e.active) return {1, stage("READY"), -1};
    const double remaining = e.ready_at - now;
    if (remaining > 0.) return {1, stage("COOLDOWN"), std::int64_t(std::ceil(remaining))};
    return {1, stage("READY"), 0};
}
// BattleRuntime._present_equipments outside the live gun runtime: publish only item transitions.
bool presentEquipments(Battle& b, double now) {
    auto& s = state(b);
    if (b.replay && !b.gun->replay_publishing) return false;
    if (!b.startup || !b.startup->client_ready_received || !s.equipment) return false;
    std::map<std::int64_t, std::array<int, 3>> previous(s.equipment_signature.begin(), s.equipment_signature.end());
    std::vector<std::pair<std::int64_t, std::array<int, 3>>> signature;
    bool changed = false;
    auto vehicle_id = py::attr(b.engine.server.get(), "vehicle_id");
    for (const auto& e : *s.equipment) {
        const auto echo = equipmentEcho(b, e, now);
        const std::array<int, 3> key{int(echo[0]), int(echo[1]), echo[2] < 0 ? 1 : 0};
        signature.emplace_back(e.contract.compact_descr, key);
        const auto found = previous.find(e.contract.compact_descr);
        if (found != previous.end() && found->second == key) continue;
        py::call(b.engine.avatar.get(), "updateVehicleAmmo", vehicle_id.get(), p::integer(e.contract.compact_descr).get(),
                 p::integer(echo[0]).get(), p::integer(echo[1]).get(), p::integer(echo[2]).get());
        changed = true;
    }
    s.equipment_signature = std::move(signature);
    return changed;
}
}

namespace params {
Value canonical(Battle&, const Value& raw) {
    auto snapshot = canonicalEffectiveParams(raw);
    return snapshot ? std::move(*snapshot) : Value();
}

std::int64_t livingSkillCount(Battle&, const Value& snapshot, const char* skill, PyObject* crew_critical) {
    return livingSkillCount(snapshot, skill, crew_critical ? cs::read(crew_critical) : Value());
}

Value acceptedLocalEffectiveParams(Battle& b) {
    if (b.worker || !b.startup) return Value();
    return b.startup->effective_params;
}

p::Ref localFactors(Battle& b, PyObject* descriptor) {
    const auto* factors = localFactorValues(b, descriptor);
    return factors ? p::Ref(cs::write(factors->values)) : p::retain(Py_None);
}

p::Ref spottingProfile(Battle& b, PyObject* descriptor, bool local) {
    auto& s = state(b);
    if (local && b.replay) {
        const auto accepted = acceptedLocalEffectiveParams(b);
        if (accepted.resolved().kind != Value::Kind::Null) return p::Ref(cs::write(accepted.get("spotting")));
    }
    if (local) {
        if (!s.local_spotting) {
            const auto& garage = startup::garage(b);
            PyObject* crew = garage.crew && p::flag(garage.crew.get()) ? garage.crew.get() : Py_None;
            PyObject* equipments = garage.equipments ? garage.equipments.get() : Py_None;
            std::optional<CrewSkills> skills;
            if (crew != Py_None) skills = crewSkillNames(crew);
            const double level = crewLevelIncrease(descriptor, equipments, skills ? &*skills : nullptr);
            s.local_spotting = spottingProfile(descriptor, crew, level, localFactorValues(b, descriptor)).toValue();
        }
        return p::Ref(cs::write(*s.local_spotting));
    }
    // Every non-local vehicle carries the default crew; its profile depends only on type and devices.
    auto name = py::optional(descriptor, "name");
    std::pair<std::string, std::vector<std::string>> key{name.get() == Py_None ? std::string() : p::string(name.get()), deviceNames(descriptor)};
    auto found = s.remote_spotting.find(key);
    if (found == s.remote_spotting.end()) {
        const double level = crewLevelIncrease(descriptor);
        const auto factors = attributeFactors(descriptor);
        found = s.remote_spotting.emplace(key, spottingProfile(descriptor, Py_None, level, factors ? &*factors : nullptr).toValue()).first;
    }
    return p::Ref(cs::write(found->second));
}

// BattleRuntime._restore_local_equipment_snapshot_cold: the local replica from one complete canonical ledger.
bool restoreEquipmentCold(Battle& b, const Value& snapshot, bool present) {
    const auto& message = snapshot.resolved();
    if (message.kind != Value::Kind::Object || b.local_player <= 0 || !b.gun) return false;
    const Value* local = nullptr;
    for (const auto& row : message.get("players").resolved().array) {
        const auto& r = row.resolved();
        if (r.kind == Value::Kind::Object && intOrZero(r.get("id")) == b.local_player) { local = &r; break; }
    }
    if (!local || !local->object.count("equipment_states")) return false;
    auto& s = state(b);
    const std::int64_t previous_revision = b.gun->equipment_revision;
    const double now = b.clock();
    std::int64_t revision = 0;
    std::vector<EquipmentState> states;
    try {
        const auto& raw = local->get("equipment_revision").resolved();
        if (raw.kind != Value::Kind::Integer && raw.kind != Value::Kind::Number) throw Invalid("player equipment revision is invalid");
        revision = std::int64_t(raw.numeric());
        if (revision < 0 || revision < previous_revision) throw Invalid("player equipment revision regressed");
        states = restoreEquipmentStates(local->get("equipment_states"), nullptr, now);
    } catch (const Invalid&) {
        throw p::Error("canonical player equipment snapshot is invalid");
    }
    bool repairkit_activated = false;
    if (revision > previous_revision && previous_revision >= 0 && s.equipment) {
        for (const auto& current : states) {
            if (current.contract.kind != "repairkit") continue;
            for (const auto& previous : *s.equipment) {
                if (previous.contract.id != current.contract.id || previous.contract.compact_descr != current.contract.compact_descr) continue;
                const bool spent_charge = previous.uses_left >= 0 && current.uses_left < previous.uses_left;
                const bool entered_cooldown = previous.ready(now) && !current.ready(now);
                repairkit_activated = repairkit_activated || spent_charge || entered_cooldown;
                break;
            }
            if (repairkit_activated) break;
        }
    }
    s.equipment = std::move(states);
    b.gun->equipment_revision = int(revision);
    const auto& result = local->get("equipment_intent_result");
    const std::int64_t sequence = intOrZero(result.get("intent_seq"));
    if (sequence > s.equipment_result_logged_seq && !result.get("accepted").truth()) {
        const auto& reason = result.get("reason").resolved();
        log("equipment rejected seq=" + std::to_string(sequence) + " reason=" + (reason.kind == Value::Kind::Text ? reason.text : std::string("unknown")));
    }
    s.equipment_result_logged_seq = std::max(sequence, s.equipment_result_logged_seq);
    if (repairkit_activated && b.critical) {
        // The server commits the kit's repair and this ledger edge together; stop suppressing that critical snapshot.
        b.critical->report = Value();
        b.critical->owned = false;
    }
    const bool live_gun = b.running() && !b.replay && !b.worker && b.gun->gun;
    if (live_gun) {
        // The live gun runtime owns the HUD equipment edges; hand it the restored rows.
        std::vector<stock_native_client_weapon_runtime::Equipment> rows;
        for (const auto& e : *s.equipment) {
            stock_native_client_weapon_runtime::Equipment row;
            row.compact = e.contract.compact_descr;
            row.uses_left = int(e.uses_left);
            row.active = e.active;
            row.ready_at = e.ready_at;
            row.rpm_limiter = e.contract.kind == "rpm_limiter";
            row.fuel = e.contract.kind == "fuel";
            row.repairkit = e.contract.kind == "repairkit";
            row.repair_bonus = e.contract.bonus_value;
            row.engine_power_factor = e.contract.engine_power_factor;
            rows.push_back(row);
        }
        stock_native_client_weapon_runtime::equipmentIngress(b, std::move(rows));
    }
    if (present && !b.worker && b.engine.avatar && b.engine.server) {
        if (live_gun) gun::publishKind(b, "equipment", now);
        else presentEquipments(b, now);
    }
    return true;
}
}
}
