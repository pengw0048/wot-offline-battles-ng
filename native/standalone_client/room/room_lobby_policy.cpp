#include "room_lobby_policy.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <iomanip>
#include <iterator>
#include <regex>
#include <sstream>
#include <tuple>
namespace wot::room::lobby {
namespace {
using K = Value::Kind;
void require(bool b, const char *message) {
  if (!b)
    throw std::invalid_argument(message);
}
bool contains(const std::vector<std::string> &values, const std::string &name) {
  return std::find(values.begin(), values.end(), name) != values.end();
}
std::vector<std::uint32_t> codepoints(const std::string &text) {
  std::vector<std::uint32_t> out;
  for (std::size_t i = 0; i < text.size();) {
    const auto byte = static_cast<unsigned char>(text[i++]);
    std::uint32_t cp = byte;
    int extra = 0;
    if (byte >= 0xc2 && byte <= 0xdf) {
      cp = byte & 31;
      extra = 1;
    } else if (byte >= 0xe0 && byte <= 0xef) {
      cp = byte & 15;
      extra = 2;
    } else if (byte >= 0xf0 && byte <= 0xf4) {
      cp = byte & 7;
      extra = 3;
    } else
      require(byte < 128, "Invalid UTF-8 text");
    require(i + static_cast<std::size_t>(extra) <= text.size(),
            "Incomplete UTF-8 text");
    for (int n = 0; n < extra; ++n) {
      const auto next = static_cast<unsigned char>(text[i++]);
      require((next & 0xc0) == 0x80, "Invalid UTF-8 continuation");
      cp = (cp << 6) | (next & 63);
    }
    require(cp <= 0x10ffff && (cp < 0xd800 || cp > 0xdfff) &&
                (extra != 1 || cp >= 128) && (extra != 2 || cp >= 2048) &&
                (extra != 3 || cp >= 65536),
            "Invalid UTF-8 scalar");
    out.push_back(cp);
  }
  return out;
}
std::string utf8(std::uint32_t c) {
  std::string out;
  if (c < 128)
    out += static_cast<char>(c);
  else if (c < 2048) {
    out += static_cast<char>(0xc0 | (c >> 6));
    out += static_cast<char>(0x80 | (c & 63));
  } else if (c < 65536) {
    out += static_cast<char>(0xe0 | (c >> 12));
    out += static_cast<char>(0x80 | ((c >> 6) & 63));
    out += static_cast<char>(0x80 | (c & 63));
  } else {
    out += static_cast<char>(0xf0 | (c >> 18));
    out += static_cast<char>(0x80 | ((c >> 12) & 63));
    out += static_cast<char>(0x80 | ((c >> 6) & 63));
    out += static_cast<char>(0x80 | (c & 63));
  }
  return out;
}
bool inRanges(std::uint32_t cp, const Value &ranges) {
  const auto &r = rows(ranges);
  const auto it = std::upper_bound(
      r.begin(), r.end(), cp, [](std::uint32_t c, const Value &p) {
        return c < static_cast<std::uint32_t>(rows(p)[0].resolved().integer);
      });
  return it != r.begin() &&
         cp <= static_cast<std::uint32_t>(rows(*(it - 1))[1].resolved().integer);
}
bool whitespace(std::uint32_t cp, const Value &r) {
  return std::any_of(rows(r.get("unicode_whitespace")).begin(),
                     rows(r.get("unicode_whitespace")).end(),
                     [&](const Value &v) { return v.resolved().integer == cp; });
}
std::vector<std::string> strings(const Value &a) {
  std::vector<std::string> r;
  for (const auto &v : rows(a))
    r.push_back(v.resolved().text);
  return r;
}
std::int64_t pythonInteger(const std::string &text, const Value &resources) {
  auto cps = codepoints(text);
  std::size_t first = 0, last = cps.size();
  while (first < last && whitespace(cps[first], resources))
    ++first;
  while (first < last && whitespace(cps[last - 1], resources))
    --last;
  require(first < last, "Empty integer");
  bool negative = false;
  if (cps[first] == '+' || cps[first] == '-') {
    negative = cps[first] == '-';
    ++first;
  }
  require(first < last, "Empty integer");
  std::int64_t result = 0;
  bool digit_before = false;
  for (std::size_t i = first; i < last; ++i) {
    const auto cp = cps[i];
    if (cp == '_') {
      require(digit_before && i + 1 < last, "Invalid integer underscore");
      digit_before = false;
      continue;
    }
    const auto &mapped =
        resources.get("unicode_decimal").get(std::to_string(cp)).resolved();
    const auto digit = cp >= '0' && cp <= '9'
                           ? static_cast<std::int64_t>(cp - '0')
                       : mapped.kind == K::Integer ? mapped.integer
                                                   : -1;
    require(digit >= 0 && digit <= 9 && result <= (INT64_MAX - digit) / 10,
            "Invalid integer digit");
    result = result * 10 + digit;
    digit_before = true;
  }
  require(digit_before, "Trailing integer underscore");
  return negative ? -result : result;
}
bool vehicleName(const Value &input) {
  const auto &value = input.resolved();
  return value.resolved().kind == K::Text && value.text.size() <= 96 &&
         std::regex_match(
             value.text,
             std::regex("[a-z][a-z0-9_]*:[A-Za-z0-9][A-Za-z0-9_.-]*"));
}
std::string base64(const std::vector<unsigned char> &bytes) {
  static const char *t =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  for (std::size_t i = 0; i < bytes.size(); i += 3) {
    unsigned n = unsigned(bytes[i]) << 16;
    if (i + 1 < bytes.size())
      n |= unsigned(bytes[i + 1]) << 8;
    if (i + 2 < bytes.size())
      n |= bytes[i + 2];
    out += t[(n >> 18) & 63];
    out += t[(n >> 12) & 63];
    out += i + 1 < bytes.size() ? t[(n >> 6) & 63] : '=';
    out += i + 2 < bytes.size() ? t[n & 63] : '=';
  }
  return out;
}
std::string canonicalBase64(const Value &input) {
  const auto &raw = input.resolved();
  require(raw.resolved().kind == K::Text, "Invalid base64 text");
  const auto &s = raw.text;
  static const std::string alphabet =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  const auto first = s.find('=');
  const auto length = first == std::string::npos ? s.size() : first;
  const auto padding = s.size() - length;
  require(length > 0 && length % 4 != 1, "Invalid base64 length");
  for (std::size_t n = length; n < s.size(); ++n)
    require(s[n] == '=', "Invalid base64 padding");
  require(length % 4 == 0 || (length % 4 == 2 && padding == 2) ||
              (length % 4 == 3 && padding == 1),
          "Invalid base64 padding");
  std::vector<unsigned char> bytes;
  unsigned acc = 0;
  int bits = 0;
  for (std::size_t i = 0; i < length; ++i) {
    const auto v = alphabet.find(s[i]);
    require(v != std::string::npos, "Invalid base64 alphabet");
    acc = (acc << 6) | static_cast<unsigned>(v);
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      bytes.push_back(static_cast<unsigned char>((acc >> bits) & 255));
    }
  }
  require(!bytes.empty() && bytes.size() <= 65536,
          "Invalid base64 payload size");
  return base64(bytes);
}
} // namespace
std::string string(const Value &v, const std::string &k,
                   const std::string &fallback) {
  const auto &r = v.get(k).resolved();
  return r.kind == K::Text ? r.text : fallback;
}
std::int64_t strictInt(const Value &raw, std::int64_t low, std::int64_t high) {
  const auto &v = raw.resolved();
  require(v.kind == K::Integer && !v.unsigned_integer && v.integer >= low &&
              v.integer <= high,
          "Invalid integer");
  return v.integer;
}
std::string pythonString(const Value &input) {
  const auto &v = input.resolved();
  switch (v.resolved().kind) {
  case K::Text:
    return v.resolved().text;
  case K::Null:
    return "None";
  case K::Boolean:
    return v.boolean ? "True" : "False";
  case K::Integer:
    return std::to_string(v.integer);
  case K::Number: {
    std::ostringstream s;
    s << std::setprecision(17) << v.number;
    auto result = s.str();
    if (result.find_first_of(".eE") == std::string::npos)
      result += ".0";
    return result;
  }
  default:
    throw std::invalid_argument("Text coercion requires a scalar");
  }
}
std::string truncate(const std::string &s, std::size_t limit) {
  auto cps = codepoints(s);
  if (cps.size() > limit)
    cps.resize(limit);
  std::string out;
  for (auto c : cps)
    out += utf8(c);
  return out;
}
std::string safe(const Value &v, const std::string &fallback, const Value &r,
                 bool vehicle) {
  auto cps = codepoints(v.truth() ? pythonString(v) : fallback);
  auto begin = cps.begin(), end = cps.end();
  while (begin != end && whitespace(*begin, r))
    ++begin;
  while (begin != end && whitespace(*(end - 1), r))
    --end;
  std::string out;
  std::size_t n = 0;
  for (auto it = begin; it != end && n < (vehicle ? 64u : 24u); ++it) {
    const auto cp = *it;
    if (inRanges(cp, r.get("unicode_alnum_ranges")) || cp == '_' || cp == '-' ||
        cp == (vehicle ? ':' : ' ')) {
      out += utf8(cp);
      ++n;
    }
  }
  return out.empty() ? fallback : out;
}
std::string lower(const std::string &s, const Value &r) {
  const auto cps = codepoints(s);
  std::string out;
  for (std::size_t i = 0; i < cps.size(); ++i) {
    const auto cp = cps[i];
    if (cp == 0x3a3) {
      bool before = false, after = false;
      for (std::size_t p = i; p > 0;) {
        --p;
        if (inRanges(cps[p], r.get("unicode_case_ignorable_ranges")))
          continue;
        before = inRanges(cps[p], r.get("unicode_cased_ranges"));
        break;
      }
      for (std::size_t p = i + 1; p < cps.size(); ++p) {
        if (inRanges(cps[p], r.get("unicode_case_ignorable_ranges")))
          continue;
        after = inRanges(cps[p], r.get("unicode_cased_ranges"));
        break;
      }
      out += utf8(before && !after ? 0x3c2 : 0x3c3);
      continue;
    }
    const auto &mapped = r.get("unicode_lower").get(std::to_string(cp)).resolved();
    out += mapped.kind == K::Text ? mapped.text : utf8(cp);
  }
  return out;
}
std::string alnum(const std::string &source, const Value &r,
                  std::size_t limit) {
  std::string out;
  std::size_t count = 0;
  for (const auto cp : codepoints(source))
    if (inRanges(cp, r.get("unicode_alnum_ranges"))) {
      if (count++ >= limit)
        break;
      out += utf8(cp);
    }
  return out;
}
int requestedTeam(const Value &input, const Value &r) {
  const auto &v = input.resolved();
  if (v.kind == K::Null ||
      (v.kind == K::Text &&
       (v.text.empty() || v.text == "auto" || v.text == "0")) ||
      (v.kind == K::Boolean && !v.boolean) ||
      ((v.kind == K::Integer || v.kind == K::Number) && v.numeric() == 0))
    return 0;
  require(v.kind != K::Boolean, "Invalid team");
  if (v.kind == K::Integer) {
    require(v.integer == 1 || v.integer == 2, "Invalid team");
    return static_cast<int>(v.integer);
  }
  require(v.kind == K::Text, "Invalid team");
  auto cps = codepoints(v.text);
  auto first = cps.begin(), last = cps.end();
  while (first != last && whitespace(*first, r))
    ++first;
  while (first != last && whitespace(*(last - 1), r))
    --last;
  require(last - first == 1 && (*first == '1' || *first == '2'),
          "Invalid team");
  return *first == '1' ? 1 : 2;
}

bool locked(const State &s) {
  return std::any_of(rows(s.config.get("bot_lineup")).begin(),
                     rows(s.config.get("bot_lineup")).end(),
                     [](const Value &r) { return has(r, "enabled"); });
}
void normalizeConfiguration(Value &c, const Value &r) {
  Value result = array();
  std::set<std::pair<int, int>> seen;
  const auto &raw = c.get("bot_lineup").resolved();
  require(raw.kind == K::Null ||
              (raw.kind == K::Array && raw.array.size() <= 30),
          "Invalid Bot lineup");
  const auto skills = strings(r.get("lobby").get("skill_tiers"));
  const auto classes = strings(r.get("lobby").get("classes"));
  const std::set<std::string> nations = {"ussr",   "germany", "usa",   "france",
                                         "uk",     "china",   "japan", "czech",
                                         "sweden", "poland"};
  for (const auto &input_row : rows(raw)) {
    const auto &row = input_row.resolved();
    require(row.kind == K::Object, "Invalid Bot lineup row");
    const int team = static_cast<int>(strictInt(row.get("team"), 1, 2)),
              slot = static_cast<int>(strictInt(row.get("slot"), 0, 14));
    require(seen.emplace(team, slot).second, "Duplicate Bot lineup slot");
    require(row.get("vehicle").resolved().kind != K::Null ||
                row.get("skill").resolved().kind != K::Null || has(row, "enabled"),
            "Empty Bot lineup row");
    Value entry = object();
    entry.object["team"] = integer(team);
    entry.object["slot"] = integer(slot);
    if (row.get("vehicle").resolved().kind != K::Null) {
      require(vehicleName(row.get("vehicle")), "Invalid Bot lineup vehicle");
      entry.object["vehicle"] = row.get("vehicle");
    }
    if (row.get("skill").resolved().kind != K::Null) {
      require(row.get("skill").resolved().kind == K::Text &&
                  contains(skills, row.get("skill").resolved().text),
              "Invalid Bot lineup skill");
      entry.object["skill"] = row.get("skill");
    }
    if (has(row, "enabled")) {
      require(row.get("enabled").resolved().kind == K::Boolean,
              "Invalid Bot lineup enabled");
      require(row.get("enabled").resolved().boolean || !has(entry, "vehicle"),
              "Disabled Bot vehicle");
      entry.object["enabled"] = row.get("enabled");
    }
    for (const auto &field : {"nation", "vehicle_class"})
      if (row.get(field).resolved().kind != K::Null) {
        require(row.get(field).resolved().kind == K::Text &&
                    (std::string(field) == "nation"
                         ? nations.count(row.get(field).resolved().text) != 0
                         : contains(classes, row.get(field).resolved().text)),
                "Invalid Bot lineup filter");
        if (has(row, "enabled"))
          entry.object[field] = row.get(field);
      }
    result.array.push_back(std::move(entry));
  }
  std::sort(result.array.begin(), result.array.end(),
            [](const Value &a, const Value &b) {
              return std::make_pair(intField(a, "team"), intField(a, "slot")) <
                     std::make_pair(intField(b, "team"), intField(b, "slot"));
            });
  c.object["bot_lineup"] = std::move(result);
  const bool explicit_slots = std::any_of(
      rows(c.get("bot_lineup")).begin(), rows(c.get("bot_lineup")).end(),
      [](const Value &v) { return has(v, "enabled"); });
  for (int team : {1, 2}) {
    auto key = "team" + std::to_string(team) + "_size";
    auto size = has(c, key) ? strictInt(c.get(key), 1, 15) : 15;
    if (explicit_slots)
      size = std::count_if(
          rows(c.get("bot_lineup")).begin(), rows(c.get("bot_lineup")).end(),
          [&](const Value &v) {
            return intField(v, "team") == team && v.get("enabled").truth();
          });
    c.object[key] = integer(size);
  }
  Value exclusions = array();
  const auto &excluded = c.get("bot_excluded_vehicles").resolved();
  require(excluded.kind == K::Null ||
              (excluded.kind == K::Array && excluded.array.size() <= 4096),
          "Invalid Bot exclusions");
  std::set<std::string> names;
  for (const auto &v : rows(excluded)) {
    require(vehicleName(v), "Invalid Bot exclusion vehicle");
    names.insert(v.resolved().text);
  }
  for (const auto &name : names)
    exclusions.array.push_back(text(name));
  c.object["bot_excluded_vehicles"] = std::move(exclusions);
}
std::vector<int> slots(const State &s, int team) {
  std::vector<int> out;
  if (locked(s)) {
    for (const auto &row : rows(s.config.get("bot_lineup")))
      if (intField(row, "team") == team && row.get("enabled").truth())
        out.push_back(static_cast<int>(intField(row, "slot")));
  } else
    for (int i = 0;
         i < intField(s.config, "team" + std::to_string(team) + "_size", 15);
         ++i)
      out.push_back(i);
  return out;
}
void spawn(Value &p, int team, int slot) {
  p.object["team"] = integer(team);
  p.object["slot"] = integer(slot);
  p.object["x"] = number(team ? slot * 12. : 0.);
  p.object["z"] = number(team ? (team == 1 ? -35. : 35.) : 0.);
  p.object["y"] = number(0.);
  p.object["yaw"] = number(team == 2 ? 3.141592653589793 : 0.);
  p.object["aim_yaw"] = p.get("yaw");
}
std::optional<std::vector<Assignment>> assignment(const State &s,
                                                  std::mt19937 &rng) {
  std::vector<Assignment> out;
  std::vector<std::int64_t> automatic;
  std::map<int, std::set<int>> occupied;
  const auto one = slots(s, 1), two = slots(s, 2);
  const auto count =
      std::count_if(s.players.begin(), s.players.end(), [](const auto &p) {
        return p.second.get("connected").truth();
      });
  if (static_cast<std::size_t>(count) > one.size() + two.size())
    return {};
  for (const auto &[id, p] : s.players) {
    if (!p.get("connected").truth())
      continue;
    const int team = static_cast<int>(intField(p, "requested_team"));
    if (team == 0) {
      automatic.push_back(id);
      continue;
    }
    auto available = slots(s, team);
    available.erase(std::remove_if(available.begin(), available.end(),
                                   [&](int slot) {
                                     return occupied[team].count(slot) != 0;
                                   }),
                    available.end());
    if (available.empty())
      return {};
    int slot = static_cast<int>(intField(p, "slot", -1));
    if (locked(s) || intField(p, "team") != team ||
        std::find(available.begin(), available.end(), slot) == available.end())
      slot = available.front();
    occupied[team].insert(slot);
    out.push_back({id, team, slot});
  }
  for (const auto id : automatic) {
    std::vector<int> candidates;
    for (int team : {1, 2})
      if (occupied[team].size() < slots(s, team).size())
        candidates.push_back(team);
    if (candidates.empty())
      return {};
    const auto minimum = std::min(occupied[candidates.front()].size(),
                                  occupied[candidates.back()].size());
    candidates.erase(std::remove_if(candidates.begin(), candidates.end(),
                                    [&](int team) {
                                      return occupied[team].size() != minimum;
                                    }),
                     candidates.end());
    const int team = candidates[std::uniform_int_distribution<std::size_t>(
        0, candidates.size() - 1)(rng)];
    for (const auto slot : slots(s, team))
      if (!occupied[team].count(slot)) {
        occupied[team].insert(slot);
        out.push_back({id, team, slot});
        break;
      }
  }
  return out;
}
Context context(const State &s, int team, const std::vector<Assignment> *plan) {
  Context out;
  std::set<std::pair<int, int>> occupied;
  int humans = 0;
  if (plan) {
    for (const auto &a : *plan) {
      occupied.emplace(a.team, a.slot);
      if (a.team == team) {
        ++humans;
        if (string(s.resources.get("lobby").get("vehicle_classes"),
                   string(s.players.at(a.id), "vehicle")) == "SPG")
          ++out.human_spgs;
      }
    }
  } else
    for (const auto &[id, p] : s.players) {
      (void)id;
      if (!p.get("connected").truth())
        continue;
      const int side = static_cast<int>(intField(p, "team"));
      if (side == 1 || side == 2)
        occupied.emplace(side, static_cast<int>(intField(p, "slot")));
      if (side == team) {
        ++humans;
        if (string(s.resources.get("lobby").get("vehicle_classes"),
                   string(p, "vehicle")) == "SPG")
          ++out.human_spgs;
      }
    }
  const auto active = slots(s, team);
  for (const auto &row : rows(s.config.get("bot_lineup")))
    if (intField(row, "team") == team && has(row, "vehicle") &&
        !occupied.count({team, static_cast<int>(intField(row, "slot"))}) &&
        std::find(active.begin(), active.end(), intField(row, "slot")) !=
            active.end())
      out.pinned.push_back(
          string(s.resources.get("lobby").get("vehicle_classes"),
                 string(row, "vehicle")));
  out.automatic = std::max(0, static_cast<int>(active.size()) - humans -
                                  static_cast<int>(out.pinned.size()));
  return out;
}
std::vector<std::string> required(const Value &row,
                                  const std::vector<std::string> &pinned) {
  std::vector<std::string> out;
  if (row.get("random").truth())
    return out;
  for (const auto &v : rows(row.get("order")))
    if (row.get(v.text).truth() && !contains(pinned, v.text))
      out.push_back(v.text);
  return out;
}
Value trim(State &s, int team, const std::string &keep,
           const std::vector<Assignment> *plan) {
  auto &row = s.config.object["team_classes"].object[std::to_string(team)];
  const auto ctx = context(s, team, plan);
  const bool was_random = row.get("random").truth();
  auto newest = strings(row.get("order"));
  std::reverse(newest.begin(), newest.end());
  const auto kept = std::find(newest.begin(), newest.end(), keep);
  if (kept != newest.end()) {
    newest.erase(kept);
    newest.push_back(keep);
  }
  Value removed = array();
  for (const auto &tag : newest) {
    int needed = 0;
    for (const auto &v : rows(row.get("order")))
      needed += row.get(v.text).truth() && !contains(ctx.pinned, v.text);
    if (needed <= ctx.automatic)
      break;
    if (contains(ctx.pinned, tag))
      continue;
    row.object[tag] = boolean(false);
    auto &order = row.object["order"].array;
    order.erase(std::remove_if(order.begin(), order.end(),
                               [&](const Value &v) { return v.text == tag; }),
                order.end());
    removed.array.push_back(text(tag));
  }
  if (rows(row.get("order")).empty())
    row.object["random"] = boolean(true);
  if (!removed.array.empty() && !was_random) {
    const auto &previous = s.config.get("team_class_notice");
    Value changes = intField(previous, "revision") == s.state_revision + 1
                        ? previous.get("changes")
                        : array();
    Value change = object();
    change.object["team"] = integer(team);
    change.object["removed"] = removed;
    changes.array.push_back(change);
    Value notice = change;
    notice.object["revision"] = integer(s.state_revision + 1);
    notice.object["changes"] = std::move(changes);
    s.config.object["team_class_notice"] = std::move(notice);
  }
  return removed;
}
int artilleryLimit(int size, int human, int pinned, const Value &row) {
  int nominal = size <= 6 ? 0 : std::min(3, (size - 4) / 3);
  if (!row.get("random").truth() && row.get("SPG").truth())
    nominal = std::max(1, nominal);
  int remaining = std::max(0, nominal - human - pinned);
  if (!row.get("random").truth() && row.get("SPG").truth() && !pinned)
    remaining = std::max(1, remaining);
  return remaining;
}
std::string validate(const Value &row, const Context &ctx,
                     const std::set<std::string> &candidates, int limit) {
  const auto needed = required(row, ctx.pinned);
  if (static_cast<int>(needed.size()) > ctx.automatic)
    return "slots";
  for (const auto &t : needed)
    if (!candidates.count(t))
      return "missing_class";
  std::set<std::string> allowed;
  for (const auto &t : candidates)
    if (row.get("random").truth() || row.get(t).truth())
      allowed.insert(t);
  if (ctx.automatic &&
      (allowed.empty() ||
       (allowed == std::set<std::string>{"SPG"} && ctx.automatic > limit)))
    return "pool";
  return {};
}
std::vector<int> matchTiers(int host, const std::string &raw_mode,
                            std::set<int> available, std::set<int> humans) {
  const auto mode = contains({"random", "same", "minus1_0", "0_plus1",
                              "minus1_plus1", "minus2_0", "0_plus2"},
                             raw_mode)
                        ? raw_mode
                        : "random";
  host = std::clamp(host, 1, 10);
  if (available.empty())
    for (int i = 1; i <= 10; ++i)
      available.insert(i);
  available.insert(host);
  if (mode != "random") {
    std::vector<int> desired;
    int low = host, high = host;
    if (mode == "minus1_0" || mode == "minus1_plus1")
      --low;
    if (mode == "minus2_0")
      low -= 2;
    if (mode == "0_plus1" || mode == "minus1_plus1")
      ++high;
    if (mode == "0_plus2")
      high += 2;
    for (int i = low; i <= high; ++i)
      if (available.count(i))
        desired.push_back(i);
    return desired;
  }
  humans.insert(host);
  available.insert(humans.begin(), humans.end());
  const int minimum = *humans.rbegin() - *humans.begin() + 1;
  if (minimum > 3)
    return {humans.begin(), humans.end()};
  for (int width = std::max(2, minimum); width >= minimum; --width) {
    std::vector<std::vector<int>> windows;
    for (int low = std::max(1, host - width + 1);
         low <= std::min(host, 11 - width); ++low) {
      std::vector<int> w;
      for (int i = low; i < low + width; ++i)
        w.push_back(i);
      if (std::includes(w.begin(), w.end(), humans.begin(), humans.end()) &&
          std::includes(available.begin(), available.end(), w.begin(), w.end()))
        windows.push_back(std::move(w));
    }
    if (!windows.empty())
      return windows[windows.size() / 2];
  }
  return {humans.begin(), humans.end()};
}
bool inTier(int host, int candidate, const std::string &mode) {
  if (!contains({"same", "minus1_0", "0_plus1", "minus1_plus1", "minus2_0",
                 "0_plus2"},
                mode))
    return std::abs(candidate - host) <= 2;
  const auto tiers = matchTiers(host, mode);
  return std::find(tiers.begin(), tiers.end(), candidate) != tiers.end();
}
std::optional<std::vector<int>>
matchingTiers(const std::vector<std::pair<int, std::string>> &candidates,
              const std::set<std::string> &needed, const std::set<int> &humans,
              std::vector<int> proposed, bool random,
              const std::vector<std::set<std::string>> &groups,
              const std::vector<std::set<int>> &slot_tiers) {
  std::sort(proposed.begin(), proposed.end());
  proposed.erase(std::unique(proposed.begin(), proposed.end()), proposed.end());
  const auto covered = [&](const std::vector<int> &tiers) {
    std::set<std::string> present;
    for (const auto &[level, tag] : candidates)
      if (std::find(tiers.begin(), tiers.end(), level) != tiers.end())
        present.insert(tag);
    if (!std::includes(present.begin(), present.end(), needed.begin(),
                       needed.end()))
      return false;
    for (const auto &group : groups)
      if (std::none_of(group.begin(), group.end(), [&](const std::string &t) {
            return present.count(t) != 0;
          }))
        return false;
    for (const auto &levels : slot_tiers)
      if (std::none_of(tiers.begin(), tiers.end(),
                       [&](int tier) { return levels.count(tier) != 0; }))
        return false;
    return true;
  };
  if (needed.empty() && groups.empty() && slot_tiers.empty())
    return proposed;
  if (covered(proposed))
    return proposed;
  if (!random)
    return {};
  std::vector<std::vector<int>> windows;
  for (int low = 1; low <= 10; ++low)
    for (int width = 1; width <= 3; ++width) {
      if (low + width > 11)
        continue;
      std::vector<int> w;
      for (int t = low; t < low + width; ++t)
        w.push_back(t);
      if (std::includes(w.begin(), w.end(), humans.begin(), humans.end()) &&
          covered(w))
        windows.push_back(std::move(w));
    }
  if (windows.empty())
    return {};
  const auto score = [&](const std::vector<int> &w) {
    std::vector<int> diff;
    std::set_symmetric_difference(w.begin(), w.end(), proposed.begin(),
                                  proposed.end(), std::back_inserter(diff));
    return std::make_tuple(std::abs(static_cast<int>(w.size()) -
                                    static_cast<int>(proposed.size())),
                           diff.size(), w);
  };
  return *std::min_element(
      windows.begin(), windows.end(),
      [&](const auto &a, const auto &b) { return score(a) < score(b); });
}
std::string vehicleClass(const State &s, const Value &v, bool fallback) {
  const auto exact = string(s.resources.get("lobby").get("vehicle_classes"),
                            string(v, "name", string(v, "vehicle")));
  if (!exact.empty() || !fallback)
    return exact;
  const auto tags = strings(v.get("tags"));
  for (const auto &t :
       {"heavyTank", "mediumTank", "AT-SPG", "lightTank", "SPG"})
    if (contains(tags, t))
      return t;
  return "mediumTank";
}
std::set<std::string> allowedNames(const State &s, const Value &catalog) {
  const std::set<std::string> excluded = {"germany:G138_VK168_02_Mauerbrecher",
                                          "germany:G79_Pz_IV_AusfGH",
                                          "uk:GB70_FV4202_105",
                                          "usa:A08_T23",
                                          "usa:A15_T57",
                                          "usa:A26_T18",
                                          "ussr:R05_KV",
                                          "ussr:R70_T_50_2"};
  const auto retired =
      strings(s.resources.get("lobby").get("retired_bot_vehicles"));
  std::set<std::string> out;
  for (const auto &row : rows(catalog)) {
    const auto name = string(row, "name");
    const auto tags = strings(row.get("tags"));
    if (name.empty() || excluded.count(name) || contains(retired, name))
      continue;
    bool skip = false;
    for (const auto &t : {"event_battles", "premiumIGR", "observer", "fallout"})
      skip = skip || contains(tags, t);
    if (contains(tags, "secret")) {
      skip = skip || name == "germany:Env_Artillery";
      for (const auto &suffix : {"_bootcamp", "_bot", "_training"}) {
        const auto n = std::char_traits<char>::length(suffix);
        skip = skip || (name.size() >= n &&
                        name.compare(name.size() - n, n, suffix) == 0);
      }
    }
    if (!skip)
      out.insert(name);
  }
  return out;
}
bool matches(const State &s, const Value &filter, const Value &candidate) {
  const auto name = string(candidate, "name");
  const auto nation = string(filter, "nation"),
             tag = string(filter, "vehicle_class");
  return (nation.empty() || name.substr(0, name.find(':')) == nation) &&
         (tag.empty() || vehicleClass(s, candidate) == tag);
}
Value outfits(const Value &input, const Value &resources) {
  const auto &raw = input.resolved();
  Value out = object();
  if (raw.kind == K::Null)
    return out;
  require(raw.kind == K::Object && raw.object.size() <= 3, "Invalid outfits");
  std::size_t total = 0;
  for (const auto &[key, value] : raw.object) {
    const auto season = pythonInteger(key, resources);
    require(season == 1 || season == 2 || season == 4, "Invalid outfit season");
    const auto encoded = canonicalBase64(value);
    const auto padding = encoded.back() == '='
                             ? (encoded[encoded.size() - 2] == '=' ? 2u : 1u)
                             : 0u;
    total += (encoded.size() / 4) * 3 - padding;
    require(total <= 192 * 1024, "Invalid outfit aggregate size");
    out.object[std::to_string(season)] = text(encoded);
  }
  return out;
}
Value marks(const Value &input) {
  const auto &raw = input.resolved();
  return integer(raw.kind == K::Null ? 0 : strictInt(raw, 0, 3));
}
Value descriptor(const Value &input) {
  const auto &raw = input.resolved();
  const auto canonical = canonicalBase64(raw);
  require(canonical == raw.text, "Noncanonical vehicle configuration");
  return text(canonical);
}
Value catalog(const Value &input, const Value &r) {
  const auto &raw = input.resolved();
  require(raw.kind == K::Array && !raw.array.empty() &&
              raw.array.size() <= 1024,
          "Invalid vehicle catalog");
  Value out = array();
  std::set<std::string> seen;
  for (const auto &input_row : raw.array) {
    const auto &row = input_row.resolved();
    require(row.kind == K::Object, "Invalid catalog row");
    const auto name = safe(row.get("name"), "", r, true);
    require(!name.empty() && seen.insert(name).second,
            "Duplicate catalog vehicle");
    std::int64_t level = 0;
    const auto &v = row.get("level").resolved();
    if (v.kind == K::Boolean)
      level = v.boolean ? 1 : 0;
    else if (v.kind == K::Number && std::isfinite(v.number) && v.number >= 1 &&
             v.number < 11)
      level = static_cast<std::int64_t>(v.number);
    else if (v.kind == K::Integer)
      level = v.integer;
    else if (v.kind == K::Text)
      level = pythonInteger(v.text, r);
    require(level >= 1 && level <= 10, "Invalid catalog tier");
    require(row.get("tags").resolved().kind == K::Array &&
                row.get("tags").resolved().array.size() <= 32,
            "Invalid vehicle tags");
    std::vector<std::string> tags;
    for (const auto &t : row.get("tags").resolved().array)
      tags.push_back(truncate(pythonString(t), 32));
    std::sort(tags.begin(), tags.end());
    Value entry = object();
    entry.object["name"] = text(name);
    entry.object["level"] = integer(level);
    entry.object["tags"] = array();
    for (const auto &t : tags)
      entry.object["tags"].array.push_back(text(t));
    out.array.push_back(std::move(entry));
  }
  return out;
}
} // namespace wot::room::lobby
