// The Bot line-up (the pinned BattleRuntime._prepare_bot_vehicle_assignments and ai/planner.py's
// line-up helpers): shared tier/class slots, then each team's vehicles drawn from the #1513 catalogue.
#include "stock_native_battle_bots.hpp"
#include "stock_native_battle_bots_random.hpp"
#include "stock_native_battle_startup.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <tuple>

namespace native_battle {
namespace bots {
namespace {
using Kind = Value::Kind;
using Slot = std::pair<std::int64_t, std::int64_t>;
using Table = std::map<Slot, std::string>;

using random27::Random;
using random27::stableSeed;

// ---------------------------------------------------------------------------------------------- protocol values

const Value* member(const Value& row, const char* key) {
    const auto& r = row.resolved();
    if (r.kind != Kind::Object) return nullptr;
    auto found = r.object.find(key);
    return found == r.object.end() ? nullptr : &found->second.resolved();
}

bool truthy(const Value* v) {
    if (!v) return false;
    switch (v->kind) {
    case Kind::Boolean: return v->boolean;
    case Kind::Integer: return v->integer != 0;
    case Kind::Number: return v->number != 0.;
    case Kind::Text: return !v->text.empty();
    case Kind::Array: return !v->array.empty();
    case Kind::Object: return !v->object.empty();
    default: return false;
    }
}

// Python int(value): nullopt where it raises TypeError, ValueError or OverflowError.
std::optional<std::int64_t> pyInt(const Value* v) {
    if (!v) return std::nullopt;
    switch (v->kind) {
    case Kind::Boolean: return v->boolean ? 1 : 0;
    case Kind::Integer: return v->integer;
    case Kind::Number:
        if (!std::isfinite(v->number) || std::fabs(v->number) >= 9.2e18) return std::nullopt;
        return std::int64_t(std::trunc(v->number));
    case Kind::Text: {
        const auto& t = v->text;
        const auto first = t.find_first_not_of(" \t\r\n\f\v"), last = t.find_last_not_of(" \t\r\n\f\v");
        if (first == std::string::npos) return std::nullopt;
        std::string digits = t.substr(first, last - first + 1);
        std::size_t start = digits[0] == '+' || digits[0] == '-' ? 1 : 0;
        if (start == digits.size() || digits.size() > 19 ||
            digits.find_first_not_of("0123456789", start) != std::string::npos) return std::nullopt;
        return std::strtoll(digits.c_str(), nullptr, 10);
    }
    default: return std::nullopt;
    }
}
std::int64_t needInt(const Value* v, const char* what) {
    auto n = pyInt(v);
    if (!n) throw p::Error(std::string("TypeError: Bot roster field is not an integer: ") + what);
    return *n;
}
// int(raw.get(key, fallback)).
std::int64_t intOr(const Value& row, const char* key, std::int64_t fallback) {
    const Value* v = member(row, key);
    return v ? needInt(v, key) : fallback;
}

// Python 2 str(value) of a decoded protocol value.
std::string pyStr(const Value* v) {
    if (!v) return "None";
    switch (v->kind) {
    case Kind::Null: return "None";
    case Kind::Boolean: return v->boolean ? "True" : "False";
    case Kind::Integer: return std::to_string(v->integer);
    case Kind::Number: {
        if (std::isnan(v->number)) return "nan";
        if (std::isinf(v->number)) return v->number < 0 ? "-inf" : "inf";
        char buffer[64];
        std::snprintf(buffer, sizeof(buffer), "%.12g", v->number);
        std::string out = buffer;
        if (out.find_first_of(".e") == std::string::npos) out += ".0";
        return out;
    }
    case Kind::Text: return v->text;
    case Kind::Array: {
        std::string out = "[";
        for (std::size_t i = 0; i < v->array.size(); ++i) out += (i ? ", " : "") + pyStr(&v->array[i].resolved());
        return out + "]";
    }
    default: {
        std::string out = "{";
        bool first = true;
        for (const auto& entry : v->object) { out += (first ? "'" : ", '") + entry.first + "': " + pyStr(&entry.second.resolved()); first = false; }
        return out + "}";
    }
    }
}
// '%s' % raw.get(key, '').
std::string strOr(const Value& row, const char* key) {
    const Value* v = member(row, key);
    return v ? pyStr(v) : std::string();
}

const std::vector<Value>& rows(const Battle& b, const char* key) {
    static const std::vector<Value> none;
    const Value* v = member(b.start, key);
    return v && v->kind == Kind::Array ? v->array : none;
}

// ---------------------------------------------------------------------------------------------- vehicle records

struct Profile {
    std::string name;
    std::int64_t level = 1;
    std::set<std::string> tags;
    bool tagged(const char* tag) const { return tags.count(tag) != 0; }
};

std::set<std::string> texts(PyObject* iterable) {
    std::set<std::string> out;
    if (!p::flag(iterable)) return out;
    p::Ref iterator(PyObject_GetIter(iterable));
    while (PyObject* raw = PyIter_Next(iterator.get())) {
        p::Ref item(raw);
        if (PyString_Check(item.get()) || PyUnicode_Check(item.get())) out.insert(p::string(item.get()));
    }
    if (PyErr_Occurred()) throw p::Error("vehicle record iteration failed");
    return out;
}

// _vehicle_profile(entry) for a #1513 vehicle item or descriptor type.
Profile profileOf(PyObject* entry) {
    Profile out;
    auto name = py::optional(entry, "name");
    if (name.get() != Py_None) out.name = p::string(p::Ref(PyObject_Str(name.get())).get());
    auto level = py::optional(entry, "level");
    if (p::flag(level.get())) out.level = p::wide(p::Ref(PyNumber_Int(level.get())).get());
    auto tags = py::optional(entry, "tags");
    out.tags = texts(tags.get());
    return out;
}

const char* const kMatchClasses[] = {"heavyTank", "mediumTank", "AT-SPG", "lightTank", "SPG"};

// planner.vehicle_match_class.
std::string matchClass(const Profile& v) {
    for (const char* tag : kMatchClasses) if (v.tagged(tag)) return tag;
    return "mediumTank";
}
// BattleRuntime._vehicle_class_order.
int classOrder(const Profile& v) {
    for (int i = 0; i < 5; ++i) if (v.tagged(kMatchClasses[i])) return i;
    return 1;
}
bool sameSlot(const Profile& a, const Profile& b) { return a.level == b.level && matchClass(a) == matchClass(b); }

p::Ref importModule(const char* name) {
    PyObject* loaded = PyDict_GetItemString(PyImport_GetModuleDict(), name);
    return loaded ? p::retain(loaded) : p::Ref(PyImport_ImportModule(name));
}

// The battle-eligibility rules of vehicle_configuration and the baked vehicle_blacklist catalogue.
struct Eligibility {
    std::set<std::string> retired, unusable, non_standard_tags, clone_suffixes, entity_suffixes, entity_names;
    std::string visibility_tag;

    Eligibility() {
        auto configuration = importModule("gui.mods.offline_lan_0922.vehicle_configuration");
        auto blacklist = importModule("gui.mods.offline_lan_0922.vehicle_blacklist");
        retired = texts(py::attr(configuration.get(), "RETIRED_BOT_VEHICLES").get());
        unusable = texts(py::attr(blacklist.get(), "UNUSABLE_VEHICLES").get());
        non_standard_tags = texts(py::attr(configuration.get(), "NON_STANDARD_BATTLE_TAGS").get());
        clone_suffixes = texts(py::attr(configuration.get(), "CLONE_NAME_SUFFIXES").get());
        entity_suffixes = texts(py::attr(configuration.get(), "NON_BATTLE_ENTITY_SUFFIXES").get());
        entity_names = texts(py::attr(configuration.get(), "NON_BATTLE_ENTITY_NAMES").get());
        visibility_tag = p::string(py::attr(configuration.get(), "CATALOGUE_VISIBILITY_TAG").get());
    }
    static bool endsWith(const std::string& name, const std::set<std::string>& suffixes) {
        for (const auto& s : suffixes)
            if (name.size() >= s.size() && name.compare(name.size() - s.size(), s.size(), s) == 0) return true;
        return false;
    }
    // vehicle_configuration.is_standard_battle_vehicle.
    bool standard(const Profile& v) const {
        if (v.name.empty()) return false;
        for (const auto& tag : v.tags) if (non_standard_tags.count(tag)) return false;
        if (!v.tags.count(visibility_tag)) return true;
        return !endsWith(v.name, clone_suffixes) && !entity_names.count(v.name) && !endsWith(v.name, entity_suffixes);
    }
    // BattleRuntime._vehicle_excluded.
    bool excluded(const Profile& v) const { return unusable.count(v.name) || retired.count(v.name) || !standard(v); }
};

// ---------------------------------------------------------------------------------------------- ai/planner.py

using Tiers = std::vector<std::int64_t>;
const char* const kTierModes[] = {"random", "same", "minus1_0", "0_plus1", "minus1_plus1", "minus2_0", "0_plus2"};

std::string normalizeTierMode(const Value* v) {
    if (v && v->kind == Kind::Text)
        for (const char* mode : kTierModes) if (v->text == mode) return mode;
    return "random";
}

Tiers chooseMatchTiers(std::int64_t player_tier, double mode_roll, double side_roll, const std::set<std::int64_t>& available_tiers,
                       const Tiers& required_tiers) {
    player_tier = std::max<std::int64_t>(1, std::min<std::int64_t>(10, player_tier));
    std::set<std::int64_t> available, required;
    for (auto v : available_tiers) if (v >= 1 && v <= 10) available.insert(v);
    for (auto v : required_tiers) if (v >= 1 && v <= 10) required.insert(v);
    required.insert(player_tier);
    available.insert(required.begin(), required.end());
    const std::int64_t low_required = *required.begin(), high_required = *required.rbegin();
    if (high_required - low_required > 2) return Tiers(required.begin(), required.end());
    std::int64_t preferred = mode_roll < 0.28 ? 1 : (mode_roll < 0.72 ? 2 : 3);
    const std::int64_t minimum = high_required - low_required + 1;
    preferred = std::max(preferred, minimum);
    for (std::int64_t width = preferred; width >= minimum; --width) {
        std::vector<Tiers> windows;
        for (std::int64_t low = std::max<std::int64_t>(1, player_tier - width + 1); low <= std::min(player_tier, 11 - width); ++low) {
            bool fits = low <= low_required && high_required < low + width;
            for (std::int64_t t = low; fits && t < low + width; ++t) fits = available.count(t) != 0;
            if (!fits) continue;
            Tiers window;
            for (std::int64_t t = low; t < low + width; ++t) window.push_back(t);
            windows.push_back(window);
        }
        if (!windows.empty()) {
            const double side = std::max(0., std::min(1., side_roll));
            return windows[std::min(windows.size() - 1, std::size_t(side * double(windows.size())))];
        }
    }
    return Tiers(required.begin(), required.end());
}

Tiers botMatchTiers(std::int64_t player_tier, const std::string& mode, double mode_roll, double side_roll, const Tiers& available_tiers,
                    const Tiers& required_tiers) {
    player_tier = std::max<std::int64_t>(1, std::min<std::int64_t>(10, player_tier));
    std::set<std::int64_t> available;
    if (available_tiers.empty()) for (std::int64_t t = 1; t <= 10; ++t) available.insert(t);
    for (auto v : available_tiers) if (v >= 1 && v <= 10) available.insert(v);
    available.insert(player_tier);
    if (mode == "random") return chooseMatchTiers(player_tier, mode_roll, side_roll, available, required_tiers);
    const std::int64_t t = player_tier;
    Tiers desired;
    if (mode == "same") desired = {t};
    else if (mode == "minus2_0") desired = {t - 2, t - 1, t};
    else if (mode == "0_plus2") desired = {t, t + 1, t + 2};
    else if (mode == "minus1_0") desired = {t - 1, t};
    else if (mode == "0_plus1") desired = {t, t + 1};
    else desired = {t - 1, t, t + 1};
    Tiers out;
    for (auto v : desired) if (available.count(v)) out.push_back(v);
    return out;
}

bool inBotTierMode(std::int64_t player_tier, std::int64_t candidate_tier, const std::string& mode) {
    if (mode == "random") return std::llabs(candidate_tier - player_tier) <= 2;
    const auto tiers = botMatchTiers(player_tier, mode, 0.5, 0.5, Tiers{1, 2, 3, 4, 5, 6, 7, 8, 9, 10}, Tiers{});
    return std::find(tiers.begin(), tiers.end(), candidate_tier) != tiers.end();
}

std::vector<Profile> sharedHumanRequirements(const std::array<std::vector<Profile>, 2>& teams) {
    std::map<std::pair<std::int64_t, std::string>, const Profile*> representatives;
    std::map<std::pair<std::int64_t, std::string>, std::size_t> required;
    for (const auto& profiles : teams) {
        std::map<std::pair<std::int64_t, std::string>, std::size_t> counts;
        for (const auto& profile : profiles) {
            const auto key = std::make_pair(profile.level, matchClass(profile));
            ++counts[key];
            representatives.emplace(key, &profile);
        }
        for (const auto& entry : counts) required[entry.first] = std::max(required[entry.first], entry.second);
    }
    std::vector<Profile> out;
    for (const auto& entry : required)
        for (std::size_t i = 0; i < entry.second; ++i) out.push_back(*representatives[entry.first]);
    return out;
}

// 9.18 priorities: low/middle/top 7/5/3, or low/top 10/5 at 15, scaled by largest remainder.
Tiers matchTierSlots(const Tiers& levels, std::int64_t team_size) {
    if (levels.empty() || team_size <= 0) return {};
    std::vector<std::int64_t> weights;
    if (levels.size() == 1) weights = {15};
    else if (levels.size() == 2) weights = {10, 5};
    else if (levels.size() == 3) weights = {7, 5, 3};
    else weights.assign(levels.size(), 1);
    std::int64_t total = 0;
    for (auto w : weights) total += w;
    std::vector<std::int64_t> counts;
    std::int64_t assigned = 0;
    for (auto w : weights) { counts.push_back(team_size * w / total); assigned += counts.back(); }
    std::vector<std::size_t> order(levels.size());
    for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
        return std::make_pair(-(team_size * weights[a] % total), a) < std::make_pair(-(team_size * weights[b] % total), b);
    });
    for (std::size_t i = 0; i < order.size() && std::int64_t(i) < team_size - assigned; ++i) ++counts[order[i]];
    Tiers out;
    for (std::size_t i = 0; i < levels.size(); ++i) out.insert(out.end(), std::size_t(counts[i]), levels[i]);
    return out;
}

bool contains(const Tiers& tiers, std::int64_t level) { return std::find(tiers.begin(), tiers.end(), level) != tiers.end(); }

// Shares tier/class slots and a feasible 0..3 SPG quota between the teams.
std::vector<Profile> buildMatchTemplate(const std::vector<Profile>& pool, std::int64_t team_size, const Profile& player, const Tiers& allowed_tiers,
                                        Random& rng, const std::vector<Profile>& required_profiles) {
    team_size = std::max<std::int64_t>(1, team_size);
    const std::set<std::int64_t> unique(allowed_tiers.begin(), allowed_tiers.end());
    Tiers allowed(unique.begin(), unique.end());
    if (allowed.empty()) allowed = {player.level};
    std::vector<Profile> usable;
    for (const auto& c : pool) if (contains(allowed, c.level)) usable.push_back(c);
    if (usable.empty()) usable = {player};
    std::vector<Profile> required = required_profiles.empty() ? std::vector<Profile>{player} : required_profiles;
    if (std::int64_t(required.size()) > team_size) required.resize(std::size_t(team_size));
    Tiers tier_slots = matchTierSlots(allowed, team_size);
    rng.shuffle(tier_slots);
    std::vector<Profile> result;
    std::map<std::string, std::int64_t> usage;
    auto take = [&](std::vector<Profile> choices) {
        rng.shuffle(choices);
        std::size_t best = 0;
        for (std::size_t i = 1; i < choices.size(); ++i)
            if (usage[choices[i].name] < usage[choices[best].name]) best = i;
        const Profile candidate = choices[best];
        ++usage[candidate.name];
        result.push_back(candidate);
        auto slot = std::find(tier_slots.begin(), tier_slots.end(), candidate.level);
        if (slot != tier_slots.end()) tier_slots.erase(slot);
        else if (!tier_slots.empty()) tier_slots.pop_back();
    };
    for (const auto& profile : required) {
        std::vector<Profile> choices;
        for (const auto& c : usable) if (sameSlot(c, profile)) choices.push_back(c);
        take(choices.empty() ? std::vector<Profile>{profile} : choices);
    }
    std::int64_t spg_count = 0;
    for (const auto& v : result) if (matchClass(v) == "SPG") ++spg_count;
    const std::int64_t spg_max = std::min<std::int64_t>(3, spg_count + team_size - std::int64_t(result.size()));
    // Uniform 0..3 is a synthetic-room choice, not an official spawn rate.
    std::int64_t quota = spg_count + std::int64_t(rng.random() * double(std::max<std::int64_t>(0, spg_max - spg_count) + 1));
    quota = std::min(spg_max, quota);
    while (spg_count < quota) {
        std::vector<Profile> choices;
        for (const auto& c : usable) if (matchClass(c) == "SPG" && contains(tier_slots, c.level)) choices.push_back(c);
        if (choices.empty()) break;
        take(choices);
        ++spg_count;
    }
    static const char* const regular_classes[] = {"heavyTank", "mediumTank", "AT-SPG", "lightTank"};
    std::vector<Profile> regular;
    for (const auto& c : usable) if (matchClass(c) != "SPG") regular.push_back(c);
    while (std::int64_t(result.size()) < team_size) {
        const std::int64_t level = tier_slots.empty() ? allowed[0] : tier_slots.back();
        const std::string role = regular_classes[result.size() % 4];
        std::vector<Profile> choices, preferred;
        for (const auto& c : regular) if (c.level == level) choices.push_back(c);
        for (const auto& c : choices) if (matchClass(c) == role) preferred.push_back(c);
        if (!preferred.empty()) choices = preferred;
        else if (choices.empty()) choices = regular;
        if (choices.empty()) {
            // Do not fill an impossible catalogue with 15 SPGs.
            if (spg_count >= 3) break;
            choices = usable;
            ++spg_count;
        }
        take(choices);
    }
    return result;
}

// Removes the closest template slot for each human already on a team.
std::vector<Profile> remainingMatchTemplate(std::vector<Profile> remaining, const std::vector<Profile>& humans) {
    for (const auto& human : humans) {
        if (remaining.empty()) break;
        const std::string human_class = matchClass(human);
        std::size_t best_index = 0;
        std::int64_t best_score = -1;
        for (std::size_t i = 0; i < remaining.size(); ++i) {
            std::int64_t score = std::llabs(remaining[i].level - human.level) * 3;
            if (matchClass(remaining[i]) != human_class) score += 8;
            if (best_score < 0 || score < best_score) { best_score = score; best_index = i; }
        }
        remaining.erase(remaining.begin() + std::ptrdiff_t(best_index));
    }
    return remaining;
}

// Fills a team while enforcing an exact SPG cap; AT-SPG does not consume the artillery quota.
std::vector<Profile> selectBotLineup(const std::vector<Profile>& pool, std::int64_t count, std::int64_t spg_limit,
                                     const std::vector<Profile>& fallback) {
    count = std::max<std::int64_t>(0, count);
    spg_limit = std::max<std::int64_t>(0, std::min<std::int64_t>(3, spg_limit));
    if (pool.empty() || count <= 0) return {};
    std::vector<Profile> regular;
    std::set<std::string> seen;
    for (const auto* source : {&pool, &fallback})
        for (const auto& c : *source) {
            if (c.tagged("SPG") || seen.count(c.name)) continue;
            seen.insert(c.name);
            regular.push_back(c);
        }
    std::vector<Profile> result;
    std::int64_t spg_count = 0;
    std::size_t regular_index = 0;
    for (std::int64_t index = 0; index < count; ++index) {
        const Profile* candidate = &pool[std::size_t(index) % pool.size()];
        bool is_spg = candidate->tagged("SPG");
        if (is_spg && spg_count >= spg_limit) {
            if (regular.empty()) continue;
            candidate = &regular[regular_index++ % regular.size()];
            is_spg = false;
        }
        if (is_spg) ++spg_count;
        result.push_back(*candidate);
    }
    return result;
}

// ---------------------------------------------------------------------------------------------- the line-up

bool numericEquals(const Value* v, std::int64_t expected) {
    if (!v) return false;
    if (v->kind == Kind::Integer) return v->integer == expected;
    if (v->kind == Kind::Number) return v->number == double(expected);
    if (v->kind == Kind::Boolean) return (v->boolean ? 1 : 0) == expected;
    return false;
}

std::vector<Profile> catalogue(Battle& b, const Eligibility& rules) {
    std::vector<Profile> out;
    auto nations = py::attr(b.engine.modules.get(), "nations");
    auto names = py::attr(nations.get(), "AVAILABLE_NAMES");
    auto indices = py::attr(nations.get(), "INDICES");
    auto list = py::attr(b.engine.vehicles.get(), "g_list");
    p::Ref nation_iterator(PyObject_GetIter(names.get()));
    while (PyObject* raw_nation = PyIter_Next(nation_iterator.get())) {
        p::Ref nation(raw_nation);
        p::Ref nation_id(PyObject_GetItem(indices.get(), nation.get()));
        auto values = py::call(list.get(), "getList", nation_id.get());
        auto itervalues = py::optional(values.get(), "itervalues");
        auto entries = py::callable(itervalues) ? py::invoke(itervalues.get()) : py::call(values.get(), "values");
        p::Ref entry_iterator(PyObject_GetIter(entries.get()));
        while (PyObject* raw_entry = PyIter_Next(entry_iterator.get())) {
            p::Ref entry(raw_entry);
            auto profile = profileOf(entry.get());
            if (!rules.excluded(profile)) out.push_back(std::move(profile));
        }
        if (PyErr_Occurred()) throw p::Error("#1513 vehicle list iteration failed");
    }
    if (PyErr_Occurred()) throw p::Error("#1513 nation iteration failed");
    return out;
}

// The complete line-up, or nullopt when this catalogue cannot fill the roster.
std::optional<Table> lineup(Battle& b, PyObject* player_descriptor) {
    // Every process derives the same speculative line-up from the server roster, anchored to the
    // lowest server player id rather than its own selected tank.
    PyObject* planning_descriptor = player_descriptor;
    p::Ref anchor_descriptor = p::retain(Py_None);
    std::vector<std::pair<std::int64_t, const Value*>> server_players;
    for (const auto& raw : rows(b, "players")) {
        if (raw.resolved().kind != Kind::Object) continue;
        const auto id = pyInt(member(raw, "id"));
        if (!id || *id <= 0) continue;
        server_players.emplace_back(*id, &raw.resolved());
    }
    std::stable_sort(server_players.begin(), server_players.end(),
                     [](const std::pair<std::int64_t, const Value*>& a, const std::pair<std::int64_t, const Value*>& c) { return a.first < c.first; });
    if (!server_players.empty()) {
        const auto& anchor = server_players.front();
        const Value* vehicle = member(*anchor.second, "vehicle");
        if (truthy(vehicle) && !(!b.worker && anchor.first == b.local_player)) {
            replace(anchor_descriptor, startup::descriptorFor(b, vehicle->kind == Kind::Text ? vehicle->text : pyStr(vehicle)));
            planning_descriptor = anchor_descriptor.get();
        }
    }
    const Profile player_profile = profileOf(py::attr(planning_descriptor, "type").get());
    const std::int64_t tier = player_profile.level;
    const std::string tier_mode = normalizeTierMode(member(b.start, "bot_tier_mode"));
    std::set<std::string> excluded;
    for (const auto& name : rows(b, "bot_excluded_vehicles"))
        if (name.resolved().kind == Kind::Text) excluded.insert(name.resolved().text);

    const Eligibility rules;
    const std::vector<Profile> all_candidates = catalogue(b, rules);
    std::vector<Profile> candidates;
    for (const auto& c : all_candidates) if (inBotTierMode(tier, c.level, tier_mode)) candidates.push_back(c);
    if (candidates.empty()) return std::nullopt;
    std::vector<Profile> automatic;
    for (const auto& c : candidates) if (!excluded.count(c.name)) automatic.push_back(c);
    // A fully excluded pool keeps the template shape for explicit slot overrides only.
    if (!automatic.empty()) candidates = automatic;
    std::stable_sort(candidates.begin(), candidates.end(), [](const Profile& a, const Profile& c) {
        return std::make_tuple(a.level, classOrder(a), a.name) < std::make_tuple(c.level, classOrder(c), c.name);
    });

    std::string seed_players, seed_bots;
    for (const auto& player : server_players) {
        if (!seed_players.empty()) seed_players += ";";
        seed_players += std::to_string(player.first) + "," + strOr(*player.second, "team") + "," + strOr(*player.second, "slot") + "," +
                        strOr(*player.second, "vehicle");
    }
    std::vector<std::pair<std::tuple<std::int64_t, std::int64_t, std::int64_t>, const Value*>> sorted_bots;
    for (const auto& raw : rows(b, "bots"))
        if (raw.resolved().kind == Kind::Object)
            sorted_bots.emplace_back(std::make_tuple(intOr(raw, "team", 0), intOr(raw, "slot", 0), intOr(raw, "id", 0)), &raw.resolved());
    std::stable_sort(sorted_bots.begin(), sorted_bots.end(),
                     [](const decltype(sorted_bots)::value_type& a, const decltype(sorted_bots)::value_type& c) { return a.first < c.first; });
    for (const auto& bot : sorted_bots) {
        if (!seed_bots.empty()) seed_bots += ";";
        seed_bots += strOr(*bot.second, "id") + "," + strOr(*bot.second, "team") + "," + strOr(*bot.second, "slot");
    }
    Random rng(stableSeed("battle-lineup-v1|" + pyStr(member(b.start, "round_id")) + "|" + pyStr(member(b.start, "map")) + "|" + seed_players +
                          "|" + seed_bots));

    std::array<std::vector<const Value*>, 2> bots_by_team;
    for (std::int64_t team = 1; team <= 2; ++team) {
        auto& team_bots = bots_by_team[std::size_t(team - 1)];
        for (const auto& raw : rows(b, "bots"))
            if (raw.resolved().kind == Kind::Object && intOr(raw, "team", 0) == team) team_bots.push_back(&raw.resolved());
        std::stable_sort(team_bots.begin(), team_bots.end(),
                         [](const Value* a, const Value* c) { return intOr(*a, "slot", 0) < intOr(*c, "slot", 0); });
    }
    std::array<std::vector<Profile>, 2> humans;
    for (const auto& raw : rows(b, "players")) {
        if (raw.resolved().kind != Kind::Object) continue;
        const Value* id = member(raw, "id");
        if (b.worker) {
            const auto number = pyInt(id);
            if (!number || *number <= 0) continue;
        }
        const Value* team_value = member(raw, "team");
        const std::int64_t team = truthy(team_value) ? needInt(team_value, "team") : 0;
        if (team != 1 && team != 2) continue;
        try {
            if (!b.worker && numericEquals(id, b.local_player)) {
                humans[std::size_t(team - 1)].push_back(profileOf(py::attr(player_descriptor, "type").get()));
            } else {
                const Value* vehicle = member(raw, "vehicle");
                auto descriptor = startup::descriptorFor(b, vehicle && vehicle->kind == Kind::Text ? vehicle->text : std::string());
                humans[std::size_t(team - 1)].push_back(profileOf(py::attr(descriptor.get(), "type").get()));
            }
        } catch (const std::exception& e) {
            describe(e);   // an unresolvable human tank leaves its slot to the template
        }
    }
    if (humans[0].empty() && humans[1].empty()) {
        auto team = py::optional(b.engine.client.get(), "team");
        const std::int64_t own_team = p::flag(team.get()) ? p::wide(p::Ref(PyNumber_Int(team.get())).get()) : 1;
        humans[own_team != 2 ? 0 : 1].push_back(player_profile);
    }

    Tiers available_tiers, required_tiers;
    {
        std::set<std::int64_t> levels;
        for (const auto& c : candidates) levels.insert(c.level);
        available_tiers.assign(levels.begin(), levels.end());
    }
    for (const auto& profiles : humans) for (const auto& profile : profiles) required_tiers.push_back(profile.level);
    const double mode_roll = rng.random();
    const double side_roll = rng.random();
    Tiers match_tiers = botMatchTiers(tier, tier_mode, mode_roll, side_roll, available_tiers, required_tiers);
    for (const auto& profiles : humans)
        for (const auto& profile : profiles) {
            if (!contains(match_tiers, profile.level)) match_tiers.push_back(profile.level);
            if (std::none_of(candidates.begin(), candidates.end(), [&](const Profile& c) { return sameSlot(c, profile); }))
                candidates.push_back(profile);
        }
    {
        std::set<std::int64_t> unique(match_tiers.begin(), match_tiers.end());
        match_tiers.assign(unique.begin(), unique.end());
    }
    std::int64_t team_size = 0;
    for (std::size_t team = 0; team < 2; ++team)
        team_size = std::max(team_size, std::int64_t(humans[team].size() + bots_by_team[team].size()));
    const auto requirements = sharedHumanRequirements(humans);
    const auto match_template = buildMatchTemplate(candidates, team_size, player_profile, match_tiers, rng, requirements);
    std::set<std::string> validated;
    for (const auto& c : all_candidates) validated.insert(c.name);
    automatic.clear();
    for (const auto& c : candidates)
        if (!excluded.count(c.name) && validated.count(c.name) && contains(match_tiers, c.level)) automatic.push_back(c);

    Table assignments;
    for (std::size_t team = 0; team < 2; ++team) {
        const auto& team_bots = bots_by_team[team];
        // Profile exclusions apply after removing human slots, so a reservation cannot put an edited tank back in a Bot.
        std::vector<Profile> picked;
        for (auto& entry : remainingMatchTemplate(match_template, humans[team]))
            if (!excluded.count(entry.name)) picked.push_back(std::move(entry));
        std::int64_t human_spgs = 0;
        for (const auto& human : humans[team]) if (matchClass(human) == "SPG") ++human_spgs;
        picked = selectBotLineup(picked.empty() ? automatic : picked, std::int64_t(team_bots.size()),
                                 std::max<std::int64_t>(0, 3 - human_spgs), automatic);
        if (picked.size() > team_bots.size()) picked.resize(team_bots.size());
        // The template balances roles and tiers; names come from the validated, non-excluded pool.
        std::vector<Profile> randomized;
        for (const auto& entry : picked) {
            std::vector<Profile> pool;
            for (const auto& c : automatic) if (sameSlot(c, entry)) pool.push_back(c);
            if (!pool.empty()) randomized.push_back(rng.choice(pool));
        }
        rng.shuffle(randomized);
        std::stable_sort(randomized.begin(), randomized.end(), [](const Profile& a, const Profile& c) { return classOrder(a) < classOrder(c); });
        for (std::size_t i = 0; i < team_bots.size() && i < randomized.size(); ++i)
            assignments[{std::int64_t(team + 1), intOr(*team_bots[i], "slot", 0)}] = randomized[i].name;
    }

    std::set<Slot> bot_slots;
    for (std::size_t team = 0; team < 2; ++team)
        for (const Value* raw : bots_by_team[team]) bot_slots.insert({std::int64_t(team + 1), intOr(*raw, "slot", 0)});
    for (const auto& raw : rows(b, "bot_lineup")) {
        if (raw.resolved().kind != Kind::Object) return std::nullopt;
        const auto team = pyInt(member(raw, "team"));
        const auto slot = pyInt(member(raw, "slot"));
        if (!team || !slot || (*team != 1 && *team != 2) || *slot < 0 || *slot >= 15) return std::nullopt;
        const Value* vehicle = member(raw, "vehicle");
        // A slot that pins only its Bot skill tier keeps the generated vehicle.
        if (!vehicle || vehicle->kind == Kind::Null) continue;
        if (vehicle->kind != Kind::Text || !validated.count(vehicle->text)) return std::nullopt;
        if (bot_slots.count({*team, *slot})) assignments[{*team, *slot}] = vehicle->text;
    }
    std::set<Slot> assigned;
    for (const auto& entry : assignments) assigned.insert(entry.first);
    if (assigned != bot_slots) return std::nullopt;
    return assignments;
}

BotAssignments& table(Battle& b) {
    if (!b.bots) b.bots = std::make_unique<BotAuthority>();
    if (!b.bots->assignments) b.bots->assignments = std::make_shared<BotAssignments>();
    return *b.bots->assignments;
}
}

bool prepareVehicleAssignments(Battle& b, PyObject* player_descriptor) {
    table(b).vehicles.clear();
    std::optional<Table> prepared;
    try {
        prepared = lineup(b, player_descriptor);
    } catch (const std::exception& e) {
        // Keep the actual preparation error instead of reporting a missing vehicle.
        log("Bot roster preparation failed: " + describe(e));
    }
    if (!prepared) return false;
    table(b).vehicles = std::move(*prepared);
    return true;
}

const std::map<std::pair<std::int64_t, std::int64_t>, std::string>& vehicleAssignments(const Battle& b) {
    static const std::map<std::pair<std::int64_t, std::int64_t>, std::string> none;
    return b.bots && b.bots->assignments ? b.bots->assignments->vehicles : none;
}
}
}
