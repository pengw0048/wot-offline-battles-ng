#pragma once
#include "room_value.hpp"
#include <map>

namespace wot::room {
// A writer stages against its last completed TCP write, never queue admission.
class ReplicaEncoder {
    struct Actor { Value motion, signature; };
    struct Baseline {
        std::int64_t round = -1, epoch = -1, own = 0;
        struct Section { std::int64_t revision = -1, tick = -1; Value value; };
        std::map<std::string, Section> sections;
        std::map<std::int64_t, Actor> players, bots;
        Value contracts;
    };
public:
    struct Prepared { Value wire; std::shared_ptr<const Baseline> baseline; };
    Prepared prepare(const Value& snapshot, std::int64_t own, bool full = false) const;
    void commit(Prepared prepared) { baseline_ = std::move(prepared.baseline); }
    void reset() { baseline_.reset(); }
private:
    std::shared_ptr<const Baseline> baseline_;
};
}
