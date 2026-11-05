#pragma once
// Owned data shared by the native room and the worker, never Python objects.
#include "../bigworld/stock_native_client_state.hpp"
#include <cmath>
#include <limits>
#include <stdexcept>

namespace wot::room {
using Value = stock_native_client_state::Value;
inline Value object() { Value v; v.kind = Value::Kind::Object; return v; }
inline Value array() { Value v; v.kind = Value::Kind::Array; return v; }
inline Value text(std::string s) { Value v; v.kind = Value::Kind::Text; v.text = std::move(s); return v; }
inline Value integer(std::int64_t n) { Value v; v.kind = Value::Kind::Integer; v.integer = n; return v; }
inline Value number(double n) { Value v; v.kind = Value::Kind::Number; v.number = n; return v; }
inline Value boolean(bool b) { Value v; v.kind = Value::Kind::Boolean; v.boolean = b; return v; }
inline bool has(const Value& v, const std::string& k) { return v.resolved().object.count(k) != 0; }
inline const std::vector<Value>& rows(const Value& v) { return v.resolved().array; }
inline std::optional<std::int64_t> exactInteger(const Value& raw) {
    const auto& v = raw.resolved();
    if (v.kind == Value::Kind::Integer && !v.unsigned_integer) return v.integer;
    if (v.kind != Value::Kind::Number || !std::isfinite(v.number) ||
        std::floor(v.number) != v.number || v.number < -9223372036854775808. ||
        v.number >= 9223372036854775808.) return {};
    return static_cast<std::int64_t>(v.number);
}
inline std::int64_t intField(const Value& v, const std::string& k, std::int64_t fallback = 0) {
    return exactInteger(v.get(k)).value_or(fallback);
}
inline std::shared_ptr<const Value> freeze(Value v) {
    return std::make_shared<const Value>(std::move(v));
}
inline Value objectView(const std::shared_ptr<const Value>& owner) {
    if (!owner || owner->resolved().kind != Value::Kind::Object)
        throw std::invalid_argument("Native room view requires an object");
    auto out = object();
    for (const auto& field : owner->resolved().object) {
        const auto& value = field.second.resolved();
        // Scalars are cheap and retain their ordinary type surface. Share
        // structured sections, whose trees dominate copy and storage cost.
        if (value.kind != Value::Kind::Array && value.kind != Value::Kind::Object) {
            out.object.emplace(field.first, value); continue;
        }
        if (field.second.reference) {
            // Reuse an existing immutable owner directly. Wrapping its field
            // again retains an entire frame and hides stable section identity.
            out.object.emplace(field.first, field.second); continue;
        }
        Value reference; reference.kind = Value::Kind::Shared;
        reference.reference = std::shared_ptr<const Value>(owner, &field.second);
        out.object.emplace(field.first, std::move(reference));
    }
    return out;
}
}
