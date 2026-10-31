// Cosmetic projectile tracers: the stock #1513 ProjectileMover presents canonical launches while the authority owns
// collision and every terminal verdict. Admission bounds cosmetic work without rejecting an authoritative shot.
#if !defined(WOT_NATIVE_BATTLE_HOST_BUILD)
#include "pch.hpp"
#endif
#include "stock_native_battle_projectiles.hpp"
#include <algorithm>
#include <cmath>

namespace native_battle {
namespace tracers {
namespace {
// A short autocannon burst keeps a generous immediate allowance; sustained fire refills slower.
constexpr double burst_capacity = 16.;
constexpr double refill_per_second = 5.;
constexpr std::size_t max_active_per_attacker = 24;
constexpr std::size_t max_active_total = 128;
constexpr std::size_t max_projectile_id = 128;

// Logs each tracer failure stage once per round.
void reportFailure(TracerPresenter& t, const std::string& stage, const std::string& error = {}) {
    if (!t.failure_stages.insert(stage).second) return;
    log("projectile visual " + stage + (error.empty() ? "" : ": " + error));
}
p::Ref vector3(const Battle& b, const Point& v) {
    return py::call(b.engine.math.get(), "Vector3", p::number(v[0]).get(), p::number(v[1]).get(), p::number(v[2]).get());
}
p::Ref smallInteger(std::int64_t v) { return p::Ref(PyInt_FromLong(long(v))); }
// A descriptor component's field: dict item or attribute, None when absent.
p::Ref component(PyObject* value, const char* name) {
    if (PyDict_Check(value)) {
        PyObject* found = PyDict_GetItemString(value, name);
        return p::retain(found ? found : Py_None);
    }
    return py::optional(value, name);
}
// The descriptor's shot at exactly shell_index, or None.
p::Ref shotAt(PyObject* descriptor, std::int64_t shell_index) {
    auto gun = py::optional(descriptor, "gun");
    auto raw = component(gun.get(), "shots");
    p::Ref shots(PySequence_Tuple(p::flag(raw.get()) ? raw.get() : p::Ref(PyTuple_New(0)).get()));
    if (shell_index < 0 || shell_index >= PyTuple_GET_SIZE(shots.get())) return p::retain(Py_None);
    return p::retain(PyTuple_GET_ITEM(shots.get(), Py_ssize_t(shell_index)));
}
std::optional<double> finite(PyObject* value) {
    PyObject* number = PyNumber_Float(value);
    if (!number) { PyErr_Clear(); return std::nullopt; }
    const double v = PyFloat_AS_DOUBLE(number);
    Py_DECREF(number);
    return std::isfinite(v) ? std::optional<double>(v) : std::nullopt;
}
// (x, y, z) of a Vector3 or a sequence, when every component is finite.
std::optional<Point> finitePoint(PyObject* value) {
    if (!value || value == Py_None) return std::nullopt;
    PyObject* parts[3] = {};
    static const char* const names[3] = {"x", "y", "z"};
    bool attributes = true;
    for (int i = 0; i < 3 && attributes; ++i) {
        parts[i] = PyObject_GetAttrString(value, names[i]);
        if (!parts[i]) {
            if (!PyErr_ExceptionMatches(PyExc_AttributeError)) { for (auto* part : parts) Py_XDECREF(part); throw p::Error(names[i]); }
            PyErr_Clear();
            attributes = false;
        }
    }
    if (!attributes) {
        for (auto*& part : parts) { Py_XDECREF(part); part = nullptr; }
        for (int i = 0; i < 3; ++i) {
            parts[i] = PySequence_GetItem(value, i);
            if (!parts[i]) {
                const bool absent = PyErr_ExceptionMatches(PyExc_TypeError) || PyErr_ExceptionMatches(PyExc_IndexError) ||
                                    PyErr_ExceptionMatches(PyExc_KeyError);
                for (auto* part : parts) Py_XDECREF(part);
                if (!absent) throw p::Error("vector component");
                PyErr_Clear();
                return std::nullopt;
            }
        }
    }
    Point out{};
    bool valid = true;
    for (int i = 0; i < 3; ++i) {
        const auto component_value = finite(parts[i]);
        Py_DECREF(parts[i]);
        if (component_value) out[i] = *component_value;
        else valid = false;
    }
    return valid ? std::optional<Point>(out) : std::nullopt;
}
bool finitePoint(const Point& v) { return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]); }
PyObject* nativeProjectiles(PyObject* mover) {
    PyObject* native = PyObject_GetAttrString(mover, "_ProjectileMover__projectiles");
    if (!native) throw p::Error("ProjectileMover projectiles");
    if (!PyDict_Check(native)) { Py_DECREF(native); throw p::Error("ProjectileMover projectiles are not a dict"); }
    return native;
}
bool contains(PyObject* dict, std::int64_t key) {
    auto k = smallInteger(key);
    const int found = PyDict_Contains(dict, k.get());
    if (found < 0) throw p::Error("ProjectileMover id lookup");
    return found != 0;
}

// hide/explode call this; canonical feedback is emitted once by stop(), including after native expiry.
PyObject* notifyProjectileHit(PyObject* original, PyObject* args) {
    PyObject *position = nullptr, *projectile = nullptr;
    if (!PyArg_ParseTuple(args, "OO", &position, &projectile)) return nullptr;
    PyObject* canonical = PyDict_Check(projectile) ? PyDict_GetItemString(projectile, "_offlineLANCanonical") : nullptr;
    if (canonical) {
        const int truth = PyObject_IsTrue(canonical);
        if (truth < 0) return nullptr;
        if (truth) Py_RETURN_NONE;
    }
    return PyObject_CallFunctionObjArgs(original, position, projectile, nullptr);
}
PyMethodDef notify_projectile_hit = {const_cast<char*>("_ProjectileMover__notifyProjectileHit"), notifyProjectileHit, METH_VARARGS, nullptr};

// Drops the instance override, which holds the stock bound method (and so the mover).
void releaseMover(TracerPresenter& t) {
    if (t.mover && PyObject_DelAttrString(t.mover.get(), "_ProjectileMover__notifyProjectileHit") < 0) PyErr_Clear();
    t.mover = Object();
}

// The round's stock mover, bound to the Avatar's space before its first addProjectile; null after a setup failure.
PyObject* projectileMover(Battle& b) {
    auto& t = b.projectiles->tracers;
    if (t.mover || t.closed) return t.mover.get();
    Object mover;
    try {
        p::Ref module(PyImport_ImportModule("ProjectileMover"));
        mover = own(py::call(module.get(), "ProjectileMover"));
        auto original = py::attr(mover.get(), "_ProjectileMover__notifyProjectileHit");
        p::Ref override_hit(PyCFunction_NewEx(&notify_projectile_hit, original.get(), nullptr));
        py::set(mover.get(), "_ProjectileMover__notifyProjectileHit", override_hit.get());
        auto set_space_id = py::optional(mover.get(), "setSpaceID");
        if (!py::callable(set_space_id)) throw p::Error("#1513 ProjectileMover has no setSpaceID");
        auto space_id = py::attr(b.engine.avatar.get(), "spaceID");
        p::Ref space(PyNumber_Int(space_id.get()));
        py::invoke(set_space_id.get(), space.get());
        t.mover = std::move(mover);
    } catch (const std::exception& e) {
        const auto error = describe(e);
        if (mover) {
            auto destroy_mover = py::optional(mover.get(), "destroy");
            try { if (py::callable(destroy_mover)) py::invoke(destroy_mover.get()); } catch (const std::exception&) { PyErr_Clear(); }
            if (PyObject_DelAttrString(mover.get(), "_ProjectileMover__notifyProjectileHit") < 0) PyErr_Clear();
        }
        t.explosions_enabled = false;
        reportFailure(t, "mover setup", error);
        return nullptr;
    }
    return t.mover.get();
}

// Counts stock's active and negative-id tail owners; native expiry frees capacity through stock's own callback.
bool capacity(PyObject* mover, std::int64_t attacker_id) {
    p::Ref native(nativeProjectiles(mover));
    if (std::size_t(PyDict_Size(native.get())) >= max_active_total) return false;
    auto attacker = smallInteger(attacker_id);
    std::size_t owned = 0;
    Py_ssize_t position = 0;
    PyObject *key = nullptr, *entry = nullptr;
    while (PyDict_Next(native.get(), &position, &key, &entry)) {
        PyObject* owner = PyDict_Check(entry) ? PyDict_GetItemString(entry, "attackerID") : nullptr;
        if (!owner) {
            if (PyDict_Check(entry)) owner = Py_None;
            else throw p::Error("ProjectileMover entry is not a dict");
        }
        const int same = PyObject_RichCompareBool(owner, attacker.get(), Py_EQ);
        if (same < 0) throw p::Error("ProjectileMover attacker");
        owned += same != 0;
    }
    return owned < max_active_per_attacker;
}

// An increasing identity unused by active and stopped (negative-id) motors.
std::int64_t takeUnusedShotId(TracerPresenter& t, PyObject* mover) {
    p::Ref native(nativeProjectiles(mover));
    std::set<std::int64_t> mapped;
    for (const auto& shot : t.shots) mapped.insert(shot.second.visual_id);
    while (true) {
        const auto id = t.next_shot_id++;
        if (!mapped.count(id) && !contains(native.get(), id) && !contains(native.get(), -id)) return id;
    }
}

// #1513 input and flock feedback for one terminal, each once.
void notifyHit(Battle& b, TracerPresenter& t, ProjectileTracer& shot, PyObject* position) {
    if (!shot.input_hit_notified) {
        try {
            auto player = py::call(b.engine.bigworld.get(), "player");
            auto input_handler = py::attr(player.get(), "inputHandler");
            auto callback = py::optional(input_handler.get(), "onProjectileHit");
            p::Ref caliber(PyObject_GetItem(shot.effects.get(), p::text("caliber").get()));
            if (!py::callable(callback)) throw p::Error("#1513 projectile-hit input handler is unavailable");
            py::invoke(callback.get(), position, caliber.get(), shot.own_shot ? Py_True : Py_False);
            shot.input_hit_notified = true;
        } catch (const std::exception& e) { reportFailure(t, "projectile hit notification", describe(e)); }
    }
    if (!shot.flock_hit_notified) {
        try {
            if (!t.flock_manager) t.flock_manager = own(p::Ref(PyImport_ImportModule("FlockManager")));
            auto manager = py::call(t.flock_manager.get(), "getManager");
            auto callback = py::optional(manager.get(), "onProjectile");
            if (!py::callable(callback)) throw p::Error("#1513 projectile flock notification is unavailable");
            py::invoke(callback.get(), position);
            shot.flock_hit_notified = true;
        } catch (const std::exception& e) { reportFailure(t, "projectile flock notification", describe(e)); }
    }
}
// The stock own-shot world-miss trigger, once.
void notifyMissed(TracerPresenter& t, ProjectileTracer& shot) {
    if (!shot.own_shot || shot.missed_notified) return;
    try {
        if (!t.triggers_manager) t.triggers_manager = own(p::Ref(PyImport_ImportModule("TriggersManager")));
        auto manager = py::optional(t.triggers_manager.get(), "g_manager");
        auto callback = py::optional(manager.get(), "fireTrigger");
        auto types = py::optional(t.triggers_manager.get(), "TRIGGER_TYPE");
        auto missed = py::optional(types.get(), "PLAYER_SHOT_MISSED");
        if (!py::callable(callback) || missed.get() == Py_None) throw p::Error("#1513 projectile-miss trigger is unavailable");
        py::invoke(callback.get(), missed.get());
        shot.missed_notified = true;
    } catch (const std::exception& e) { reportFailure(t, "projectile miss notification", describe(e)); }
}

// The retail ground explosion for one world terminal; an expired motor still plays the material effect.
bool explode(Battle& b, TracerPresenter& t, PyObject* mover, std::int64_t shot_id, PyObject* end, const Explosion& explosion) {
    if (!t.explosions_enabled) return false;
    auto explode_effect = py::optional(mover, "explode");
    if (!py::callable(explode_effect)) return false;
    if (!explosion.effects || !explosion.material || !p::flag(explosion.effects.get()) || !p::flag(explosion.material.get())) return false;
    // An artillery-strike descriptor makes ProjectileMover.explode return before it plays anything.
    auto get = py::optional(explosion.effects.get(), "get");
    if (!py::callable(get) || py::invoke(get.get(), p::text("artilleryID").get()).get() != Py_None) return false;
    const auto velocity = finitePoint(explosion.direction.get());
    if (!velocity) return false;
    try {
        auto direction = vector3(b, *velocity);
        if (p::scalar(py::attr(direction.get(), "length").get()) <= 0.) return false;
        py::call(direction.get(), "normalise");
        p::Ref material(PyObject_Str(explosion.material.get()));
        py::invoke(explode_effect.get(), smallInteger(shot_id).get(), explosion.effects.get(), material.get(), end, direction.get());
    } catch (const std::exception& e) {
        t.explosions_enabled = false;
        reportFailure(t, "terminal explosion exception", describe(e));
        return false;
    }
    return true;
}
}

bool admit(Battle& b, std::int64_t attacker_id, const std::string& projectile_id, double now) {
    auto& t = b.projectiles->tracers;
    if (t.closed || !t.launches_enabled || attacker_id <= 0) return false;
    if (projectile_id.empty() || projectile_id.size() > max_projectile_id) return false;
    const auto decided = t.admissions.find(projectile_id);
    if (decided != t.admissions.end()) return decided->second.first == attacker_id && decided->second.second;
    const double moment = std::isfinite(now) ? now : b.clock();
    double tokens = burst_capacity;
    const auto budget = t.budgets.find(attacker_id);
    if (budget != t.budgets.end())
        tokens = std::min(burst_capacity, budget->second.first + std::max(0., moment - budget->second.second) * refill_per_second);
    bool admitted = tokens >= 1. - 1e-9;
    if (admitted && t.mover) admitted = capacity(t.mover.get(), attacker_id);
    if (admitted) tokens = std::max(0., tokens - 1.);
    t.budgets[attacker_id] = {tokens, moment};
    t.admissions[projectile_id] = {attacker_id, admitted};
    return admitted;
}

bool play(Battle& b, PyObject* descriptor, std::int64_t shell_index, const Point& reference_origin, const Point& reference_velocity,
          double gravity, double max_distance, std::int64_t attacker_id, const std::string& projectile_id, bool is_ricochet,
          const std::optional<Point>& visual_start) {
    auto& t = b.projectiles->tracers;
    if (t.closed || !t.launches_enabled) return false;
    auto shot = shotAt(descriptor, shell_index);
    auto shell = component(shot.get(), "shell");
    auto effects_index = component(shell.get(), "effectsIndex");
    const Point start = visual_start ? *visual_start : reference_origin;
    if (shot.get() == Py_None || shell.get() == Py_None || effects_index.get() == Py_None || !finitePoint(reference_origin) ||
        !finitePoint(reference_velocity) || !finitePoint(start) || !std::isfinite(gravity) || !std::isfinite(max_distance) ||
        gravity < 0. || max_distance <= 0. || attacker_id <= 0 || projectile_id.empty() || projectile_id.size() > max_projectile_id)
        return false;
    const auto existing = t.shots.find(projectile_id);
    if (existing != t.shots.end()) return existing->second.started && !existing->second.terminal_failed;
    try {
        auto cache = py::attr(b.engine.vehicles.get(), "g_cache");
        p::Ref effects(PyObject_GetItem(py::attr(cache.get(), "shotEffects").get(), effects_index.get()));
        if (effects.get() == Py_None) return false;
        if (py::call(effects.get(), "get", p::text("artilleryID").get()).get() != Py_None) {
            // PySalvo has no reviewed per-shot terminal boundary; the standard catalog uses the ordinary tracer.
            t.admissions.erase(projectile_id);
            reportFailure(t, "canonical combat-equipment artillery rejected");
            return false;
        }
        if (!admit(b, attacker_id, projectile_id, b.clock())) return false;
        PyObject* mover = projectileMover(b);
        if (!mover || !capacity(mover, attacker_id)) return false;
        const auto visual_id = takeUnusedShotId(t, mover);
        auto player = py::call(b.engine.bigworld.get(), "player");
        auto camera = py::call(b.engine.bigworld.get(), "camera");
        const auto camera_position = finitePoint(py::attr(camera.get(), "position").get());
        if (!camera_position) return false;
        ProjectileTracer entry;
        entry.visual_id = visual_id;
        entry.effects = borrow(effects.get());
        entry.attacker_id = attacker_id;
        entry.own_shot = attacker_id == p::wide(py::attr(player.get(), "playerVehicleID").get());
        // Native addProjectile may re-enter Python: publish the identity first and keep it if creation fails part-way.
        t.shots[projectile_id] = std::move(entry);
        auto visual = smallInteger(visual_id);
        try {
            py::call(mover, "add", visual.get(), effects.get(), p::number(gravity).get(), vector3(b, reference_origin).get(),
                     vector3(b, reference_velocity).get(), vector3(b, start).get(), p::number(max_distance).get(),
                     smallInteger(attacker_id).get(), vector3(b, *camera_position).get());
        } catch (const std::exception&) {
            t.launches_enabled = false;
            throw;
        }
        p::Ref native(nativeProjectiles(mover));
        PyObject* created = PyDict_GetItem(native.get(), visual.get());
        if (!created) {
            // Stock add returns before model creation when the native simulator refuses a projectile.
            t.shots.erase(projectile_id);
            reportFailure(t, "native add rejected");
            return false;
        }
        // Native range expiry is cosmetic, not an authoritative miss; the terminal owns input/flock and miss feedback.
        if (PyObject_SetItem(created, p::text("fireMissedTrigger").get(), Py_False) < 0 ||
            PyObject_SetItem(created, p::text("_offlineLANCanonical").get(), Py_True) < 0)
            throw p::Error("ProjectileMover entry");
        t.shots[projectile_id].started = true;
        if (is_ricochet) py::call(mover, "hold", visual.get());
        return true;
    } catch (const std::exception& e) {
        reportFailure(t, "native tracer creation", describe(e));
        return false;
    }
}

bool stop(Battle& b, const std::string& projectile_id, const Point& end_point, const std::optional<Explosion>& explosion, bool missed) {
    auto& t = b.projectiles->tracers;
    if (t.closed || !finitePoint(end_point)) return false;
    auto end = vector3(b, end_point);
    const auto found = t.shots.find(projectile_id);
    if (found == t.shots.end()) {
        const auto admission = t.admissions.find(projectile_id);
        if (admission == t.admissions.end() || !admission->second.second || !explosion) return false;
        t.admissions.erase(admission);
        PyObject* mover = projectileMover(b);
        return mover && explode(b, t, mover, takeUnusedShotId(t, mover), end.get(), *explosion);
    }
    auto& shot = found->second;
    // Partial stock creation has no safe per-shot recovery; reset retires its owner.
    if (!shot.started || !t.mover) return false;
    PyObject* mover = t.mover.get();
    try {
        if (!shot.terminal) {
            p::Ref native(nativeProjectiles(mover));
            // A failed material effect still retires an active tracer at the authoritative endpoint.
            const bool exploded = explosion && explode(b, t, mover, shot.visual_id, end.get(), *explosion);
            if (!exploded && contains(native.get(), shot.visual_id)) py::call(mover, "hide", smallInteger(shot.visual_id).get(), end.get());
            shot.terminal = true;
        }
        if (missed) notifyMissed(t, shot);
        notifyHit(b, t, shot, end.get());
    } catch (const std::exception& e) {
        // A ricochet may reuse the logical id; a motor that could not retire is never reported as the new segment.
        shot.terminal_failed = true;
        reportFailure(t, "native tracer terminal", describe(e));
        return false;
    }
    t.shots.erase(found);
    t.admissions.erase(projectile_id);
    return true;
}

bool reset(Battle& b) {
    auto& t = b.projectiles->tracers;
    if (t.mover) {
        try { py::call(t.mover.get(), "destroy"); }
        catch (const std::exception& e) {
            t.launches_enabled = false;
            reportFailure(t, "projectile mover reset", describe(e));
            return false;
        }
        releaseMover(t);
    }
    t.shots.clear();
    t.budgets.clear();
    t.admissions.clear();
    return true;
}

void destroy(Battle& b) {
    auto& t = b.projectiles->tracers;
    t.closed = true;
    if (t.mover) {
        py::call(t.mover.get(), "destroy");
        releaseMover(t);
    }
    reset(b);
}
}
}
