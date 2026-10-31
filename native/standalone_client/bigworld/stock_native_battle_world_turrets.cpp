// The visible client's detached-turret presentation over the native world's accepted rows: the client-created
// stock DetachedTurret entities, their frozen-arc or body-buffer matrices, landing effects and obstacle geometry.
#if !defined(WOT_NATIVE_BATTLE_HOST_BUILD)
#include "pch.hpp"
#include "pyscript/script_math.hpp"
#endif
#include "stock_native_battle_world_parts.hpp"
#include "stock_native_battle_world_imports.hpp"
#include "stock_native_client_turret_presentation.hpp"
#include "stock_native_client_query.hpp"
#include <algorithm>
#include <cmath>

namespace native_battle {
namespace present = stock_native_client_turret_presentation;
namespace detached = stock_native_detached_turret;
namespace turret = stock_native_bot_turret;

struct TurretPresentation {
    // One detached turret owns a turret plus gun compound for the rest of the round; the 32-bit client bounds how
    // many are resident. The server applies the same accepted-record cap to the whole room.
    static constexpr std::size_t max_active = 12;
    static constexpr double create_retry_seconds = 1., model_timeout_seconds = 30., visual_retry_seconds = .25;
    struct Entry {
        std::int64_t id = 0, vehicle = 0, motion_sequence = 0, presented_impact = 0;
        std::string key;               // accepted actor key, e.g. "bot:7"
        detached::Row row;
        present::Buffer buffer;
        Object entity, matrix;
        double started = 0., created = 0.;
        bool settled = false, impacted = false, failed = false;
        explicit Entry(present::Tuning t) : buffer(t) {}
    };
    struct Retiring {
        std::int64_t id = 0, vehicle = 0;
        std::string key;
        Object entity;
    };
    struct Attempt {
        detached::Row row;
        double next_retry = 0.;
    };
    struct Accepted {
        detached::Row row;
        bool geometry = false, failed = false;
        std::shared_ptr<const void> attempted_geometry;
        std::int64_t visual_revision = 0;
    };
    // turret_detachment.GRAVITY / FLIGHT_STEP_SECONDS and the snapshot interpolation delay law.
    present::Tuning tuning{9.81, .04, .06, .09, .005};
    std::vector<std::shared_ptr<Entry>> entries;
    std::vector<Retiring> retiring;
    std::map<std::string, Attempt> attempts;
    std::map<std::string, Accepted> accepted;
    std::vector<std::string> accepted_order;
    std::uint64_t generation = 0;
    bool closed = false, spg = false;
    std::string strategic, arty;
};

namespace world {
namespace {
using Entry = TurretPresentation::Entry;

p::Ref field(PyObject* o, const char* n) {
    if (!o || o == Py_None) return p::retain(Py_None);
    if (PyDict_Check(o)) return p::value(o, n);
    auto name = p::text(n);
    auto* v = p::directAttribute(o, name.get());
    if (!v && PyErr_ExceptionMatches(PyExc_AttributeError)) { PyErr_Clear(); return p::retain(Py_None); }
    return p::Ref(v);
}
void put(PyObject* o, const char* n, PyObject* v) {
    auto name = p::text(n);
    if (PyObject_SetAttr(o, name.get(), v) < 0) throw p::Error(std::string("detached turret attribute ") + n + " is not writable");
}
p::Ref builtin(PyObject* o, const char* n, PyObject* args) {
    auto method = field(o, n);
    if (!PyCFunction_Check(method.get())) throw p::Error("native turret presentation requires the reviewed builtin");
    return p::Ref(PyObject_CallObject(method.get(), args));
}
Point xyz(PyObject* v) {
    if (PyList_Check(v) || PyTuple_Check(v)) return p::numbers<3>(v);
    return {p::numeric(v, "x"), p::numeric(v, "y"), p::numeric(v, "z")};
}
p::Ref vector(Battle& b, const Point& v) {
    auto args = p::tupleNumbers(v);
    return builtin(b.engine.math.get(), "Vector3", args.get());
}
// A per-turret failure is local to that turret; the warning stays enabled for later rows.
void note(Battle& b, const std::string& what, const std::exception& error) { b.warn(what, describe(error), false); }

TurretPresentation& presentation(Battle& b) {
    auto& w = *b.world;
    if (!w.presentation) w.presentation = std::make_shared<TurretPresentation>();
    return *w.presentation;
}
bool resident(const TurretPresentation& t, std::int64_t vehicle) {
    for (const auto& e : t.entries) if (e->vehicle == vehicle) return true;
    for (const auto& r : t.retiring) if (r.vehicle == vehicle) return true;
    return false;
}
// The source identity check before adopting a never-observed entity id.
bool matches(PyObject* entity, std::int64_t vehicle) {
    auto direct = field(entity, "vehicleID");
    auto properties = field(entity, "properties");
    auto id = direct.get() != Py_None ? p::retain(direct.get())
                                       : PyDict_Check(properties.get()) ? field(properties.get(), "vehicleID") : p::retain(Py_None);
    if (id.get() == Py_None) return false;
    PyObject* number = PyNumber_Int(id.get());
    if (!number) { PyErr_Clear(); return false; }
    p::Ref held(number);
    return p::wide(number) == vehicle;
}
bool destroyEntity(Battle& b, std::int64_t id) {
    try {
        auto value = p::integer(id);
        p::Ref args(PyTuple_Pack(1, value.get()));
        builtin(b.engine.bigworld.get(), "destroyEntity", args.get());
        return true;
    } catch (const std::exception& error) {
        note(b, "detached turret teardown failed", error);
        return false;
    }
}
// Retires engine-owned ids only; a pending creation stays a tombstone until its entity exists.
std::size_t retirePoll(Battle& b, TurretPresentation& t) {
    std::size_t retired = 0;
    for (auto at = t.retiring.begin(); at != t.retiring.end();) {
        auto entity = b.entity(at->id);
        if (entity.get() == Py_None && !at->entity) { ++at; continue; }
        if (entity.get() != Py_None && (!at->entity || at->entity.get() == entity.get())) {
            if (!matches(entity.get(), at->vehicle)) { at = t.retiring.erase(at); continue; }
            at->entity = borrow(entity.get());
            if (!destroyEntity(b, at->id)) { ++at; continue; }
            ++retired;
        }
        at = t.retiring.erase(at);
    }
    return retired;
}
void retire(TurretPresentation& t, const std::shared_ptr<Entry>& e) {
    t.retiring.push_back({e->id, e->vehicle, e->key, e->entity});
    t.entries.erase(std::remove(t.entries.begin(), t.entries.end(), e), t.entries.end());
}
void failedLoad(Battle& b, TurretPresentation& t, const std::shared_ptr<Entry>& e, double now) {
    retire(t, e);
    const auto attempt = t.attempts.find(e->key);
    if (attempt != t.attempts.end()) attempt->second.next_retry = now + TurretPresentation::create_retry_seconds;
    retirePoll(b, t);
    note(b, "canonical turret model load timed out",
         p::Error("vehicle " + std::to_string(e->vehicle) + " entity " + std::to_string(e->id)));
}
bool exploded(PyObject* component) {
    auto models = field(component, "models");
    return field(models.get(), "exploded").get() != Py_None;
}
bool sameRow(const detached::Row& a, const detached::Row& b) {
    return a.motion_sequence == b.motion_sequence && a.created_time_ms == b.created_time_ms && a.motion_time_ms == b.motion_time_ms;
}

// A visible started source with both exploded models; the accepted flight is never recomputed from this client.
struct Plan {
    std::int64_t entity_id = 0, space_id = 0;
    Object compact_descr;
};
std::optional<Plan> prepareCanonical(Battle& b, const TurretPresentation& t, PyObject* entity) {
    if (t.closed || !p::flag(field(entity, "isStarted").get())) return std::nullopt;
    auto descriptor = field(entity, "typeDescriptor");
    if (!exploded(field(descriptor.get(), "turret").get()) || !exploded(field(descriptor.get(), "gun").get())) return std::nullopt;
    try {
        Plan plan;
        plan.entity_id = p::wide(py::attr(entity, "id").get());
        plan.compact_descr = own(py::call(descriptor.get(), "makeCompactDescr"));
        plan.space_id = p::wide(py::attr(b.engine.avatar.get(), "spaceID").get());
        return plan;
    } catch (const std::exception& error) {
        note(b, "canonical turret descriptor unavailable", error);
        return std::nullopt;
    }
}

bool launchFrozen(Battle& b, TurretPresentation& t, const Plan& plan, const detached::Row& row, double started, double now,
                  const std::string& key) {
    const auto pose = present::poseAt(row, std::max(0., now - started), t.tuning);
    std::optional<std::int64_t> entity_id;
    try {
        auto properties = p::dict();
        p::set(properties.get(), "vehicleID", p::integer(plan.entity_id));
        p::set(properties.get(), "vehicleCompDescr", p::retain(plan.compact_descr.get()));
        p::set(properties.get(), "isUnderWater", p::boolean(false));
        p::set(properties.get(), "isCollidingWithWorld", p::boolean(false));
        auto name = p::text("DetachedTurret");
        auto space = p::integer(plan.space_id), zero = p::integer(0);
        auto direction = p::tupleNumbers(std::array<double, 3>{pose.attitude[2], pose.attitude[1], pose.attitude[0]});
        auto position = vector(b, pose.position);
        p::Ref args(PyTuple_Pack(6, name.get(), space.get(), zero.get(), position.get(), direction.get(), properties.get()));
        auto created = p::call(b.engine.bigworld.get(), "createEntity", args.get());
        if (created.get() == Py_None) throw p::Error("createEntity returned no DetachedTurret");
        entity_id = p::wide(created.get());
        auto e = std::make_shared<Entry>(t.tuning);
        e->id = *entity_id;
        e->vehicle = plan.entity_id;
        e->key = key;
        e->row = row;
        e->started = started;
        e->created = now;
        t.entries.push_back(std::move(e));
        log("TURRET vehicle=" + std::to_string(plan.entity_id) + " entity=" + std::to_string(*entity_id) + " state=created");
        return true;
    } catch (const std::exception& error) {
        // An allocated id may still be loading prerequisites; retain it for safe retirement.
        if (entity_id) {
            t.retiring.push_back({*entity_id, plan.entity_id, key, Object()});
            retirePoll(b, t);
        }
        note(b, "detached turret creation failed", error);
        return false;
    }
}

// One accepted flight, including late AOI admission: a late admission starts at the current flight/rest pose.
bool launchCanonical(Battle& b, TurretPresentation& t, const Plan& plan, const std::string& key, const detached::Row& row,
                     double now, double elapsed) {
    if (t.closed) return false;
    auto attempt = t.attempts.find(key);
    if (attempt == t.attempts.end()) {
        if (t.attempts.size() >= TurretPresentation::max_active) return false;
        attempt = t.attempts.emplace(key, TurretPresentation::Attempt{row, now}).first;
    } else if (!sameRow(attempt->second.row, row)) {
        if (row.motion_sequence <= attempt->second.row.motion_sequence) return false;
        attempt->second.row = row;
    }
    const double started = now - std::max(0., elapsed);
    for (const auto& e : t.entries) {
        if (e->key != key) continue;
        if (row.motion_sequence > e->motion_sequence) {
            const auto serial = [](const detached::Row& r) { return r.flight.body ? r.flight.body->impact_serial : 0; };
            const bool new_impact = serial(row) > serial(e->row);
            e->row = row;
            e->started = started;
            e->settled = false;
            e->impacted = e->impacted && !new_impact;
            e->motion_sequence = row.motion_sequence;
            e->failed = false;
            e->buffer.push(row, now, elapsed);
        }
        return true;
    }
    retirePoll(b, t);
    bool retiring_key = false;
    for (const auto& r : t.retiring) retiring_key = retiring_key || r.key == key;
    if (resident(t, plan.entity_id) || retiring_key || now < attempt->second.next_retry) return false;
    attempt->second.next_retry = now + TurretPresentation::create_retry_seconds;
    if (!launchFrozen(b, t, plan, row, started, now, key)) return false;
    auto& e = *t.entries.back();
    e.motion_sequence = row.motion_sequence;
    e.buffer.push(row, now, elapsed);
    return true;
}

// Replaces the native filter matrix with the frozen arc's provider and gates stock collision off the debris.
bool bind(Battle& b, PyObject* entity, Entry& e) {
    try {
        p::Ref none(PyTuple_New(0));
        auto matrix = builtin(b.engine.math.get(), "Matrix", none.get());
        auto model = py::attr(entity, "model");
        put(model.get(), "matrix", matrix.get());
        auto caps = p::list();
        put(entity, "targetCaps", caps.get());
        e.matrix = own(std::move(matrix));
    } catch (const std::exception& error) {
        note(b, "detached turret binding failed", error);
        return false;
    }
    try {
        put(entity, "_offlineNativeRemote", Py_True);
        put(entity, "_offlineNativeDrawVisible", Py_False);
    } catch (const std::exception& error) {
        // A turret that flies but can still catch the aim marker is a smaller fault than no turret.
        note(b, "detached turret collision gate failed", error);
    }
    return true;
}

void writeMatrix(Battle& b, const Entry& e, const present::Pose& pose) {
#if defined(WOT_NATIVE_BATTLE_HOST_BUILD)
    auto angles = p::tupleNumbers(pose.attitude);
    p::Ref args(PyTuple_Pack(1, angles.get()));
    builtin(e.matrix.get(), "setRotateYPR", args.get());
    put(e.matrix.get(), "translation", vector(b, pose.position).get());
#else
    (void)b;
    if (!BW::PyMatrix::Check(e.matrix.get())) throw p::Error("native turret provider is not a writable Matrix");
    auto* matrix = static_cast<BW::PyMatrix*>(e.matrix.get());
    matrix->setRotate(float(pose.attitude[0]), float(pose.attitude[1]), float(pose.attitude[2]));
    matrix->translation(BW::Vector3(float(pose.position[0]), float(pose.position[1]), float(pose.position[2])));
#endif
}

// Hands one landing to stock's own touchdown effect (DetachedTurret.onStaticCollision).
void reportImpact(Battle& b, PyObject* entity, double energy, const Point& contact, const Point& normal) {
    auto collision = py::optional(entity, "onStaticCollision");
    if (!py::callable(collision)) return;
    try {
        auto e = p::number(energy);
        auto c = vector(b, contact), n = vector(b, normal);
        py::invoke(collision.get(), e.get(), c.get(), n.get());
    } catch (const std::exception& error) {
        note(b, "detached turret impact effect failed", error);
    }
}

std::size_t advance(Battle& b, TurretPresentation& t, double now) {
    retirePoll(b, t);
    if (t.closed) return 0;
    std::size_t written = 0;
    const auto active = t.entries;
    for (const auto& e : active) {
        try {
            auto entity = b.entity(e->id);
            if (entity.get() == Py_None) {
                // createEntity returns an id before prerequisites and onEnterWorld publish the entity.
                if (e->entity) t.entries.erase(std::remove(t.entries.begin(), t.entries.end(), e), t.entries.end());
                else if (now - e->created >= TurretPresentation::model_timeout_seconds) failedLoad(b, t, e, now);
                continue;
            }
            if ((e->entity && e->entity.get() != entity.get()) || !matches(entity.get(), e->vehicle)) {
                t.entries.erase(std::remove(t.entries.begin(), t.entries.end(), e), t.entries.end());
                continue;
            }
            if (!e->entity) e->entity = borrow(entity.get());
            if (field(entity.get(), "model").get() == Py_None) {
                if (now - e->created >= TurretPresentation::model_timeout_seconds) failedLoad(b, t, e, now);
                continue;
            }
            if (e->settled) continue;
            if (!e->matrix) {
                if (!bind(b, entity.get(), *e)) {
                    retire(t, e);
                    retirePoll(b, t);
                    continue;
                }
                log("TURRET vehicle=" + std::to_string(e->vehicle) + " entity=" + std::to_string(e->id) +
                    " state=bound elapsed_ms=" + std::to_string(std::max<std::int64_t>(0, std::int64_t((now - e->started) * 1000.))));
            }
            const double elapsed = std::max(0., now - e->started);
            const auto buffered = e->buffer.pose(now);
            const auto pose = buffered ? *buffered : present::poseAt(e->row, elapsed, t.tuning);
            writeMatrix(b, *e, pose);
            ++written;
            const bool landed = pose.body ? bool(pose.body->impact) : elapsed >= e->row.flight.duration;
            if (pose.body) e->settled = pose.body->sleeping && (!buffered || *e->buffer.cursor() >= e->buffer.samples().back().time);
            else if (landed) e->settled = true;
            if (!landed) continue;
            if (pose.body && pose.body->impact_serial > e->presented_impact) {
                e->presented_impact = pose.body->impact_serial;
                const auto& impact = *pose.body->impact;
                reportImpact(b, entity.get(), impact.energy, impact.point, impact.normal);
            } else if (!pose.body && !e->impacted) {
                e->impacted = true;
                // No surface inside the search window: the arc has a terminal pose but no contact to report.
                if (e->row.flight.landed && e->row.flight.contact)
                    reportImpact(b, entity.get(), e->row.flight.energy, *e->row.flight.contact, Point{0., 1., 0.});
            }
        } catch (const std::exception& error) {
            PyErr_Clear();
            if (!e->failed) { e->failed = true; note(b, "detached turret pose write failed", error); }
        }
    }
    return written;
}

// The descriptor turret and gun components below the detached root (turret_obstacles.turret_components).
turret::DetachedDescriptor detachedDescriptor(PyObject* descriptor) {
    turret::DetachedDescriptor out;
    auto turret_part = py::attr(descriptor, "turret"), gun = py::attr(descriptor, "gun");
    const auto gun_position = xyz(py::attr(turret_part.get(), "gunPosition").get());
    auto component = [](PyObject* part, const Point& offset) {
        auto tester = py::attr(part, "hitTester");
        if (!py::callable(py::optional(tester.get(), "localHitTest"))) throw p::Error("detached turret hit tester is unavailable");
        auto bbox = py::attr(tester.get(), "bbox");
        turret::Component value;
        value.bounds.minimum = xyz(p::item(bbox.get(), 0).get());
        value.bounds.maximum = xyz(p::item(bbox.get(), 1).get());
        for (unsigned axis = 0; axis < 3; ++axis)
            if (!(value.bounds.minimum[axis] < value.bounds.maximum[axis])) throw p::Error("detached turret component bounds are empty");
        value.offset = offset;
        return value;
    };
    out.turret = component(turret_part.get(), Point{0., 0., 0.});
    out.gun = component(gun.get(), gun_position);
    return out;
}

void attach(Battle& b, TurretPresentation& t) {
    if (t.generation == b.generation) return;
    t.generation = b.generation;
    t.accepted.clear();
    t.accepted_order.clear();
    const Actor* local = b.localVehicle();
    auto entity = local ? b.entity(local->engine_id) : p::retain(Py_None);
    auto descriptor = field(entity.get(), "typeDescriptor"), type = field(descriptor.get(), "type"), tags = field(type.get(), "tags");
    auto tag = p::text("SPG");
    t.spg = tags.get() != Py_None && PySequence_Contains(tags.get(), tag.get()) == 1;
    auto input = field(b.engine.modules.get(), "avatar_input_handler"), modes = field(input.get(), "_CTRL_MODE");
    t.strategic = p::string(field(modes.get(), "STRATEGIC").get());
    t.arty = p::string(field(modes.get(), "ARTY").get());
}

// Accepted rows in acceptance order; an unchanged row keeps its presentation progress.
void ingest(TurretPresentation& t, const DetachedTurrets& turrets) {
    std::vector<std::string> order;
    for (const auto& key : turrets.order) {
        const auto row = turrets.rows.find(key);
        if (row == turrets.rows.end()) continue;
        order.push_back(key);
        const auto found = t.accepted.find(key);
        if (found != t.accepted.end() && sameRow(found->second.row, row->second)) continue;
        TurretPresentation::Accepted fresh;
        fresh.row = row->second;
        if (found != t.accepted.end()) fresh.visual_revision = found->second.visual_revision;
        t.accepted[key] = std::move(fresh);
    }
    for (auto at = t.accepted.begin(); at != t.accepted.end();)
        if (std::find(order.begin(), order.end(), at->first) == order.end()) at = t.accepted.erase(at);
        else ++at;
    t.accepted_order = std::move(order);
}

void schedule(Battle& b, World& w, TurretPresentation& t, double now, double server) {
    if (t.closed) return;
    const Actor* local = b.localVehicle();
    if (!local || !local->has_current) return;
    Point origin{local->current[0], local->current[1], local->current[2]};
    if (const auto spectated = visibility::spectatedEngineId(b)) {
        for (const auto& actor : b.vehicles->actors)
            if (actor.second->engine_id == *spectated && actor.second->ready) {
                const auto& pose = actor.second->current;
                origin = actor.second->local ? Point{pose[0], pose[1], pose[2]} : Point{float(pose[0]), float(pose[1]), float(pose[2])};
                break;
            }
    }
    bool spg_view = false;
    if (t.spg) {
        auto handler = field(b.engine.avatar.get(), "inputHandler"), mode = field(handler.get(), "_AvatarInputHandler__ctrlModeName");
        const auto name = p::string(mode.get());
        spg_view = name == t.strategic || name == t.arty;
    }
    const double aoi = visibility::vehicleAoiRadius();
    const auto order = t.accepted_order;
    for (const auto& key : order) {
        if (t.closed) break;
        const auto found = t.accepted.find(key);
        if (found == t.accepted.end()) continue;
        auto& record = found->second;
        auto* actor = b.vehicles->find(key);
        if (!actor || !actor->ready || actor->tombstone || actor->missing) continue;
        auto entity = b.entity(actor->engine_id);
        if (entity.get() == Py_None) continue;
        // Visual-only debris owns no ray, hull, navigation or contact geometry.
        if (!record.geometry && !record.row.flight.body) { w.turrets.geometry.insert(key); record.geometry = true; }
        if (!record.geometry && actor->geometry && record.attempted_geometry != actor->geometry) {
            record.attempted_geometry = actor->geometry;
            if (auto* descriptor = stock_native_client_query::descriptor(*actor)) {
                try {
                    if (w.turrets.obstacles.put(detached::obstacleRecord(record.row), detachedDescriptor(descriptor))) {
                        w.turrets.geometry.insert(key);
                        record.geometry = true;
                    }
                } catch (const std::exception& error) {
                    PyErr_Clear();
                    if (!record.failed) { record.failed = true; note(b, "detached turret obstacle geometry", error); }
                }
            }
        }
        if (!p::flag(py::optional(entity.get(), "isTurretDetached").get()) ||
            (!spg_view && std::hypot(origin[0] - record.row.flight.rest[0], origin[2] - record.row.flight.rest[2]) > aoi))
            continue;
        const bool present_now = resident(t, actor->engine_id);
        if (present_now && record.visual_revision >= record.row.motion_sequence) continue;
        const std::string retry_key = "visual:" + key;
        const auto due = w.turrets.visual_retry.find(retry_key);
        if (!present_now && due != w.turrets.visual_retry.end() && now < due->second) continue;
        w.turrets.visual_retry[retry_key] = now + TurretPresentation::visual_retry_seconds;
        const auto plan = prepareCanonical(b, t, entity.get());
        if (!plan) continue;
        const double age = std::max(0., (server - double(record.row.created_time_ms)) / 1000.);
        if (launchCanonical(b, t, *plan, key, record.row, now, age)) {
            record.visual_revision = record.row.motion_sequence;
            w.turrets.visual_revisions[key] = record.visual_revision;
        }
    }
}
}

void presentTurretRows(Battle& b) {
    if (b.worker) return;
    auto& t = presentation(b);
    attach(b, t);
    ingest(t, b.world->turrets);
}

void advanceTurretPresentation(Battle& b, double now) {
    if (b.worker) return;
    auto& t = presentation(b);
    if (t.generation != b.generation) { attach(b, t); ingest(t, b.world->turrets); }
    const auto server = projectiles::serverTimeMs(b, now);
    if (!server) return;
    schedule(b, *b.world, t, now, std::max(0., std::trunc(*server)));
    advance(b, t, now);
}

void retireTurretPresentation(Battle& b) {
    auto& w = *b.world;
    if (!w.presentation) return;
    // Close presentation and retire only engine-owned turret entities; a pending creation stays a tombstone for the
    // teardown poll before the battle space retires.
    auto& t = *w.presentation;
    t.closed = true;
    for (const auto& e : t.entries) t.retiring.push_back({e->id, e->vehicle, e->key, e->entity});
    t.entries.clear();
    try {
        retirePoll(b, t);
    } catch (const std::exception&) {
        PyErr_Clear();
    }
}
}
}
