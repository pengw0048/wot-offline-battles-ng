#pragma once

#include <cstdint>

namespace wot
{
// Sampling policy at 009c19a0. A caller submits only completed native GPU
// query results; failure includes a missing frequency or reversed timestamps.
class StockGpuSampling
{
public:
    StockGpuSampling( unsigned target, double limitMilliseconds ) :
        target_( target ), limit_( limitMilliseconds ), minimum_( limitMilliseconds ),
        successes_( 0 ), failures_( 0 ), slow_( 0 ) {}

    bool needsSample() const
    {
        return successes_ < target_ && failures_ < 5 && slow_ < 3;
    }

    void failure() { if (needsSample()) ++failures_; }

    void timestamps( std::uint64_t frequency, std::uint64_t start, std::uint64_t end )
    {
        if (!needsSample()) return;
        if (!frequency || end < start) { failure(); return; }
        const double elapsed = double( end - start ) * (1000. / double( frequency ));
        if (elapsed >= limit_ && minimum_ >= limit_) ++slow_;
        if (minimum_ > elapsed) minimum_ = elapsed;
        ++successes_;
    }

    bool passed() const
    {
        return successes_ == target_ && failures_ < 5 && slow_ < 3 && minimum_ < limit_;
    }
    double minimumMilliseconds() const { return minimum_; }

private:
    unsigned target_;
    double limit_;
    double minimum_;
    unsigned successes_;
    unsigned failures_;
    unsigned slow_;
};
} // namespace wot
