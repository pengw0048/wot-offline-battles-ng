#include "room_inputs.hpp"
#include "room_json.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <locale>
#include <set>
#include <sstream>
#include <tuple>

namespace wot::room {
namespace {
using ID = std::int64_t;
using Kind = Value::Kind;
constexpr double pi = 3.14159265358979323846;
constexpr ID max_id = 2147483647, max_motion = 10000000000000000LL;
bool obj(const Value &v) { return v.resolved().kind == Kind::Object; }
bool list(const Value &v) { return v.resolved().kind == Kind::Array; }
bool null(const Value &v) { return v.resolved().kind == Kind::Null; }
std::string field(const Value &v, const char *k) {
  return v.get(k).resolved().text;
}
void require(bool b, const char *why) {
  if (!b)
    throw std::invalid_argument(why);
}
ID exact(const Value &raw, ID low = 0, ID high = max_id) {
  const auto &v = raw.resolved();
  require(v.kind == Kind::Integer && !v.unsigned_integer && v.integer >= low &&
              v.integer <= high,
          "invalid input integer");
  return v.integer;
}
double scalar(const Value &raw, double low, double high,
              bool inclusive = true) {
  const auto &v = raw.resolved();
  require(v.kind == Kind::Number || v.kind == Kind::Integer,
          "invalid input number");
  const auto n = v.numeric(std::numeric_limits<double>::quiet_NaN());
  require(std::isfinite(n) && n <= high && (inclusive ? n >= low : n > low),
          "input number outside bounds");
  return n;
}
double val(const Value &v, const char *k, double fallback = 0.) {
  return v.get(k).numeric(fallback);
}
double rounded(double n, int places) {
  std::ostringstream out;
  out.imbue(std::locale::classic());
  out << std::fixed << std::setprecision(places) << n;
  std::istringstream input(out.str());
  input.imbue(std::locale::classic());
  double result = 0.;
  input >> result;
  return result;
}
double angle(double n) {
  double r = std::fmod(n + pi, 2 * pi);
  if (r < 0)
    r += 2 * pi;
  return r - pi;
}
bool capability(const Value &p, const char *k) {
  for (const auto &v : rows(p.get("capabilities")))
    if (v.resolved().text == k)
      return true;
  return false;
}
std::string fingerprint(const Value &raw) {
  const auto &v = raw.resolved();
  std::ostringstream out;
  out << int(v.kind) << ':';
  if (v.kind == Kind::Array)
    for (const auto &i : v.array) {
      const auto s = fingerprint(i);
      out << s.size() << ':' << s;
    }
  else if (v.kind == Kind::Object)
    for (const auto &i : v.object) {
      const auto s = fingerprint(i.second);
      out << i.first.size() << ':' << i.first << s.size() << ':' << s;
    }
  else
    out << writeJson(v);
  return out.str();
}
Value select(const Value &v, std::initializer_list<const char *> keys) {
  auto out = object();
  for (const auto *k : keys)
    if (has(v, k))
      out.object[k] = v.get(k);
  return out;
}
void shape(const Value &v, const std::set<std::string> &keys) {
  require(obj(v) && v.resolved().object.size() == keys.size(),
          "invalid input object shape");
  for (const auto &x : v.resolved().object)
    require(keys.count(x.first) != 0, "invalid input object field");
}
Value normalizePushes(const Value &raw, bool turret) {
  require(list(raw) && rows(raw).size() <= 30,
          "invalid contact checkpoint list");
  auto out = array();
  std::set<std::string> seen;
  for (const auto &v : rows(raw)) {
    const auto &r = rows(v);
    require(list(v) && r.size() == (turret ? 8 : 7),
            "invalid contact checkpoint width");
    auto row = array();
    std::string key;
    if (turret) {
      const auto &s = r[0].resolved();
      require(s.kind == Kind::Text, "invalid detached contact actor");
      const auto split = s.text.find(':');
      require(split != std::string::npos &&
                  (s.text.substr(0, split) == "bot" ||
                   s.text.substr(0, split) == "player"),
              "invalid detached contact actor");
      std::size_t end = 0;
      ID id = 0;
      try {
        id = std::stoll(s.text.substr(split + 1), &end);
      } catch (...) {
        throw std::invalid_argument("invalid detached actor id");
      }
      require(end == s.text.size() - split - 1 && id >= 1 && id <= max_id,
              "invalid detached actor id");
      key = s.text.substr(0, split) + ":" + std::to_string(id);
      row.array.push_back(text(key));
    } else {
      const auto id = exact(r[0], 1);
      key = std::to_string(id);
      row.array.push_back(integer(id));
    }
    require(seen.insert(key).second, "duplicate contact actor");
    row.array.push_back(integer(exact(r[1], 1)));
    for (std::size_t i = 2; i < r.size(); ++i)
      row.array.push_back(number(scalar(r[i], -1.e12, 1.e12)));
    out.array.push_back(std::move(row));
  }
  return out;
}
Value checkpoint(const Value &raw) {
  shape(raw,
        {"reload_time", "reload_duration", "clip", "clip_size", "dispersion"});
  auto out = object();
  const auto reload = scalar(raw.get("reload_time"), 0., 3600.),
             duration = scalar(raw.get("reload_duration"), 0., 3600., false),
             dispersion = scalar(raw.get("dispersion"), 0., .5);
  const auto clip = exact(raw.get("clip"), 0, 255),
             size = exact(raw.get("clip_size"), 1, 255);
  require(reload <= duration && clip <= size,
          "inconsistent player gun checkpoint");
  out.object["reload_time"] = number(reload);
  out.object["reload_duration"] = number(duration);
  out.object["dispersion"] = number(dispersion);
  out.object["clip"] = integer(clip);
  out.object["clip_size"] = integer(size);
  return out;
}
Value hydraulic(const Value &raw) {
  require(list(raw) && rows(raw).size() == 8, "invalid hydraulic body width");
  auto out = array();
  for (std::size_t i = 0; i < 8; ++i) {
    const double bound = i == 1 ? 1000. : i < 3 ? 2000. : 2 * pi;
    out.array.push_back(number(scalar(rows(raw)[i], -bound, bound)));
  }
  return out;
}
double pivot(const Value &physics, const Value &contact) {
  if (null(physics) || physics.get("rotationIsAroundCenter").truth(true))
    return 0.;
  const double yaw = val(contact, "yaw"), end = val(contact, "end_yaw"),
               x = std::cos(yaw) - std::cos(end),
               z = std::sin(end) - std::sin(yaw), norm = x * x + z * z;
  if (norm <= 1.e-18)
    return 0.;
  const double dx = val(contact, "end_x") - val(contact, "x"),
               dz = val(contact, "end_z") - val(contact, "z"),
               offset = (dx * x + dz * z) / norm;
  if (std::abs(offset) > val(physics, "trackCenter") + 1.e-7 ||
      std::abs(dx - offset * x) > 1.e-7 || std::abs(dz - offset * z) > 1.e-7 ||
      std::abs(val(contact, "end_y") - val(contact, "y")) > 1.e-7)
    return 0.;
  return offset;
}
std::array<double, 4> siege(const Value &p) {
  const auto vehicle = field(p, "vehicle");
  if (vehicle == "sweden:S10_Strv_103_0_Series" ||
      vehicle == "sweden:S11_Strv_103B")
    return {2., 1.3, 10. / 3.6, 2.};
  if (vehicle == "sweden:S21_UDES_03")
    return {2., 2., 5. / 3.6, 2.};
  if (vehicle == "sweden:S22_Strv_S1")
    return {2., 1.3, 8. / 3.6, 2.};
  return {};
}
void requestSiege(Value &p, bool enabled) {
  const auto params = siege(p);
  const auto state = intField(p, "siege_state");
  if (!p.get("participating").truth() || params[0] == 0. || state == 1 || state == 3)
    return;
  bool destroyed = false, damaged = false;
  for (const auto &v : rows(p.get("critical").get("destroyed")))
    if (v.resolved().text == "engineHealth")
      destroyed = true;
  for (const auto &v : rows(p.get("critical").get("devices")))
    if (field(v, "name") == "engineHealth") {
      destroyed = destroyed || field(v, "state") == "destroyed";
      damaged = damaged || field(v, "state") == "critical";
    }
  if (destroyed)
    return;
  if ((enabled && state == 2) || (!enabled && state == 0) ||
      val(p, "speed") != 0.)
    return;
  double duration = params[enabled ? 0 : 1];
  if (damaged)
    duration *= params[3];
  p.object["siege_state"] = integer(enabled ? 1 : 3);
  p.object["siege_transition_ticks"] =
      integer(std::max<ID>(1, ID(std::nearbyint(duration * 30.))));
  for (const auto *k : {"forward", "turn", "speed"})
    p.object[k] = number(0.);
}
std::string pushKey(const Value &v, bool turret) {
  return turret ? rows(v)[0].resolved().text
                : std::to_string(rows(v)[0].resolved().integer);
}
void advanceRam(Value &p) {
  ID resolved = intField(p, "ram_contact_resolved_seq"),
     admitted = intField(p, "ram_contact_seq");
  while (resolved < admitted) {
    const ID next = resolved + 1;
    if (std::any_of(rows(p.get("ram_contacts")).begin(),
                    rows(p.get("ram_contacts")).end(),
                    [&](const Value &v) { return intField(v, "seq") == next; }))
      break;
    resolved = next;
  }
  p.object["ram_contact_resolved_seq"] = integer(resolved);
}
void destructibleResolved(Value &p, ID seq) {
  ID resolved = intField(p, "destructible_contact_resolved_seq");
  if (seq <= resolved)
    return;
  auto &pending = p.object["destructible_contact_resolved_seqs"];
  if (!list(pending))
    pending = array();
  std::set<ID> values;
  for (const auto &v : rows(pending))
    values.insert(exactInteger(v).value_or(0));
  values.insert(seq);
  while (values.erase(resolved + 1))
    ++resolved;
  pending = array();
  for (const auto n : values)
    pending.array.push_back(integer(n));
  p.object["destructible_contact_resolved_seq"] = integer(resolved);
  require(values.size() <= 64, "destructible acknowledgement window exceeded");
}
} // namespace
void Inputs::reset() {
  players_.clear();
  archive_.clear();
  for (auto &kv : state_.players) {
    auto &p = kv.second;
    for (const auto *k :
         {"input_seq", "input_processed_seq", "gun_checkpoint_seq",
          "ram_contact_seq", "ram_contact_admitted_seq",
          "ram_contact_resolved_seq", "destructible_contact_seq",
          "destructible_contact_admitted_seq",
          "destructible_contact_resolved_seq"})
      p.object[k] = integer(0);
    for (const auto *k :
         {"ram_contacts", "destructible_contacts", "pose_history",
          "tank_pushes", "turret_pushes", "destructible_contact_resolved_seqs",
          "destructible_contact_rejected_seqs", "input_applied_seqs"})
      p.object[k] = array();
    p.object["gun_checkpoint"] = object();
    p.object["gun_checkpoints"] = object();
    p.object["ram_contact"] = object();
    p.object["ram_contact_rejections"] = array();
    p.object["input_reject_counts"] = object();
    p.object["pose_time_us"] = {};
    p.object["last_input_reject"] = object();
  }
}
void Inputs::advanceTicks(ID count) {
  require(count >= 0, "invalid input tick count");
  if (!count)
    return;
  for (auto &kv : state_.players) {
    auto &p = kv.second;
    const auto mode = intField(p, "siege_state");
    if (mode != 1 && mode != 3)
      p.object["siege_transition_ticks"] = integer(0);
    else {
      const auto left =
          std::max<ID>(0, intField(p, "siege_transition_ticks") - count);
      p.object["siege_transition_ticks"] = integer(left);
      if (!left)
        p.object["siege_state"] = integer(mode == 1 ? 2 : 0);
    }
    p.object["siege_time_left_ms"] = integer(ID(std::ceil(
        double(std::max<ID>(0, intField(p, "siege_transition_ticks"))) * 1000. /
        30.)));
  }
}
void Inputs::sampleBot(Sample &sample, const Value &v) {
  const auto id = exactInteger(v.get("id"));
  if (!id || !obj(v))
    return;
  sample.bots[*id] =
      select(v, {"id", "x", "y", "z", "yaw", "pitch", "roll", "aim_yaw",
                 "gun_pitch", "alive", "team", "vehicle", "ram_motion"});
}
void Inputs::storeSample(ID revision, Sample sample) {
  archive_.emplace(revision, std::move(sample));
  while (archive_.size() > 512)
    archive_.erase(archive_.begin());
}
void Inputs::rememberBots(ID revision, ID stamp, const Value &bots) {
  if (archive_.count(revision))
    return;
  Sample sample;
  sample.stamp = stamp;
  for (const auto &v : rows(bots))
    sampleBot(sample, v);
  storeSample(revision, std::move(sample));
}
void Inputs::rememberBots(ID revision, ID stamp,
                          const std::map<ID, Value> &bots) {
  if (archive_.count(revision))
    return;
  Sample sample;
  sample.stamp = stamp;
  for (const auto &entry : bots)
    sampleBot(sample, entry.second);
  storeSample(revision, std::move(sample));
}
Outcome Inputs::reject(Value &p, std::optional<ID> seq,
                       const std::string &reason, const std::string &field_name,
                       bool consumed, bool active) {
  auto &counts = p.object["input_reject_counts"];
  if (!obj(counts))
    counts = object();
  if (!has(counts, reason) && counts.object.size() >= 32)
    counts = object();
  const auto repeats = intField(counts, reason) + 1;
  counts.object[reason] = integer(repeats);
  auto value = object();
  value.object["reason"] = text(reason);
  value.object["field"] = text(field_name);
  value.object["player_id"] = p.get("id");
  value.object["round_id"] = integer(state_.round_id);
  value.object["submitted_seq"] = seq ? integer(*seq) : Value{};
  value.object["expected_seq"] =
      integer(intField(p, "input_processed_seq") + 1);
  value.object["processed_seq"] = integer(intField(p, "input_processed_seq"));
  value.object["applied_seq"] = integer(intField(p, "input_seq"));
  value.object["checkpoint_seq"] = integer(intField(p, "gun_checkpoint_seq"));
  value.object["active"] = boolean(active);
  value.object["consumed"] = boolean(consumed);
  value.object["repeats"] = integer(repeats);
  p.object["last_input_reject"] = std::move(value);
  return Outcome::reject(reason, field_name);
}
void Inputs::decision(Value &p, ID seq, const std::string &finger,
                      const std::string &outcome, const std::string &reason,
                      const std::string &field_name, bool active) {
  p.object["input_processed_seq"] = integer(seq);
  auto &decisions = players_[intField(p, "id")].decisions;
  decisions[seq] = {finger, outcome, reason, field_name, active};
  while (decisions.size() > 128)
    decisions.erase(decisions.begin());
}
void Inputs::pose(Value &p, const Value &m) {
  std::optional<ID> stamp;
  try {
    stamp = exact(m.get("pose_time_us"), 0, max_motion);
  } catch (const std::invalid_argument &) {
  }
  auto &history = p.object["pose_history"];
  if (!list(history))
    history = array();
  const auto receipt = state_.motion_time_us;
  if (!stamp || *stamp > receipt + 250000 || *stamp < receipt - 3000000) {
    history = array();
    p.object["pose_time_us"] = {};
    return;
  }
  *stamp = std::min(*stamp, receipt);
  if (!null(p.get("pose_time_us")) && *stamp <= intField(p, "pose_time_us"))
    history = array();
  auto sample = select(p, {"x", "y", "z", "yaw", "forward", "turn", "pitch",
                           "roll", "hydraulic_body"});
  sample.object["input_seq"] = integer(intField(p, "input_seq"));
  sample.object["time_us"] = integer(*stamp);
  if (null(p.get("hydraulic_body")))
    sample.object.erase("hydraulic_body");
  const double speed = p.get("alive").truth() ? val(p, "speed") : 0.;
  sample.object["speed"] = number(speed);
  sample.object["vx"] = number(std::sin(val(p, "yaw")) * speed);
  sample.object["vz"] = number(std::cos(val(p, "yaw")) * speed);
  history.array.push_back(std::move(sample));
  p.object["pose_time_us"] = integer(*stamp);
  const auto oldest = *stamp - 1500000;
  while (history.array.size() > 2 &&
         intField(history.array[1], "time_us") < oldest)
    history.array.erase(history.array.begin());
}
std::pair<Value, std::string>
Inputs::ramContact(const Value &p, const Value &raw, bool current) const {
  try {
    const std::set<std::string> required = {"seq",
                                            "bot_id",
                                            "bot_state_revision",
                                            "presentation_time_us",
                                            "native_contact_time_us",
                                            "contact_x",
                                            "contact_y",
                                            "contact_z",
                                            "contact_normal_x",
                                            "contact_normal_z",
                                            "contact_armor_player",
                                            "contact_armor_bot",
                                            "contact_screened_player",
                                            "contact_screened_bot",
                                            "contact_spall_player",
                                            "contact_bonus_player",
                                            "x",
                                            "y",
                                            "z",
                                            "yaw",
                                            "pitch",
                                            "roll",
                                            "vx",
                                            "vy",
                                            "vz",
                                            "bot_vx",
                                            "bot_vy",
                                            "bot_vz"},
                                optional = {"bot_history_bracket",
                                            "bot_ram_motion_seq",
                                            "track_contacts_player",
                                            "track_contacts_bot"};
    require(obj(raw), "malformed_contact");
    for (const auto &k : required)
      require(has(raw, k), "malformed_contact");
    for (const auto &k : raw.resolved().object)
      require(required.count(k.first) || optional.count(k.first),
              "malformed_contact");
    auto out = object();
    out.object["seq"] = integer(exact(raw.get("seq"), 1));
    out.object["bot_id"] = integer(exact(raw.get("bot_id"), 1, 30));
    out.object["bot_state_revision"] =
        integer(exact(raw.get("bot_state_revision")));
    out.object["presentation_time_us"] =
        integer(exact(raw.get("presentation_time_us"), 0, max_motion));
    out.object["native_contact_time_us"] =
        integer(exact(raw.get("native_contact_time_us"), 0, max_motion));
    for (const auto *k : {"contact_armor_player", "contact_armor_bot"})
      out.object[k] = number(rounded(scalar(raw.get(k), 0., 5000., false), 4));
    out.object["contact_spall_player"] =
        number(rounded(scalar(raw.get("contact_spall_player"), 1., 1.5), 4));
    out.object["contact_bonus_player"] =
        number(rounded(scalar(raw.get("contact_bonus_player"), 0., .15), 6));
    for (const auto *k : {"x", "z", "contact_x", "contact_z"})
      out.object[k] = number(rounded(scalar(raw.get(k), -2000., 2000.), 4));
    for (const auto *k : {"y", "contact_y"})
      out.object[k] = number(rounded(scalar(raw.get(k), -1000., 1000.), 4));
    out.object["yaw"] =
        number(rounded(scalar(raw.get("yaw"), -2 * pi, 2 * pi), 5));
    for (const auto *k : {"pitch", "roll"})
      out.object[k] = number(rounded(scalar(raw.get(k), -pi, pi), 5));
    for (const auto *k : {"vx", "vy", "vz", "bot_vx", "bot_vy", "bot_vz"})
      out.object[k] = number(rounded(scalar(raw.get(k), -200., 200.), 4));
    const double nx = scalar(raw.get("contact_normal_x"), -1., 1.),
                 nz = scalar(raw.get("contact_normal_z"), -1., 1.),
                 length = std::hypot(nx, nz);
    if (std::abs(intField(out, "native_contact_time_us") -
                 intField(out, "presentation_time_us")) > 1500000 ||
        length < .999 || length > 1.001)
      return {{}, "invalid_contact_contract"};
    for (const auto *k : {"contact_screened_player", "contact_screened_bot"}) {
      if (raw.get(k).resolved().kind != Kind::Boolean || raw.get(k).truth())
        return {{}, "invalid_contact_contract"};
      out.object[k] = raw.get(k);
    }
    out.object["contact_normal_x"] = number(rounded(nx / length, 6));
    out.object["contact_normal_z"] = number(rounded(nz / length, 6));
    const auto &profile = state_.resources.get("human_collision_profiles")
                              .get(std::to_string(intField(p, "id")));
    if (null(profile))
      return {{}, "missing_collision_profile"};
    if (std::abs(val(raw, "contact_spall_player") -
                 val(profile.get("ram_profile"), "spall_coefficient")) > .0001)
      return {{}, "ram_profile_mismatch"};
    const auto &body_shape = rows(profile.get("shape"));
    if (body_shape.size() != 4)
      return {{}, "contact_outside_player_body"};
    const double sy = std::sin(val(raw, "yaw")), cy = std::cos(val(raw, "yaw")),
                 sp = std::sin(val(raw, "pitch")),
                 cp = std::cos(val(raw, "pitch")),
                 sr = std::sin(val(raw, "roll")),
                 cr = std::cos(val(raw, "roll"));
    const std::array<std::array<double, 3>, 3> axes{
        {{{cy * cr + sy * (sp * sr), cp * sr, -sy * cr + cy * (sp * sr)}},
         {{-cy * sr + sy * (sp * cr), cp * cr, sy * sr + cy * (sp * cr)}},
         {{sy * cp, -sp, cy * cp}}}};
    const std::array<double, 3> delta{{val(raw, "contact_x") - val(raw, "x"),
                                       val(raw, "contact_y") - val(raw, "y"),
                                       val(raw, "contact_z") - val(raw, "z")}};
    std::array<double, 3> local{};
    for (std::size_t i = 0; i < 3; ++i)
      for (std::size_t k = 0; k < 3; ++k)
        local[i] += axes[i][k] * delta[k];
    if (std::abs(local[0]) > body_shape[0].numeric() + .75 ||
        local[1] < body_shape[2].numeric() - .75 ||
        local[1] > body_shape[3].numeric() + .75 ||
        std::abs(local[2]) > body_shape[1].numeric() + .75)
      return {{}, "contact_outside_player_body"};
    if (nx * (val(raw, "x") - val(raw, "contact_x")) +
            nz * (val(raw, "z") - val(raw, "contact_z")) <=
        .000001)
      return {{}, "contact_normal_mismatch"};
    const auto revision = intField(out, "bot_state_revision"),
               stamp = intField(out, "presentation_time_us");
    if (has(raw, "bot_history_bracket")) {
      const auto &bracket = rows(raw.get("bot_history_bracket"));
      if (!list(raw.get("bot_history_bracket")) || bracket.size() != 4)
        return {{}, "invalid_history_bracket"};
      ID lr, lt, rr, rt;
      try {
        lr = exact(bracket[0]);
        lt = exact(bracket[1], 0, 9007199254740991LL);
        rr = exact(bracket[2]);
        rt = exact(bracket[3], 0, 9007199254740991LL);
      } catch (const std::invalid_argument &) {
        return {{}, "invalid_history_bracket"};
      }
      if (lr > rr || rr != revision || rr - lr > 255 || lt > stamp ||
          stamp > rt || ((lr == rr) != (lt == rt)))
        return {{}, "invalid_history_bracket"};
      out.object["bot_history_bracket"] = raw.get("bot_history_bracket");
    }
    if (has(raw, "bot_ram_motion_seq")) {
      try {
        out.object["bot_ram_motion_seq"] =
            integer(exact(raw.get("bot_ram_motion_seq"), 1));
      } catch (const std::invalid_argument &) {
        return {{}, "invalid_ram_motion_identity"};
      }
    }
    for (const auto *k : {"track_contacts_player", "track_contacts_bot"})
      if (has(raw, k)) {
        if (!list(raw.get(k)) || rows(raw.get(k)).size() > 2)
          return {{}, "invalid_track_contact"};
        std::set<std::string> names;
        for (const auto &v : rows(raw.get(k))) {
          const auto name = v.resolved().text;
          if (name != "leftTrackHealth" && name != "rightTrackHealth")
            return {{}, "invalid_track_contact"};
          names.insert(name);
        }
        auto values = array();
        for (const auto &n : names)
          values.array.push_back(text(n));
        out.object[k] = std::move(values);
      }
    if (!current)
      return {out, {}};
    const auto bot = intField(out, "bot_id");
    if (!state_.bots.count(bot) || revision > state_.bot_state_revision ||
        revision + 255 < state_.bot_state_revision ||
        stamp > state_.bot_state_time_us)
      return {{}, "invalid_contact_contract"};
    if (has(out, "bot_history_bracket")) {
      const auto &bracket = rows(out.get("bot_history_bracket"));
      const auto lr = bracket[0].integer, lt = bracket[1].integer,
                 rr = bracket[2].integer, rt = bracket[3].integer;
      const auto left = archive_.find(lr), right = archive_.find(rr);
      if (left == archive_.end() || right == archive_.end())
        return {{}, "canonical_history_missing"};
      if (left->second.stamp != lt || right->second.stamp != rt)
        return {{}, "canonical_history_time_mismatch"};
      const auto l = left->second.bots.find(bot),
                 r = right->second.bots.find(bot);
      if (l == left->second.bots.end() || r == right->second.bots.end() ||
          intField(l->second, "id") != intField(r->second, "id"))
        return {{}, "canonical_history_body_missing"};
      auto pinned = l->second;
      std::map<ID, Value> motions;
      const auto collect = [&](const Value &source) {
        for (const auto &m : rows(source.get("ram_motion")))
          if (rows(m).size() >= 21 && rows(m)[2].numeric() <= double(stamp)) {
            const auto peer = rows(m)[0].integer;
            const auto at = motions.find(peer);
            if (at == motions.end() ||
                rows(m)[1].integer > rows(at->second)[1].integer)
              motions[peer] = m;
          }
      };
      if (stamp < rt)
        collect(l->second);
      collect(r->second);
      pinned.object["ram_motion"] = array();
      for (const auto &m : motions)
        pinned.object["ram_motion"].array.push_back(m.second);
      if (lt != rt) {
        const double progress = double(stamp - lt) / double(rt - lt);
        for (const auto *k : {"x", "y", "z", "gun_pitch"})
          if (has(l->second, k) && has(r->second, k))
            pinned.object[k] =
                number(val(l->second, k) +
                       (val(r->second, k) - val(l->second, k)) * progress);
        for (const auto *k : {"yaw", "aim_yaw", "pitch", "roll"})
          if (has(l->second, k) && has(r->second, k))
            pinned.object[k] =
                number(val(l->second, k) +
                       angle(val(r->second, k) - val(l->second, k)) * progress);
        if (progress >= 1.)
          pinned.object["alive"] = boolean(r->second.get("alive").truth(true));
      }
      out.object["ram_bot_state"] = std::move(pinned);
    }
    if (has(out, "bot_ram_motion_seq")) {
      const Value *witness = nullptr;
      for (const auto &m : rows(out.get("ram_bot_state").get("ram_motion")))
        if (rows(m).size() >= 21 &&
            rows(m)[0].numeric() == double(intField(p, "id"))) {
          witness = &m;
          break;
        }
      if (!witness || rows(*witness)[1].numeric() !=
                          double(intField(out, "bot_ram_motion_seq")))
        return {{}, "canonical_ram_motion_mismatch"};
      std::size_t i = 0;
      for (const auto *k : {"bot_vx", "bot_vy", "bot_vz"})
        if (std::abs(val(out, k) - rows(*witness)[3 + i++].numeric()) > .0002)
          return {{}, "canonical_ram_motion_mismatch"};
      i = 0;
      for (const auto *k : {"x", "y", "z", "yaw", "pitch", "roll"}) {
        if (std::abs(val(out, k) - rows(*witness)[12 + i].numeric()) > .0002)
          return {{}, "canonical_ram_motion_pose_mismatch"};
        out.object["ram_bot_state"].object[k] = rows(*witness)[6 + i++];
      }
      i = 0;
      for (const auto *k : {"vx", "vy", "vz"})
        if (std::abs(val(out, k) - rows(*witness)[18 + i++].numeric()) > .0002)
          return {{}, "canonical_ram_motion_player_velocity_mismatch"};
    }
    return {std::move(out), {}};
  } catch (const std::invalid_argument &) {
    return {{}, "malformed_contact"};
  }
}
Value Inputs::destructibleContact(const Value &raw,
                                  const Value &physics) const {
  try {
    shape(raw, {"seq", "x", "y", "z", "yaw", "speed", "dt", "end_x", "end_y",
                "end_z", "end_yaw", "token"});
    auto out = object();
    out.object["seq"] = integer(exact(raw.get("seq"), 1));
    for (const auto *k : {"x", "z", "end_x", "end_z"})
      out.object[k] = number(scalar(raw.get(k), -2000., 2000.));
    for (const auto *k : {"y", "end_y"})
      out.object[k] = number(scalar(raw.get(k), -1000., 1000.));
    for (const auto *k : {"yaw", "end_yaw"})
      out.object[k] = number(scalar(raw.get(k), -2 * pi, 2 * pi));
    const double speed = scalar(raw.get("speed"), -200., 200.),
                 dt = scalar(raw.get("dt"), 0., .1, false);
    const double dx = val(out, "end_x") - val(out, "x"),
                 dy = val(out, "end_y") - val(out, "y"),
                 dz = val(out, "end_z") - val(out, "z"),
                 dyaw = angle(val(out, "end_yaw") - val(out, "yaw")),
                 offset = pivot(physics, out);
    if (std::abs(dy) > .25 ||
        std::hypot(dx, dz) >
            std::abs(speed) * dt + .25 + std::abs(offset * dyaw) ||
        std::abs(dyaw) > 8. * dt + .001 ||
        (std::hypot(dx, dz) <= .0001 && std::abs(dyaw) <= .00001))
      return {};
    const auto &tokens = rows(raw.get("token"));
    if (!list(raw.get("token")) || tokens.empty() || tokens.size() > 64)
      return {};
    std::set<std::array<ID, 3>> ordered;
    for (const auto &t : tokens) {
      const auto &r = rows(t);
      if (!list(t) || r.size() != 3)
        return {};
      ordered.insert({exact(r[0]), exact(r[1]), null(r[2]) ? -1 : exact(r[2])});
    }
    if (ordered.size() != tokens.size())
      return {};
    auto token = array();
    for (const auto &t : ordered) {
      auto row = array();
      row.array = {integer(t[0]), integer(t[1]),
                   t[2] < 0 ? Value{} : integer(t[2])};
      token.array.push_back(std::move(row));
    }
    out.object["token"] = std::move(token);
    out.object["speed"] = number(rounded(speed, 4));
    out.object["dt"] = number(rounded(dt, 6));
    return out;
  } catch (const std::invalid_argument &) {
    return {};
  }
}
void Inputs::contacts(Value &p, const Value &m, Outcome &outcome) {
  for (bool turret : {false, true}) {
    const auto *k = turret ? "turret_pushes" : "tank_pushes";
    if (!has(m, k))
      continue;
    Value incoming;
    try {
      incoming = normalizePushes(m.get(k), turret);
    } catch (const std::invalid_argument &) {
      continue;
    }
    auto &current = p.object[k];
    if (!list(current))
      current = array();
    for (const auto &row : rows(incoming)) {
      const auto key = pushKey(row, turret);
      if (turret) {
        const auto &objects = state_.resources.get("detached_turrets");
        if (!has(objects, key))
          continue;
      } else if (!state_.bots.count(rows(row)[0].integer))
        continue;
      auto previous = std::find_if(
          current.array.begin(), current.array.end(),
          [&](const Value &v) { return pushKey(v, turret) == key; });
      if (previous != current.array.end() &&
          rows(row)[1].integer <= rows(*previous)[1].integer)
        continue;
      if (!turret) {
        const double mx = rows(row)[2].numeric() -
                          (previous == current.array.end()
                               ? 0.
                               : rows(*previous)[2].numeric()),
                     mz = rows(row)[3].numeric() -
                          (previous == current.array.end()
                               ? 0.
                               : rows(*previous)[3].numeric()),
                     drive_x = std::sin(val(p, "yaw")) * val(p, "forward"),
                     drive_z = std::cos(val(p, "yaw")) * val(p, "forward");
        if (mx * drive_x + mz * drive_z > .05) {
          auto e = object();
          e.object["kind"] = text("environment_push");
          e.object["victim_kind"] = text("player");
          e.object["victim_id"] = p.get("id");
          e.object["attacker_kind"] = text("bot");
          e.object["attacker_id"] = rows(row)[0];
          state_.events.push_back(std::move(e));
        }
      }
      if (previous == current.array.end())
        current.array.push_back(row);
      else
        *previous = row;
    }
  }
  Value raw_ram;
  if (capability(p, "ram_contact_ledger_v2") && has(m, "ram_contacts")) {
    if (list(m.get("ram_contacts")) && rows(m.get("ram_contacts")).size() <= 16)
      raw_ram = m.get("ram_contacts");
  } else if (obj(m.get("ram_contact"))) {
    raw_ram = array();
    raw_ram.array.push_back(m.get("ram_contact"));
  }
  if (!null(raw_ram)) {
    auto &pending = p.object["ram_contacts"];
    if (!list(pending))
      pending = array();
    std::map<ID, Value> by_seq;
    std::set<ID> conflict;
    for (const auto &raw : rows(raw_ram)) {
      try {
        const auto seq = exact(raw.get("seq"), 1);
        auto old = by_seq.find(seq);
        if (old == by_seq.end())
          by_seq[seq] = raw;
        else if (old->second != raw)
          conflict.insert(seq);
      } catch (const std::invalid_argument &) {
      }
    }
    for (const auto &entry : by_seq) {
      const auto seq = entry.first;
      if (seq <= intField(p, "ram_contact_seq"))
        continue;
      if (capability(p, "ram_contact_ledger_v2") &&
          seq != intField(p, "ram_contact_seq") + 1)
        break;
      if (pending.array.size() >= 32)
        break;
      auto contact =
          conflict.count(seq)
              ? std::make_pair(Value{},
                               std::string("conflicting_contact_payload"))
              : ramContact(p, entry.second, true);
      p.object["ram_contact_seq"] = integer(seq);
      p.object["ram_contact_admitted_seq"] = integer(seq);
      if (null(contact.first)) {
        auto &rejections = p.object["ram_contact_rejections"];
        if (!list(rejections))
          rejections = array();
        auto r = object();
        r.object["seq"] = integer(seq);
        r.object["reason"] = text(contact.second);
        rejections.array.push_back(std::move(r));
        while (rejections.array.size() > 32)
          rejections.array.erase(rejections.array.begin());
        advanceRam(p);
        continue;
      }
      contact.first.object["input_seq"] = integer(intField(p, "input_seq"));
      pending.array.push_back(contact.first);
      p.object["ram_contact"] = std::move(contact.first);
    }
  }
  if (!capability(p, "human_ram_timeline_v1") ||
      !has(m, "destructible_contacts") ||
      !list(m.get("destructible_contacts")) ||
      rows(m.get("destructible_contacts")).size() > 16)
    return;
  auto &pending = p.object["destructible_contacts"];
  if (!list(pending))
    pending = array();
  std::map<ID, Value> by_seq;
  std::set<ID> conflict;
  for (const auto &raw : rows(m.get("destructible_contacts"))) {
    try {
      const auto seq = exact(raw.get("seq"), 1);
      const auto old = by_seq.find(seq);
      if (old == by_seq.end())
        by_seq[seq] = raw;
      else if (old->second != raw)
        conflict.insert(seq);
    } catch (const std::invalid_argument &) {
    }
  }
  for (const auto &entry : by_seq) {
    const auto seq = entry.first;
    if (seq <= intField(p, "destructible_contact_seq"))
      continue;
    if (seq != intField(p, "destructible_contact_seq") + 1 ||
        seq > intField(p, "destructible_contact_resolved_seq") + 64 ||
        pending.array.size() >= 16)
      break;
    auto contact =
        conflict.count(seq)
            ? Value{}
            : destructibleContact(entry.second,
                                  p.get("effective_params").get("physics"));
    const Value *sample = nullptr;
    if (!null(contact)) {
      const double ht = std::max(.02, std::abs(val(contact, "speed")) *
                                              val(contact, "dt") +
                                          .25),
                   yt = .25, at = std::max(.002, 8 * val(contact, "dt") + .001);
      const auto &history = rows(p.get("pose_history"));
      for (auto i = history.rbegin(); i != history.rend(); ++i) {
        if (std::hypot(val(*i, "x") - val(contact, "x"),
                       val(*i, "z") - val(contact, "z")) > ht ||
            std::abs(val(*i, "y") - val(contact, "y")) > yt ||
            std::abs(angle(val(*i, "yaw") - val(contact, "yaw"))) > at)
          continue;
        sample = &*i;
        break;
      }
    }
    p.object["destructible_contact_seq"] = integer(seq);
    p.object["destructible_contact_admitted_seq"] = integer(seq);
    if (null(contact) || !sample) {
      destructibleResolved(p, seq);
      auto &rejected = p.object["destructible_contact_rejected_seqs"];
      if (!list(rejected))
        rejected = array();
      rejected.array.push_back(integer(seq));
      while (rejected.array.size() > 16)
        rejected.array.erase(rejected.array.begin());
      auto terminal = object();
      terminal.object["type"] = text("player_destructible_contact_result");
      terminal.object["round_id"] = integer(state_.round_id);
      terminal.object["contact_seq"] = integer(seq);
      terminal.object["accepted"] = boolean(false);
      outcome.replies.push_back(std::move(terminal));
      continue;
    }
    const double offset =
                     pivot(p.get("effective_params").get("physics"), contact),
                 dyaw = angle(val(contact, "end_yaw") - val(contact, "yaw"));
    contact.object["input_seq"] = sample->get("input_seq");
    contact.object["pose_time_us"] = sample->get("time_us");
    for (const auto *k : {"x", "y", "z"})
      contact.object[k] = number(rounded(val(*sample, k), 4));
    for (const auto *k : {"yaw", "pitch", "roll"})
      contact.object[k] = number(rounded(val(*sample, k), 5));
    contact.object["forward"] = number(rounded(val(*sample, "forward"), 4));
    if (offset != 0.) {
      const double yaw = val(contact, "yaw"), end = angle(yaw + dyaw);
      contact.object["end_yaw"] = number(end);
      contact.object["end_x"] =
          number(val(contact, "x") + offset * (std::cos(yaw) - std::cos(end)));
      contact.object["end_y"] = contact.get("y");
      contact.object["end_z"] =
          number(val(contact, "z") + offset * (std::sin(end) - std::sin(yaw)));
    }
    pending.array.push_back(contact);
    auto relay = object();
    relay.object["type"] = text("player_destructible_contact");
    relay.object["protocol"] = integer(5);
    relay.object["round_id"] = integer(state_.round_id);
    relay.object["authority_epoch"] = integer(state_.authority_epoch);
    auto player = select(p, {"id", "vehicle", "vehicle_compact_descr"});
    player.object["destructible_contacts"] = array();
    player.object["destructible_contacts"].array.push_back(std::move(contact));
    relay.object["player"] = std::move(player);
    state_.events.push_back(std::move(relay));
  }
}
Outcome Inputs::dispatch(Sender sender, const Value &m) {
  if (sender.worker)
    return Outcome::reject("input_owner");
  auto at = state_.players.find(sender.id);
  if (at == state_.players.end())
    return Outcome::reject("unknown_player");
  auto &p = at->second;
  const bool inactive =
      state_.phase != "battle" || !null(state_.battle_result) ||
      !p.get("participating").truth() || !p.get("alive").truth();
  std::optional<ID> submitted;
  try {
    if (has(m, "input_seq"))
      submitted = exact(m.get("input_seq"), 1);
  } catch (const std::invalid_argument &) {
  }
  bool round = false;
  try {
    const auto &raw = m.get("round_id").resolved();
    if (raw.kind == Kind::Integer || raw.kind == Kind::Number)
      round = raw.numeric() == double(state_.round_id);
    else if (raw.kind == Kind::Text) {
      std::size_t end = 0;
      const auto n = std::stoll(raw.text, &end);
      while (end < raw.text.size() &&
             std::isspace(static_cast<unsigned char>(raw.text[end])))
        ++end;
      round = end == raw.text.size() && n == state_.round_id;
    }
  } catch (...) {
  }
  if (!obj(m) || !round)
    return reject(p, submitted, "round_mismatch", "round_id", false, !inactive);
  if (!p.get("connected").truth())
    return reject(p, submitted, "player_disconnected", {}, false, false);
  const bool ledger = capability(p, "human_ram_timeline_v1");
  ID seq = 0;
  std::string finger;
  if (ledger) {
    try {
      seq = exact(m.get("input_seq"), 1);
      finger = fingerprint(m);
    } catch (const std::invalid_argument &) {
      return reject(p, {}, "sequence_identity", "input_seq");
    }
    auto &book = players_[sender.id];
    const auto old = book.decisions.find(seq);
    if (old != book.decisions.end()) {
      const auto &d = old->second;
      if (d.fingerprint != finger)
        return reject(p, seq, "identity_conflict", "input_seq");
      if (d.outcome == "rejected")
        return reject(p, seq, d.reason, d.field, true, d.active);
      return Outcome::accept();
    }
    if (seq <= intField(p, "input_processed_seq"))
      return reject(p, seq, "sequence_retired", "input_seq");
    if (seq != intField(p, "input_processed_seq") + 1)
      return reject(p, seq, "sequence_gap", "input_seq");
  }
  std::string reason, where;
  Value shell, gun, up, body;
  const auto fail = [&](const char *r, const char *k) {
    if (reason.empty()) {
      reason = r;
      where = k;
    }
  };
  const std::set<std::string> allowed = {"type",
                                         "round_id",
                                         "input_seq",
                                         "forward",
                                         "turn",
                                         "speed",
                                         "aim_yaw",
                                         "gun_pitch",
                                         "x",
                                         "y",
                                         "z",
                                         "yaw",
                                         "pitch",
                                         "roll",
                                         "pose_time_us",
                                         "fire_seq",
                                         "shell_index",
                                         "next_shell_index",
                                         "shell_change_pending",
                                         "gun_checkpoint",
                                         "ram_contacts",
                                         "tank_pushes",
                                         "turret_pushes",
                                         "ram_contact",
                                         "destructible_contacts",
                                         "siege_enabled",
                                         "up_cosine",
                                         "wreck_pose",
                                         "hydraulic_body"};
  if (has(m, "type") && field(m, "type") != "input")
    fail("field_whitelist", "type");
  if (!has(m, "round_id"))
    fail("field_required", "round_id");
  for (const auto &v : m.resolved().object)
    if (!allowed.count(v.first))
      fail("field_whitelist", v.first.c_str());
  if (has(m, "wreck_pose") &&
      m.get("wreck_pose").resolved().kind != Kind::Boolean)
    fail("wreck_pose_shape", "wreck_pose");
  const bool has_next = has(m, "next_shell_index"),
             has_pending = has(m, "shell_change_pending");
  if (has_next != has_pending)
    fail("shell_pair_shape",
         has_next ? "next_shell_index" : "shell_change_pending");
  if (has_next && !has(m, "shell_index"))
    fail("shell_pair_shape", "shell_index");
  if (has(m, "shell_index")) {
    try {
      const auto loaded = exact(m.get("shell_index"), 0, 9);
      ID next = loaded;
      try {
        if (has_next)
          next = exact(m.get("next_shell_index"), 0, 9);
      } catch (const std::invalid_argument &) {
        fail("shell_selection", "next_shell_index");
      }
      if (has_pending &&
          m.get("shell_change_pending").resolved().kind != Kind::Boolean)
        fail("shell_selection", "shell_change_pending");
      const bool pending = has_pending && m.get("shell_change_pending").truth();
      if (!pending && next != loaded)
        fail("shell_selection", "next_shell_index");
      shell = array();
      shell.array = {integer(loaded), integer(next), boolean(pending)};
    } catch (const std::invalid_argument &) {
      fail("shell_selection", "shell_index");
    }
  }
  if (field(m, "type") == "input" &&
      capability(p, "player_projectile_owner_v1") && !has(m, "gun_checkpoint"))
    fail("gun_checkpoint_missing", "gun_checkpoint");
  if (has(m, "gun_checkpoint")) {
    if (null(shell) || !ledger)
      fail("gun_checkpoint_context", "gun_checkpoint");
    try {
      gun = checkpoint(m.get("gun_checkpoint"));
    } catch (const std::invalid_argument &) {
      fail("gun_checkpoint_shape", "gun_checkpoint");
    }
  }
  if (has(m, "up_cosine")) {
    try {
      up = number(scalar(m.get("up_cosine"), -1., 1.));
    } catch (const std::invalid_argument &) {
      fail("world_up", "up_cosine");
    }
  }
  if (has(m, "hydraulic_body")) {
    if (!(has(m, "x") && has(m, "y") && has(m, "z") && has(m, "yaw")))
      fail("hydraulic_body_context", "hydraulic_body");
    try {
      body = hydraulic(m.get("hydraulic_body"));
    } catch (const std::invalid_argument &) {
      fail("hydraulic_body_shape", "hydraulic_body");
    }
  }
  try {
    exact(m.get("round_id"), 1);
  } catch (const std::invalid_argument &) {
    fail("envelope_round_id", "round_id");
  }
  const std::array<std::tuple<const char *, double, double>, 11> bounded{
      {{"forward", -1., 1.},
       {"turn", -1., 1.},
       {"speed", -200., 200.},
       {"aim_yaw", -2 * pi, 2 * pi},
       {"gun_pitch", -1.2, 1.2},
       {"x", -2000., 2000.},
       {"y", -1000., 1000.},
       {"z", -2000., 2000.},
       {"yaw", -2 * pi, 2 * pi},
       {"pitch", -pi, pi},
       {"roll", -pi, pi}}};
  for (const auto &v : bounded)
    if (has(m, std::get<0>(v)))
      try {
        scalar(m.get(std::get<0>(v)), std::get<1>(v), std::get<2>(v));
      } catch (const std::invalid_argument &) {
        fail("envelope_numeric", std::get<0>(v));
      }
  for (const auto *k : {"pose_time_us", "fire_seq", "input_seq"})
    if (has(m, k))
      try {
        exact(m.get(k), std::string(k) == "input_seq" ? 1 : 0,
              std::string(k) == "pose_time_us" ? max_motion : max_id);
      } catch (const std::invalid_argument &) {
        fail("envelope_integer", k);
      }
  if (ledger && !has(m, "input_seq"))
    fail("envelope_integer", "input_seq");
  if (has(m, "siege_enabled") &&
      m.get("siege_enabled").resolved().kind != Kind::Boolean)
    fail("envelope_siege", "siege_enabled");
  if (inactive) {
    for (bool turret : {false, true}) {
      const auto *k = turret ? "turret_pushes" : "tank_pushes";
      if (has(m, k))
        try {
          normalizePushes(m.get(k), turret);
        } catch (const std::invalid_argument &) {
          fail("envelope_contacts", k);
        }
    }
    const auto &ram = m.get("ram_contacts");
    if (has(m, "ram_contacts") && (!list(ram) || rows(ram).size() > 16))
      fail("envelope_contacts", "ram_contacts");
    else
      for (const auto &r : rows(ram))
        if (null(ramContact(p, r, false).first)) {
          fail("envelope_contacts", "ram_contacts");
          break;
        }
    if (has(m, "ram_contact") &&
        null(ramContact(p, m.get("ram_contact"), false).first))
      fail("envelope_contacts", "ram_contact");
    const auto &destructible = m.get("destructible_contacts");
    if (has(m, "destructible_contacts") &&
        (!list(destructible) || rows(destructible).size() > 16))
      fail("envelope_contacts", "destructible_contacts");
    else
      for (const auto &r : rows(destructible))
        if (null(destructibleContact(
                r, p.get("effective_params").get("physics")))) {
          fail("envelope_contacts", "destructible_contacts");
          break;
        }
  }
  if (!reason.empty()) {
    auto outcome = reject(p, ledger ? std::optional<ID>(seq) : std::nullopt,
                          reason, where, ledger, !inactive);
    if (ledger)
      decision(p, seq, finger, "rejected", reason, where, !inactive);
    return outcome;
  }
  const auto previous = p;
  if (inactive) {
    if (m.get("wreck_pose").resolved().kind == Kind::Boolean &&
        m.get("wreck_pose").truth() && !p.get("alive").truth() &&
        state_.phase == "battle" && null(state_.battle_result) &&
        p.get("participating").truth()) {
      for (const auto *k : {"x", "y", "z", "yaw", "pitch", "roll"})
        if (has(m, k))
          p.object[k] = number(val(m, k));
      for (const auto *k : {"forward", "turn", "speed"})
        p.object[k] = number(0.);
      p.object["client_position"] = boolean(true);
      pose(p, m);
    }
    if (ledger)
      decision(p, seq, finger, "inactive", "inactive", {}, false);
    return Outcome::accept();
  }
  if (ledger) {
    decision(p, seq, finger, "applied");
    p.object["input_seq"] = integer(seq);
    auto &history = players_[sender.id].fingerprints;
    history[seq] = finger;
    while (history.size() > 128)
      history.erase(history.begin());
    auto applied = array();
    for (const auto &v : history)
      applied.array.push_back(integer(v.first));
    p.object["input_applied_seqs"] = std::move(applied);
    if (!null(gun)) {
      p.object["gun_checkpoint_seq"] = integer(seq);
      p.object["gun_checkpoint"] = gun;
      auto &checkpoints = p.object["gun_checkpoints"];
      if (!obj(checkpoints))
        checkpoints = object();
      checkpoints.object[std::to_string(seq)] = gun;
      while (checkpoints.object.size() > 128) {
        auto first = checkpoints.object.begin();
        for (auto i = checkpoints.object.begin(); i != checkpoints.object.end();
             ++i)
          if (std::stoll(i->first) < std::stoll(first->first))
            first = i;
        checkpoints.object.erase(first->first);
      }
    }
  }
  if (state_.server_tick <
      ID(std::nearbyint(state_.config.get("prebattle_seconds").numeric(10.) *
                        30.))) {
    p.object["forward"] = number(0.);
    p.object["turn"] = number(0.);
    return Outcome::reject("combat_inactive");
  }
  const auto mode = intField(p, "siege_state");
  if (mode == 1 || mode == 3) {
    for (const auto *k : {"forward", "turn", "speed"})
      p.object[k] = number(0.);
  } else {
    for (const auto *k : {"forward", "turn"})
      if (has(m, k))
        p.object[k] = number(val(m, k));
    if (has(m, "speed")) {
      const auto params = siege(p);
      const double limit = params[0] != 0. && mode == 2 ? params[2] : 200.;
      p.object["speed"] =
          number(std::max(-limit, std::min(limit, val(m, "speed"))));
    }
  }
  if (has(m, "siege_enabled"))
    requestSiege(p, m.get("siege_enabled").truth());
  for (const auto *k : {"aim_yaw", "gun_pitch"})
    if (has(m, k))
      p.object[k] = number(val(m, k));
  if (has(m, "x") && has(m, "z")) {
    for (const auto *k : {"x", "y", "z", "yaw", "pitch", "roll"})
      if (has(m, k))
        p.object[k] = number(val(m, k));
    if (!null(up))
      p.object["up_cosine"] = number(rounded(up.numeric(), 6));
    p.object["hydraulic_body"] = body;
    p.object["client_position"] = boolean(true);
    if (ledger)
      pose(p, m);
  }
  Outcome outcome = Outcome::accept();
  contacts(p, m, outcome);
  if (!null(shell)) {
    p.object["shell_index"] = rows(shell)[0];
    p.object["next_shell_index"] = rows(shell)[1];
    p.object["shell_change_pending"] = rows(shell)[2];
  }
  if (advanced_)
    advanced_(sender.id, previous, m);
  return outcome;
}
} // namespace wot::room
