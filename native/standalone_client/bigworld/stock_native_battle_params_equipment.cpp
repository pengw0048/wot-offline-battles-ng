// equipment_mechanics.py battle law: consumable contracts, the per-item state machine, passives and effect policy.
#if !defined(WOT_NATIVE_BATTLE_HOST_BUILD)
#include "pch.hpp"
#endif
#include "stock_native_battle_params.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <sstream>

namespace native_battle {
namespace params {
const std::set<std::string> ACTIVATION_DEVICE_NAMES{
    "engineHealth", "ammoBayHealth", "fuelTankHealth", "radioHealth", "leftTrackHealth", "rightTrackHealth",
    "gunHealth", "turretRotatorHealth", "surveyingDeviceHealth"};
const std::set<std::string> ACTIVATION_CREW_NAMES{
    "commander", "driver", "gunner1", "gunner2", "loader1", "loader2", "radioman1", "radioman2"};

namespace {
const char* const DEFAULT_BOT_CONSUMABLE_NAMES[] = {"autoExtinguishers", "largeMedkit", "largeRepairkit"};
const std::set<std::string> BOT_YELLOW_REPAIR_DEVICES{"ammoBayHealth", "engineHealth", "gunHealth"};
const char* const CONTRACT_FIELDS[] = {
    "name", "kind", "id", "compactDescr", "tags", "reuseCount", "cooldownSeconds", "autoactivate", "fireStartingChanceFactor",
    "repairAll", "bonusValue", "crewLevelIncrease", "enginePowerFactor", "turretRotationSpeedFactor", "engineHpLossPerSecond",
    "autoReactionSeconds"};
const char* const SNAPSHOT_FIELDS[] = {"equipment", "usesLeft", "cooldownTimeLeft", "active", "autoPendingElapsed", "aiPendingElapsed"};
const char* const STUN_FIELDS[] = {"stunResistanceDuration", "stunResistanceEffect"};

using K = Value::Kind;
const Value& at(const Value& v, const char* name) { return v.resolved().get(name); }
bool has(const Value& v, const char* name) { return v.resolved().kind == K::Object && v.resolved().object.count(name); }
bool numeric(const Value& v) { const auto k = v.resolved().kind; return k == K::Integer || k == K::Number || k == K::Boolean || k == K::Text; }

// equipment_mechanics._number: float(value), the default when it fails or is not finite.
double number(const Value& v, double fallback) { return numeric(v) ? v.numeric(fallback) : fallback; }
// equipment_mechanics._integer: an exact integer, the default for bool, fractions and failures.
std::int64_t integer(const Value& v, std::int64_t fallback) {
    const auto& r = v.resolved();
    switch (r.kind) {
    case K::Integer: return r.integer;
    case K::Number: return std::isfinite(r.number) && std::floor(r.number) == r.number ? std::int64_t(r.number) : fallback;
    case K::Text: {
        std::size_t used = 0;
        try {
            const long long n = std::stoll(r.text, &used);
            return used == r.text.size() ? std::int64_t(n) : fallback;
        } catch (...) { return fallback; }
    }
    default: return fallback;
    }
}
// float(raw) == parsed for a non-bool raw value.
bool exact(const Value& raw, std::int64_t parsed) {
    const auto& r = raw.resolved();
    if (r.kind == K::Boolean || !numeric(r)) return false;
    const double n = r.numeric(std::nan(""));
    return n == double(parsed);
}
std::string lower(std::string text) {
    for (auto& c : text) c = char(std::tolower(static_cast<unsigned char>(c)));
    return text;
}
// str(value) of a wire scalar.
std::string str(const Value& v) {
    const auto& r = v.resolved();
    switch (r.kind) {
    case K::Text: return r.text;
    case K::Integer: return std::to_string(r.integer);
    case K::Boolean: return r.boolean ? "True" : "False";
    case K::Null: return "None";
    case K::Number: { std::ostringstream out; out.precision(12); out << r.number; return out.str(); }
    default: return {};
    }
}
Value make(double v) { Value x; x.kind = K::Number; x.number = v; return x; }
Value makeInt(std::int64_t v) { Value x; x.kind = K::Integer; x.integer = v; return x; }
Value makeBool(bool v) { Value x; x.kind = K::Boolean; x.boolean = v; return x; }
Value makeText(const std::string& v) { Value x; x.kind = K::Text; x.text = v; return x; }

// equipment_kind over already-read fields.
std::string kindOf(const std::string& raw_name, const std::vector<std::string>& tags, double hp_loss, double crew_increase,
                   double power, double turret) {
    const std::string name = lower(raw_name);
    auto tagged = [&](const char* t) { return std::find(tags.begin(), tags.end(), t) != tags.end(); };
    if (tagged("medkit") || name.find("medkit") != std::string::npos) return "medkit";
    if (tagged("repairkit") || name.find("repairkit") != std::string::npos) return "repairkit";
    if (name.find("extinguisher") != std::string::npos ||
        std::any_of(tags.begin(), tags.end(), [](const std::string& t) { return t.find("extinguisher") != std::string::npos; }))
        return "extinguisher";
    if (name.find("removedrpmlimiter") != std::string::npos || hp_loss > 0.) return "rpm_limiter";
    if (crew_increase != 0.) return "stimulator";
    if (power != 1. || turret != 1.) return "fuel";
    return "passive";
}

// ---- descriptor (stock object) reads ----
p::Ref attr(PyObject* d, const char* name) { return py::optional(d, name); }
double pyNumber(PyObject* v, double fallback) {
    if (!v || v == Py_None) return fallback;
    PyObject* f = PyNumber_Float(v);
    if (!f) {
        if (PyErr_ExceptionMatches(PyExc_TypeError) || PyErr_ExceptionMatches(PyExc_ValueError) || PyErr_ExceptionMatches(PyExc_OverflowError)) {
            PyErr_Clear();
            return fallback;
        }
        throw p::Error("equipment number conversion failed");
    }
    const double out = PyFloat_AS_DOUBLE(f);
    Py_DECREF(f);
    return std::isfinite(out) ? out : fallback;
}
std::int64_t pyInteger(PyObject* v, std::int64_t fallback) {
    if (!v || PyBool_Check(v)) return fallback;
    PyObject* n = PyNumber_Int(v);
    if (!n) { PyErr_Clear(); return fallback; }
    p::Ref held(n);
    long long exact_value = PyLong_Check(n) ? PyLong_AsLongLong(n) : PyInt_AS_LONG(n);
    if (PyErr_Occurred()) { PyErr_Clear(); return fallback; }
    PyObject* f = PyNumber_Float(v);
    if (!f) { PyErr_Clear(); return fallback; }
    const double as_float = PyFloat_AS_DOUBLE(f);
    Py_DECREF(f);
    return as_float == double(exact_value) ? std::int64_t(exact_value) : fallback;
}
std::vector<std::string> pyTags(PyObject* d) {
    std::vector<std::string> out;
    auto tags = attr(d, "tags");
    if (!p::flag(tags.get())) return out;
    PyObject* iterator = PyObject_GetIter(tags.get());
    if (!iterator) {
        if (PyErr_ExceptionMatches(PyExc_TypeError)) { PyErr_Clear(); return out; }
        throw p::Error("equipment tags iteration failed");
    }
    p::Ref held(iterator);
    while (PyObject* tag = PyIter_Next(iterator)) {
        p::Ref t(tag);
        out.push_back(lower(p::string(p::Ref(PyObject_Str(tag)).get())));
    }
    if (PyErr_Occurred()) throw p::Error("equipment tags iteration failed");
    std::sort(out.begin(), out.end());
    return out;
}
std::string pyName(PyObject* d) {
    auto name = attr(d, "name");
    return p::flag(name.get()) ? p::string(p::Ref(PyObject_Str(name.get())).get()) : std::string();
}

// ---- contract validation (equipment_mechanics._validate_contract) ----
EquipmentContract validate(const Value& raw) {
    const auto& c = raw.resolved();
    if (c.kind != K::Object) throw Invalid("equipment contract is not an object");
    std::set<std::string> keys;
    for (const auto& item : c.object) keys.insert(item.first);
    for (const char* s : STUN_FIELDS) keys.erase(s);
    const std::set<std::string> expected(std::begin(CONTRACT_FIELDS), std::end(CONTRACT_FIELDS));
    if (keys != expected) throw Invalid("equipment contract fields are incomplete");
    const std::string name = at(c, "name").truth() ? str(at(c, "name")) : std::string();
    const std::string kind = at(c, "kind").truth() ? str(at(c, "kind")) : std::string();
    const auto& raw_tags = at(c, "tags").resolved();
    std::vector<std::string> tags_for_kind;
    if (raw_tags.kind == K::Array) for (const auto& t : raw_tags.array) tags_for_kind.push_back(lower(str(t)));
    const std::string expected_kind = kindOf(name, tags_for_kind, number(at(c, "engineHpLossPerSecond"), 0.), number(at(c, "crewLevelIncrease"), 0.),
                                             number(at(c, "enginePowerFactor"), 1.), number(at(c, "turretRotationSpeedFactor"), 1.));
    if (name.empty() || kind != expected_kind) throw Invalid("equipment contract identity is invalid");
    if (raw_tags.kind != K::Array) throw Invalid("equipment contract tags are invalid");
    EquipmentContract out;
    out.name = name;
    out.kind = kind;
    out.id = integer(at(c, "id"), -1);
    out.compact_descr = integer(at(c, "compactDescr"), -1);
    out.tags = tags_for_kind;
    out.reuse_count = integer(at(c, "reuseCount"), 0);
    out.cooldown_seconds = number(at(c, "cooldownSeconds"), -1.);
    out.fire_starting_chance_factor = number(at(c, "fireStartingChanceFactor"), -1.);
    out.bonus_value = number(at(c, "bonusValue"), 0.);
    out.crew_level_increase = number(at(c, "crewLevelIncrease"), 0.);
    out.engine_power_factor = number(at(c, "enginePowerFactor"), -1.);
    out.turret_rotation_speed_factor = number(at(c, "turretRotationSpeedFactor"), -1.);
    out.engine_hp_loss_per_second = number(at(c, "engineHpLossPerSecond"), -1.);
    out.auto_reaction_seconds = number(at(c, "autoReactionSeconds"), -1.);
    for (const char* key : STUN_FIELDS) {
        if (!has(c, key)) continue;
        const auto& s = at(c, key).resolved();
        if ((s.kind != K::Integer && s.kind != K::Number) || !(s.numeric(-1.) >= 0. && s.numeric(-1.) <= 1.))
            throw Invalid("equipment stun resistance is invalid");
        (std::string(key) == "stunResistanceDuration" ? out.stun_resistance_duration : out.stun_resistance_effect) = s.numeric();
    }
    const auto& autoactivate = at(c, "autoactivate").resolved();
    const auto& repair_all = at(c, "repairAll").resolved();
    if (out.id < 0 || out.compact_descr < 0 || out.cooldown_seconds < 0. || out.fire_starting_chance_factor < 0. ||
        out.engine_power_factor < 0. || out.turret_rotation_speed_factor < 0. || out.engine_hp_loss_per_second < 0. ||
        out.auto_reaction_seconds < 0. || autoactivate.kind != K::Boolean || repair_all.kind != K::Boolean)
        throw Invalid("equipment contract values are invalid");
    out.autoactivate = autoactivate.boolean;
    out.repair_all = repair_all.boolean;
    if (!exact(at(c, "id"), out.id) || !exact(at(c, "compactDescr"), out.compact_descr) || !exact(at(c, "reuseCount"), out.reuse_count))
        throw Invalid("equipment contract integer is invalid");
    for (const char* key : {"cooldownSeconds", "fireStartingChanceFactor", "bonusValue", "crewLevelIncrease", "enginePowerFactor",
                            "turretRotationSpeedFactor", "engineHpLossPerSecond", "autoReactionSeconds"}) {
        const auto& v = at(c, key).resolved();
        if (v.kind == K::Boolean || !numeric(v)) throw Invalid("equipment contract number is invalid");
        if (!std::isfinite(v.numeric(std::nan("")))) throw Invalid("equipment contract number is invalid");
    }
    if (!std::is_sorted(out.tags.begin(), out.tags.end())) throw Invalid("equipment contract tags are not canonical");
    return out;
}

// equipment_mechanics._validated_snapshot_state: one mutable wire row against its canonical contract.
struct SnapshotState { std::int64_t uses_left; double cooldown; bool active; std::optional<double> pending[2]; };
SnapshotState validatedSnapshot(const Value& raw, const EquipmentContract& contract) {
    const auto& s = raw.resolved();
    std::set<std::string> keys;
    if (s.kind == K::Object) for (const auto& item : s.object) keys.insert(item.first);
    if (s.kind != K::Object || keys != std::set<std::string>(std::begin(SNAPSHOT_FIELDS), std::end(SNAPSHOT_FIELDS)))
        throw Invalid("equipment wire snapshot is incomplete");
    SnapshotState out;
    const auto& raw_uses = at(s, "usesLeft");
    out.uses_left = integer(raw_uses, -2);
    const std::int64_t maximum = contract.reuse_count < 0 ? -1 : contract.reuse_count + 1;
    if (raw_uses.resolved().kind == K::Boolean || (maximum < 0 && out.uses_left != -1) || (maximum >= 0 && !(0 <= out.uses_left && out.uses_left <= maximum)))
        throw Invalid("equipment wire quantity is invalid");
    const auto& raw_cooldown = at(s, "cooldownTimeLeft");
    out.cooldown = number(raw_cooldown, -1.);
    const double limit = std::max(0., contract.cooldown_seconds);
    if (raw_cooldown.resolved().kind == K::Boolean || out.cooldown < 0. || out.cooldown > limit + 1.0e-6) throw Invalid("equipment wire cooldown is invalid");
    const auto& active = at(s, "active").resolved();
    if (active.kind != K::Boolean || (active.boolean && contract.kind != "rpm_limiter")) throw Invalid("equipment wire active state is invalid");
    out.active = active.boolean;
    const char* clocks[] = {"autoPendingElapsed", "aiPendingElapsed"};
    for (int i = 0; i < 2; ++i) {
        const auto& v = at(s, clocks[i]).resolved();
        if (v.kind == K::Null) continue;
        if (v.kind == K::Boolean) throw Invalid("equipment wire pending clock is invalid");
        const double value = number(v, -1.);
        if (value < 0. || value > 3600.) throw Invalid("equipment wire pending clock is invalid");
        out.pending[i] = value;
    }
    if (out.pending[0] && !contract.autoactivate) throw Invalid("equipment wire auto clock is invalid");
    if (out.pending[1] && contract.kind != "repairkit" && contract.kind != "medkit") throw Invalid("equipment wire AI clock is invalid");
    if (out.uses_left == 0 && (out.pending[0] || out.pending[1])) throw Invalid("exhausted equipment has a pending activation");
    return out;
}

bool botRepairWorthwhile(const Value& critical) {
    const auto& c = critical.resolved();
    if (c.kind != K::Object) return false;
    if (at(c, "destroyed").truth()) return true;
    for (const auto& record : at(c, "devices").resolved().array) {
        if (record.resolved().kind != K::Object) continue;
        const std::string state = at(record, "state").truth() ? str(at(record, "state")) : std::string();
        if (state == "destroyed") return true;
        if (state == "critical" && BOT_YELLOW_REPAIR_DEVICES.count(at(record, "name").truth() ? str(at(record, "name")) : std::string())) return true;
    }
    return false;
}
// The contract behind a wire row: `{equipment: {...}}` or the contract itself.
const Value& projection(const Value& row) {
    const auto& r = row.resolved();
    if (r.kind == K::Object && at(r, "equipment").resolved().kind == K::Object) return at(r, "equipment").resolved();
    return r;
}
void fold(Passives& out, const std::string& kind, bool spent, bool active_state, double fire, double bonus, double crew,
          double power, double turret, double hp_loss) {
    if (kind == "extinguisher") out.fire_starting_chance_factor *= std::max(0., fire);
    else if (kind == "repairkit") { if (!spent) out.repairkit_bonus_value += bonus; }
    else if (kind == "medkit") { if (!spent) out.medkit_bonus_value += bonus; }
    out.crew_level_increase += crew;
    if (kind == "fuel" || (kind == "rpm_limiter" && active_state)) out.engine_power_factor *= std::max(0., power);
    if (kind == "fuel") out.turret_rotation_speed_factor *= std::max(0., turret);
    if (kind == "rpm_limiter" && active_state) out.engine_hp_loss_per_second += std::max(0., hp_loss);
}
}

bool EquipmentContract::operator==(const EquipmentContract& v) const {
    return name == v.name && kind == v.kind && id == v.id && compact_descr == v.compact_descr && reuse_count == v.reuse_count && tags == v.tags &&
           cooldown_seconds == v.cooldown_seconds && fire_starting_chance_factor == v.fire_starting_chance_factor && bonus_value == v.bonus_value &&
           crew_level_increase == v.crew_level_increase && engine_power_factor == v.engine_power_factor &&
           turret_rotation_speed_factor == v.turret_rotation_speed_factor && engine_hp_loss_per_second == v.engine_hp_loss_per_second &&
           auto_reaction_seconds == v.auto_reaction_seconds && autoactivate == v.autoactivate && repair_all == v.repair_all &&
           stun_resistance_duration == v.stun_resistance_duration && stun_resistance_effect == v.stun_resistance_effect;
}

Value EquipmentContract::toValue() const {
    Value o; o.kind = K::Object;
    o.object["name"] = makeText(name);
    o.object["kind"] = makeText(kind);
    o.object["id"] = makeInt(id);
    o.object["compactDescr"] = makeInt(compact_descr);
    Value t; t.kind = K::Array;
    for (const auto& tag : tags) t.array.push_back(makeText(tag));
    o.object["tags"] = std::move(t);
    o.object["reuseCount"] = makeInt(reuse_count);
    o.object["cooldownSeconds"] = make(cooldown_seconds);
    o.object["autoactivate"] = makeBool(autoactivate);
    o.object["fireStartingChanceFactor"] = make(fire_starting_chance_factor);
    o.object["repairAll"] = makeBool(repair_all);
    o.object["bonusValue"] = make(bonus_value);
    o.object["crewLevelIncrease"] = make(crew_level_increase);
    o.object["enginePowerFactor"] = make(engine_power_factor);
    o.object["turretRotationSpeedFactor"] = make(turret_rotation_speed_factor);
    o.object["engineHpLossPerSecond"] = make(engine_hp_loss_per_second);
    o.object["autoReactionSeconds"] = make(auto_reaction_seconds);
    if (stun_resistance_duration) o.object["stunResistanceDuration"] = make(*stun_resistance_duration);
    if (stun_resistance_effect) o.object["stunResistanceEffect"] = make(*stun_resistance_effect);
    return o;
}

std::string equipmentKind(PyObject* d) {
    return kindOf(p::flag(attr(d, "name").get()) ? p::string(p::Ref(PyObject_Str(attr(d, "name").get())).get()) : std::string(), pyTags(d),
                  pyNumber(attr(d, "engineHpLossPerSecond").get(), 0.), pyNumber(attr(d, "crewLevelIncrease").get(), 0.),
                  pyNumber(attr(d, "enginePowerFactor").get(), 1.), pyNumber(attr(d, "turretRotationSpeedFactor").get(), 1.));
}

EquipmentContract projectEquipment(PyObject* d, double reaction_seconds) {
    if (!d || d == Py_None) throw Invalid("equipment descriptor is unavailable");
    EquipmentContract out;
    out.name = pyName(d);
    if (out.name.empty()) throw Invalid("equipment descriptor has no name");
    out.kind = equipmentKind(d);
    out.autoactivate = p::flag(attr(d, "autoactivate").get());
    auto raw_id = attr(d, "id");
    if ((PyList_Check(raw_id.get()) || PyTuple_Check(raw_id.get())) && PySequence_Size(raw_id.get()) > 1)
        out.id = pyInteger(p::Ref(PySequence_GetItem(raw_id.get(), 1)).get(), 0);
    else out.id = pyInteger(raw_id.get(), 0);
    out.compact_descr = pyInteger(attr(d, "compactDescr").get(), 0);
    out.tags = pyTags(d);
    out.reuse_count = pyInteger(attr(d, "reuseCount").get(), 0);
    out.cooldown_seconds = std::max(0., pyNumber(attr(d, "cooldownSeconds").get(), 0.));
    out.fire_starting_chance_factor = std::max(0., pyNumber(attr(d, "fireStartingChanceFactor").get(), 1.));
    out.repair_all = p::flag(attr(d, "repairAll").get());
    out.bonus_value = pyNumber(attr(d, "bonusValue").get(), 0.);
    out.crew_level_increase = pyNumber(attr(d, "crewLevelIncrease").get(), 0.);
    out.engine_power_factor = std::max(0., pyNumber(attr(d, "enginePowerFactor").get(), 1.));
    out.turret_rotation_speed_factor = std::max(0., pyNumber(attr(d, "turretRotationSpeedFactor").get(), 1.));
    out.engine_hp_loss_per_second = std::max(0., pyNumber(attr(d, "engineHpLossPerSecond").get(), 0.));
    // #1513 exposes autoactivation but no reaction delay; the server loop owns the next observation boundary.
    out.auto_reaction_seconds = std::max(0., reaction_seconds);
    auto duration = attr(d, "stunResistanceDuration"), effect = attr(d, "stunResistanceEffect");
    if (duration.get() != Py_None) out.stun_resistance_duration = pyNumber(duration.get(), 0.);
    if (effect.get() != Py_None) out.stun_resistance_effect = pyNumber(effect.get(), 0.);
    return out;
}

std::vector<EquipmentContract> defaultBotConsumables(PyObject* cache) {
    auto ids = py::call(cache, "equipmentIDs");
    auto descriptors = py::call(cache, "equipments");
    std::vector<EquipmentContract> out;
    for (const char* name : DEFAULT_BOT_CONSUMABLE_NAMES) {
        auto id = py::call(ids.get(), "get", p::text(name).get());
        auto descriptor = py::call(descriptors.get(), "get", id.get());
        if (descriptor.get() == Py_None) throw Invalid(std::string("client equipment '") + name + "' is unavailable");
        auto projected = projectEquipment(descriptor.get());
        if (projected.name != name) throw Invalid(std::string("client equipment '") + name + "' resolved as '" + projected.name + "'");
        out.push_back(std::move(projected));
    }
    return out;
}

EquipmentContract validateContract(const Value& contract) { return validate(contract); }

std::vector<EquipmentContract> botConsumableContracts(const Value* bot_consumables, const Value* snapshots) {
    Value recovered;
    const Value* raw = bot_consumables && bot_consumables->resolved().kind != K::Null ? bot_consumables : nullptr;
    if (!raw && snapshots && snapshots->resolved().kind == K::Array) {
        if (snapshots->resolved().array.empty()) return {};
        recovered.kind = K::Array;
        for (const auto& row : snapshots->resolved().array)
            if (row.resolved().kind == K::Object) recovered.array.push_back(at(row, "equipment"));
        raw = &recovered;
    }
    if (!raw) return {};
    const auto& rows = raw->resolved();
    if (rows.kind != K::Array || rows.array.size() != 3) throw Invalid("bot consumable contract count is invalid");
    std::vector<EquipmentContract> out;
    for (const auto& row : rows.array) {
        if (row.resolved().kind != K::Object) throw p::Error("bot consumable contract is not a mapping");
        out.push_back(validate(row));
    }
    for (std::size_t i = 0; i < 3; ++i)
        if (out[i].name != DEFAULT_BOT_CONSUMABLE_NAMES[i]) throw Invalid("bot consumable contract order is invalid");
    if (out[0].kind != "extinguisher" || out[1].kind != "medkit" || out[2].kind != "repairkit") throw Invalid("bot consumable contract kinds are invalid");
    if (!out[0].autoactivate || !out[1].repair_all || !out[2].repair_all) throw Invalid("bot consumable effect policy is invalid");
    return out;
}

std::vector<EquipmentContract> participantEquipmentContracts(const Value* mounted_loadout, const Value* snapshots) {
    Value empty; empty.kind = K::Object;
    const Value& mounted = mounted_loadout && mounted_loadout->truth() ? mounted_loadout->resolved() : empty;
    Value recovered;
    const Value* raw = has(mounted, "equipment_contracts") && at(mounted, "equipment_contracts").resolved().kind != K::Null
        ? &at(mounted, "equipment_contracts") : nullptr;
    if (!raw && snapshots && snapshots->resolved().kind == K::Array) {
        recovered.kind = K::Array;
        for (const auto& row : snapshots->resolved().array)
            if (row.resolved().kind == K::Object) recovered.array.push_back(at(row, "equipment"));
        raw = &recovered;
    }
    if (!raw) return {};
    const auto& rows = raw->resolved();
    if (rows.kind != K::Array || rows.array.size() > 3 ||
        std::any_of(rows.array.begin(), rows.array.end(), [](const Value& v) { return v.resolved().kind != K::Object; }))
        throw Invalid("participant equipment contract count is invalid");
    std::vector<EquipmentContract> out;
    std::set<std::pair<std::int64_t, std::int64_t>> identities;
    for (const auto& row : rows.array) {
        out.push_back(validate(row));
        if (!identities.insert({out.back().id, out.back().compact_descr}).second) throw Invalid("participant equipment contract is duplicated");
    }
    if (has(mounted, "equipments") && at(mounted, "equipments").resolved().kind != K::Null) {
        const auto& names = at(mounted, "equipments").resolved();
        bool same = names.kind == K::Array && names.array.size() == out.size();
        for (std::size_t i = 0; same && i < out.size(); ++i) same = str(names.array[i]) == out[i].name;
        if (!same) throw Invalid("participant equipment contracts do not match loadout");
    }
    return out;
}

std::optional<EquipmentEffect> effectPolicy(const EquipmentContract& equipment, const Value& critical_raw, const std::optional<std::string>& selected,
                                            std::optional<bool> requested_active, bool active, bool stunned) {
    Value none; none.kind = K::Object;
    const Value& critical = critical_raw.resolved().kind == K::Object ? critical_raw.resolved() : none;
    const std::string& kind = equipment.kind;
    EquipmentEffect effect;
    if (kind == "extinguisher") {
        if (!at(critical, "fire").truth()) return {};
        effect.action = "extinguish_fire";
        return effect;
    }
    if (kind == "repairkit") {
        std::set<std::string> damaged;
        for (const auto& record : at(critical, "devices").resolved().array) {
            if (record.resolved().kind != K::Object || !at(record, "name").truth()) continue;
            const std::string state = at(record, "state").truth() ? str(at(record, "state")) : std::string();
            if (state == "critical" || state == "destroyed") damaged.insert(str(at(record, "name")));
        }
        for (const auto& name : at(critical, "destroyed").resolved().array) damaged.insert(str(name));
        if (damaged.empty() || (!equipment.repair_all && !(selected && damaged.count(*selected)))) return {};
        effect.action = "repair_devices";
        effect.repair_all = equipment.repair_all;
        if (!equipment.repair_all) effect.selected = selected;
        effect.bonus_value = equipment.bonus_value;
        return effect;
    }
    if (kind == "medkit") {
        std::set<std::string> knocked_out;
        for (const auto& name : at(critical, "crew_ko").resolved().array) knocked_out.insert(str(name));
        const bool chosen = selected && knocked_out.count(*selected);
        if ((knocked_out.empty() && !stunned) || (!equipment.repair_all && !chosen && !stunned)) return {};
        effect.action = "restore_crew";
        effect.repair_all = equipment.repair_all;
        if (!equipment.repair_all) effect.selected = selected ? selected : std::optional<std::string>("None");
        effect.bonus_value = equipment.bonus_value;
        effect.clear_stun = stunned;
        return effect;
    }
    if (kind == "rpm_limiter") {
        const bool requested = requested_active.value_or(false);
        if (requested == active) return {};
        effect.action = "set_rpm_limiter";
        effect.active = requested;
        effect.engine_power_factor = equipment.engine_power_factor;
        effect.engine_hp_loss_per_second = equipment.engine_hp_loss_per_second;
        return effect;
    }
    return {};
}

EquipmentState::EquipmentState(EquipmentContract c, double now) : contract(std::move(c)) {
    // reuseCount counts uses after the initial charge; -1 is unlimited.
    uses_left = contract.reuse_count < 0 ? -1 : contract.reuse_count + 1;
    ready_at = std::max(0., now);
}

std::optional<EquipmentEffect> EquipmentState::activate(double now, const Value& critical, const std::optional<std::string>& selected,
                                                        std::optional<bool> requested_active, bool stunned) {
    if (!ready(now)) return {};
    auto effect = effectPolicy(contract, critical, selected, requested_active, active, stunned);
    if (!effect) return {};
    if (effect->action == "set_rpm_limiter") active = effect->active;
    else {
        if (uses_left > 0) --uses_left;
        ready_at = now + std::max(0., contract.cooldown_seconds);
    }
    auto_pending_since_.reset();
    ai_pending_since_.reset();
    return effect;
}

std::optional<EquipmentEffect> EquipmentState::pollAuto(double now, const Value& critical) {
    if (!contract.autoactivate) return {};
    const auto candidate = effectPolicy(contract, critical, {}, {}, false, false);
    if (!candidate || !ready(now)) { auto_pending_since_.reset(); return {}; }
    const double delay = std::max(0., contract.auto_reaction_seconds);
    if (!auto_pending_since_) {
        auto_pending_since_ = now;
        if (delay > 0.) return {};
    }
    if (now - *auto_pending_since_ + 1.0e-9 < delay) return {};
    return activate(now, critical);
}

std::optional<EquipmentEffect> EquipmentState::pollBot(double now, const Value& critical, bool stunned) {
    if (contract.kind == "extinguisher") return pollAuto(now, critical);
    if (contract.kind != "repairkit" && contract.kind != "medkit") { ai_pending_since_.reset(); return {}; }
    const auto candidate = effectPolicy(contract, critical, {}, {}, false, stunned);
    if (!candidate || !ready(now) || (contract.kind == "repairkit" && !botRepairWorthwhile(critical))) { ai_pending_since_.reset(); return {}; }
    if (!ai_pending_since_) { ai_pending_since_ = now; return {}; }
    // A kit is never consumed in the same observation that first noticed the damage.
    if (now <= *ai_pending_since_ + 1.0e-9) return {};
    return activate(now, critical, {}, {}, stunned);
}

Value EquipmentState::snapshot(double now) const {
    now = std::max(0., now);
    Value o; o.kind = K::Object;
    o.object["equipment"] = contract.toValue();
    o.object["usesLeft"] = makeInt(uses_left);
    o.object["cooldownTimeLeft"] = make(std::max(0., ready_at - now));
    o.object["active"] = makeBool(active);
    o.object["autoPendingElapsed"] = auto_pending_since_ ? make(std::max(0., now - *auto_pending_since_)) : Value();
    o.object["aiPendingElapsed"] = ai_pending_since_ ? make(std::max(0., now - *ai_pending_since_)) : Value();
    return o;
}

Value EquipmentState::trustedSnapshot(double now) const {
    Value o; o.kind = K::Object;
    o.object["equipment"] = contract.toValue();
    o.object["usesLeft"] = makeInt(uses_left);
    o.object["cooldownTimeLeft"] = make(std::max(0., ready_at - now));
    o.object["active"] = makeBool(active);
    o.object["autoPendingElapsed"] = auto_pending_since_ ? make(std::max(0., now - *auto_pending_since_)) : Value();
    o.object["aiPendingElapsed"] = ai_pending_since_ ? make(std::max(0., now - *ai_pending_since_)) : Value();
    return o;
}

void EquipmentState::restore(const Value& raw, double now) {
    const auto& s = raw.resolved();
    std::set<std::string> keys;
    if (s.kind == K::Object) for (const auto& item : s.object) keys.insert(item.first);
    if (s.kind != K::Object || keys != std::set<std::string>(std::begin(SNAPSHOT_FIELDS), std::end(SNAPSHOT_FIELDS)))
        throw Invalid("equipment wire snapshot is incomplete");
    if (validate(at(s, "equipment")) != contract) throw Invalid("equipment wire contract changed");
    const auto state = validatedSnapshot(s, contract);
    now = std::max(0., now);
    uses_left = state.uses_left;
    ready_at = now + state.cooldown;
    active = state.active;
    auto_pending_since_.reset();
    ai_pending_since_.reset();
    if (state.pending[0]) auto_pending_since_ = now - *state.pending[0];
    if (state.pending[1]) ai_pending_since_ = now - *state.pending[1];
}

std::vector<EquipmentState> restoreEquipmentStates(const Value& snapshots, const std::vector<EquipmentContract>* contracts, double now) {
    if (snapshots.resolved().kind != K::Array) throw Invalid("equipment snapshots are invalid");
    const auto recovered = contracts ? *contracts : participantEquipmentContracts(nullptr, &snapshots);
    const auto& rows = snapshots.resolved().array;
    if (rows.size() != recovered.size()) throw Invalid("equipment snapshot count changed");
    std::vector<EquipmentState> out;
    for (std::size_t i = 0; i < rows.size(); ++i) {
        out.emplace_back(recovered[i], now);
        out.back().restore(rows[i], now);
    }
    return out;
}

Passives passiveEffects(const std::vector<EquipmentState>& states) {
    Passives out;
    for (const auto& s : states) {
        const auto& c = s.contract;
        // An extinguisher's factor is a mounted one; a kit's passive holds only while it is unused.
        fold(out, c.kind, s.uses_left == 0, s.active, c.fire_starting_chance_factor, c.bonus_value, c.crew_level_increase,
             c.engine_power_factor, c.turret_rotation_speed_factor, c.engine_hp_loss_per_second);
    }
    return out;
}

Passives passiveEffects(const Value& rows) {
    Passives out;
    for (const auto& raw : rows.resolved().array) {
        const auto& value = projection(raw);
        const std::string kind = at(value, "kind").truth() ? str(at(value, "kind")) : std::string();
        const bool spent = raw.resolved().kind == K::Object && has(raw, "usesLeft") && integer(at(raw, "usesLeft"), -1) == 0;
        // A wire row is not a live ledger: its rpm limiter switch never applies here.
        fold(out, kind, spent, false, number(at(value, "fireStartingChanceFactor"), 1.), number(at(value, "bonusValue"), 0.),
             number(at(value, "crewLevelIncrease"), 0.), number(at(value, "enginePowerFactor"), 1.), number(at(value, "turretRotationSpeedFactor"), 1.),
             number(at(value, "engineHpLossPerSecond"), 0.));
    }
    return out;
}
}
}
