#include "room_rules_achievements.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <map>
#include <set>
#include <tuple>

namespace wot::room {
namespace {
using ID = std::int64_t;
using Kind = Value::Kind;
using Identity = std::pair<std::string, ID>;
ID integral(const Value &raw, ID fallback = 0, bool allow_bool = false) {
  const auto &value = raw.resolved();
  if (value.kind == Kind::Integer && !value.unsigned_integer)
    return value.integer;
  if (value.kind == Kind::Boolean)
    return allow_bool ? ID(value.boolean) : fallback;
  if (value.kind == Kind::Number && std::isfinite(value.number) &&
      value.number >= -9223372036854775808. &&
      value.number < 9223372036854775808.)
    return ID(value.number);
  if (value.kind == Kind::Text)
    try {
      std::size_t end = 0;
      const auto n = std::stoll(value.text, &end);
      while (end < value.text.size() &&
             std::isspace(static_cast<unsigned char>(value.text[end])))
        ++end;
      if (end == value.text.size())
        return n;
    } catch (...) {
    }
  return fallback;
}
double real(const Value &raw, double fallback = 0.) {
  const auto &value = raw.resolved();
  if (value.kind == Kind::Boolean)
    return fallback;
  if (value.kind == Kind::Integer || value.kind == Kind::Number)
    return value.numeric(fallback);
  if (value.kind == Kind::Text)
    try {
      std::size_t end = 0;
      const auto n = std::stod(value.text, &end);
      while (end < value.text.size() &&
             std::isspace(static_cast<unsigned char>(value.text[end])))
        ++end;
      if (end == value.text.size() && std::isfinite(n))
        return n;
    } catch (...) {
    }
  return fallback;
}
ID val(const Value &actor, const char *key) { return integral(actor.get(key)); }
bool yes(const Value &actor, const char *key) { return actor.get(key).truth(); }
std::string str(const Value &raw) {
  const auto &value = raw.resolved();
  if (value.kind == Kind::Text)
    return value.text;
  if (value.kind == Kind::Integer)
    return std::to_string(value.integer);
  if (value.kind == Kind::Boolean)
    return value.boolean ? "True" : "False";
  if (value.kind == Kind::Null)
    return "None";
  throw std::invalid_argument("Unsupported settlement identity");
}
Identity identity(const Value &actor) {
  return {has(actor, "actor_kind") ? str(actor.get("actor_kind")) : "",
          val(actor, "actor_id")};
}
Identity victim(const Value &kill) {
  return {has(kill, "victim_kind") ? str(kill.get("victim_kind")) : "",
          val(kill, "victim_id")};
}
ID stat(const Value &actor, const char *key) {
  return std::max<ID>(0, integral(actor.get("stats").get(key)));
}
const std::vector<Value> &kills(const Value &actor) {
  return rows(actor.get("kills"));
}
bool direct(const std::string &name) {
  return name == "lightTank" || name == "mediumTank" || name == "heavyTank" ||
         name == "AT-SPG";
}
ID classKills(const Value &actor, const std::string &name) {
  ID total = 0;
  for (const auto &kill : kills(actor))
    if (kill.get("victim_class").resolved().text == name)
      ++total;
  return total;
}
ID tierKills(const Value &actor, ID delta, bool direct_fire) {
  const auto tier = val(actor, "tier");
  if (tier <= 0)
    return 0;
  ID total = 0;
  for (const auto &kill : kills(actor)) {
    const auto name = kill.get("victim_class").resolved().text;
    if (direct_fire ? !direct(name) : name != "SPG")
      continue;
    const auto other = val(kill, "victim_tier");
    if (other > 0 && other - tier >= delta)
      ++total;
  }
  return total;
}
std::set<Identity> sources(const Value &actor, const char *name) {
  std::set<Identity> result;
  for (const auto &row : rows(actor.get(name))) {
    const auto &values = rows(row);
    if (values.size() != 2)
      throw std::invalid_argument("Invalid settlement source identity");
    result.emplace(str(values[0]), integral(values[1]));
  }
  return result;
}
void add(Value &earned, const char *name, bool condition) {
  if (condition)
    earned.array.push_back(text(name));
}
struct Context {
  ID captured = 0;
  std::map<ID, ID> health;
  std::map<ID, std::map<std::string, ID>> classes;
};
std::optional<ID> hero(const std::string &name, const Value &actor,
                       const Context &context) {
  if (name == "warrior") {
    const auto n = stat(actor, "kills");
    if (n >= 6)
      return n;
  } else if (name == "invader") {
    const auto n = stat(actor, "capture_points");
    if (context.captured == val(actor, "team") && n >= 80)
      return n;
  } else if (name == "defender") {
    const auto n = stat(actor, "dropped_capture_points");
    if (n >= 70)
      return n;
  } else if (name == "steelwall") {
    const auto n = val(actor, "potential_damage_received");
    if (yes(actor, "survived") && n >= 1000 &&
        val(actor, "hits_received") >= 11)
      return n;
  } else if (name == "mainGun") {
    if (val(actor, "ally_hits"))
      return {};
    const auto n = stat(actor, "damage"),
               health = context.health.at(val(actor, "team"));
    if (n >= 1000 && health > 0 && double(n) >= .2 * double(health))
      return n;
  } else if (name == "supporter") {
    const auto n = val(actor, "support_targets");
    if (n >= 6)
      return n;
  } else if (name == "scout") {
    const auto n = stat(actor, "spotted");
    if (yes(actor, "won") && n >= 9)
      return n;
  } else if (name == "evileye") {
    const auto n = val(actor, "exclusive_spot_assists");
    if (n >= 6)
      return n;
  } else if (name == "sniper2") {
    if (actor.get("vehicle_class").resolved().text == "SPG" ||
        val(actor, "ally_hits"))
      return {};
    const auto shots = stat(actor, "shots"), hits = stat(actor, "direct_hits"),
               damage = val(actor, "sniper_damage");
    if (shots >= 8 && double(hits) / double(shots) >= .85 && hits > 0 &&
        double(val(actor, "hits_with_damage")) / double(hits) >= .8 &&
        damage >= 1000 && damage > val(actor, "max_health"))
      return damage;
  }
  return {};
}
void epic(Value &out, const Value &actor, const Context &context) {
  const auto vehicle_class = actor.get("vehicle_class").resolved().text;
  const auto tier = val(actor, "tier"), n = stat(actor, "kills");
  const bool survived = yes(actor, "survived"), won = yes(actor, "won");
  add(out, "heroesOfRassenay", n >= 14 && n <= 255);
  add(out, "medalLafayettePool", tier >= 5 && n >= 10 && n <= 13);
  add(out, "medalRadleyWalters", tier >= 5 && n >= 8 && n <= 9);
  if (vehicle_class == "lightTank") {
    const auto count = tierKills(actor, 1, true);
    add(out, "medalOrlik", count >= 2 && count <= 255);
  }
  if (vehicle_class == "mediumTank") {
    const auto count = tierKills(actor, 1, true);
    add(out, "medalOskin", count == 3);
    add(out, "medalNikolas", count >= 4 && count <= 255);
    add(out, "medalLehvaslaiho", count == 2);
  }
  if (vehicle_class == "AT-SPG") {
    const auto count = tierKills(actor, 2, true);
    add(out, "medalHalonen", count >= 2 && count <= 255);
  }
  if (vehicle_class != "SPG") {
    const auto count = classKills(actor, "SPG"),
               higher = tierKills(actor, 1, false);
    add(out, "medalBurda", higher >= 3 && higher <= 255);
    add(out, "medalPascucci", count == 2);
    add(out, "medalDumitru", count >= 3 && count <= 255);
  }
  if (vehicle_class == "lightTank" && survived) {
    const auto count = tierKills(actor, 1, false);
    add(out, "medalTamadaYoshio", count >= 2 && count <= 255);
  }
  const auto maximum = val(actor, "max_health");
  const double health_percent =
      maximum > 0 ? 100. * double(std::max<ID>(0, val(actor, "health"))) /
                        double(maximum)
                  : 100.;
  if (survived && won && val(actor, "crits_received") >= 5 &&
      health_percent <= 20.) {
    add(out, "medalBillotte", n == 2);
    add(out, "medalBrunoPietro", n >= 3 && n <= 4);
    add(out, "medalTarczay", n >= 5 && n <= 255);
  }
  add(out, "medalKolobanov", won && val(actor, "lone_stand_enemies") >= 5);
  ID defence = 0;
  for (const auto &kill : kills(actor))
    if (yes(kill, "defended_base"))
      ++defence;
  add(out, "medalDeLanglade", defence >= 4);
  const auto &classes = context.classes.at(val(actor, "team"));
  const auto countClass = [&](const std::string &name) {
    const auto at = classes.find(name);
    return at == classes.end() ? ID(0) : at->second;
  };
  const auto light = countClass("lightTank");
  add(out, "huntsman", light >= 3 && classKills(actor, "lightTank") >= light);
  add(out, "ironMan", val(actor, "best_deflection_streak") >= 10);
  add(out, "luckyDevil", yes(actor, "lucky_devil"));
  add(out, "medalFadin", yes(actor, "last_shell_finisher"));
  if (vehicle_class == "SPG") {
    const bool clean = !val(actor, "team_kills");
    const auto damage = stat(actor, "damage");
    add(out, "medalGore",
        clean && damage >= 2000 && maximum > 0 && damage >= 8 * maximum);
    bool monolith = false;
    ID close = 0;
    for (const auto &kill : kills(actor)) {
      if (val(kill, "death_reason") == 2 &&
          real(kill.get("actor_speed"), -1.) >= 0. &&
          real(kill.get("actor_speed")) <= 3.0555555555555554 &&
          real(kill.get("victim_speed"), -1.) > real(kill.get("actor_speed")))
        monolith = true;
      if (kill.get("victim_class").resolved().text == "lightTank" &&
          real(kill.get("distance"), -1.) >= 0. &&
          real(kill.get("distance")) <= 100.)
        ++close;
    }
    add(out, "medalMonolith", clean && survived && monolith);
    const auto absorbed =
        val(actor, "enemy_damage_received") + stat(actor, "damage_blocked");
    add(out, "medalStark",
        survived && n >= 2 && val(actor, "damaging_hits_received") >= 2 &&
            maximum > 0 && 3 * absorbed >= 2 * maximum);
    add(out, "medalCoolBlood", clean && tier >= 4 && close >= 2);
    const auto spgs = countClass("SPG");
    add(out, "medalAntiSpgFire",
        clean && spgs >= 2 && classKills(actor, "SPG") >= spgs);
  }
  add(out, "bombardier", val(actor, "best_multi_kill_shot") >= 2);
  bool rammed = false;
  for (const auto &kill : kills(actor))
    if (val(kill, "death_reason") == 2 && val(kill, "victim_tier") > 0 &&
        val(kill, "victim_tier") - tier >= 1)
      rammed = true;
  add(out, "kamikaze", tier > 0 && rammed);
  add(out, "sturdy",
      survived && val(actor, "deflected_hits_at_low_health") > 0);
  add(out, "raider",
      yes(actor, "captured_base") && yes(actor, "solo_capture") &&
          !yes(actor, "ever_spotted") &&
          context.captured == val(actor, "team"));
}
void approachable(Value &out, const Value &actor,
                  const std::vector<Value> &actors) {
  const auto maximum = val(actor, "max_health");
  if (maximum > 0) {
    add(out, "impenetrable",
        yes(actor, "survived") && stat(actor, "damage_blocked") > maximum);
    add(out, "shootToKill",
        actor.get("vehicle_class").resolved().text != "SPG" &&
            stat(actor, "damage") > maximum);
  }
  const auto n = stat(actor, "kills");
  add(out, "fighter", n >= 4 && n <= 5);
  const auto duelist = sources(actor, "duelist_sources");
  ID count = 0;
  bool ammo_rack = false, fire = false, even = false;
  const auto self = identity(actor);
  for (const auto &kill : kills(actor)) {
    const auto target = victim(kill);
    if (duelist.count(target))
      ++count;
    ammo_rack = ammo_rack || yes(kill, "ammo_rack");
    fire = fire || val(kill, "death_reason") == 1;
    const auto other =
        std::find_if(actors.begin(), actors.end(),
                     [&](const Value &v) { return identity(v) == target; });
    if (other != actors.end())
      for (const auto &back : kills(*other))
        if (victim(back) == self)
          even = true;
  }
  add(out, "duelist", count >= 2);
  add(out, "demolition", ammo_rack);
  add(out, "arsonist", fire);
  add(out, "bonecrusher", val(actor, "critical_hits") >= 5);
  add(out, "charmed",
      yes(actor, "won") && yes(actor, "survived") &&
          sources(actor, "damage_sources").size() >= 4);
  add(out, "aimer", yes(actor, "won") && stat(actor, "assist_radio") >= 1000);
  add(out, "even", even);
}
const std::map<std::string, std::array<int, 3>> medal_bonds{
    {"mainGun", {1, 1, 2}},
    {"supporter", {1, 1, 2}},
    {"steelwall", {1, 1, 2}},
    {"medalPascucci", {1, 1, 3}},
    {"warrior", {1, 1, 3}},
    {"sniper2", {1, 1, 3}},
    {"medalLehvaslaiho", {1, 1, 0}},
    {"evileye", {1, 3, 5}},
    {"medalOrlik", {1, 3, 0}},
    {"invader", {1, 3, 5}},
    {"defender", {1, 3, 5}},
    {"scout", {1, 3, 5}},
    {"medalDumitru", {3, 5, 7}},
    {"medalOskin", {3, 5, 0}},
    {"medalStark", {3, 5, 7}},
    {"huntsman", {3, 5, 7}},
    {"medalGore", {3, 5, 7}},
    {"medalTamadaYoshio", {3, 5, 0}},
    {"medalKolobanov", {5, 7, 10}},
    {"medalFadin", {5, 7, 10}},
    {"medalNikolas", {5, 7, 0}},
    {"medalBillotte", {5, 10, 10}},
    {"medalBurda", {5, 10, 0}},
    {"medalDeLanglade", {5, 10, 10}},
    {"medalBrunoPietro", {5, 10, 10}},
    {"medalTarczay", {5, 10, 10}},
    {"heroesOfRassenay", {5, 10, 15}},
    {"medalHalonen", {3, 5, 0}},
    {"medalRadleyWalters", {3, 5, 7}},
    {"medalLafayettePool", {5, 10, 10}}};
} // namespace

Value awardBattleAchievements(const Value &battle) {
  const auto &actors = rows(battle.get("actors"));
  auto awards = object();
  if (actors.empty())
    return awards;
  Context context;
  context.captured = val(battle, "base_captured_team");
  for (ID team : {1, 2}) {
    context.health[team] = 0;
    context.classes[team] = {};
    for (const auto &actor : actors)
      if (val(actor, "team") != team) {
        context.health[team] += std::max<ID>(0, val(actor, "max_health"));
        if (actor.get("vehicle_class").truth())
          ++context.classes[team][str(actor.get("vehicle_class"))];
      }
  }
  std::map<Identity, Value> values;
  for (const auto &actor : actors)
    values[identity(actor)] = array();
  for (const auto *name :
       {"warrior", "invader", "defender", "steelwall", "mainGun", "supporter",
        "scout", "evileye", "sniper2"}) {
    std::optional<std::tuple<ID, ID, bool, ID>> best;
    Identity winner;
    for (const auto &actor : actors) {
      const auto metric = hero(name, actor, context);
      if (!metric)
        continue;
      const auto who = identity(actor);
      const auto key = std::make_tuple(*metric, val(actor, "xp"),
                                       who.first == "player", -who.second);
      if (!best || key > *best) {
        best = key;
        winner = who;
      }
    }
    if (best)
      values[winner].array.push_back(text(name));
  }
  for (const auto &actor : actors) {
    auto &earned = values[identity(actor)];
    epic(earned, actor, context);
    approachable(earned, actor, actors);
  }
  for (auto &item : values)
    awards.object[item.first.first + ":" + std::to_string(item.first.second)] =
        std::move(item.second);
  return awards;
}
Value offlineRewards(const Value &statistics, bool won, bool participated,
                     int tier, int spotted_spgs, int capture_participants,
                     int killed_durability) {
  const auto value = [&](const char *name) {
    return std::max<ID>(0, integral(statistics.get(name), 0, true));
  };
  const auto damage = value("damage_dealt"),
             assist = value("damage_assisted_track") +
                      value("damage_assisted_radio") +
                      value("damage_assisted_stun");
  const auto spotted = value("spotted"), capture = value("capture_points"),
             dropped = value("dropped_capture_points");
  tier = std::max(1, std::min(10, tier));
  const auto spgs = std::max<ID>(0, std::min<ID>(spotted, spotted_spgs));
  const auto participants = std::max(0, capture_participants),
             durability = std::max(0, killed_durability);
  constexpr std::array<ID, 10> tier_scale{
      {1547, 1547, 1547, 1547, 1547, 1356, 1176, 1000, 763, 582}};
  const auto scale = tier_scale[std::size_t(tier - 1)];
  const auto terms = damage / 5 + assist / 10 + durability / 14;
  const auto base_xp = (participated ? 100 : 0) + terms * scale / 1000 +
                       spotted * 20 + capture * 2 + dropped * 2;
  auto xp = won ? base_xp * 150 / 100 : base_xp;
  const auto penalty_terms =
      (value("team_damage_penalized") * scale + 4999) / 5000 +
      (value("team_killed_durability") * scale + 13999) / 14000;
  const auto penalty = std::min(xp, penalty_terms * (won ? 150 : 100) / 100);
  xp -= penalty;
  ID participation_credits = participated ? 1000 * tier : 0;
  if (won)
    participation_credits = participation_credits * 185 / 100;
  const auto credits = participation_credits + damage * 2 +
                       100 * (spotted + spgs) +
                       (participants ? 1000 / participants : 0);
  auto result = object();
  result.object["credits"] = integer(credits);
  result.object["xp"] = integer(xp);
  result.object["free_xp"] = integer(xp * 5 / 100);
  result.object["repair_cost"] = integer(0);
  result.object["ammo_cost"] = integer(0);
  if (penalty)
    result.object["xp_penalty"] = integer(penalty);
  return result;
}
Value medalRewards(const Value &achievements, int tier) {
  auto result = object();
  if (tier < 4 || tier > 10)
    return result;
  for (const auto &raw : rows(achievements)) {
    const auto name = raw.resolved().text;
    const auto at = medal_bonds.find(name);
    if (at == medal_bonds.end())
      continue;
    if ((name == "medalRadleyWalters" || name == "medalLafayettePool") &&
        tier < 5)
      continue;
    if (name == "medalHalonen" && tier > 8)
      continue;
    const auto count = at->second[tier <= 7 ? 0 : tier <= 9 ? 1 : 2];
    if (count)
      result.object[name] = integer(count);
  }
  return result;
}
ID battleBondReward(ID base_xp, bool eligible) {
  if (!eligible || base_xp < 400)
    return 0;
  const double exponent = double(base_xp - 400) / 100., maximum = 2147483647.;
  if (exponent >= std::log(maximum / 4.) / std::log(1.18))
    return 2147483647;
  return ID(std::floor(4. * std::pow(1.18, exponent) + .5));
}
} // namespace wot::room
