#pragma once
#include <cstdint>

namespace wot {
// Owned by the client thread, as are Python mapping admission and native space
// creation. Each admitted mapping copies its mask before background loading.
class OfflineMappingPolicy {
public:
    std::uint32_t mask() const { return active_ ? 1u : 0xffffffffu; }
    long apply() {
        if (active_) return 101;
        active_ = true;
        return 0;
    }
    long restore() {
        if (!active_) return 102;
        active_ = false;
        return 0;
    }
private:
    bool active_ = false;
};
}
