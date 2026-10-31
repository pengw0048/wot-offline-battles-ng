// The battle render callback: every subsystem's per-frame step in the pinned order, then the next callback.
#if !defined(WOT_NATIVE_BATTLE_HOST_BUILD)
#include "pch.hpp"
#endif
#include "stock_native_battle_subsystems.hpp"
#include "stock_native_battle_vehicles_internal.hpp"
#include "stock_native_battle_pending.hpp"
#include "stock_native_battle_link.hpp"
#include <chrono>

namespace native_battle {
namespace {
double wall() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
}

namespace {
// BattleRuntime._prewarm_critical_layout: on the worker before the battle goes live, one vehicle layout per frame.
void prewarmCriticalLayout(Battle& b) {
    if (!b.worker || b.live || b.prebattle_deadline < 0.) return;
    for (auto& entry : b.vehicles->actors) {
        Actor& v = *entry.second;
        if (!v.engine_id || !v.ready || v.tombstone || v.local || v.presented.critical_layout_prewarmed) continue;
        auto entity = b.entity(v.engine_id);
        if (entity.get() == Py_None || !p::flag(py::optional(entity.get(), "isStarted").get())) continue;
        auto descriptor = py::optional(entity.get(), "typeDescriptor");
        if (descriptor.get() == Py_None) continue;
        bool completed = true;
        try { completed = critical::prewarmLayout(descriptor.get()); }
        catch (const std::exception& e) { b.warn("critical layout prewarm for " + entry.first, describe(e)); }
        if (completed) { v.presented.critical_layout_prewarmed = true; return; }
    }
}
}

// BattleRuntime._server_clock: BigWorld.serverTime(), else the battle clock.
double startup::serverClock(Battle& b) {
    PyObject* raw = PyObject_CallMethod(b.engine.bigworld.get(), const_cast<char*>("serverTime"), nullptr);
    if (raw) {
        const double value = PyFloat_AsDouble(raw);
        Py_DECREF(raw);
        if (!PyErr_Occurred()) return value;
    }
    PyErr_Clear();
    return b.clock();
}

// One render callback of a running battle.
void startup::frame(Battle& b) {
    if (!b.running()) return;
    FrameTiming timing;
    timing.start = wall();
    const double now = b.clock();
    const double raw_dt = b.last_frame < 0. ? 0. : now - b.last_frame;
    double rule_dt = std::max(0., raw_dt);
    timing.raw_dt = raw_dt;
    if (b.worker) {
        link::recordFrame(raw_dt);
        bots::frameStarted(b, rule_dt, raw_dt);
    }
    double dt = rule_dt;
    const double frame_start = b.last_frame;
    b.last_frame = now;
    double boundary = timing.start;
    auto stage = [&](const char* name) {
        const double next = wall();
        timing.stages.emplace_back(name, std::max(0., next - boundary));
        boundary = next;
    };
    const bool worker = b.worker, replay = b.replay;
    try {
        b.feature("map visibility filtering", [&] { visibility::maintainSpaceVisibility(b, now); });
        vehicles::flushPendingBotCreate(b, now);
        vehicles::flushPendingEntities(b, now);
        startup::startTeamChatWhenRosterReady(b);
        events::drain(b);
        b.feature("foliage camouflage", [&] { world::foliage(b, now); });
        bots::retryManifest(b, now);
        startup::maybeSendBattleReady(b);
        startup::ensureRecording(b);
        stage("house");
        remote::advance(b, now);
        if (b.live && worker) bots::playerEnvironment(b, rule_dt, now);
        stage("sync");
        if (b.live && !worker && !replay) critical::tick(b, rule_dt);
        if (!worker && !replay) b.feature("Expert damaged-device presentation", [&] { visibility::expert(b, now); });
        b.feature("detached turret lifecycle", [&] { world::detachedTurrets(b, now); }, false);
        stage("critical");
        if (b.live && !worker && !replay) {
            local::tickDrowning(b, rule_dt, now);
            local::tickOverturn(b, rule_dt, now);
        }
        stage("drown");
        if (!b.live && b.prebattle_deadline >= 0.) bots::prewarm(b, now);
        try { world::prewarm(b, now); } catch (const std::exception& e) { b.warn("destructible prewarm", describe(e), false); }
        try { prewarmCriticalLayout(b); } catch (const std::exception& e) { b.warn("critical layout prewarm", describe(e), false); }
        stage("prewarm");
        if (!b.live) projectiles::prebattleSample(b, now);
        if (!b.live && startup::prebattleTransitionReady(b, now)) {
            const double live_edge = b.prebattle_deadline;
            startup::beginBattle(b);
            const double interval_start = frame_start < 0. ? now : frame_start;
            dt = rule_dt = std::max(0., now - std::max(interval_start, live_edge));
            timing.transitioned = true;
            if (worker) bots::playerEnvironment(b, rule_dt, now);
            else if (!replay) {
                critical::tick(b, rule_dt);
                local::tickDrowning(b, rule_dt, now);
                local::tickOverturn(b, rule_dt, now);
            }
        }
        stage("transition");
        if (b.live && !worker && !replay) local::drive(b, dt);
        if (!worker && !replay) gun::frame(b, now);
        stage("local");
        if (b.live && !worker && !replay) b.feature("target outline", [&] { visibility::outline(b, now); });
        stage("outline");
        if (b.live && !replay) bots::update(b, now, rule_dt);
        stage("bots_update");
        if (b.live && !replay) timing.bot_count = bots::presentPoses(b, now);
        stage("bot_present");
        if (worker && b.live && !bots::drainWorkerTransport(b)) return;
        if (b.live) projectiles::advance(b, now);
        stage("bot_events");
        if (!worker && !replay) {
            if (b.live) b.feature("spotting", [&] { visibility::spotting(b, now, false); }, false);
            else if (b.prebattle_deadline >= 0.) b.feature("spotting", [&] { visibility::spotting(b, now, true); }, false);
        }
        stage("spot");
        if (b.live && !worker && !replay) b.feature("target lock validation", [&] { visibility::validateTargetLock(b); }, false);
        if (replay) local::presentReplay(b, dt);
        else startup::recordLocal(b);
        stage("lock");
    } catch (const std::exception& e) {
        b.fail(describe(e));
        return;
    }
    // FRAME_SECONDS is zero: the next callback runs on the next rendered frame.
    startup::scheduleFrame(b, 0.);
    stage("schedule");
    std::map<std::string, double> stages;
    for (const auto& item : timing.stages) stages[item.first] += item.second;
    startup::diagnosticsFrame(b, timing.start, wall() - timing.start, stages);
}
}
