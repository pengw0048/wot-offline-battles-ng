#pragma once
// Receiver side of the replica snapshot split: merges state and motion records into rows.
#include "stock_native_client_state.hpp"
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace stock_wire {

class ReplicaStore {
public:
    // The complete snapshot (envelope, players, bots) after one replica message. Unchanged
    // rows keep their previous shared version. Throws on a message that does not fit the
    // merged state; the store then accepts only a full message.
    std::shared_ptr<const stock_native_client_state::Value> apply(stock_native_client_state::Value message);
    bool valid() const { return valid_; }

private:
    using Shared = std::shared_ptr<const stock_native_client_state::Value>;
    struct Actor { stock_native_client_state::Value state, motion; Shared row; };
    void restate(std::map<std::int64_t, Actor>& actors, const stock_native_client_state::Value& states, bool bot);
    void move(std::map<std::int64_t, Actor>& actors, const stock_native_client_state::Value& motion);
    std::map<std::int64_t, Actor> players_, bots_;
    std::vector<stock_native_client_state::Value> contracts_;
    bool valid_ = false;
};

}
