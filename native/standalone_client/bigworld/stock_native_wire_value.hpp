#pragma once
// Decoded LAN protocol Values for the projectile and detached-turret owners: builders for outgoing
// rows and the p::scalar/wide/numeric/integral/string reading laws over incoming ones.
#include "stock_native_client_state.hpp"
#include "stock_native_bot_python.hpp"
#include <array>
#include <cmath>
#include <cstdint>
#include <string>

namespace stock_native_wire_value {
using Value=stock_native_client_state::Value;
using Kind=Value::Kind;
inline Value integer(std::int64_t v){Value x;x.kind=Kind::Integer;x.integer=v;return x;}
// A u64 above int64 keeps its bits with the unsigned mark, as the decoder reads it.
inline Value unsignedInteger(std::uint64_t v){Value x=integer(std::int64_t(v));x.unsigned_integer=v>std::uint64_t(INT64_MAX);return x;}
inline Value number(double v){Value x;x.kind=Kind::Number;x.number=v;return x;}
inline Value text(std::string v){Value x;x.kind=Kind::Text;x.text=std::move(v);return x;}
inline Value boolean(bool v){Value x;x.kind=Kind::Boolean;x.boolean=v;return x;}
inline Value object(){Value x;x.kind=Kind::Object;return x;}
inline Value array(){Value x;x.kind=Kind::Array;return x;}
template<std::size_t N>Value numbers(const std::array<double,N>&v){Value x=array();for(double c:v)x.array.push_back(number(c));return x;}
template<class Sequence>Value strings(const Sequence&values){Value x=array();for(const auto&v:values)x.array.push_back(text(v));return x;}

// p::scalar: a finite int or float; None, Boolean and text are invalid.
inline double scalar(const Value&raw){
    const auto&v=raw.resolved();
    if(v.kind==Kind::Integer)return v.numeric();
    if(v.kind==Kind::Number&&std::isfinite(v.number))return v.number;
    throw stock_native_bot_python::Error(v.kind==Kind::Number?"Native Bot number invalid":"Native Bot number missing");
}
// p::wide: an int64 int, or an integral float.
inline std::int64_t wide(const Value&raw){
    const auto&v=raw.resolved();
    if(v.kind==Kind::Integer){if(v.unsigned_integer)throw stock_native_bot_python::Error("Native Bot integer overflow");return v.integer;}
    const double n=scalar(v);
    if(n<-9223372036854775808.||n>=9223372036854775808.||std::floor(n)!=n)throw stock_native_bot_python::Error("Native Bot integer invalid");
    return std::int64_t(n);
}
// p::numeric/p::integral: an absent or null field (or a non-mapping owner) yields the fallback.
inline double numeric(const Value&owner,const char*key,double fallback=0.){const auto&v=owner.resolved().get(key).resolved();return v.kind==Kind::Null?fallback:scalar(v);}
inline std::int64_t integral(const Value&owner,const char*key,std::int64_t fallback=0){const auto&v=owner.resolved().get(key).resolved();return v.kind==Kind::Null?fallback:wide(v);}
// p::string: None is empty text; anything but text is invalid.
inline std::string string(const Value&raw){
    const auto&v=raw.resolved();if(v.kind==Kind::Null)return {};
    if(v.kind!=Kind::Text)throw stock_native_bot_python::Error("Native Bot text invalid");return v.text;
}
// p::size: only a sequence has a length.
inline std::size_t size(const Value&raw){const auto&v=raw.resolved();if(v.kind!=Kind::Array)throw stock_native_bot_python::Error("Native Bot event is not a sequence");return v.array.size();}
}
