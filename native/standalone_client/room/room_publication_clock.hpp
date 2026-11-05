#pragma once
#include "room_state.hpp"

namespace wot::room {
// A clock proposal is committed only after every actor and side ledger in the
// publication has been admitted. Malformed data cannot move the source epoch.
class PublicationClock {
public:
    struct Proposal {
        std::int64_t mapped_sample = 0, motion_offset = 0;
        std::optional<std::int64_t> source, horizon, launch_offset;
        bool rebase = false, same_batch = false;
        std::int64_t receipt = 0;
        std::uint64_t version = 0;
    };
    struct Failure : std::invalid_argument {
        std::string code;
        explicit Failure(std::string value) : std::invalid_argument(value), code(std::move(value)) {}
    };
    Proposal stage(const Value&, const State&, std::int64_t raw_receipt_us) const;
    void commit(State&, const Proposal&);
    void reset();
    void producerChanged();
    std::int64_t motionTime(std::int64_t raw_us) const;
private:
    std::optional<std::int64_t> source_, receipt_, horizon_;
    std::int64_t motion_offset_ = 0;
    std::uint64_t version_ = 0;
};
}
