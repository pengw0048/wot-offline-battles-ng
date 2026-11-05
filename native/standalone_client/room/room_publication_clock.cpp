#include "room_publication_clock.hpp"
#include <algorithm>

namespace wot::room {
namespace {
constexpr std::int64_t maximum = 10000000000000000LL, lead = 200000;
std::int64_t sample(const Value& raw) {
    const auto& v = raw.resolved();
    if (v.kind != Value::Kind::Integer || v.unsigned_integer || v.integer < 0 || v.integer > maximum)
        throw PublicationClock::Failure("sample_time");
    return v.integer;
}
}
PublicationClock::Proposal PublicationClock::stage(const Value& message, const State& state, std::int64_t raw) const {
    Proposal next; next.version = version_; next.receipt = std::max<std::int64_t>(0, raw);
    next.launch_offset = state.bot_launch_clock_offset_us;
    const auto received = motionTime(next.receipt);
    if (has(message, "sample_time_us")) {
        next.source = sample(message.get("sample_time_us"));
        next.horizon = sample(message.get("source_batch_horizon_us"));
        if (*next.source > *next.horizon) throw Failure("sample_horizon");
        if (horizon_ && *next.horizon < *horizon_) throw Failure("sample_horizon_order");
        next.same_batch = horizon_ && *next.horizon == *horizon_;
        if (source_ && *next.source <= *source_) throw Failure("sample_time_order");
        if (!source_) next.mapped_sample = std::max(received, state.bot_state_time_us + 1);
        else {
            if (!receipt_) throw Failure("sample_time_clock");
            const auto source_delta = *next.source - *source_;
            const auto elapsed = std::max<std::int64_t>(0, next.receipt - *receipt_);
            if (!next.same_batch && source_delta > elapsed + lead) {
                next.rebase = true; next.mapped_sample = std::max(received, state.bot_state_time_us + 1);
            } else next.mapped_sample = state.bot_state_time_us + source_delta;
        }
    } else {
        if (has(message, "source_batch_horizon_us")) throw Failure("sample_horizon_without_time");
        if (source_) throw Failure("sample_time_missing");
        next.mapped_sample = std::max(received, state.bot_state_time_us + 1);
    }
    if (next.mapped_sample > maximum) throw Failure("sample_time_range");
    next.motion_offset = std::max(motion_offset_, next.mapped_sample - next.receipt);
    if (next.rebase || (next.source && !next.launch_offset)) next.launch_offset = state.server_time_ms * 1000 - *next.horizon;
    return next;
}
void PublicationClock::commit(State& state, const Proposal& next) {
    if (next.version != version_) throw std::logic_error("Stale native room publication clock proposal");
    state.bot_state_time_us = next.mapped_sample;
    state.bot_launch_clock_offset_us = next.launch_offset;
    motion_offset_ = next.motion_offset;
    if (next.source) { source_ = next.source; receipt_ = next.receipt; horizon_ = next.horizon; }
    ++version_;
}
void PublicationClock::producerChanged() { source_.reset(); receipt_.reset(); horizon_.reset(); ++version_; }
void PublicationClock::reset() { source_.reset(); receipt_.reset(); horizon_.reset(); motion_offset_ = 0; ++version_; }
std::int64_t PublicationClock::motionTime(std::int64_t raw) const {
    return std::min(maximum, std::max<std::int64_t>(0, raw) + motion_offset_);
}
}
