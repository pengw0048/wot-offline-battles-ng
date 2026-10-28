#pragma once

#include <array>
#include <cfloat>
#include <cstdint>

namespace wot
{
// #1513 statistics owner 006da4c0/006d84e0. These are half-second
// samples, not DebugApp's instantaneous/50-frame FPS measurements.
class StockBattleFps
{
public:
    void enable( bool value )
    {
        if (value)
        {
            sampleCount_ = frameCount_ = windowFrames_ = 0;
            elapsed_ = windowElapsed_ = 0.f;
        }
        enabled_ = value;
    }

    void tick( float elapsed )
    {
        if (!enabled_) return;
        windowElapsed_ += elapsed;
        if (windowElapsed_ < 0.f) return;
        ++windowFrames_;
        if (windowElapsed_ >= 0.5f)
        {
            // The full buffer disables collection at the next complete
            // window, before that frame enters the aggregate average.
            if (sampleCount_ >= samples_.size())
            {
                sampleCount_ = static_cast<uint32_t>( samples_.size() );
                enabled_ = false;
                return;
            }
            samples_[sampleCount_++] = float( windowFrames_ ) / windowElapsed_;
            windowFrames_ = 0;
            windowElapsed_ = 0.f;
        }
        elapsed_ += elapsed;
        ++frameCount_;
    }

    std::array<float, 3> snapshot() const
    {
        float minimum = FLT_MAX;
        float maximum = FLT_MIN;
        for (uint32_t index = 0; index < sampleCount_; ++index)
        {
            const float value = samples_[index];
            if (minimum > value) minimum = value;
            if (value > maximum) maximum = value;
        }
        return {{ minimum, maximum, frameCount_ ? float( frameCount_ ) / elapsed_ : 0.f }};
    }

private:
    std::array<float, 2400> samples_;
    uint32_t sampleCount_ = 0;
    uint32_t frameCount_ = 0;
    uint32_t windowFrames_ = 0;
    float elapsed_ = 0.f;
    float windowElapsed_ = 0.f;
    bool enabled_ = false;
};
} // namespace wot
