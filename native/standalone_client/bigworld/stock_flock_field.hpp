// #1513 WGFlockManager trigger state (008a2440..008a31f0) without engine types.
// stock_flock_manager.cpp binds it to BigWorld; tests drive it directly.
#pragma once
#include <cstdint>
#include <vector>

namespace wot {

struct StockFlockPoint { float x, z; };

// ClientSpace::bounds() as read by 008a2fb4: min then max.
struct StockFlockBounds { float minX, minY, minZ, maxX, maxY, maxZ; };

struct StockFlockRecord
{
    float x, z;
    float triggerRadiusSquared;
    float explosionMinSquared, explosionMaxSquared;
    float respawnTime;
    int id;
    float lastTriggerTime;
    bool inside;
};

class StockFlockField
{
public:
    // Inclusive cell window around the camera, [x, z] per bound.
    struct Window { int lo[2], hi[2]; };

    bool running() const { return running_; }
    float interval() const { return interval_; }
    float elapsed() const { return elapsed_; }
    int width() const { return width_; }
    int height() const { return height_; }
    unsigned generation() const { return generation_; }
    bool scheduled() const { return scheduled_; }
    const std::vector<StockFlockRecord> & records() const { return records_; }
    const std::vector<int> & cell(int z, int x) const { return grid_[z][x]; }
    size_t pendingActivationPoints() const { return activation_.size(); }

    // True once the camera space assignment the run started under was replaced.
    bool stale(uint32_t cameraSpace) const { return running_ && cameraSpace != space_; }

    // 008a1f70 squares every radius; 008a25d0 indexes a late record while
    // running and below 150 records.
    void addFlock(StockFlockPoint position, int id, float triggerRadius,
        StockFlockPoint explosionRadius, float respawnTime)
    {
        StockFlockRecord record = { position.x, position.z,
            triggerRadius * triggerRadius,
            explosionRadius.x * explosionRadius.x,
            explosionRadius.z * explosionRadius.z,
            respawnTime, id, -1.f, false };
        records_.push_back(record);
        if (running_ && double(records_.size()) < 150.0)
            index(position, int(records_.size()) - 1);
    }

    void addActivationPoint(StockFlockPoint position) { activation_.push_back(position); }

    // 008a2f40. A missing camera space leaves the owner stopped. A negative
    // first delay is refused by the timer queue (008a55d0), so no sweep runs.
    void start(float requestedInterval, const StockFlockBounds * bounds,
        uint32_t cameraSpace, double now, uint32_t frame)
    {
        if (running_ || !bounds) return;
        space_ = cameraSpace;
        if (requestedInterval > interval_) interval_ = requestedInterval;
        running_ = true;
        ++generation_;
        offsetX_ = -bounds->minX;
        offsetZ_ = -bounds->minZ;
        const bool valid = !(bounds->minX > bounds->maxX) &&
            !(bounds->minY > bounds->maxY) && !(bounds->minZ > bounds->maxZ);
        width_ = truncate64((valid ? bounds->maxZ - bounds->minZ : 0.f) * kCellScale + 0.5f);
        height_ = truncate64((valid ? bounds->maxX - bounds->minX : 0.f) * kCellScale + 0.5f);
        grid_.assign(width_ > 0 ? size_t(width_) : 0,
            std::vector<std::vector<int> >(height_ > 0 ? size_t(height_) : 0));
        for (size_t i = 0; i < records_.size(); ++i)
            index(StockFlockPoint{ records_[i].x, records_[i].z }, int(i));
        elapsed_ = 0.f;
        scheduled_ = !(requestedInterval < 0.f);
        due_ = now + double(requestedInterval);
        scheduledFrame_ = frame;
    }

    // 008a31f0: providers belong to the binding; the callback survives.
    void stop()
    {
        grid_.clear();
        records_.clear();
        activation_.clear();
        running_ = false;
        width_ = height_ = 0;
        scheduled_ = false;
        ++generation_;
    }

    // 008a5860 skips an entry scheduled in the current frame.
    bool due(double now, uint32_t frame) const
    {
        return scheduled_ && frame != scheduledFrame_ && !(now < due_);
    }

    // 008a2400 returns the owner's interval to the queue.
    void reschedule(double now, uint32_t frame)
    {
        due_ = now + double(interval_);
        scheduledFrame_ = frame;
    }

    // 008a32a0 first advances its own clock by the interval.
    void advance() { elapsed_ = interval_ + elapsed_; }

    // 008a32a0 then bounds the 3x3 cell window around the camera.
    Window window(StockFlockPoint camera) const
    {
        const int x = clampHigh(cellOf(offsetX_ + camera.x), height_ - 1);
        const int z = clampHigh(cellOf(offsetZ_ + camera.z), width_ - 1);
        Window window;
        window.lo[0] = wrapAdd(x, -1) > 0 ? wrapAdd(x, -1) : 0;
        window.lo[1] = wrapAdd(z, -1) > 0 ? wrapAdd(z, -1) : 0;
        window.hi[0] = clampHigh(wrapAdd(window.lo[0], 2), height_ - 1);
        window.hi[1] = clampHigh(wrapAdd(window.lo[1], 2), width_ - 1);
        return window;
    }

    // Activation points queued so far; later ones wait for the next sweep.
    std::vector<StockFlockPoint> takeActivationPoints()
    {
        std::vector<StockFlockPoint> points;
        points.swap(activation_);
        return points;
    }

    // 008a2c90 with 008a18b0 (vehicle) or 008a18f0 (activation point).
    // Returns false once a callback stopped or restarted this owner.
    template <class Trigger>
    bool test(StockFlockPoint source, bool activation, const Window & window,
        unsigned generation, Trigger & trigger)
    {
        if (generation != generation_) return false;
        const int x = clampHigh(cellOf(offsetX_ + source.x), height_ - 1);
        const int z = clampHigh(cellOf(offsetZ_ + source.z), width_ - 1);
        if (!(window.lo[0] <= x && x <= window.hi[0] && window.lo[1] <= z &&
                z <= window.hi[1] && window.lo[1] <= window.hi[1]))
            return true;
        for (int row = window.lo[1]; row <= window.hi[1]; ++row)
        {
            for (int column = window.lo[0]; column <= window.hi[0]; ++column)
            {
                // 008a2c90 fixes the cell end first; a callback may still
                // append to it or reallocate the records.
                const size_t count = grid_[row][column].size();
                for (size_t slot = 0; slot < count; ++slot)
                {
                    StockFlockRecord & record = records_[grid_[row][column][slot]];
                    const float time = elapsed_;
                    if (!(record.lastTriggerTime <= 0.f))
                    {
                        if (!(record.respawnTime <= time - record.lastTriggerTime)) continue;
                        record.lastTriggerTime = -1.f;
                    }
                    const bool hit = activation ? insideRing(source, record) : insideRadius(source, record);
                    if (!hit)
                    {
                        record.inside = false;
                        continue;
                    }
                    if (record.inside) continue;
                    record.lastTriggerTime = time;
                    record.inside = true;
                    trigger(record.id);
                    if (generation != generation_) return false;
                }
            }
        }
        return true;
    }

private:
    // 014b7bf8: 1 / 200 m.
    static constexpr float kCellScale = 0.005f;

    static int wrapAdd(int value, int delta) { return int(uint32_t(value) + uint32_t(delta)); }
    static int clampHigh(int value, int limit) { return limit < value ? limit : value; }

    // cvttss2si: out-of-range and NaN become the integer indefinite.
    static int cellOf(float coordinate)
    {
        const float scaled = coordinate * kCellScale;
        if (!(scaled > -2147483648.f && scaled < 2147483648.f)) return INT32_MIN;
        return int(scaled);
    }

    // 013d81d0 (__ftol3) truncates toward zero; the bounds keep it in range.
    static int truncate64(float value)
    {
        if (!(value > -9.2233720368547758e18f && value < 9.2233720368547758e18f)) return 0;
        return int(int64_t(value));
    }

    static bool insideRadius(StockFlockPoint source, const StockFlockRecord & record)
    {
        const float dx = record.x - source.x;
        const float dz = record.z - source.z;
        return dx * dx + dz * dz <= record.triggerRadiusSquared;
    }

    static bool insideRing(StockFlockPoint source, const StockFlockRecord & record)
    {
        const float dx = record.x - source.x;
        const float dz = record.z - source.z;
        const float distance = dz * dz + dx * dx;
        return record.explosionMinSquared <= distance && distance <= record.explosionMaxSquared;
    }

    // 008a2c10 and the 008a2f40 loop index only cells with both indices > 0.
    void index(StockFlockPoint position, int record)
    {
        const int x = clampHigh(cellOf(offsetX_ + position.x), height_ - 1);
        const int z = clampHigh(cellOf(offsetZ_ + position.z), width_ - 1);
        if (z > 0 && x > 0) grid_[z][x].push_back(record);
    }

    bool running_ = false;
    float interval_ = 1.f;
    float elapsed_ = 0.f;
    int width_ = 0;
    int height_ = 0;
    float offsetX_ = 0.f;
    float offsetZ_ = 0.f;
    unsigned generation_ = 0;
    bool scheduled_ = false;
    double due_ = 0.0;
    uint32_t scheduledFrame_ = 0;
    uint32_t space_ = 0;
    std::vector<StockFlockRecord> records_;
    std::vector<std::vector<std::vector<int> > > grid_;
    std::vector<StockFlockPoint> activation_;
};

} // namespace wot
