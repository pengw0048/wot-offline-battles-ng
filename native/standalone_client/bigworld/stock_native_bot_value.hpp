#pragma once
// Builders for the Bot owner's outgoing protocol rows (owned Values, never Python objects).
#include "stock_native_bot_python.hpp"
#include "stock_native_client_state.hpp"
#include <cmath>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace stock_native_bot_value {
using Value = stock_native_client_state::Value;
using Kind = Value::Kind;
inline Value null() { return Value(); }
inline Value boolean(bool v) { Value x; x.kind = Kind::Boolean; x.boolean = v; return x; }
inline Value integer(std::int64_t v) { Value x; x.kind = Kind::Integer; x.integer = v; return x; }
// A finite float, as p::number required of every publication.
inline Value number(double v) { if (!std::isfinite(v)) throw stock_native_bot_python::Error("Non-finite native Bot publication"); Value x; x.kind = Kind::Number; x.number = v; return x; }
// A float without the finite check (the former PyFloat_FromDouble).
inline Value real(double v) { Value x; x.kind = Kind::Number; x.number = v; return x; }
inline Value text(std::string v) { Value x; x.kind = Kind::Text; x.text = std::move(v); return x; }
inline Value object() { Value x; x.kind = Kind::Object; return x; }
inline Value array() { Value x; x.kind = Kind::Array; return x; }
template<class Sequence> Value numbers(const Sequence& values) { auto out = array(); for (const auto& v : values) out.array.push_back(number(v)); return out; }
template<class Sequence> Value integers(const Sequence& values) { auto out = array(); for (const auto& v : values) out.array.push_back(integer(std::int64_t(v))); return out; }
template<class Sequence> Value strings(const Sequence& values) { auto out = array(); for (const auto& v : values) out.array.push_back(text(v)); return out; }

// Strict readers of accepted rows with the former stock_native_bot_python rules (p::scalar, p::wide, ...).
using Error = stock_native_bot_python::Error;
// A present field (a present null included), or nullptr when absent; a non-object row is an error.
inline const Value* field(const Value& row, const char* key) {
    const auto& r = row.resolved();
    if (r.kind != Kind::Object) throw Error("Native Bot event is not a dictionary");
    const auto found = r.object.find(key);
    return found == r.object.end() ? nullptr : &found->second.resolved();
}
// The field, or null when absent or the row is not an object.
inline const Value& value(const Value& row, const char* key) { return row.resolved().get(key).resolved(); }
inline const std::vector<Value>& rows(const Value& v) { const auto& r = v.resolved(); if (r.kind != Kind::Array) throw Error("Native Bot event is not a sequence"); return r.array; }
inline double scalar(const Value& raw) {
    const auto& v = raw.resolved();
    if (v.kind != Kind::Integer && v.kind != Kind::Number) throw Error(v.kind == Kind::Null || v.kind == Kind::Boolean ? "Native Bot number missing" : "Native Bot number invalid");
    const double n = v.kind == Kind::Number ? v.number : v.numeric();
    if (!std::isfinite(n)) throw Error("Native Bot number invalid");
    return n;
}
inline std::int64_t wide(const Value& raw) {
    const auto& v = raw.resolved();
    if (v.kind == Kind::Null || v.kind == Kind::Boolean) throw Error("Native Bot integer missing");
    if (v.kind == Kind::Integer) { if (v.unsigned_integer) throw Error("Native Bot integer overflow"); return v.integer; }
    const double n = scalar(v);
    if (n < -9223372036854775808. || n >= 9223372036854775808. || std::floor(n) != n) throw Error("Native Bot integer invalid");
    return std::int64_t(n);
}
inline bool flag(const Value& v) { return v.resolved().truth(false); }
inline std::string string(const Value& raw) {
    const auto& v = raw.resolved();
    if (v.kind == Kind::Null) return {};
    if (v.kind != Kind::Text) throw Error("Native Bot text invalid");
    return v.text;
}
inline double numeric(const Value& row, const char* key, double fallback = 0.) { const auto& v = value(row, key); return v.kind == Kind::Null ? fallback : scalar(v); }
inline std::int64_t integral(const Value& row, const char* key, std::int64_t fallback = 0) { const auto& v = value(row, key); return v.kind == Kind::Null ? fallback : wide(v); }
inline bool truth(const Value& row, const char* key, bool fallback = false) { const auto& v = value(row, key); return v.kind == Kind::Null ? fallback : flag(v); }
}
