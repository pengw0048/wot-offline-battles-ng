// Drives the production stock_flock_field.hpp through #1513 WGFlockManager
// scenarios and prints one "name value" line per observation.
#include "stock_flock_field.hpp"
#include <cmath>
#include <cstdio>
#include <functional>
#include <limits>
#include <vector>

using wot::StockFlockBounds;
using wot::StockFlockField;
using wot::StockFlockPoint;

static void report(const char * name, double value) { std::printf("%s %.9g\n", name, value); }

static const uint32_t kSpace = 1;
// DeprecatedSpaceHelpers::cameraSpaceChanges() as WGFlockManager reads it.
static uint32_t g_cameraSpace = kSpace;

// WGFlockManager::fence retires a run whose camera space was replaced.
static void fence(StockFlockField & field)
{
    if (field.stale(g_cameraSpace)) field.stop();
}

struct Trigger
{
    std::vector<int> ids;
    std::function<void(int)> reenter;
    StockFlockField * owner = nullptr;
    void operator()(int id)
    {
        ids.push_back(id);
        if (reenter) reenter(id);
        if (owner) fence(*owner);
    }
};

// One WG queue entry as WGFlockManager::tick runs it.
static bool tick(StockFlockField & field, double now, uint32_t frame, StockFlockPoint camera,
    const std::vector<StockFlockPoint> & vehicles, Trigger & trigger)
{
    fence(field);
    trigger.owner = &field;
    if (!field.due(now, frame)) return false;
    const unsigned generation = field.generation();
    field.advance();
    const StockFlockField::Window window = field.window(camera);
    bool alive = true;
    for (size_t i = 0; alive && i < vehicles.size(); ++i)
        alive = field.test(vehicles[i], false, window, generation, trigger);
    if (alive)
    {
        const std::vector<StockFlockPoint> points = field.takeActivationPoints();
        for (size_t i = 0; alive && i < points.size(); ++i)
            alive = field.test(points[i], true, window, generation, trigger);
    }
    if (field.generation() == generation) field.reschedule(now, frame);
    return true;
}

// 2000 m x 1000 m space: 10 x-cells (+0x18) and 5 z-cells (+0x14).
static const StockFlockBounds kBounds = { -1000.f, -50.f, -500.f, 1000.f, 300.f, 500.f };

int main()
{
    {
        StockFlockField field;
        report("default.interval", field.interval());
        report("default.running", field.running());
        field.addFlock({ 10.f, 20.f }, 77, 20.f, { 8.f, 35.f }, 400.f);
        const wot::StockFlockRecord & r = field.records()[0];
        report("record.trigger2", r.triggerRadiusSquared);
        report("record.min2", r.explosionMinSquared);
        report("record.max2", r.explosionMaxSquared);
        report("record.respawn", r.respawnTime);
        report("record.id", r.id);
        report("record.last", r.lastTriggerTime);
        report("record.inside", r.inside);
        field.start(1.f, nullptr, kSpace, 5.0, 1);
        report("nospace.running", field.running());
        report("nospace.records", double(field.records().size()));
    }
    {
        StockFlockField field;
        field.start(0.5f, &kBounds, kSpace, 10.0, 7);
        report("start.running", field.running());
        report("start.width", field.width());
        report("start.height", field.height());
        report("start.interval", field.interval());
        report("start.due-same-frame", field.due(11.0, 7));
        report("start.due-early", field.due(10.4, 8));
        report("start.due", field.due(10.5, 8));
        field.start(3.f, &kBounds, kSpace, 20.0, 9);
        report("restart-ignored.interval", field.interval());
        field.stop();
        field.start(2.f, &kBounds, kSpace, 20.0, 9);
        report("restart.interval", field.interval());
        field.reschedule(30.0, 10);
        report("reschedule.due-before", field.due(31.9, 11));
        report("reschedule.due", field.due(32.0, 11));
        StockFlockField negative;
        negative.start(-1.f, &kBounds, kSpace, 0.0, 0);
        report("negative.running", negative.running());
        report("negative.scheduled", negative.scheduled());
        StockFlockField nan;
        nan.start(std::numeric_limits<float>::quiet_NaN(), &kBounds, kSpace, 0.0, 0);
        report("nan.interval", nan.interval());
        report("nan.due", nan.due(0.0, 1));
        const StockFlockBounds inside = { 10.f, 0.f, 10.f, -10.f, 0.f, -10.f };
        StockFlockField empty;
        empty.start(1.f, &inside, kSpace, 0.0, 0);
        report("insideout.width", empty.width());
        report("insideout.height", empty.height());
        const StockFlockBounds rounding = { 0.f, 0.f, 0.f, 299.f, 0.f, 300.f };
        StockFlockField rounded;
        rounded.start(1.f, &rounding, kSpace, 0.0, 0);
        report("rounding.width", rounded.width());
        report("rounding.height", rounded.height());
    }
    {
        // Cells hold a record only when both indices are > 0; clamps stay high-only.
        StockFlockField field;
        field.addFlock({ -900.f, 0.f }, 1, 20.f, { 8.f, 35.f }, 5.f);
        field.addFlock({ 0.f, -450.f }, 2, 20.f, { 8.f, 35.f }, 5.f);
        field.addFlock({ 50.f, 50.f }, 3, 20.f, { 8.f, 35.f }, 5.f);
        field.addFlock({ 5000.f, 5000.f }, 4, 20.f, { 8.f, 35.f }, 5.f);
        field.start(1.f, &kBounds, kSpace, 0.0, 0);
        report("bin.x0", double(field.cell(2, 0).size()));
        report("bin.z0", double(field.cell(0, 5).size()));
        report("bin.center", double(field.cell(2, 5).size()));
        report("bin.clamped", double(field.cell(4, 9).size()));
        report("bin.clamped-record", field.cell(4, 9).empty() ? -1 : field.cell(4, 9)[0]);
    }
    {
        // Late records index only while the record count stays below 150.
        StockFlockField field;
        field.start(1.f, &kBounds, kSpace, 0.0, 0);
        for (int i = 0; i < 150; ++i) field.addFlock({ 50.f, 50.f }, i, 1.f, { 0.f, 1.f }, 1.f);
        report("late.indexed", double(field.cell(2, 5).size()));
        report("late.records", double(field.records().size()));
    }
    {
        StockFlockField field;
        field.start(1.f, &kBounds, kSpace, 0.0, 0);
        StockFlockField::Window w = field.window({ -1000.f, -500.f });
        report("window.corner", w.lo[0] * 1000 + w.hi[0] * 100 + w.lo[1] * 10 + w.hi[1]);
        w = field.window({ 0.f, 0.f });
        report("window.center", w.lo[0] * 1000 + w.hi[0] * 100 + w.lo[1] * 10 + w.hi[1]);
        w = field.window({ 5000.f, 5000.f });
        report("window.high", w.lo[0] * 1000 + w.hi[0] * 100 + w.lo[1] * 10 + w.hi[1]);
        w = field.window({ std::numeric_limits<float>::quiet_NaN(), 0.f });
        report("window.nan-empty", w.lo[0] > w.hi[0]);
    }
    {
        // Inclusive disk, inclusive annulus, ordered sources and the inside/respawn machine.
        StockFlockField field;
        field.addFlock({ 50.f, 50.f }, 11, 20.f, { 8.f, 35.f }, 2.5f);
        field.start(1.f, &kBounds, kSpace, 0.0, 0);
        Trigger trigger;
        const StockFlockPoint camera = { 50.f, 50.f };
        uint32_t frame = 1;
        double now = 1.0;
        auto step = [&](const std::vector<StockFlockPoint> & vehicles) {
            ++frame;
            tick(field, now, frame, camera, vehicles, trigger);
            now += 1.0;
            return trigger.ids.size();
        };
        report("disk.outside", double(step({ { 70.001f, 50.f } })));
        report("disk.edge", double(step({ { 70.f, 50.f } })));
        report("disk.inside-no-repeat", double(step({ { 60.f, 50.f } })));
        report("cooldown.outside-skipped", double(step({ { 200.f, 50.f } })));
        report("cooldown.inside-after", field.records()[0].inside);
        report("respawn.still-inside", double(step({ { 60.f, 50.f } })));
        report("respawn.left", double(step({ { 200.f, 50.f } })));
        report("respawn.reentered", double(step({ { 55.f, 50.f } })));
        report("cooldown.both-sources-skipped", double(step({ { 55.f, 50.f }, { 52.f, 50.f } })));
        step({ { 200.f, 50.f } });
        report("ordered.left-after-cooldown", double(step({ { 200.f, 50.f } })));
        report("ordered.left-inside", field.records()[0].inside);
        report("ordered.first-of-two", double(step({ { 55.f, 50.f }, { 52.f, 50.f } })));
        report("elapsed", field.elapsed());
    }
    {
        StockFlockField field;
        field.addFlock({ 50.f, 50.f }, 21, 1.f, { 8.f, 35.f }, 0.5f);
        field.start(1.f, &kBounds, kSpace, 0.0, 0);
        Trigger trigger;
        uint32_t frame = 1;
        double now = 1.0;
        auto shot = [&](StockFlockPoint p) {
            field.addActivationPoint(p);
            tick(field, now, ++frame, { 50.f, 50.f }, {}, trigger);
            now += 1.0;
            const size_t count = trigger.ids.size();
            // An in-window miss inside the inner radius clears inside.
            field.addActivationPoint({ 50.f, 50.f });
            tick(field, now, ++frame, { 50.f, 50.f }, {}, trigger);
            now += 1.0;
            return count;
        };
        report("ring.inner", double(shot({ 57.999f, 50.f })));
        report("ring.min-edge", double(shot({ 58.f, 50.f })));
        report("ring.max-edge", double(shot({ 50.f, 85.f })));
        report("ring.outer", double(shot({ 50.f, 85.01f })));
        report("ring.consumed", double(field.pendingActivationPoints()));
        // A vehicle miss clears inside before a later activation hit in the same sweep.
        field.addActivationPoint({ 60.f, 50.f });
        tick(field, now, ++frame, { 50.f, 50.f }, {}, trigger);
        now += 1.0;
        const size_t entered = trigger.ids.size();
        field.addActivationPoint({ 60.f, 50.f });
        tick(field, now, ++frame, { 50.f, 50.f }, { { 52.f, 50.f } }, trigger);
        now += 1.0;
        report("ordered.vehicle-then-shot", double(trigger.ids.size() - entered));
        field.addActivationPoint({ 60.f, 50.f });
        tick(field, now, ++frame, { 50.f, 50.f }, {}, trigger);
        now += 1.0;
        report("ordered.shot-while-inside", double(trigger.ids.size() - entered));
        // Out-of-window sources are not evaluated.
        field.addActivationPoint({ 900.f, 450.f });
        const size_t before = trigger.ids.size();
        now += 5.0;
        tick(field, now, ++frame, { 50.f, 50.f }, {}, trigger);
        report("window.outside-source", double(trigger.ids.size() - before));
    }
    {
        // A callback that stops, restarts or appends never resumes the old sweep.
        StockFlockField field;
        field.addFlock({ 50.f, 50.f }, 31, 20.f, { 8.f, 35.f }, 1.f);
        field.addFlock({ 51.f, 50.f }, 32, 20.f, { 8.f, 35.f }, 1.f);
        field.start(1.f, &kBounds, kSpace, 0.0, 0);
        Trigger trigger;
        trigger.reenter = [&](int) { field.stop(); };
        tick(field, 1.0, 1, { 50.f, 50.f }, { { 50.f, 50.f } }, trigger);
        report("reenter.stop.callbacks", double(trigger.ids.size()));
        report("reenter.stop.running", field.running());
        report("reenter.stop.records", double(field.records().size()));
        report("reenter.stop.scheduled", field.scheduled());

        field.addFlock({ 50.f, 50.f }, 41, 20.f, { 8.f, 35.f }, 1.f);
        field.addFlock({ 51.f, 50.f }, 42, 20.f, { 8.f, 35.f }, 1.f);
        field.start(1.f, &kBounds, kSpace, 2.0, 2);
        Trigger restart;
        restart.reenter = [&](int) {
            restart.reenter = nullptr;
            field.stop();
            field.addFlock({ 50.f, 50.f }, 51, 20.f, { 8.f, 35.f }, 1.f);
            field.start(0.25f, &kBounds, kSpace, 3.0, 3);
        };
        tick(field, 3.0, 4, { 50.f, 50.f }, { { 50.f, 50.f } }, restart);
        report("reenter.restart.callbacks", double(restart.ids.size()));
        report("reenter.restart.running", field.running());
        // The new entry is due at 3.25; a re-armed old sweep would move it to 4.0.
        report("reenter.restart.new-schedule-kept", field.due(3.3, 5));
        report("reenter.restart.interval", field.interval());
        report("reenter.restart.new-record-fresh", field.records()[0].inside);

        StockFlockField growing;
        growing.addFlock({ 50.f, 50.f }, 61, 20.f, { 8.f, 35.f }, 1.f);
        growing.start(1.f, &kBounds, kSpace, 0.0, 0);
        Trigger append;
        append.reenter = [&](int) {
            for (int i = 0; i < 64; ++i) growing.addFlock({ 50.f, 50.f }, 70 + i, 20.f, { 8.f, 35.f }, 1.f);
        };
        tick(growing, 1.0, 1, { 50.f, 50.f }, { { 50.f, 50.f } }, append);
        report("reenter.append.callbacks", double(append.ids.size()));
        append.reenter = nullptr;
        tick(growing, 2.0, 2, { 50.f, 50.f }, { { 50.f, 50.f } }, append);
        report("reenter.append.next-sweep", double(append.ids.size()));
    }
    {
        // Stop before start and twice; the activation queue is cleared.
        StockFlockField field;
        field.addActivationPoint({ 1.f, 1.f });
        field.stop();
        field.stop();
        report("stop.idle.points", double(field.pendingActivationPoints()));
        report("stop.idle.running", field.running());
        StockFlockField empty;
        empty.start(1.f, &kBounds, kSpace, 0.0, 0);
        Trigger trigger;
        tick(empty, 1.0, 1, { 0.f, 0.f }, { { 0.f, 0.f } }, trigger);
        report("empty.callbacks", double(trigger.ids.size()));
        report("empty.rescheduled", empty.due(2.0, 2));
    }
    {
        // The started camera space assignment fences the queued timer.
        StockFlockField field;
        field.addFlock({ 50.f, 50.f }, 81, 20.f, { 8.f, 35.f }, 1.f);
        report("space.idle-stale", field.stale(kSpace + 1));
        field.start(1.f, &kBounds, kSpace, 0.0, 0);
        Trigger same;
        report("space.same.swept", tick(field, 1.0, 1, { 50.f, 50.f }, { { 50.f, 50.f } }, same));
        report("space.same.callbacks", double(same.ids.size()));
        g_cameraSpace = kSpace + 1;
        Trigger replaced;
        report("space.replaced.swept", tick(field, 2.0, 2, { 50.f, 50.f }, { { 50.f, 50.f } }, replaced));
        report("space.replaced.callbacks", double(replaced.ids.size()));
        report("space.replaced.running", field.running());
        report("space.replaced.records", double(field.records().size()));
        // A record added under the new space waits for its own start.
        field.addFlock({ 50.f, 50.f }, 82, 20.f, { 8.f, 35.f }, 1.f);
        field.start(1.f, &kBounds, g_cameraSpace, 3.0, 3);
        Trigger next;
        tick(field, 4.0, 4, { 50.f, 50.f }, { { 50.f, 50.f } }, next);
        report("space.next.first", next.ids.empty() ? -1 : next.ids[0]);
        report("space.next.running", field.running());
    }
    {
        // A callback whose script replaced the camera space ends the sweep.
        g_cameraSpace = kSpace;
        StockFlockField field;
        field.addFlock({ 50.f, 50.f }, 91, 20.f, { 8.f, 35.f }, 1.f);
        field.addFlock({ 51.f, 50.f }, 92, 20.f, { 8.f, 35.f }, 1.f);
        field.start(1.f, &kBounds, kSpace, 0.0, 0);
        Trigger trigger;
        trigger.reenter = [&](int) { g_cameraSpace = kSpace + 2; };
        tick(field, 1.0, 1, { 50.f, 50.f }, { { 50.f, 50.f } }, trigger);
        report("space.callback.callbacks", double(trigger.ids.size()));
        report("space.callback.running", field.running());
        report("space.callback.scheduled", field.scheduled());
        g_cameraSpace = kSpace;
    }
    return 0;
}
