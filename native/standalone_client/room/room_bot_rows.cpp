#include "room_bot_rows.hpp"
#include <array>
#include <set>

namespace wot::room {
namespace {
using ID = std::int64_t;
using Kind = Value::Kind;
constexpr double pi = 3.14159265358979323846;
constexpr ID shot = ID(1) << 6, critical = ID(1) << 7, equipment = ID(1) << 8,
             ammo = ID(1) << 9, burst = ID(1) << 14, clip = ID(1) << 15,
             siege = ID(1) << 16, contact = ID(1) << 17, ram = ID(1) << 21,
             aim = ID(1) << 23, aligned = ID(1) << 24, bodies = ID(1) << 26;
constexpr std::array<const char *, 38> names{{"id",
                                              "_flags",
                                              "x",
                                              "y",
                                              "z",
                                              "yaw",
                                              "pitch",
                                              "roll",
                                              "aim_yaw",
                                              "gun_pitch",
                                              "speed",
                                              "fire_seq",
                                              "shell_index",
                                              "next_shell_index",
                                              "clip",
                                              "clip_size",
                                              "reload_time",
                                              "reload_duration",
                                              "burst_group_seq",
                                              "burst_count",
                                              "burst_next_index",
                                              "burst_shell_index",
                                              "burst_interval",
                                              "burst_time_left",
                                              "siege_state",
                                              "siege_time_left_ms",
                                              "siege_transition_total_ms",
                                              "health",
                                              "display_health",
                                              "combat_base_revision",
                                              "combat_seq",
                                              "combat_fire_elapsed",
                                              "combat_fire_timer",
                                              "death_reason",
                                              "stun_end_server_time_ms",
                                              "shot_yaw",
                                              "shot_pitch",
                                              "turret_yaw"}};
constexpr std::array<double, 38> scales{
    {0.,       0.,       10000., 10000.,  10000.,  100000., 100000.,  100000.,
     100000.,  100000.,  10000., 0.,      0.,      0.,      0.,       0.,
     1000000., 1000000., 0.,     0.,      0.,      0.,      1000000., 1000000.,
     0.,       0.,       0.,     0.,      0.,      0.,      0.,       1000000.,
     1000000., 0.,       0.,     100000., 100000., 100000.}};
constexpr std::array<const char *, 9> device_names{
    {"ammoBayHealth", "engineHealth", "fuelTankHealth", "gunHealth",
     "leftTrackHealth", "radioHealth", "rightTrackHealth",
     "surveyingDeviceHealth", "turretRotatorHealth"}};
constexpr std::array<const char *, 8> crew_names{
    {"commander", "driver", "gunner1", "gunner2", "loader1", "loader2",
     "radioman1", "radioman2"}};
constexpr std::array<const char *, 3> device_states{
    {"normal", "critical", "destroyed"}};
constexpr std::array<double, 18> motion_scales{
    {10000., 10000., 10000., 10000., 10000., 10000., 100000., 100000., 100000.,
     10000., 10000., 10000., 100000., 100000., 100000., 10000., 10000.,
     10000.}};
constexpr std::array<double, 18> motion_limits{
    {200., 200., 200., 2000., 1000., 2000., 2. * pi, pi, pi, 2000., 1000.,
     2000., 2. * pi, pi, pi, 200., 200., 200.}};
[[noreturn]] void fail(const char *message) {
  throw std::invalid_argument(message);
}
Value owned(const Value &input) {
  const auto &value = input.resolved();
  if (value.kind == Kind::Array) {
    auto result = array();
    for (const auto &item : value.array)
      result.array.push_back(owned(item));
    return result;
  }
  if (value.kind == Kind::Object) {
    auto result = object();
    for (const auto &item : value.object)
      result.object[item.first] = owned(item.second);
    return result;
  }
  return value;
}
Value crew(ID mask) {
  if (mask < 0 || mask > 255)
    fail("Bot crew mask is out of range");
  auto result = array();
  for (std::size_t i = 0; i < crew_names.size(); ++i)
    if (mask & (ID(1) << i))
      result.array.push_back(text(crew_names[i]));
  return result;
}
struct Cursor {
  const std::vector<Value> &row;
  std::size_t index = names.size();
  ID take() {
    if (index == row.size())
      fail("Bot state row is truncated");
    return row[index++].resolved().integer;
  }
  ID count(ID maximum, const char *message) {
    const auto result = take();
    if (result < 0 || result > maximum)
      fail(message);
    return result;
  }
  double real(double scale) { return double(take()) / scale; }
};
} // namespace

Value decodeBotRow(const Value &input, const Value &static_state) {
  const auto &row = input.resolved();
  if (row.kind != Kind::Array || row.array.size() < names.size())
    fail("Bot state row is truncated");
  for (const auto &item : row.array)
    if (item.resolved().kind != Kind::Integer ||
        item.resolved().unsigned_integer)
      fail("Bot state row must contain signed integers");
  auto result = object();
  for (std::size_t i = 0; i < names.size(); ++i) {
    if (i == 1)
      continue;
    result.object[names[i]] =
        scales[i] ? number(double(row.array[i].resolved().integer) / scales[i])
                  : owned(row.array[i]);
  }
  const auto flags = row.array[1].resolved().integer;
  for (const auto &field : std::array<std::pair<const char *, ID>, 9>{
           {{"alive", 1},
            {"world_pose", 2},
            {"airborne", ID(1) << 18},
            {"service_brake", ID(1) << 19},
            {"route_wreck_blocked", ID(1) << 20},
            {"local_combat_manoeuvre", ID(1) << 22},
            {"ammo_reload_pending", 4},
            {"ammo_magazine_started", ID(1) << 25},
            {"burst_active", 8}}})
    result.object[field.first] = boolean(bool(flags & field.second));
  result.object["movement_dir"] = integer(flags & (ID(1) << 10)   ? 1
                                          : flags & (ID(1) << 11) ? -1
                                                                  : 0);
  result.object["rotation_dir"] = integer(flags & (ID(1) << 12)   ? 1
                                          : flags & (ID(1) << 13) ? -1
                                                                  : 0);
  if (!(flags & shot)) {
    result.object.erase("shot_yaw");
    result.object.erase("shot_pitch");
  }
  if (!(flags & ammo)) {
    result.object.erase("next_shell_index");
    result.object.erase("ammo_reload_pending");
  }
  if (!(flags & burst))
    for (const auto *field :
         {"burst_active", "burst_group_seq", "burst_count", "burst_next_index",
          "burst_shell_index", "burst_interval", "burst_time_left"})
      result.object.erase(field);
  if (!(flags & clip)) {
    result.object.erase("clip");
    result.object.erase("clip_size");
  }
  if (!(flags & siege))
    for (const auto *field :
         {"siege_state", "siege_time_left_ms", "siege_transition_total_ms"})
      result.object.erase(field);
  Cursor cursor{row.array};
  if (flags & ammo) {
    auto values = array();
    const auto count = cursor.count(5, "Bot shell type count is out of range");
    for (ID i = 0; i < count; ++i)
      values.array.push_back(integer(cursor.take()));
    result.object["ammo_remaining"] = std::move(values);
  }
  if (flags & critical) {
    auto value = object(), devices = array(), destroyed = array();
    const auto count = cursor.count(ID(device_names.size()),
                                    "Bot device count is out of range");
    for (ID i = 0; i < count; ++i) {
      const auto slot = cursor.take(), hp = cursor.take(),
                 maximum = cursor.take(), state = cursor.take();
      if (slot < 0 || slot >= ID(device_names.size()) || state < 0 ||
          state >= ID(device_states.size()))
        fail("Bot device row is out of range");
      auto device = object();
      device.object["name"] = text(device_names[std::size_t(slot)]);
      device.object["hp"] = number(double(hp) / 1000.);
      device.object["max_hp"] = number(double(maximum) / 1000.);
      device.object["state"] = text(device_states[std::size_t(state)]);
      devices.array.push_back(std::move(device));
      if (state == 2)
        destroyed.array.push_back(text(device_names[std::size_t(slot)]));
    }
    value.object["devices"] = std::move(devices);
    value.object["destroyed"] = std::move(destroyed);
    value.object["crew_ko"] = crew(cursor.take());
    value.object["fire"] = boolean(bool(flags & 16));
    value.object["ammo_rack_death"] = boolean(bool(flags & 32));
    value.object["events"] = array();
    const auto roster = cursor.take();
    if (roster >= 0)
      value.object["crew_roster"] = crew(roster);
    result.object["critical"] = std::move(value);
  }
  if (flags & equipment) {
    const auto &contracts = static_state.get("equipment_contracts").resolved();
    const auto count = cursor.count(3, "Bot consumable count is out of range");
    if (count &&
        (contracts.kind != Kind::Array || ID(contracts.array.size()) != count))
      fail("Bot consumable count does not match the round manifest");
    auto values = array();
    for (ID i = 0; i < count; ++i) {
      auto value = object();
      const auto uses = cursor.take(), cooldown = cursor.take(),
                 active = cursor.take();
      value.object["equipment"] = owned(contracts.array[std::size_t(i)]);
      value.object["usesLeft"] = integer(uses);
      value.object["cooldownTimeLeft"] = number(double(cooldown) / 1000000.);
      value.object["active"] = boolean(active != 0);
      for (const auto *field : {"autoPendingElapsed", "aiPendingElapsed"}) {
        const auto pending = cursor.take();
        value.object[field] =
            pending == -1 ? Value{} : number(double(pending) / 1000000.);
      }
      values.array.push_back(std::move(value));
    }
    result.object["equipment_states"] = std::move(values);
  }
  if (flags & contact) {
    result.object["push_x"] = number(cursor.real(10000.));
    result.object["push_z"] = number(cursor.real(10000.));
    result.object["push_yaw"] = number(cursor.real(100000.));
    auto values = array();
    std::set<ID> actors;
    const auto count =
        cursor.count(30, "Invalid contact acknowledgement count");
    for (ID i = 0; i < count; ++i) {
      auto value = array();
      const auto actor = cursor.take(), sequence = cursor.take();
      if (actor < 1 || actor > 2147483647 || sequence < 1 ||
          sequence > 2147483647 || !actors.insert(actor).second)
        fail("Invalid contact acknowledgement identity");
      value.array = {integer(actor), integer(sequence)};
      for (std::size_t k = 0; k < 5; ++k) {
        const auto v = cursor.real(10000.);
        if (std::abs(v) > 1.e12)
          fail("Invalid contact acknowledgement total");
        value.array.push_back(number(v));
      }
      values.array.push_back(std::move(value));
    }
    result.object["contact_push_acks"] = std::move(values);
  }
  if (flags & ram) {
    auto values = array();
    std::set<ID> peers;
    const auto count = cursor.count(30, "Invalid ram motion count");
    for (ID i = 0; i < count; ++i) {
      auto value = array();
      const auto peer = cursor.take(), sequence = cursor.take(),
                 stamp = cursor.take();
      if (peer < 1 || peer > 2147483647 || sequence < 1 ||
          sequence > 2147483647 || stamp < 0 || stamp > 9007199254740991LL ||
          !peers.insert(peer).second)
        fail("Invalid ram motion identity");
      value.array = {integer(peer), integer(sequence), integer(stamp)};
      for (std::size_t k = 0; k < motion_scales.size(); ++k) {
        const auto v = cursor.real(motion_scales[k]);
        if (std::abs(v) > motion_limits[k])
          fail("Invalid ram motion value");
        value.array.push_back(number(v));
      }
      auto metadata = object(), body = Value{};
      if (flags & bodies) {
        const auto present = cursor.take();
        if (present != 0 && present != 1)
          fail("Invalid ram body presence");
        if (present) {
          body = array();
          for (std::size_t k = 0; k < 8; ++k) {
            const auto v = cursor.real(k < 3 ? 10000. : 100000.);
            const double limit = k == 0 || k == 2 ? 2000.
                                 : k == 1         ? 1000.
                                                  : 2. * pi;
            if (std::abs(v) > limit)
              fail("Invalid ram body receipt");
            body.array.push_back(number(v));
          }
        }
      }
      metadata.object["hydraulic_body"] = std::move(body);
      value.array.push_back(std::move(metadata));
      values.array.push_back(std::move(value));
    }
    result.object["ram_motion"] = std::move(values);
  }
  if (flags & aim) {
    const auto kind = cursor.take(), target = cursor.take(),
               error = cursor.take();
    if (kind < 0 || kind > 2 || target < 0 || bool(kind) != bool(target) ||
        error < 0 || error > 314160)
      fail("Invalid aim feedback");
    constexpr std::array<const char *, 3> kinds{{"", "human", "bot"}};
    result.object["aim_feedback_kind"] = text(kinds[std::size_t(kind)]);
    result.object["aim_feedback_id"] = integer(target);
    result.object["aim_feedback_error"] = number(double(error) / 100000.);
    result.object["aim_feedback_aligned"] = boolean(bool(flags & aligned));
  } else if (flags & aligned)
    fail("Aim alignment without feedback identity");
  if (cursor.index != row.array.size())
    fail("Bot state row has trailing columns");
  return result;
}
Value decodeBotRow(const Value &row) { return decodeBotRow(row, object()); }

std::pair<Value, Value> encodeSnapshotEquipment(const Value &input) {
  const auto &bots = input.resolved();
  if (bots.kind != Kind::Array)
    fail("Snapshot bots must be an array");
  auto projected = array(), contracts = array();
  for (const auto &raw : bots.array) {
    if (raw.resolved().kind != Kind::Object)
      fail("Snapshot bot must be an object");
    auto bot = owned(raw);
    auto &states = bot.object["equipment_states"];
    if (!raw.get("equipment_states").truth()) {
      // Preserve an absent field rather than manufacturing a null one.
      if (!has(raw, "equipment_states"))
        bot.object.erase("equipment_states");
      projected.array.push_back(std::move(bot));
      continue;
    }
    if (states.kind != Kind::Array)
      fail("Snapshot equipment states must be an array");
    for (auto &item : states.array) {
      if (item.kind != Kind::Object)
        fail("Snapshot equipment state must be an object");
      const auto &contract = item.get("equipment");
      if (contract.resolved().kind != Kind::Object)
        continue;
      std::size_t index = 0;
      while (index < contracts.array.size() &&
             contracts.array[index] != contract)
        ++index;
      if (index == contracts.array.size())
        contracts.array.push_back(owned(contract));
      item.object["equipment"] = integer(ID(index));
    }
    projected.array.push_back(std::move(bot));
  }
  return {std::move(projected), std::move(contracts)};
}
} // namespace wot::room
