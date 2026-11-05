#include "room_json.hpp"
#include "room_strategy_data.hpp"
#include <cctype>
#include <sstream>

namespace wot::room {
using namespace strategy;
namespace {
const std::set<std::string> radio_commands{"ATTACK",
                                     "BACKTOBASE",
                                     "HELPME",
                                     "FOLLOWME",
                                     "TURNBACK",
                                     "HELPMEEX",
                                     "STOP",
                                     "ATTACKENEMY",
                                     "SUPPORTMEWITHFIRE",
                                     "ATTENTIONTOCELL",
                                     "POSITIVE",
                                     "NEGATIVE",
                                     "SPG_AIM_AREA",
                                     "RELOADINGGUN",
                                     "RELOADING_CASSETE",
                                     "RELOADING_READY",
                                     "RELOADING_READY_CASSETE",
                                     "RELOADING_UNAVAILABLE"};
const std::set<std::string> enemy{"ATTACKENEMY", "SUPPORTMEWITHFIRE"},
    ally{"FOLLOWME", "TURNBACK", "HELPMEEX", "STOP"},
    movement{"ATTACK",   "BACKTOBASE", "HELPME",         "HELPMEEX",
             "FOLLOWME", "TURNBACK",   "ATTENTIONTOCELL"},
    broadcast{"POSITIVE",
              "NEGATIVE",
              "SPG_AIM_AREA",
              "RELOADINGGUN",
              "RELOADING_CASSETE",
              "RELOADING_READY",
              "RELOADING_READY_CASSETE",
              "RELOADING_UNAVAILABLE"};
std::optional<ID> exactRadio(const Value &v, ID low, ID high) {
  const auto &x = v.resolved();
  if (x.kind != Value::Kind::Integer || x.unsigned_integer || x.integer < low ||
      x.integer > high)
    return {};
  return x.integer;
}
bool radioNumber(const Value &v, double low, double high,
                 bool inclusive = true) {
  const auto &x = v.resolved();
  if (x.kind != Value::Kind::Integer && x.kind != Value::Kind::Number)
    return false;
  const double value = x.numeric(std::numeric_limits<double>::quiet_NaN());
  return std::isfinite(value) && value <= high &&
         (inclusive ? value >= low : value > low);
}
std::string radioFingerprint(const Value &raw) {
  const auto &v = raw.resolved();
  std::ostringstream out;
  out << int(v.kind) << ':';
  if (list(v))
    for (const auto &i : rows(v)) {
      const auto key = radioFingerprint(i);
      out << key.size() << ':' << key;
    }
  else if (dict(v))
    for (const auto &i : v.object) {
      const auto key = radioFingerprint(i.second);
      out << i.first.size() << ':' << i.first << key.size() << ':' << key;
    }
  else
    out << writeJson(v);
  return out.str();
}
bool radioRound(const Value &m, ID round) {
  const auto &raw = m.get("round_id").resolved();
  if (raw.kind == Value::Kind::Integer || raw.kind == Value::Kind::Number)
    return raw.numeric(-1.) == double(round);
  if (raw.kind == Value::Kind::Text)
    try {
      std::size_t end = 0;
      const auto value = std::stoll(raw.text, &end);
      while (end < raw.text.size() &&
             std::isspace(static_cast<unsigned char>(raw.text[end])))
        ++end;
      return end == raw.text.size() && value == round;
    } catch (...) {
    }
  return false;
}
Value radioAck(const Value &p, const Value &m, bool accepted,
               const std::string &code, const std::string &command = {},
               const Value &recipients = array(), ID expiry = 0) {
  auto ack = record({{"type", text("team_command_ack")},
                     {"protocol", integer(5)},
                     {"round_id", m.get("round_id")},
                     {"command_seq", m.get("command_seq")},
                     {"accepted", boolean(accepted)},
                     {"code", text(code)},
                     {"recipient_bot_ids", recipients},
                     {"expires_tick", integer(expiry)},
                     {"issuer_id", p.get("id")},
                     {"team", p.get("team")}});
  if (!command.empty())
    ack.object["command"] = text(command);
  for (const auto *k : {"target_id", "target_kind", "cell_index", "aim_point",
                        "reload_time", "quantity"})
    if (has(m, k))
      ack.object[k] = m.get(k);
  return ack;
}
Value radioTerminal(const Value &command, const std::string &code) {
  auto value =
      record({{"type", text("team_command_terminal")},
              {"protocol", integer(5)},
              {"round_id", command.get("round_id")},
              {"command_id", command.get("command_id")},
              {"command_seq", command.get("command_seq")},
              {"command", command.get("command")},
              {"code", text(code)},
              {"recipient_bot_ids", command.get("recipient_bot_ids")}});
  for (const auto *k : {"target_id", "target_kind", "cell_index"})
    if (has(command, k))
      value.object[k] = command.get(k);
  return value;
}
void terminal(State &state, const Value &command, const std::string &code) {
  const auto issuer = intField(command, "issuer_id");
  const auto at = state.players.find(issuer);
  if (at != state.players.end() && b(at->second, "connected"))
    state.pending_deliveries.emplace_back(issuer, radioTerminal(command, code));
}
bool playerPose(const Value &p) {
  return b(p, "client_position") &&
         std::isfinite(n(p, "x", std::numeric_limits<double>::quiet_NaN())) &&
         std::isfinite(n(p, "z", std::numeric_limits<double>::quiet_NaN()));
}
bool respond(const Value &bot, const Value &order, const std::string &command) {
  if (emptyConfirmed(bot, Value()) || b(order, "empty_ammo"))
    return false;
  if (!b(bot, "world_pose", true) ||
      !std::isfinite(n(bot, "x", std::numeric_limits<double>::quiet_NaN())) ||
      !std::isfinite(n(bot, "z", std::numeric_limits<double>::quiet_NaN())))
    return false;
  const auto phase = s(order.get("route_attempt"), "phase");
  if (!null(order.get("route_attempt").get("phase")) && phase != "normal" &&
      phase != "complete")
    return false;
  if (oneOf(s(order, "combat_mode"),
            {"magazine_reload_retreat", "magazine_reload_hold", "engage",
             "flank", "advance_contact", "take_cover", "cover_hold",
             "cover_peek", "cover_return", "support_hold", "route_transfer",
             "recovery_return", "base_defense", "low_health_retreat",
             "low_health_defend", "under_fire_withdraw", "under_fire_hold",
             "crossfire_withdraw", "crossfire_hold"}))
    return false;
  if (command == "STOP")
    return true;
  const auto &critical = bot.get("critical");
  for (const auto *k :
       {"engineHealth", "leftTrackHealth", "rightTrackHealth"}) {
    if (contains(critical.get("destroyed"), text(k)))
      return false;
    for (const auto &d : rows(critical.get("devices")))
      if (s(d, "name") == k && s(d, "state") == "destroyed")
        return false;
  }
  return true;
}
void trimRadio(Value &values) {
  while (values.object.size() > 64) {
    auto first = values.object.begin();
    for (auto i = values.object.begin(); i != values.object.end(); ++i)
      if (std::stoll(i->first) < std::stoll(first->first))
        first = i;
    values.object.erase(first->first);
  }
}
void cacheRadio(Value &p, ID sequence, const std::string &fingerprint,
                const Value &ack) {
  p.object["team_command_seq"] = integer(sequence);
  auto &fingerprints = ensure(p, "team_command_fingerprints"),
       &acks = ensure(p, "team_command_acks");
  fingerprints.object[std::to_string(sequence)] = text(fingerprint);
  acks.object[std::to_string(sequence)] = ack;
  trimRadio(fingerprints);
  trimRadio(acks);
}
bool captureAvailable(const Value &raw, int own) {
  bool found[3] = {false, false, false};
  for (int side : {1, 2}) {
    const auto &values = rows(raw.get(std::to_string(side)));
    for (std::size_t i = 0; i < std::min<std::size_t>(4, values.size()); ++i) {
      const auto &value = values[i];
      const Value *x = nullptr, *z = nullptr;
      if (dict(value)) {
        x = &value.get("x");
        z = &value.get("z");
      } else if (list(value) && rows(value).size() >= 2) {
        x = &rows(value)[0];
        z = &rows(value)[1];
      }
      if (!x || !z ||
          !std::isfinite(
              x->numeric(std::numeric_limits<double>::quiet_NaN())) ||
          !std::isfinite(z->numeric(std::numeric_limits<double>::quiet_NaN())))
        continue;
      if (dict(value) && has(value, "radius")) {
        const double radius =
            n(value, "radius", std::numeric_limits<double>::quiet_NaN());
        if (!std::isfinite(radius * radius) || radius <= 0.)
          continue;
      }
      found[side] = true;
    }
  }
  return own >= 1 && own <= 2 && found[1] && found[2];
}
} // namespace
Outcome Planner::dispatchRadio(Sender sender, const Value &m) {
  if (sender.worker)
    return Outcome::reject("radio_owner");
  auto found = state_.players.find(sender.id);
  if (found == state_.players.end() || !b(found->second, "connected"))
    return Outcome::reject("unknown_player");
  auto &p = found->second;
  const auto command = s(m, "command");
  const auto reply = [&](bool accepted, const std::string &code,
                         const std::string &name = {},
                         const Value &recipients = array(), ID expiry = 0) {
    auto result = Outcome::accept();
    result.replies.push_back(
        radioAck(p, m, accepted, code, name, recipients, expiry));
    return result;
  };
  if (!dict(m) || !radioRound(m, state_.round_id) || state_.phase != "battle" ||
      !b(p, "participating"))
    return reply(false, "inactive_round");
  if (!radio_commands.count(command))
    return reply(false, "unknown_command");
  std::set<std::string> expected{"type", "round_id", "command_seq", "command"};
  if (enemy.count(command) || ally.count(command)) {
    expected.insert("target_id");
    expected.insert("target_kind");
    if (command == "ATTACKENEMY")
      expected.insert("reload_time");
  } else if (command == "ATTENTIONTOCELL")
    expected.insert("cell_index");
  else if (command == "SPG_AIM_AREA")
    for (const auto *k : {"aim_point", "cell_index", "reload_time"})
      expected.insert(k);
  else if (command == "RELOADINGGUN" || command == "RELOADING_CASSETE") {
    expected.insert("reload_time");
    if (command == "RELOADING_CASSETE")
      expected.insert("quantity");
  } else if (command == "RELOADING_READY_CASSETE")
    expected.insert("quantity");
  std::set<std::string> actual;
  for (const auto &v : m.resolved().object)
    actual.insert(v.first);
  auto optional_reload = expected;
  optional_reload.erase("reload_time");
  if (actual != expected &&
      !(command == "ATTACKENEMY" && actual == optional_reload))
    return reply(false, "invalid_shape", command);
  const auto seq = exactRadio(m.get("command_seq"), 1, 2147483647);
  if (!seq)
    return reply(false, "invalid_sequence", command);
  const auto fingerprint = radioFingerprint(m), key = std::to_string(*seq);
  const auto &old = p.get("team_command_fingerprints").get(key);
  if (!null(old)) {
    if (old.resolved().text != fingerprint)
      return reply(false, "sequence_conflict", command);
    auto result = Outcome::accept();
    result.replies.push_back(p.get("team_command_acks").get(key));
    return result;
  }
  if (*seq <= intField(p, "team_command_seq"))
    return reply(false, "stale_sequence", command);
  std::vector<Value> live;
  for (const auto &identity : rows(state_.bot_manifest)) {
    const auto bot = state_.bots.find(id(identity));
    if (bot != state_.bots.end() && b(bot->second, "alive") &&
        team(identity) == team(p)) {
      auto value = bot->second;
      value.object["id"] = identity.get("id");
      value.object["team"] = p.get("team");
      live.push_back(std::move(value));
    }
  }
  if (command == "SPG_AIM_AREA" || command == "RELOADINGGUN" ||
      command == "RELOADING_CASSETE" ||
      (command == "ATTACKENEMY" && has(m, "reload_time")))
    if (!radioNumber(m.get("reload_time"), 0., 3600.,
                     command == "ATTACKENEMY" || command == "SPG_AIM_AREA"))
      return reply(false, "invalid_reload_time", command);
  if (command == "RELOADING_CASSETE" || command == "RELOADING_READY_CASSETE")
    if (!exactRadio(m.get("quantity"), 1, 255))
      return reply(false, "invalid_quantity", command);
  if (command == "SPG_AIM_AREA") {
    if (!list(m.get("aim_point")) || rows(m.get("aim_point")).size() != 3)
      return reply(false, "invalid_aim_point", command);
    for (const auto &v : rows(m.get("aim_point")))
      if (!radioNumber(v, -5000., 5000.))
        return reply(false, "invalid_aim_point", command);
  }
  std::optional<ID> target_id;
  std::string target_kind;
  const Value *target = nullptr;
  Identity target_marker;
  bool marker = false;
  if (enemy.count(command) || ally.count(command)) {
    target_id = exactRadio(m.get("target_id"), 1, 2147483647);
    target_kind = s(m, "target_kind");
    bool valid = false;
    if (target_id && target_kind == "human") {
      const auto at = state_.players.find(*target_id);
      if (at != state_.players.end()) {
        target = &at->second;
        valid = b(*target, "connected") && b(*target, "participating") &&
                (enemy.count(command) ? team(*target) != team(p)
                                      : team(*target) == team(p));
      }
      target_marker = {"player", *target_id};
      marker = true;
    } else if (target_id && target_kind == "bot") {
      for (const auto &identity : rows(state_.bot_manifest))
        if (id(identity) == *target_id) {
          target = &identity;
          valid = enemy.count(command) ? team(identity) != team(p)
                                       : team(identity) == team(p);
          break;
        }
      target_marker = {"bot", *target_id};
      marker = true;
    }
    if (!target_id || !valid)
      return reply(false,
                   enemy.count(command) ? "invalid_enemy" : "invalid_ally",
                   command);
  } else if (command == "ATTENTIONTOCELL" || command == "SPG_AIM_AREA")
    if (!exactRadio(m.get("cell_index"), 0, 99))
      return reply(false, "invalid_cell", command);
  auto recipients = array();
  ID expiry = 0;
  bool can_order =
      b(p, "alive") &&
      state_.server_tick >=
          ID(std::nearbyint(
              state_.config.get("prebattle_seconds").numeric(10.) * 30.)) &&
      null(state_.battle_result) && !broadcast.count(command);
  if (can_order && enemy.count(command)) {
    bool alive = false;
    if (target_kind == "human")
      alive = target && b(*target, "alive");
    else {
      const auto bot = state_.bots.find(*target_id);
      alive = bot != state_.bots.end() && b(bot->second, "alive");
    }
    const auto spotted = state_.player_spotted.find(sender.id);
    if (!alive || spotted == state_.player_spotted.end() ||
        !spotted->second.count(target_marker))
      can_order = false;
  }
  if (can_order && oneOf(command, {"FOLLOWME", "HELPME", "HELPMEEX"}))
    can_order = playerPose(p);
  const auto &context = state_.rules.get("capture_context");
  if (can_order && command == "BACKTOBASE")
    can_order = captureAvailable(context.get("capture_bases"), team(p));
  if (can_order && command == "ATTENTIONTOCELL")
    can_order = !null(context.get("arena_bounds"));
  if (can_order) {
    std::size_t limit = 3;
    std::vector<Value> bots;
    if (ally.count(command)) {
      if (target_kind == "bot")
        for (const auto &bot : live)
          if (id(bot) == *target_id && respond(bot, Value(), command))
            bots.push_back(bot);
    } else if (playerPose(p)) {
      std::map<ID, Value> orders;
      const auto &raw = state_.bot_orders;
      const auto &values = dict(raw) ? rows(raw.get("orders")) : rows(raw);
      for (const auto &order : values)
        if (order.get("id").resolved().kind == Value::Kind::Integer)
          orders[id(order)] = order;
      for (const auto &bot : live) {
        const auto at = orders.find(id(bot));
        if (respond(bot, at == orders.end() ? Value() : at->second, command))
          bots.push_back(bot);
      }
      std::vector<Value> nearby;
      for (const auto &bot : bots)
        if (std::hypot(n(bot, "x") - n(p, "x"), n(bot, "z") - n(p, "z")) <=
            300.)
          nearby.push_back(bot);
      if (!nearby.empty())
        bots = std::move(nearby);
      else
        limit = 2;
    }
    const auto sort_key = [&](const Value &bot) {
      const auto spotted = state_.bot_spotted.find(id(bot));
      const bool sees = marker && spotted != state_.bot_spotted.end() &&
                        spotted->second.count(target_marker);
      return std::make_tuple(
          sees ? 0 : 1,
          rounded(std::hypot(n(bot, "x") - n(p, "x"), n(bot, "z") - n(p, "z")),
                  6),
          id(bot));
    };
    std::stable_sort(bots.begin(), bots.end(),
                     [&](const Value &a, const Value &c) {
                       return sort_key(a) < sort_key(c);
                     });
    for (std::size_t i = 0; i < std::min(limit, bots.size()); ++i)
      recipients.array.push_back(integer(id(bots[i])));
    if (!recipients.array.empty() &&
        state_.server_tick < intField(p, "team_command_ready_tick")) {
      auto responders =
          b(p, "team_command_refusal_sent") ? array() : recipients;
      p.object["team_command_refusal_sent"] = boolean(true);
      auto ack = radioAck(p, m, false, "cooldown", command, responders);
      cacheRadio(p, *seq, fingerprint, ack);
      auto result = Outcome::accept();
      result.replies.push_back(std::move(ack));
      return result;
    }
    if (!recipients.array.empty()) {
      p.object["team_command_ready_tick"] = integer(state_.server_tick + 300);
      p.object["team_command_refusal_sent"] = boolean(false);
      auto &pending = state_.team_commands.array;
      for (auto i = pending.begin(); i != pending.end();) {
        const auto previous = *i;
        auto retained = array();
        for (const auto &bot : rows(i->get("recipient_bot_ids")))
          if (!contains(recipients, bot))
            retained.array.push_back(bot);
        i->object["recipient_bot_ids"] = std::move(retained);
        if (rows(i->get("recipient_bot_ids")).empty()) {
          terminal(state_, previous, "superseded");
          i = pending.erase(i);
        } else
          ++i;
      }
      const ID ttl = command == "BACKTOBASE"   ? 2700
                     : movement.count(command) ? 3600
                                               : 450;
      expiry = state_.server_tick + ttl;
      auto order = record({{"command_id", text(std::to_string(state_.round_id) +
                                               ":" + std::to_string(sender.id) +
                                               ":" + std::to_string(*seq))},
                           {"command_seq", integer(*seq)},
                           {"round_id", integer(state_.round_id)},
                           {"command", text(command)},
                           {"team", p.get("team")},
                           {"issuer_id", integer(sender.id)},
                           {"issued_tick", integer(state_.server_tick)},
                           {"expires_tick", integer(expiry)},
                           {"recipient_bot_ids", recipients}});
      if (target_id) {
        order.object["target_id"] = integer(*target_id);
        if (enemy.count(command))
          order.object["target_kind"] = text(target_kind);
      }
      if (command == "ATTENTIONTOCELL")
        order.object["cell_index"] = m.get("cell_index");
      pending.push_back(std::move(order));
      state_.bot_planner_dirty = true;
      while (pending.size() > 64) {
        terminal(state_, pending.front(), "superseded");
        pending.erase(pending.begin());
      }
    }
  }
  auto ack = radioAck(p, m, true, "accepted", command, recipients, expiry);
  cacheRadio(p, *seq, fingerprint, ack);
  auto &publications = ensure(p, "team_command_publications");
  publications.object[key] = text(fingerprint);
  trimRadio(publications);
  auto relay = record({{"type", text("team_command")},
                       {"protocol", integer(5)},
                       {"round_id", integer(state_.round_id)},
                       {"command_seq", integer(*seq)},
                       {"command", text(command)},
                       {"issuer_id", integer(sender.id)},
                       {"team", p.get("team")}});
  for (const auto *k : {"target_kind", "target_id", "cell_index", "aim_point",
                        "reload_time", "quantity"})
    if (has(ack, k))
      relay.object[k] = ack.get(k);
  for (const auto &kv : state_.players)
    if (b(kv.second, "connected") && b(kv.second, "participating") &&
        team(kv.second) == team(p))
      state_.pending_deliveries.emplace_back(kv.first, relay);
  auto result = Outcome::accept();
  result.replies.push_back(std::move(ack));
  return result;
}
void Planner::advanceRadio() {
  auto &commands = state_.team_commands.array;
  for (auto i = commands.begin(); i != commands.end();) {
    std::string code;
    const auto command = s(*i, "command");
    if (command == "BACKTOBASE") {
      auto &phases = ensure(*i, "guard_phases");
      const auto &context = state_.rules.get("capture_context");
      const auto bases = defensePoints(context.get("capture_bases"), team(*i));
      const bool occupied =
          intField(context.get("states").get(std::to_string(team(*i))),
                   "invaders") > 0 ||
          !contributors(context, team(*i)).empty();
      auto keep = array();
      for (const auto &recipient : rows(i->get("recipient_bot_ids"))) {
        const auto bot = state_.bots.find(exactInteger(recipient).value_or(0));
        const auto pose = bot == state_.bots.end() ? Value() : bot->second;
        auto &phase =
            ensure(phases,
                   std::to_string(exactInteger(recipient).value_or(0)).c_str());
        const auto wrapped = record({{"state", pose}});
        if (std::any_of(bases.begin(), bases.end(), [&](const Value &base) {
              return insideBase(wrapped, base);
            }))
          phase.object["arrived"] = boolean(true);
        if (b(phase, "arrived")) {
          if (occupied)
            phase.object.erase("safe_tick");
          else if (!has(phase, "safe_tick"))
            phase.object["safe_tick"] = integer(state_.server_tick);
          if (!occupied &&
              state_.server_tick - intField(phase, "safe_tick") >= 900)
            continue;
        } else if (state_.server_tick >= intField(*i, "expires_tick"))
          continue;
        keep.array.push_back(recipient);
      }
      i->object["recipient_bot_ids"] = std::move(keep);
      if (rows(i->get("recipient_bot_ids")).empty())
        code = "expired";
    } else if (state_.server_tick >= intField(*i, "expires_tick"))
      code = "expired";
    const auto issuer = state_.players.find(intField(*i, "issuer_id"));
    if (issuer == state_.players.end() || !b(issuer->second, "connected"))
      code = "issuer_disconnected";
    else if (!b(issuer->second, "participating"))
      code = "issuer_left";
    else if (!b(issuer->second, "alive"))
      code = "issuer_dead";
    else if (team(issuer->second) != team(*i))
      code = "issuer_changed";
    if (!code.empty()) {
      state_.bot_planner_dirty = true;
      terminal(state_, *i, code);
      i = commands.erase(i);
    } else
      ++i;
  }
}
void Planner::clearRadio(const std::string &code) {
  for (const auto &command : rows(state_.team_commands))
    terminal(state_, command, code);
  state_.team_commands = array();
  team_commands_ = array();
}
void Planner::cancelRadio(const std::set<ID> &cancelled) {
  auto &commands = state_.team_commands.array;
  for (auto i = commands.begin(); i != commands.end();) {
    const auto previous = *i;
    auto keep = array();
    for (const auto &recipient : rows(i->get("recipient_bot_ids")))
      if (!cancelled.count(exactInteger(recipient).value_or(0)))
        keep.array.push_back(recipient);
    i->object["recipient_bot_ids"] = std::move(keep);
    if (rows(i->get("recipient_bot_ids")).empty()) {
      terminal(state_, previous, "superseded");
      i = commands.erase(i);
    } else
      ++i;
  }
}
} // namespace wot::room
