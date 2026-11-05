#pragma once
// Test-only resource shapes: the production Host view and nested borrowed trees.
#include "room_value.hpp"
#include <cstdlib>

namespace wot::room {
inline Value borrowedResourceTree(const Value& raw, bool scalars = false) {
    const auto& value = raw.resolved();
    if (value.kind != Value::Kind::Object && value.kind != Value::Kind::Array) {
        if (!scalars) return value;
        Value reference; reference.kind = Value::Kind::Shared; reference.reference = freeze(value);
        return reference;
    }
    auto out = value.kind == Value::Kind::Object ? object() : array();
    for (const auto& field : value.object) out.object.emplace(field.first, borrowedResourceTree(field.second, scalars));
    for (const auto& item : value.array) out.array.push_back(borrowedResourceTree(item, scalars));
    Value reference; reference.kind = Value::Kind::Shared; reference.reference = freeze(std::move(out));
    return reference;
}
inline Value testResources(const Value& raw) {
    auto value = raw.resolved().kind == Value::Kind::Null ? object() : raw;
    const auto* shape = std::getenv("WOT_ROOM_TEST_RESOURCE_VIEW");
    if (shape && std::string(shape) == "nested") value = borrowedResourceTree(value);
    return objectView(freeze(std::move(value)));
}
}
