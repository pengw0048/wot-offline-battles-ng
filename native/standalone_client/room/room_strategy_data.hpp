#pragma once
#include "room_strategy.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <initializer_list>
#include <limits>

namespace wot::room::strategy {
inline double n(const Value& v, const char* key, double fallback = 0.) {
    return v.get(key).numeric(fallback);
}
inline ID id(const Value& v) { return intField(v, "id"); }
inline int team(const Value& v) { return int(intField(v, "team")); }
inline std::string s(const Value& v, const char* key, std::string fallback = {}) {
    const auto& x = v.get(key).resolved();
    return x.kind == Value::Kind::Text ? x.text : fallback;
}
inline bool b(const Value& v, const char* key, bool fallback = false) {
    return has(v, key) ? v.get(key).truth() : fallback;
}
inline bool null(const Value& v) { return v.resolved().kind == Value::Kind::Null; }
inline bool list(const Value& v) { return v.resolved().kind == Value::Kind::Array; }
inline bool dict(const Value& v) { return v.resolved().kind == Value::Kind::Object; }
inline double rounded(double value, int places = 3) {
    // Scaling first can erase the binary remainder at a decimal half-way
    // boundary. printf's decimal conversion rounds the original double,
    // matching Python 3 round(value, places) used by the pinned room planner.
    if (!std::isfinite(value)) return value;
    char buffer[512]; ::snprintf(buffer,sizeof buffer,"%.*f",places,value);
    return std::strtod(buffer,nullptr);
}
inline Value record(std::initializer_list<std::pair<std::string, Value>> fields) {
    auto v = object(); for (const auto& x : fields) v.object[x.first] = x.second; return v;
}
inline Value sequence(std::initializer_list<Value> values) {
    auto v = array(); v.array.assign(values); return v;
}
inline bool oneOf(const std::string& value, std::initializer_list<const char*> values) {
    return std::any_of(values.begin(), values.end(), [&](const char* x) { return value == x; });
}
inline bool contains(const Value& values, const Value& value) {
    return std::find(rows(values).begin(), rows(values).end(), value) != rows(values).end();
}
inline void erase(Value& v, std::initializer_list<const char*> names) {
    for (const char* name : names) v.object.erase(name);
}
inline Value& ensure(Value& v, const char* key, const Value& initial = object()) {
    if (!has(v,key)) v.object[key] = initial; return v.object[key];
}
inline Value& entry(Records& map, ID key) {
    auto i = map.find(key); if (i == map.end()) i = map.emplace(key, object()).first; return i->second;
}
inline const Value& lookup(const Records& map, ID key) {
    auto i = map.find(key); static const Value empty; return i == map.end() ? empty : i->second;
}
inline Identity identity(const Value& v) { return {s(v,"target_kind"), id(v)}; }
inline Value identityValue(const Identity& key) { return sequence({text(key.first), integer(key.second)}); }
inline void target(Value& order, const Value& contact) {
    order.object["target_id"] = contact.get("id");
    order.object["target_kind"] = contact.get("target_kind");
    order.object["aim_position"] = contact.get("position");
    order.object["face_position"] = contact.get("position");
}
}
