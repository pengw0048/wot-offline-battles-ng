#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace wot
{
// #1513 6bed00. None of these wire records contains an aligned engine vector.
struct StockLightBinary
{
    using U32 = std::uint32_t;
    struct Omni
    {
        float position[3], innerRadius, outerRadius, colour[4];
        U32 metadata;
        float multiplier;
        U32 flags;
    };
    struct Spot
    {
        float position[3], direction[3], innerRadius, outerRadius, cosConeAngle, colour[4];
        U32 metadata;
        float multiplier;
        U32 flags;
    };
    struct Pulse { U32 animation, firstFrame, frameCount; float duration; };
    struct PulseOmni { Omni light; Pulse pulse; };
    struct PulseSpot { Spot light; Pulse pulse; };
    struct Frame { float time, value; };
    std::vector<Omni> omnis;
    std::vector<Spot> spots;
    std::vector<PulseOmni> pulseOmnis;
    std::vector<PulseSpot> pulseSpots;
    std::vector<Frame> frames;

    bool read(const void* data, std::size_t size, std::string& error)
    {
        *this = StockLightBinary();
        error.clear();
        StockLightBinary next;
        Reader reader(data, size, error);
        if (!reader.array(next.omnis) || !reader.array(next.spots) ||
            !reader.array(next.pulseOmnis) || !reader.array(next.pulseSpots) ||
            !reader.array(next.frames) || !reader.finished()) return false;
        for (const auto& light : next.omnis) if (!valid(light)) return reader.fail("Invalid BWLC omni record");
        for (const auto& light : next.spots) if (!valid(light)) return reader.fail("Invalid BWLC spot record");
        for (const auto& light : next.pulseOmnis)
            if (!valid(light.light) || !next.valid(light.pulse)) return reader.fail("Invalid BWLC pulse-omni record");
        for (const auto& light : next.pulseSpots)
            if (!valid(light.light) || !next.valid(light.pulse)) return reader.fail("Invalid BWLC pulse-spot record");
        for (const auto& frame : next.frames)
            if (!std::isfinite(frame.time) || !std::isfinite(frame.value)) return reader.fail("Non-finite BWLC curve key");
        *this = std::move(next);
        return true;
    }

private:
    template<std::size_t N> static bool finite(const float (&values)[N])
    {
        for (float value : values) if (!std::isfinite(value)) return false;
        return true;
    }
    static bool valid(const Omni& light)
    {
        return finite(light.position) && finite(light.colour) &&
            std::isfinite(light.innerRadius) && std::isfinite(light.outerRadius) &&
            std::isfinite(light.multiplier);
    }
    static bool valid(const Spot& light)
    {
        return finite(light.position) && finite(light.direction) && finite(light.colour) &&
            std::isfinite(light.innerRadius) && std::isfinite(light.outerRadius) &&
            std::isfinite(light.multiplier) && std::isfinite(light.cosConeAngle) &&
            light.cosConeAngle >= -1.f && light.cosConeAngle <= 1.f;
    }
    bool valid(const Pulse& pulse) const
    {
        return pulse.firstFrame <= frames.size() && pulse.frameCount <= frames.size() - pulse.firstFrame &&
            std::isfinite(pulse.duration);
    }
    struct Reader
    {
        const unsigned char* data;
        std::size_t size, offset = 0;
        std::string& error;
        Reader(const void* source, std::size_t length, std::string& out) :
            data(static_cast<const unsigned char*>(source)), size(length), error(out) {}
        bool fail(const char* message) { error = message; return false; }
        template<class T> bool array(std::vector<T>& output)
        {
            if (!data || offset > size || size - offset < 8) return fail("Truncated BWLC array header");
            U32 stride, count;
            std::memcpy(&stride, data + offset, 4);
            std::memcpy(&count, data + offset + 4, 4);
            offset += 8;
            if (stride != sizeof(T)) return fail("Incorrect BWLC array stride");
            if (count > (size - offset) / sizeof(T)) return fail("Truncated BWLC array body");
            output.resize(count);
            if (count) std::memcpy(output.data(), data + offset, sizeof(T) * count);
            offset += sizeof(T) * count;
            return true;
        }
        bool finished() { return offset == size || fail("Unexpected BWLC trailing data"); }
    };
};
static_assert(sizeof(StockLightBinary::Omni) == 48, "BWLC omni stride");
static_assert(sizeof(StockLightBinary::Spot) == 64, "BWLC spot stride");
static_assert(sizeof(StockLightBinary::PulseOmni) == 64, "BWLC pulse-omni stride");
static_assert(sizeof(StockLightBinary::PulseSpot) == 80, "BWLC pulse-spot stride");
static_assert(sizeof(StockLightBinary::Frame) == 8, "BWLC curve-key stride");

// 6cfb50/6cfd80 use almost-equality at the preceding key, unlike the public
// LinearAnimation's exact equality. Keep the original tolerance in this owner.
class StockLightCurve
{
    std::map<float, float> keys_;
    bool loop_ = false;
    float duration_ = 0.f;
public:
    void reset() { keys_.clear(); loop_ = false; duration_ = 0.f; }
    void addKey(float time, float value)
    {
        if (!loop_ || (time >= 0.f && time < duration_)) keys_[time] = value;
    }
    void loop(bool enabled, float duration) { loop_ = enabled; duration_ = duration; }
    float getTotalTime() const
    {
        if (keys_.empty()) return 0.f;
        const float difference = keys_.rbegin()->first - keys_.begin()->first;
        if (difference > -.0004f && difference < .0004f) return 0.f;
        return loop_ ? duration_ : difference;
    }
    float animate(float time) const
    {
        if (keys_.empty()) return 0.f;
        if (keys_.size() == 1) return keys_.begin()->second;
        if (loop_ && (time > duration_ || time < 0.f)) time -= std::floor(time / duration_) * duration_;
        auto upper = keys_.upper_bound(time);
        if (!loop_)
        {
            if (upper == keys_.end()) return keys_.rbegin()->second;
            if (upper == keys_.begin()) return upper->second;
        }
        auto lower = upper;
        if (lower == keys_.begin()) lower = keys_.end();
        --lower;
        if (upper == keys_.end()) upper = keys_.begin();
        const float firstTime = lower->first, firstValue = lower->second;
        float lastTime = upper->first;
        if (std::fabs(firstTime - time) < .0004f) return firstValue;
        if (loop_)
        {
            if (firstTime > lastTime) lastTime += duration_;
            if (firstTime > time) time += duration_;
        }
        const float fraction = (time - firstTime) / (lastTime - firstTime);
        return firstValue + (upper->second - firstValue) * fraction;
    }
};
}
