#include "room_core.hpp"
#include "room_inputs.hpp"
#include "room_lobby_policy.hpp"
#include <algorithm>
#include <chrono>
#include <numeric>
namespace wot::room {
namespace {
using namespace lobby;
using K = Value::Kind;
void require(bool okay, const char *code) {
  if (!okay)
    throw Rejection(code);
}
bool connected(const Value &p) { return p.get("connected").truth(); }
Value values(const std::vector<std::string> &v) {
  Value out = array();
  for (const auto &s : v)
    out.array.push_back(text(s));
  return out;
}
int tier(const Value &catalog, const std::string &vehicle, int fallback = 1) {
  for (const auto &row : rows(catalog))
    if (string(row, "name") == vehicle)
      return static_cast<int>(intField(row, "level", fallback));
  return fallback;
}
std::size_t connectedCount(const State &s) {
  return static_cast<std::size_t>(
      std::count_if(s.players.begin(), s.players.end(),
                    [](const auto &p) { return connected(p.second); }));
}
Value classError(int team, const std::string &reason,
                 const std::vector<std::string> &classes) {
  Value v = object();
  v.object["team"] = integer(team);
  v.object["reason"] = text(reason);
  v.object["classes"] = values(classes);
  return v;
}
} // namespace
std::vector<int> Core::slots(int team) const {
  return lobby::slots(state_, team);
}
void Core::assignSlots(bool starting) {
  if (starting) {
    const auto plan = assignment(state_, random_);
    require(plan.has_value(), "team_full");
    for (const auto &a : *plan)
      spawn(state_.players.at(a.id), a.team, a.slot);
    return;
  }
  std::map<int, std::set<int>> occupied;
  for (auto &[id, p] : state_.players) {
    (void)id;
    if (!connected(p))
      continue;
    const int requested = static_cast<int>(intField(p, "requested_team"));
    if (!requested) {
      spawn(p, 0, -1);
      continue;
    }
    const auto active = slots(requested);
    int slot = static_cast<int>(intField(p, "slot", -1));
    if (intField(p, "team") != requested || occupied[requested].count(slot) ||
        std::find(active.begin(), active.end(), slot) == active.end()) {
      const auto it = std::find_if(active.begin(), active.end(), [&](int s) {
        return !occupied[requested].count(s);
      });
      if (it == active.end())
        continue;
      slot = *it;
    }
    occupied[requested].insert(slot);
    spawn(p, requested, slot);
  }
  for (int side : {1, 2})
    trim(state_, side);
}
void Core::makeRoster() {
  std::set<std::pair<int, int>> occupied;
  for (const auto &[id, p] : state_.players) {
    (void)id;
    if (connected(p) && intField(p, "team") > 0)
      occupied.emplace(static_cast<int>(intField(p, "team")),
                       static_cast<int>(intField(p, "slot")));
  }
  auto names = rows(state_.resources.get("lobby").get("callsigns"));
  std::shuffle(names.begin(), names.end(), random_);
  roster_ = array();
  for (int team : {1, 2})
    for (int slot : slots(team)) {
      if (occupied.count({team, slot}))
        continue;
      require(!names.empty(), "Bot callsign pool is empty");
      Value bot = object();
      bot.object["id"] = integer(slot + (team == 1 ? 1 : 16));
      bot.object["team"] = integer(team);
      bot.object["slot"] = integer(slot);
      bot.object["name"] = names.back();
      names.pop_back();
      roster_.array.push_back(std::move(bot));
    }
}
Value Core::admitPlayer(const Value &message, std::int64_t id,
                        const std::string &address) {
  int requested = 0;
  try {
    requested = requestedTeam(message.get("requested_team"), state_.resources);
  } catch (...) {
    throw Rejection("invalid_team");
  }
  std::string account;
  if (message.get("account_key").resolved().kind == K::Null) {
    Value source = message.get("name");
    if (!has(message, "name"))
      source = text("player");
    account = alnum(pythonString(source), state_.resources, 48);
    account = "legacy_" + (account.empty() ? "player" : truncate(account, 48));
  } else {
    require(message.get("account_key").resolved().kind == K::Text, "invalid_account_key");
    account = message.get("account_key").resolved().text;
  }
  require(!account.empty() && account.size() <= 64 &&
              std::all_of(account.begin(), account.end(),
                          [](unsigned char c) {
                            return (c >= '0' && c <= '9') ||
                                   (c >= 'a' && c <= 'z') ||
                                   (c >= 'A' && c <= 'Z') || c == '-' ||
                                   c == '_';
                          }),
          "invalid_account_key");
  for (const auto &[key, p] : state_.players) {
    (void)key;
    require(!connected(p) || string(p, "account_key") != account,
            "duplicate_account_key");
  }
  Value outfit, mark, descr;
  std::optional<Value> params;
  std::int64_t hp = 0;
  try {
    outfit = outfits(message.get("outfits"), state_.resources);
  } catch (...) {
    throw Rejection("invalid_outfits");
  }
  try {
    mark = marks(message.get("marks_on_gun"));
  } catch (...) {
    throw Rejection("invalid_marks_on_gun");
  }
  try {
    descr = descriptor(message.get("vehicle_compact_descr"));
  } catch (...) {
    throw Rejection("invalid_vehicle_configuration");
  }
  try {
    params = parameters_(message.get("effective_params"));
  } catch (...) {
    throw Rejection("invalid_effective_params");
  }
  require(params.has_value(), "invalid_effective_params");
  try {
    hp = strictInt(message.get("max_health"), 1, 100000);
  } catch (...) {
    throw Rejection("invalid_max_health");
  }
  const auto total = slots(1).size() + slots(2).size();
  require(connectedCount(state_) < std::max<std::size_t>(1, total),
          "team_full");
  int selected = requested;
  if (locked(state_) && state_.players.empty() && !total)
    selected = 0;
  std::set<int> occupied;
  for (const auto &[key, p] : state_.players) {
    (void)key;
    if (connected(p) && intField(p, "requested_team") == selected &&
        intField(p, "team") == selected)
      occupied.insert(static_cast<int>(intField(p, "slot")));
  }
  int slot = -1;
  if (selected) {
    const auto active = slots(selected);
    const auto available =
        std::find_if(active.begin(), active.end(),
                     [&](int n) { return !occupied.count(n); });
    require(available != active.end(), "team_full");
    slot = *available;
  }
  auto name = safe(message.get("name"), "Player" + std::to_string(id),
                   state_.resources);
  const auto folded = lower(name, state_.resources);
  if (folded == "defaultplayer" || folded == "player" ||
      folded == "offline_player") {
    auto tail = address.substr(
        address.rfind('.') == std::string::npos ? 0 : address.rfind('.') + 1);
    if (tail.empty() ||
        !std::all_of(tail.begin(), tail.end(),
                     [](unsigned char c) { return c >= '0' && c <= '9'; }))
      tail = std::to_string(id);
    name = "Player-" + tail;
  }
  std::set<std::string> existing;
  for (const auto &[key, p] : state_.players) {
    (void)key;
    if (connected(p))
      existing.insert(lower(string(p, "name"), state_.resources));
  }
  const auto base = name;
  int suffix = 2;
  while (existing.count(lower(name, state_.resources))) {
    const auto ending = "-" + std::to_string(suffix++);
    name =
        truncate(base, std::max<std::size_t>(1, 24 - ending.size())) + ending;
  }
  Value actor = state_.resources.get("public_player_defaults").resolved();
  actor.object["id"] = integer(id);
  actor.object["player_id"] = integer(id);
  actor.object["connected"] = boolean(true);
  actor.object["name"] = text(name);
  actor.object["account_key"] = text(account);
  actor.object["vehicle"] = text(
      safe(message.get("vehicle"), "ussr:R11_MS-1", state_.resources, true));
  actor.object["requested_team"] = integer(selected);
  spawn(actor, selected, slot);
  actor.object["health"] = integer(hp);
  actor.object["max_health"] = integer(hp);
  actor.object["display_health"] = {};
  actor.object["outfits"] = std::move(outfit);
  actor.object["marks_on_gun"] = std::move(mark);
  actor.object["vehicle_compact_descr"] = std::move(descr);
  actor.object["effective_params"] = std::move(*params);
  actor.object["critical"] = object();
  actor.object["hydraulic_body"] = {};
  actor.object["siege_transition_ticks"] = integer(0);
  actor.object["client_position"] = boolean(false);
  actor.object["capabilities"] = message.get("capabilities");
  return actor;
}
void Core::selectVehicle(Peer peer, Connection &connection,
                         const Value &message) {
  (void)peer;
  const auto found = state_.players.find(connection.sender.id);
  if (found == state_.players.end() || !connected(found->second) ||
      state_.phase != "waiting")
    return;
  try {
    auto &p = found->second;
    const auto hp = strictInt(message.get("max_health"), 1, 100000);
    const auto vehicle = safe(message.get("vehicle"), string(p, "vehicle"),
                              state_.resources, true);
    auto outfit = outfits(message.get("outfits"), state_.resources);
    auto mark = message.get("marks_on_gun").resolved().kind == K::Null
                    ? p.get("marks_on_gun")
                    : marks(message.get("marks_on_gun"));
    auto descr = descriptor(message.get("vehicle_compact_descr"));
    auto params = parameters_(message.get("effective_params"));
    if (!params)
      return;
    if (vehicle == string(p, "vehicle") && hp == intField(p, "max_health") &&
        outfit == p.get("outfits") && mark == p.get("marks_on_gun") &&
        descr == p.get("vehicle_compact_descr") &&
        *params == p.get("effective_params"))
      return;
    p.object["vehicle"] = text(vehicle);
    p.object["hydraulic_body"] = {};
    p.object["max_health"] = integer(hp);
    p.object["health"] = integer(hp);
    p.object["display_health"] = {};
    p.object["siege_state"] = integer(0);
    p.object["siege_transition_ticks"] = integer(0);
    p.object["siege_time_left_ms"] = integer(0);
    p.object["outfits"] = std::move(outfit);
    p.object["marks_on_gun"] = std::move(mark);
    p.object["vehicle_compact_descr"] = std::move(descr);
    p.object["effective_params"] = std::move(*params);
    ++state_.state_revision;
    broadcast(lobby());
  } catch (const std::exception &) {
  }
}
bool Core::lobbyCommand(Peer peer, Connection &connection,
                        const Value &message) {
  const auto type = string(message, "type");
  if (type == "descriptor_catalog") {
    const auto p = state_.players.find(connection.sender.id);
    if (p != state_.players.end() && connected(p->second))
      try {
        catalogs_[connection.sender.id] =
            catalog(message.get("vehicles"), state_.resources);
      } catch (const std::exception &) {
      }
    return true;
  }
  if (type == "select_vehicle") {
    selectVehicle(peer, connection, message);
    return true;
  }
  if (type != "select_team" && type != "set_team_size" &&
      type != "set_team_class" && type != "set_bot_skill_mode" &&
      type != "set_bot_tier_mode")
    return false;
  const auto found = state_.players.find(connection.sender.id);
  const bool waiting = found != state_.players.end() &&
                       connected(found->second) && state_.phase == "waiting";
  std::string code;
  bool changed = false;
  if (type == "select_team") {
    if (!waiting)
      code = "not_waiting";
    else {
      int requested = 0;
      try {
        requested = requestedTeam(message.get("team"), state_.resources);
      } catch (...) {
        code = "invalid_team";
      }
      if (code.empty()) {
        auto &p = found->second;
        if (requested != intField(p, "requested_team")) {
          if (!requested) {
            p.object["requested_team"] = integer(0);
            spawn(p, 0, -1);
            ++state_.state_revision;
            changed = true;
          } else {
            std::set<int> occupied;
            for (const auto &[id, other] : state_.players)
              if (id != connection.sender.id && connected(other) &&
                  intField(other, "requested_team") == requested &&
                  intField(other, "team") == requested)
                occupied.insert(static_cast<int>(intField(other, "slot")));
            const auto active = slots(requested);
            const auto it =
                std::find_if(active.begin(), active.end(),
                             [&](int slot) { return !occupied.count(slot); });
            if (it == active.end())
              code = "team_full";
            else {
              p.object["requested_team"] = integer(requested);
              spawn(p, requested, *it);
              for (int team : {1, 2})
                trim(state_, team);
              ++state_.state_revision;
              changed = true;
            }
          }
        } else
          changed = true;
      }
    }
  } else if (type == "set_team_size") {
    if (locked(state_))
      code = "host_only";
    else if (!waiting)
      code = "not_waiting";
    else if (connection.sender.id != host_player_)
      code = "host_only";
    else {
      int team = 0, size = 0;
      try {
        team = static_cast<int>(strictInt(message.get("team"), 1, 2));
      } catch (...) {
        code = "invalid_team";
      }
      if (code.empty())
        try {
          size = static_cast<int>(strictInt(message.get("size"), 1, 15));
        } catch (...) {
          code = "invalid_size";
        }
      if (code.empty()) {
        std::vector<std::int64_t> participants;
        for (const auto &[id, p] : state_.players)
          if (connected(p) && intField(p, "requested_team") == team)
            participants.push_back(id);
        std::sort(
            participants.begin(), participants.end(), [&](auto a, auto b) {
              return std::make_pair(intField(state_.players.at(a), "slot"), a) <
                     std::make_pair(intField(state_.players.at(b), "slot"), b);
            });
        if (participants.size() > static_cast<std::size_t>(size) ||
            connectedCount(state_) >
                static_cast<std::size_t>(size) + slots(3 - team).size())
          code = "team_occupied";
        else {
          const auto key = "team" + std::to_string(team) + "_size";
          if (intField(state_.config, key) != size) {
            for (std::size_t n = 0; n < participants.size(); ++n) {
              auto &p = state_.players.at(participants[n]);
              if (intField(p, "slot") != static_cast<std::int64_t>(n))
                spawn(p, team, static_cast<int>(n));
            }
            state_.config.object[key] = integer(size);
            trim(state_, team);
            makeRoster();
            ++state_.state_revision;
          }
          changed = true;
        }
      }
    }
  } else if (type == "set_team_class") {
    if (!locked(state_) && waiting && connection.sender.id == host_player_)
      try {
        const auto team = strictInt(message.get("team"), 1, 2);
        const auto tag = string(message, "class_tag");
        const auto &allowed = message.get("allowed").resolved();
        bool valid = tag == "random";
        for (const auto &v : rows(state_.resources.get("lobby").get("classes")))
          valid = valid || tag == v.text;
        if (valid && allowed.kind == K::Boolean) {
          auto &row =
              state_.config.object["team_classes"].object[std::to_string(team)];
          if (tag == "random") {
            row.object[tag] = allowed;
            if (!allowed.boolean && rows(row.get("order")).empty()) {
              row.object["order"] =
                  state_.resources.get("lobby").get("classes").resolved();
              for (const auto &v : rows(row.get("order")))
                row.object[v.text] = boolean(true);
            }
          } else {
            if (row.get("random").truth())
              return true;
            row.object[tag] = allowed;
            auto &order = row.object["order"].array;
            order.erase(
                std::remove_if(order.begin(), order.end(),
                               [&](const Value &v) { return v.text == tag; }),
                order.end());
            if (allowed.boolean)
              order.push_back(text(tag));
            if (order.empty())
              row.object["random"] = boolean(true);
          }
          trim(state_, static_cast<int>(team),
               tag != "random" && allowed.boolean ? tag : "");
          ++state_.state_revision;
          changed = true;
        }
      } catch (const std::exception &) {
      }
  } else {
    const auto field =
        type == "set_bot_skill_mode" ? "bot_skill_mode" : "bot_tier_mode";
    const auto supported =
        type == "set_bot_skill_mode" ? "skill_modes" : "tier_modes";
    if (locked(state_))
      code = "host_only";
    else if (!waiting)
      code = "not_waiting";
    else if (connection.sender.id != host_player_)
      code = "host_only";
    else {
      const auto &raw = message.get("mode");
      const auto &choices = rows(state_.resources.get("lobby").get(supported));
      if (raw.kind != K::Text ||
          std::none_of(choices.begin(), choices.end(),
                       [&](const Value &v) { return v == raw; }))
        code = "invalid_mode";
      else {
        if (state_.config.get(field) != raw) {
          state_.config.object[field] = raw;
          ++state_.state_revision;
        }
        changed = true;
      }
    }
  }
  if (changed) {
    broadcast(lobby());
    return true;
  }
  if (!code.empty()) {
    Value denied = object();
    denied.object["type"] =
        text(type == "select_team"          ? "team_denied"
             : type == "set_team_size"      ? "team_size_denied"
             : type == "set_bot_skill_mode" ? "bot_skill_mode_denied"
                                            : "bot_tier_mode_denied");
    denied.object["protocol"] = state_.resources.get("protocol");
    denied.object["round_id"] = integer(state_.round_id);
    denied.object["state_revision"] = integer(state_.state_revision);
    denied.object["code"] = text(code);
    if (type == "select_team" || type == "set_team_size") {
      denied.object["team"] = message.get("team");
      if (type == "set_team_size")
        denied.object["size"] = message.get("size");
      denied.object["team_sizes"] = teamSizes();
      denied.object["lineup_locked"] = boolean(locked(state_));
      denied.object["team_classes"] = state_.config.get("team_classes");
      denied.object["team_class_notice"] =
          state_.config.get("team_class_notice");
    } else {
      denied.object["mode"] = message.get("mode");
      const auto key =
          type == "set_bot_skill_mode" ? "bot_skill_mode" : "bot_tier_mode";
      denied.object[key] = state_.config.get(key);
    }
    emit(peer, std::move(denied));
  }
  return true;
}
void Core::start(Peer peer, const Value &message) {
  const auto connection = peers_.find(peer);
  if (connection == peers_.end())
    return;
  const auto id = connection->second.sender.id;
  const auto deny = [&](const std::string &code) {
    Value out = object();
    out.object["type"] = text("start_denied");
    out.object["protocol"] = state_.resources.get("protocol");
    out.object["round_id"] = integer(state_.round_id);
    out.object["state_revision"] = integer(state_.state_revision);
    out.object["code"] = text(code);
    out.object["class_error"] = state_.config.get("last_bot_class_error");
    out.object["players"] =
        integer(static_cast<std::int64_t>(state_.players.size()));
    emit(peer, std::move(out));
    broadcast(lobby());
  };
  const auto player = state_.players.find(id);
  if (player == state_.players.end() || !connected(player->second)) {
    deny("player_not_found");
    return;
  }
  if (state_.phase != "waiting") {
    deny("already_started");
    return;
  }
  if (id != host_player_) {
    deny("host_only");
    return;
  }
  const auto mode =
      has(message, "battle_mode") ? string(message, "battle_mode") : "regular";
  const auto training = has(message, "training_bots")
                            ? message.get("training_bots")
                            : boolean(false);
  if ((mode != "regular" && mode != "training") ||
      training.kind != K::Boolean) {
    deny("invalid_battle_mode");
    return;
  }
  if (locked(state_) && (slots(1).empty() || slots(2).empty())) {
    deny("empty_lineup_team");
    return;
  }
  std::int64_t duration = 900;
  try {
    if (message.get("round_seconds").resolved().kind != K::Null)
      duration = strictInt(message.get("round_seconds"), 60, 14400);
  } catch (...) {
    deny("invalid_round_length");
    return;
  }
  if (!worker_ready_) {
    deny("simulation_worker_required");
    return;
  }
  const std::vector<std::pair<std::string, std::string>> capabilities = {
      {"projectile_ledger_v2", "missing_projectile_capability"},
      {"human_ram_timeline_v1", "missing_human_ram_timeline_capability"},
      {"ram_contact_ledger_v2", "missing_ram_contact_ledger_capability"},
      {"player_projectile_owner_v1",
       "missing_player_projectile_owner_capability"},
      {"player_environment_v2", "missing_player_environment_capability"},
      {"effective_params_v1", "missing_effective_params_capability"},
      {"ricochet_continuation_v1", "missing_ricochet_continuation_capability"}};
  for (const auto &[cap, code] : capabilities) {
    bool missing = false;
    for (const auto &[key, conn] : peers_) {
      (void)key;
      if (conn.probe || (conn.sender.worker && cap == "projectile_ledger_v2"))
        continue;
      missing = missing || !conn.capabilities.count(cap);
    }
    if (missing) {
      deny(code);
      return;
    }
  }
  std::map<std::int64_t, Value> canonical;
  for (const auto &[actor_id, p] : state_.players)
    if (connected(p)) {
      std::optional<Value> params;
      try {
        params = parameters_(p.get("effective_params"));
      } catch (...) {
      }
      if (!params) {
        deny("invalid_effective_params");
        return;
      }
      canonical[actor_id] = std::move(*params);
    }
  const auto found_catalog = catalogs_.find(host_player_);
  const Value host_catalog =
      found_catalog == catalogs_.end() ? array() : found_catalog->second;
  auto allowed = allowedNames(state_, host_catalog);
  for (const auto &row : rows(state_.config.get("bot_lineup")))
    if (has(row, "vehicle") && !allowed.count(string(row, "vehicle"))) {
      deny("invalid_bot_lineup");
      return;
    }
  const auto plan = assignment(state_, random_);
  if (!plan) {
    deny("team_full");
    return;
  }
  state_.config.object["last_bot_class_error"] = {};
  const bool trim1 = !rows(trim(state_, 1, "", &*plan)).empty(),
             trim2 = !rows(trim(state_, 2, "", &*plan)).empty();
  if (trim1 || trim2)
    ++state_.state_revision;
  if (mode == "regular" || training.boolean) {
    for (const auto &v : rows(state_.config.get("bot_excluded_vehicles")))
      allowed.erase(v.text);
    const int host_tier = tier(host_catalog, string(player->second, "vehicle"));
    const auto tier_mode = string(state_.config, "bot_tier_mode", "random");
    std::vector<Value> pool;
    std::set<std::string> pool_names;
    for (const auto &row : rows(host_catalog))
      if (allowed.count(string(row, "name")) &&
          inTier(host_tier, static_cast<int>(intField(row, "level", 1)),
                 tier_mode)) {
        pool.push_back(row);
        pool_names.insert(string(row, "name"));
      }
    std::set<std::pair<int, int>> occupied;
    for (const auto &a : *plan)
      occupied.emplace(a.team, a.slot);
    std::vector<std::set<int>> slot_tiers;
    for (const auto &filter : rows(state_.config.get("bot_lineup")))
      if (filter.get("enabled").truth() && !filter.get("vehicle").truth() &&
          !occupied.count({static_cast<int>(intField(filter, "team")),
                           static_cast<int>(intField(filter, "slot"))}) &&
          (filter.get("nation").truth() ||
           filter.get("vehicle_class").truth())) {
        std::set<int> levels;
        for (const auto &candidate : pool)
          if (matches(state_, filter, candidate))
            levels.insert(static_cast<int>(intField(candidate, "level", 1)));
        if (levels.empty()) {
          deny("invalid_bot_lineup");
          return;
        }
        slot_tiers.push_back(std::move(levels));
      }
    for (const auto &a : *plan) {
      const auto vehicle = string(state_.players.at(a.id), "vehicle");
      if (allowed.count(vehicle) && !pool_names.count(vehicle))
        for (const auto &row : rows(host_catalog))
          if (string(row, "name") == vehicle) {
            pool.push_back(row);
            pool_names.insert(vehicle);
            break;
          }
    }
    std::set<std::string> needed;
    std::vector<std::set<std::string>> groups;
    for (int side : {1, 2}) {
      const auto ctx = context(state_, side, &*plan);
      const auto &choices =
          state_.config.get("team_classes").get(std::to_string(side));
      const auto required_classes = required(choices, ctx.pinned);
      needed.insert(required_classes.begin(), required_classes.end());
      if (ctx.automatic && !choices.get("random").truth()) {
        std::set<std::string> group;
        for (const auto &tag :
             rows(state_.resources.get("lobby").get("classes")))
          if (choices.get(tag.text).truth())
            group.insert(tag.text);
        groups.push_back(std::move(group));
      }
    }
    std::set<int> humans, available;
    for (const auto &a : *plan)
      humans.insert(tier(
          host_catalog, string(state_.players.at(a.id), "vehicle"), host_tier));
    std::vector<std::pair<int, std::string>> candidates;
    for (const auto &v : pool) {
      const int level = static_cast<int>(intField(v, "level", 1));
      available.insert(level);
      candidates.emplace_back(level, vehicleClass(state_, v));
    }
    auto proposed = matchTiers(host_tier, tier_mode, available, humans);
    proposed.insert(proposed.end(), humans.begin(), humans.end());
    const auto tiers =
        matchingTiers(candidates, needed, humans, std::move(proposed),
                      tier_mode == "random", groups, slot_tiers);
    if (!tiers) {
      if (!slot_tiers.empty()) {
        deny("invalid_bot_lineup");
        return;
      }
      std::set<std::string> all_classes;
      for (const auto &v : pool)
        all_classes.insert(vehicleClass(state_, v));
      for (int team : {1, 2}) {
        const auto &row =
            state_.config.get("team_classes").get(std::to_string(team));
        if (row.get("random").truth())
          continue;
        const auto need = required(row, context(state_, team, &*plan).pinned);
        std::vector<std::string> missing;
        for (const auto &tag : need)
          if (!all_classes.count(tag))
            missing.push_back(tag);
        if (!missing.empty()) {
          state_.config.object["last_bot_class_error"] =
              classError(team, "missing_class", missing);
          deny("bot_class_pool_empty");
          return;
        }
      }
      int team = 1;
      if (state_.config.get("team_classes").get("1").get("random").truth())
        team = 2;
      state_.config.object["last_bot_class_error"] =
          classError(team, "tier_window", {needed.begin(), needed.end()});
      deny("bot_class_pool_empty");
      return;
    }
    for (int team : {1, 2}) {
      const auto &row =
          state_.config.get("team_classes").get(std::to_string(team));
      const auto ctx = context(state_, team, &*plan);
      if (row.get("random").truth() && rows(host_catalog).empty())
        continue;
      const int quota =
          artilleryLimit(static_cast<int>(slots(team).size()), ctx.human_spgs,
                         static_cast<int>(std::count(ctx.pinned.begin(),
                                                     ctx.pinned.end(), "SPG")),
                         row);
      std::set<std::string> classes;
      for (const auto &[level, tag] : candidates)
        if (std::find(tiers->begin(), tiers->end(), level) != tiers->end())
          classes.insert(tag);
      const auto reason = validate(row, ctx, classes, quota);
      if (!reason.empty()) {
        state_.config.object["last_bot_class_error"] =
            classError(team, reason, required(row, ctx.pinned));
        deny("bot_class_pool_empty");
        return;
      }
    }
  }
  auto map = state_.map_name;
  const auto &request_map = message.get("map");
  if (request_map.kind != K::Null &&
      !(request_map.kind == K::Text && request_map.text.empty())) {
    const auto requested = pythonString(request_map);
    const auto &pool = rows(state_.resources.get("lobby").get("map_pool"));
    if (requested == "server_random") {
      if (pool.empty()) {
        deny("invalid_map");
        return;
      }
      map = pool[std::uniform_int_distribution<std::size_t>(0, pool.size() -
                                                                   1)(random_)]
                .text;
    } else if (std::none_of(pool.begin(), pool.end(), [&](const Value &v) {
                 return v.text == requested;
               })) {
      deny("invalid_map");
      return;
    } else
      map = requested;
  }
  if (state_.resources.get("maps").get(map).get("tactics").resolved().kind != K::Object) {
    deny("invalid_bot_tactics");
    return;
  }
  state_.map_name = map;
  state_.config.object["battle_duration_seconds"] =
      number(static_cast<double>(duration));
  for (const auto &[actor_id, params] : canonical) {
    state_.players.at(actor_id).object["effective_params"] = params;
    if (!rules_.initializePlayerEquipment(actor_id)) {
      deny("invalid_player_critical_profile");
      return;
    }
  }
  state_.config.object["round_start_time"] =
      integer(std::chrono::duration_cast<std::chrono::seconds>(
                  std::chrono::system_clock::now().time_since_epoch())
                  .count());
  state_.config.object["battle_mode"] = text(mode);
  state_.config.object["training_bots"] = training;
  state_.config.object["bot_tactics"] =
      state_.resources.get("maps").get(map).get("tactics");
  planner_.reset();
  rules_.reset();
  inputs_->reset();
  for (const auto &a : *plan) {
    auto &p = state_.players.at(a.id);
    spawn(p, a.team, a.slot);
    p.object["participating"] = boolean(true);
    p.object["crew_receipt_id"] =
        text(string(state_.config, "receipt_namespace") + ":" +
             std::to_string(state_.round_id) + ":" + std::to_string(a.id));
    p.object["effective_params"] = canonical.at(a.id);
    const auto own = catalogs_.find(a.id);
    p.object["vehicle_tier"] = integer(tier(
        own == catalogs_.end() ? array() : own->second, string(p, "vehicle")));
    p.object["vehicle_class"] = text(vehicleClass(state_, p, false));
    p.object["equipment_intent_seq"] = integer(0);
    rules_.initializeActor("player", a.id);
  }
  if (mode == "regular" || training.boolean)
    makeRoster();
  else
    roster_ = array();
  state_.config.object["bot_roster"] = roster_;
  state_.config.object["roster_finalized"] = boolean(true);
  state_.phase = "loading";
  ++state_.state_revision;
  live_published_ = false;
  strategy_ready_ = false;
  battle_started_us_ = 0;
  prebattle_us_ = 0;
  for (auto &[key, conn] : peers_) {
    (void)key;
    conn.ready_round = 0;
    conn.manifest_sent = -1;
    conn.order_sent = -1;
    conn.destructible_sent = -1;
  }
  auto out = lobby("battle_start");
  for (const auto &key : {"map_pool"})
    out.object.erase(key);
  out.object["battle_mode"] = text(mode);
  out.object["training_bots"] = training;
  out.object["requested_by"] = integer(id);
  out.object["delay"] = number(.75);
  out.object["bots"] = roster_;
  for (const auto &key : {"bot_lineup", "bot_tactics", "bot_excluded_vehicles"})
    out.object[key] = state_.config.get(key);
  out.object["bot_manifest"] = state_.bot_manifest;
  out.object["bot_order_revision"] = integer(state_.bot_order_revision);
  out.object["bot_orders"] = state_.bot_orders;
  out.object["rules"] = state_.rules;
  out.object["battle_result"] = state_.battle_result;
  out.object["destructible_revision"] = integer(state_.destructible_revision);
  out.object["destructibles"] = state_.destructibles;
  out.object["destructible_base_revision"] = integer(0);
  out.object["detached_turrets"] = detached_turrets_;
  out.object["server_time_ms"] = integer(state_.server_time_ms);
  broadcast(std::move(out));
}
void Core::resetRound() {
  planner_.clearRadio("round_complete");
  state_.phase = "waiting";
  ++state_.round_id;
  state_.server_tick = 0;
  state_.server_time_ms = 0;
  const auto &maps = rows(state_.resources.get("lobby").get("map_pool"));
  state_.map_name = maps[std::uniform_int_distribution<std::size_t>(
                             0, maps.size() - 1)(random_)]
                        .text;
  for (auto &[id, p] : state_.players) {
    (void)id;
    p.object["health"] = p.get("max_health");
    p.object["alive"] = boolean(true);
    p.object["critical"] = object();
    p.object["critical_profile"] = {};
    p.object["display_health"] = {};
    for (const auto &key :
         {"critical_revision", "critical_base_revision", "critical_ack_seq",
          "equipment_revision", "equipment_intent_seq", "death_reason",
          "death_attacker_id", "stun_attacker_id", "stun_end_server_time_ms",
          "frags", "siege_state", "siege_transition_ticks",
          "siege_time_left_ms", "fire_seq", "shell_index", "next_shell_index",
          "landing_observation_seq", "landing_observation_input_seq"})
      p.object[key] = integer(0);
    for (const auto &key :
         {"equipment_clock", "rpm_damage_elapsed", "combat_fire_elapsed",
          "combat_fire_timer", "forward", "turn", "speed", "pitch", "roll",
          "gun_pitch"})
      p.object[key] = number(0.);
    p.object["up_cosine"] = number(1.);
    for (const auto &key :
         {"death_attacker_kind", "stun_attacker_kind", "fire_attacker_kind"})
      p.object[key] = text("");
    p.object["fire_attacker_id"] = integer(0);
    p.object["equipment_states"] = array();
    p.object["stun_factors"] = object();
    p.object["equipment_intent_result"] = object();
    p.object["equipment_intent_result"].object["intent_seq"] = integer(0);
    p.object["equipment_intent_result"].object["accepted"] = boolean(false);
    p.object["equipment_intent_result"].object["reason"] = text("");
    for (const auto &key : {"team_killer", "shell_change_pending",
                            "client_position", "world_pose"})
      p.object[key] = boolean(false);
    p.object["participating"] = boolean(true);
    p.object["hydraulic_body"] = {};
    if (!intField(p, "requested_team"))
      spawn(p, 0, -1);
    else
      spawn(p, static_cast<int>(intField(p, "team")),
            static_cast<int>(intField(p, "slot")));
    std::vector<std::string> chat_keys;
    for (const auto &kv : p.object)
      if (kv.first.rfind("team_chat_", 0) == 0)
        chat_keys.push_back(kv.first);
    for (const auto &key : chat_keys)
      p.object.erase(key);
  }
  next_player_ =
      state_.players.empty() ? 1 : state_.players.rbegin()->first + 1;
  host_player_ = 0;
  for (const auto &[id, p] : state_.players)
    if (connected(p)) {
      host_player_ = id;
      break;
    }
  state_.bots.clear();
  state_.bot_manifest = array();
  state_.equipment_contracts = array();
  state_.bot_orders = array();
  state_.bot_manifest_revision = 0;
  state_.bot_state_revision = 0;
  state_.bot_order_revision = 0;
  state_.bot_planner_dirty = false;
  state_.bot_state_time_us = 0;
  state_.motion_time_us = 0;
  state_.bot_launch_clock_offset_us.reset();
  state_.authority_epoch = worker_ready_ ? 1 : 0;
  state_.destructible_revision = 0;
  state_.projectile_revision = 0;
  state_.destructibles = array();
  detached_turrets_ = array();
  state_.events.clear();
  state_.accepted_visibility = array();
  state_.latest_observation = {};
  state_.player_spotted.clear();
  state_.bot_spotted.clear();
  state_.resources.object.erase("human_collision_profiles");
  state_.resources.object.erase("detached_turrets");
  state_.resources.object.erase("capture_bases");
  state_.resources.object.erase("arena_bounds");
  rules_.reset();
  planner_.reset();
  inputs_->reset();
  publication_clock_.reset();
  worker_manifest_fingerprint_ = {};
  simulation_progress_frame_seq_ = -1;
  planner_.setGraph(nullptr);
  manifest_cache_ = {};
  orders_cache_ = {};
  destructibles_cache_ = {};
  result_reset_tick_.reset();
  next_planner_tick_ = 0;
  last_receipt_delivery_tick_ = -1;
  motion_origin_us_ = clock_us_;
  prebattle_us_ = 0;
  battle_started_us_ = 0;
  live_published_ = false;
  strategy_ready_ = false;
  for (auto &[key, conn] : peers_) {
    (void)key;
    conn.ready_round = 0;
    conn.manifest_sent = -1;
    conn.order_sent = -1;
    conn.destructible_sent = -1;
  }
  makeRoster();
  state_.config.object["bot_roster"] = roster_;
  state_.config.object["roster_finalized"] = boolean(false);
  ++state_.state_revision;
}
} // namespace wot::room
