#pragma once
// The vehicle parameter law: loadout, equipment and effective parameters (loadout.py, equipment_mechanics.py, effective_params.py).
// Stock #1513 descriptors, crews and artefacts are Python handles; every result here is C++ data.
#include "stock_native_battle.hpp"
#include <algorithm>
#include <array>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace native_battle {
namespace params {
// A rejected wire value or descriptor (the Python ValueError of these modules).
struct Invalid : std::runtime_error { using std::runtime_error::runtime_error; };

// ---- loadout.py constants ---------------------------------------------------------------------------------
constexpr double CREW_FACTOR_BASE = 0.57, CREW_FACTOR_SLOPE = 0.0043, BASE_CREW_LEVEL = 100.0, COMMANDER_SHARE = 0.1;
constexpr double VENTILATION_CREW_BONUS = 5.0, BROTHERHOOD_CREW_BONUS = 5.0, RATION_CREW_BONUS = 10.0;
constexpr double RAMMER_RELOAD_FACTOR = 0.9, AIM_DRIVE_DIVISOR = 1.1, STABILISER_BLOOM_FACTOR = 0.8;
constexpr double SNAP_SHOT_TURRET_FACTOR = 0.925, SMOOTH_RIDE_MOVE_FACTOR = 0.96;
constexpr double INTUITION_CHANCE = 0.17, RECON_FACTOR_PER_LEVEL = 0.0002, SITUATIONAL_FACTOR_PER_LEVEL = 0.0003;
constexpr int MIN_DEFAULT_CREW_LEVEL = 50;

// ---- #1513 attribute factors (loadout.attribute_factors) ---------------------------------------------------
// The `factors` dictionary #1513's VehicleParameters reads, as battle data. Dict keys are text ("0"/"2" for the
// integer VEHICLE_TTC_ASPECTS keys of `invisibility`); `_aspects` holds [DEFAULT, WHEN_STILL].
struct Factors {
    Value values;
    // loadout._factor: float(factors[name]), else the fallback.
    double number(const std::string& name, double fallback = 1.0) const;
    const Value& get(const std::string& name) const { return values.resolved().get(name); }
};
// The exact #1513 factors for one loadout; empty when the client machinery is unavailable or rejects the state
// (the failure is logged like the Python). `crew` and `equipments` are stock sequences or nullptr; `activity`
// holds one flag per crew member; `crew_level` trains a generated default crew.
std::optional<Factors> attributeFactors(PyObject* descriptor, PyObject* crew = nullptr, PyObject* equipments = nullptr,
                                        const std::vector<bool>* activity = nullptr, bool is_fire = false,
                                        std::optional<std::int64_t> crew_level = {});
// loadout.invisibility_pair: (additive, multiplier) of the still or moving aspect; (0, 1) without factors.
std::pair<double, double> invisibilityPair(const Factors* factors, bool still);

// ---- names (loadout.device_names / equipment_names / crew_skill_names) -------------------------------------
using CrewSkills = std::vector<std::vector<std::string>>;
std::vector<std::string> deviceNames(PyObject* descriptor);
std::vector<std::string> equipmentNames(PyObject* equipments);
// One lowercased skill-name list per crew member; an unreadable member contributes an empty list.
CrewSkills crewSkillNames(PyObject* crew);

// ---- loadout.modifiers --------------------------------------------------------------------------------------
struct Loadout {
    double crew_level = BASE_CREW_LEVEL, commander_level = BASE_CREW_LEVEL, effective_crew_level = 0.;
    double crew_multiplier = 1., crew_factor = 1., gun_rotation_factor = 1., reload_factor = 1., aim_time_factor = 1.;
    double dispersion_factor = 1., repair_factor = 1., vehicle_rotation_factor = 1., radio_factor = 1.;
    std::array<double, 3> terrain_resistance_factors{{1., 1., 1.}};
    double bloom_move_factor = 1., bloom_rotation_factor = 1., bloom_turret_factor = 1.;
    bool has_big_kit = false, from_client_factors = false, has_rammer = false, has_aim_drives = false;
    bool has_ventilation = false, has_stabiliser = false, has_rations = false, has_brotherhood = false;
    bool has_snap_shot = false, has_smooth_ride = false, has_sixth_sense = false;
    // The effective-params wire object (`loadout`), and back from a canonical snapshot.
    Value toValue() const;
    static Loadout fromValue(const Value& loadout);
};
// The passive modifier bundle of one loadout; `crew_skills` null means the crew is unknown.
Loadout modifiers(PyObject* descriptor = nullptr, PyObject* equipments = nullptr, const CrewSkills* crew_skills = nullptr,
                  const Factors* factors = nullptr);
inline Loadout baseline() { return modifiers(); }
// loadout._misc_factor: float(misc.get(name)) when positive, else the fallback.
double miscFactor(PyObject* misc, const char* name, double fallback);
// #1513's per-crewman level increase: miscAttrs crewLevelIncrease plus Brothers in Arms and rations.
double crewLevelIncrease(PyObject* descriptor, PyObject* equipments = nullptr, const CrewSkills* crew_skills = nullptr);

// ---- loadout.spotting_profile -------------------------------------------------------------------------------
struct SpottingProfile {
    double commander_level = 100., recon_level = 0., situational_level = 0., camouflage_level = 0.;
    double binocular_factor = 1., binocular_delay = 3., camouflage_net_bonus = 0., camouflage_net_delay = 3.;
    bool has_binoculars = false, has_camouflage_net = false, from_client_factors = false;
    double vision_factor = 1., camouflage_factor = CREW_FACTOR_BASE;
    std::pair<double, double> invisibility_moving{0., 1.}, invisibility_still{0., 1.};
    Value toValue() const;
    static SpottingProfile fromValue(const Value& spotting);
};
SpottingProfile spottingProfile(PyObject* descriptor, PyObject* crew, double level_increase, const Factors* factors);
// Whether a stationary-only device finished its activation delay.
inline bool stillDeviceActive(double still_seconds, double delay_seconds) { return still_seconds >= std::max(0.0, delay_seconds); }

// ---- equipment_mechanics.py ---------------------------------------------------------------------------------
extern const std::set<std::string> ACTIVATION_DEVICE_NAMES, ACTIVATION_CREW_NAMES;
// One exact consumable projected from its descriptor (the immutable wire contract).
struct EquipmentContract {
    std::string name, kind;
    std::int64_t id = 0, compact_descr = 0, reuse_count = 0;
    std::vector<std::string> tags;
    double cooldown_seconds = 0., fire_starting_chance_factor = 1., bonus_value = 0., crew_level_increase = 0.;
    double engine_power_factor = 1., turret_rotation_speed_factor = 1., engine_hp_loss_per_second = 0., auto_reaction_seconds = 0.;
    bool autoactivate = false, repair_all = false;
    std::optional<double> stun_resistance_duration, stun_resistance_effect;
    bool operator==(const EquipmentContract&) const;
    bool operator!=(const EquipmentContract& v) const { return !(*this == v); }
    Value toValue() const;
};
// equipment_kind of a descriptor-like value: medkit, repairkit, extinguisher, rpm_limiter, stimulator, fuel, passive.
std::string equipmentKind(PyObject* descriptor);
EquipmentContract projectEquipment(PyObject* descriptor, double reaction_seconds = 0.);
// The three Bot defaults from this client's equipment cache (vehicles.g_cache).
std::vector<EquipmentContract> defaultBotConsumables(PyObject* cache);
// The strict runtime projection of one wire contract (equipment_mechanics._validate_contract).
EquipmentContract validateContract(const Value& contract);
// The fixed Bot consumable ledger: from manifest `botConsumables` rows, else recovered from wire snapshots.
std::vector<EquipmentContract> botConsumableContracts(const Value* bot_consumables, const Value* snapshots = nullptr);
// A player's donated regular-consumable contracts (mounted_loadout), else recovered from wire snapshots.
std::vector<EquipmentContract> participantEquipmentContracts(const Value* mounted_loadout, const Value* snapshots = nullptr);

// The mutation one valid activation requests.
struct EquipmentEffect {
    std::string action;            // extinguish_fire, repair_devices, restore_crew, set_rpm_limiter
    bool repair_all = false, clear_stun = false, active = false;
    std::optional<std::string> selected;
    double bonus_value = 0., engine_power_factor = 1., engine_hp_loss_per_second = 0.;
};
std::optional<EquipmentEffect> effectPolicy(const EquipmentContract& equipment, const Value& critical, const std::optional<std::string>& selected,
                                            std::optional<bool> requested_active, bool active, bool stunned);

// Cooldown and quantity state around one immutable contract (equipment_mechanics.EquipmentState).
class EquipmentState {
public:
    EquipmentContract contract;
    std::int64_t uses_left = 0;
    double ready_at = 0.;
    bool active = false;
    explicit EquipmentState(EquipmentContract contract, double now = 0.);
    bool ready(double now) const { return uses_left != 0 && now >= ready_at; }
    std::optional<EquipmentEffect> activate(double now, const Value& critical, const std::optional<std::string>& selected = {},
                                            std::optional<bool> requested_active = {}, bool stunned = false);
    std::optional<EquipmentEffect> pollAuto(double now, const Value& critical);
    std::optional<EquipmentEffect> pollBot(double now, const Value& critical, bool stunned = false);
    // The validated wire snapshot (`snapshot`) and the worker's trusted projection with fresh clocks.
    Value snapshot(double now) const;
    Value trustedSnapshot(double now) const;
    // Loads a relative wire snapshot; throws Invalid on partial or inconsistent state.
    void restore(const Value& snapshot, double now);
    // Every discrete ledger edge the wire represents (trusted_snapshot_edge): unchanged edges need no new wire row.
    struct Edge {
        std::int64_t uses_left; bool active; double ready_at; bool ready; std::optional<double> auto_since, ai_since;
        bool operator==(const Edge& v) const {
            return uses_left == v.uses_left && active == v.active && ready_at == v.ready_at && ready == v.ready && auto_since == v.auto_since && ai_since == v.ai_since;
        }
        bool operator!=(const Edge& v) const { return !(*this == v); }
    };
    Edge edge(double now) const { return {uses_left, active, ready_at, now >= ready_at, auto_pending_since_, ai_pending_since_}; }
private:
    std::optional<double> auto_pending_since_, ai_pending_since_;
};
// Validates and restores one complete server-owned ledger; `contracts` null recovers them from the snapshots.
std::vector<EquipmentState> restoreEquipmentStates(const Value& snapshots, const std::vector<EquipmentContract>* contracts, double now);

// equipment_mechanics.passive_effects: mounted or active equipment folded into stat policy values.
struct Passives {
    double fire_starting_chance_factor = 1., repairkit_bonus_value = 0., medkit_bonus_value = 0., crew_level_increase = 0.;
    double engine_power_factor = 1., turret_rotation_speed_factor = 1., engine_hp_loss_per_second = 0.;
};
Passives passiveEffects(const std::vector<EquipmentState>& states);
// The same fold over wire rows: equipment snapshots (`{equipment, usesLeft, ...}`) or bare contracts.
Passives passiveEffects(const Value& rows);

// ---- effective_params.py ------------------------------------------------------------------------------------
constexpr const char* EFFECTIVE_PARAMS_CAPABILITY = "effective_params_v1";
constexpr std::int64_t EFFECTIVE_PARAMS_SCHEMA_VERSION = 1;
constexpr double MAX_CRITICAL_DEVICE_HP = 1000000000.0;
// A detached canonical effective-parameter snapshot, or empty when `value` is invalid.
std::optional<Value> canonicalEffectiveParams(const Value& value);
// The role an exact or role-prefixed skill name requires, if any.
std::optional<std::string> skillRequiredRole(const std::string& name);
struct SkillCarrier { std::string instance; std::vector<std::string> roles; double level = 0.; };
// Completed skill states whose carriers are conscious. `snapshot` is a canonical snapshot or its `crew` object;
// `crew_ko` is a critical object (its `crew_ko`), a list of knocked-out instances, or null for none. Throws Invalid when malformed.
std::vector<SkillCarrier> livingSkillStates(const Value& snapshot, const std::string& wanted, const Value& crew_ko = Value());
inline std::int64_t livingSkillCount(const Value& snapshot, const std::string& wanted, const Value& crew_ko = Value()) {
    return std::int64_t(livingSkillStates(snapshot, wanted, crew_ko).size());
}
double livingSkillLevel(const Value& snapshot, const std::string& wanted, const Value& crew_ko = Value());
struct SkillSummary {
    bool sixth_sense = false, expert = false, deadeye = false, controlled_impact = false, designated_target = false, last_effort = false;
    std::int64_t intuition_chances = 0;
};
SkillSummary skillSummary(const Value& crew, const Value& crew_ko = Value());
// The accepted battle booster's override for one skill timing, else the fallback.
double boosterSkillValue(const Value& snapshot, const std::string& name, double fallback);

// ---- the battle's own law inputs (BattleRuntime caches and the local equipment ledger) -----------------------
// effective_params.canonical(raw): the canonical local snapshot, or Null when unusable.
Value canonical(Battle& b, const Value& raw);
// Conscious carriers of one skill; `crew_critical` is the local critical object or {'crew_ko': [...]}.
std::int64_t livingSkillCount(Battle& b, const Value& snapshot, const char* skill, PyObject* crew_critical);
// BattleRuntime._spotting_profile: the player's own garage crew and devices, a replay's accepted profile, or a
// remote descriptor's default crew.
p::Ref spottingProfile(Battle& b, PyObject* descriptor, bool local);
// BattleRuntime._local_factors: the player's #1513 attribute factors (a dict), or None; computed once per round.
p::Ref localFactors(Battle& b, PyObject* descriptor);
// BattleRuntime._accepted_local_effective_params: the server-accepted snapshot, Null on the worker.
Value acceptedLocalEffectiveParams(Battle& b);
// The cold equipment restore: contract, quantity and activation-result changes, repair-kit acknowledgement.
bool restoreEquipmentCold(Battle& b, const Value& snapshot, bool present);
// Publish the accepted ledger at a cold-start or recorded replay presentation boundary.
bool presentEquipment(Battle& b, double now);
}

struct Params {
    // The player's attribute factors and spotting inputs, resolved once per round.
    bool local_factors_resolved = false;
    std::optional<params::Factors> local_factors;
    std::optional<Value> local_spotting;
    std::map<std::pair<std::string, std::vector<std::string>>, Value> remote_spotting;
    // The cold-restored local equipment ledger and its last stock presentation.
    std::optional<std::vector<params::EquipmentState>> equipment;
    std::vector<std::pair<std::int64_t, std::array<int, 3>>> equipment_signature;
    std::int64_t equipment_result_logged_seq = 0;
};
}
