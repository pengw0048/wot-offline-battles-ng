// The visible client's detached-turret presentation over the native world's accepted rows. The stock entity
// manager (DetachedTurretPresentation) still creates the turret entities; this owner drives their matrices.
#if !defined(WOT_NATIVE_BATTLE_HOST_BUILD)
#include "pch.hpp"
#include "pyscript/script_math.hpp"
#endif
#include "stock_native_battle_world_parts.hpp"
#include "stock_native_battle_world_imports.hpp"
#include "stock_native_client_turret_presentation.hpp"
#include "stock_native_detached_turret_snapshot.hpp"
#include "stock_native_client_query.hpp"
#include <algorithm>
#include <cmath>

namespace native_battle {
namespace world {
namespace {
namespace present = stock_native_client_turret_presentation;
namespace detached = stock_native_detached_turret;
using present::Row;
using present::Frame;
using present::Tuning;
const char* capsule_name = "wot.native.battle.turret.presentation";

p::Ref field(PyObject* o, const char* n) {
    if (!o || o == Py_None) return p::retain(Py_None);
    if (PyDict_Check(o)) return p::value(o, n);
    auto name = p::text(n);
    auto* v = p::directAttribute(o, name.get());
    if (!v && PyErr_ExceptionMatches(PyExc_AttributeError)) { PyErr_Clear(); return p::retain(Py_None); }
    return p::Ref(v);
}
void put(PyObject* o, const char* n, const p::Ref& v) {
    if (PyDict_Check(o)) { p::set(o, n, v); return; }
    auto name = p::text(n);
    if (PyObject_GenericSetAttr(o, name.get(), v.get()) < 0) throw p::Error("native turret presentation mirror failed");
}
p::Ref builtin(PyObject* o, const char* n, PyObject* args) {
    auto method = field(o, n);
    if (!PyCFunction_Check(method.get())) throw p::Error("native turret presentation requires the reviewed builtin");
    return p::Ref(PyObject_CallObject(method.get(), args));
}
p::Ref call(PyObject* object, const char* method, PyObject* args) { return p::call(object, method, args); }
void removeIdentity(PyObject* list, PyObject* value) {
    if (!PyList_Check(list)) throw p::Error("native turret presentation list is invalid");
    for (Py_ssize_t i = 0; i < PyList_GET_SIZE(list); ++i)
        if (PyList_GET_ITEM(list, i) == value) {
            if (PySequence_DelItem(list, i) < 0) throw p::Error("native turret presentation retirement failed");
            return;
        }
}
Point point(PyObject* v) {
    if (PyList_Check(v) || PyTuple_Check(v)) return p::numbers<3>(v);
    return {p::numeric(v, "x"), p::numeric(v, "y"), p::numeric(v, "z")};
}
Point optionalPoint(PyObject* o, const char* n) {
    auto value = field(o, n);
    return value.get() == Py_None ? Point{} : point(value.get());
}
Frame frame(PyObject* raw) {
    Frame out;
    out.position = point(field(raw, "position").get());
    out.attitude = point(field(raw, "attitude").get());
    out.centre = optionalPoint(raw, "centre");
    out.velocity = optionalPoint(raw, "velocity");
    out.angular_velocity = optionalPoint(raw, "angular_velocity");
    out.grounded = p::truth(raw, "grounded");
    out.sleeping = p::truth(raw, "sleeping");
    out.impact_serial = p::integral(raw, "impact_serial");
    auto impact = field(raw, "impact");
    if (impact.get() != Py_None) {
        detached::Impact value;
        value.point = point(field(impact.get(), "point").get());
        value.normal = point(field(impact.get(), "normal").get());
        value.velocity = point(field(impact.get(), "velocity").get());
        value.energy = p::numeric(impact.get(), "energy");
        out.impact = value;
    }
    return out;
}
// The visual row a locally adopted (legacy) turret carries in its raw dictionary.
Row visualRow(PyObject* raw) {
    Row out;
    out.attitude = point(field(raw, "attitude").get());
    out.spin = point(field(raw, "spin").get());
    auto source = field(raw, "flight");
    auto& f = out.flight;
    f.origin = optionalPoint(source.get(), "origin");
    f.velocity = optionalPoint(source.get(), "velocity");
    f.rest = point(field(source.get(), "rest").get());
    f.duration = p::numeric(source.get(), "duration");
    f.energy = p::numeric(source.get(), "energy");
    f.landed = p::truth(source.get(), "landed");
    auto contact = field(source.get(), "contact"), rest = field(source.get(), "rest_attitude"), body = field(source.get(), "body"),
         segments = field(source.get(), "segments");
    if (contact.get() != Py_None) f.contact = point(contact.get());
    if (rest.get() != Py_None) f.rest_attitude = point(rest.get());
    if (body.get() != Py_None) f.body = frame(body.get());
    for (Py_ssize_t i = 0; i < p::size(segments.get()); ++i) {
        auto value = p::item(segments.get(), i);
        f.segments.push_back({point(field(value.get(), "origin").get()), point(field(value.get(), "velocity").get()),
                              p::numeric(value.get(), "start"), p::numeric(value.get(), "duration")});
    }
    return out;
}

struct Entry {
    p::Ref raw;
    std::unique_ptr<p::Ref> entity, matrix;
    Row row;
    present::Buffer buffer;
    std::int64_t id = 0, vehicle = 0, presented_impact = 0;
    double started = 0., created = 0.;
    bool canonical = false, settled = false, impacted = false, failed = false;
    Entry(PyObject* r, Tuning t) : raw(p::retain(r)), buffer(t) {}
};
// One accepted row: the raw dictionary the stock manager consumes and its presentation progress.
struct Accepted {
    p::Ref raw;
    Row row;
    bool geometry = false, failed = false;
    std::shared_ptr<const void> attempted_geometry;
    std::int64_t visual_revision = 0;
    Accepted(PyObject* r, Row value) : raw(p::retain(r)), row(std::move(value)) {}
};

struct Owner {
    p::Ref weak_manager, bigworld, math;
    std::uint64_t generation = 0;
    Tuning tuning;
    std::vector<std::shared_ptr<Entry>> entries;
    std::map<std::string, std::shared_ptr<Accepted>> accepted;
    std::vector<std::string> accepted_order;
    double timeout = 0., aoi = 0.;
    bool closed = false, spg = false;
    std::string strategic, arty;

    explicit Owner(PyObject* m)
        : weak_manager(PyWeakref_NewRef(m, nullptr)), bigworld(field(m, "_bigworld")), math(field(m, "_math")) {
        p::Ref flight(PyImport_ImportModule("gui.mods.offline_lan_0922.turret_detachment")),
            sync(PyImport_ImportModule("gui.mods.offline_lan_0922.snapshot_sync")),
            presenter(PyImport_ImportModule("gui.mods.offline_lan_0922.entities.detached_turret")),
            spotting(PyImport_ImportModule("gui.mods.offline_lan_0922.spotting"));
        tuning = {p::numeric(flight.get(), "GRAVITY"), p::numeric(flight.get(), "FLIGHT_STEP_SECONDS"),
                  p::numeric(sync.get(), "MIN_TIMED_DELAY_US") / 1.e6, p::numeric(sync.get(), "INITIAL_TIMED_DELAY_US") / 1.e6,
                  p::numeric(sync.get(), "TIMED_DELAY_DECAY_RATIO")};
        timeout = p::numeric(presenter.get(), "CANONICAL_MODEL_TIMEOUT_SECONDS");
        aoi = p::numeric(spotting.get(), "VEHICLE_AOI_RADIUS");
    }
    PyObject* manager() const {
        auto* m = PyWeakref_GetObject(weak_manager.get());
        if (m == Py_None) throw p::Error("native turret manager was released");
        return m;
    }
    Battle* battle() const {
        Battle* b = current();
        return b && generation && b->generation == generation ? b : nullptr;
    }
    bool active() const { return !closed && PyWeakref_GetObject(weak_manager.get()) != Py_None && (!generation || battle()); }
    std::shared_ptr<Entry> find(PyObject* raw) const {
        for (const auto& e : entries) if (e->raw.get() == raw) return e;
        return {};
    }
    p::Ref resolve(std::int64_t id) const {
        Battle* b = current();
        return b ? b->entity(id) : p::retain(Py_None);
    }
    bool matches(PyObject* entity, PyObject* raw) const {
        auto id = field(entity, "vehicleID");
        if (id.get() == Py_None) {
            auto properties = field(entity, "properties");
            if (PyDict_Check(properties.get())) {
                auto actual = field(properties.get(), "vehicleID");
                return actual.get() != Py_None && p::wide(actual.get()) == p::integral(raw, "vehicle_id");
            }
            return false;
        }
        return p::wide(id.get()) == p::integral(raw, "vehicle_id");
    }
    void erase(const std::shared_ptr<Entry>& e) {
        auto rows = field(manager(), "_turrets");
        removeIdentity(rows.get(), e->raw.get());
        entries.erase(std::remove(entries.begin(), entries.end(), e), entries.end());
    }
    std::size_t retirePoll() {
        std::size_t retired = 0;
        auto retiring = field(manager(), "_retiring");
        if (!PyList_Check(retiring.get())) throw p::Error("native retiring turret list is invalid");
        p::Ref snapshot(PyList_AsTuple(retiring.get()));
        for (Py_ssize_t i = 0; i < PyTuple_GET_SIZE(snapshot.get()); ++i) {
            auto* raw = PyTuple_GET_ITEM(snapshot.get(), i);
            auto entity = resolve(p::integral(raw, "id")), previous = field(raw, "entity");
            if (entity.get() == Py_None && previous.get() == Py_None) continue;
            if (entity.get() != Py_None && (previous.get() == Py_None || previous.get() == entity.get())) {
                if (!matches(entity.get(), raw)) { removeIdentity(retiring.get(), raw); continue; }
                put(raw, "entity", entity);
                auto id = p::integer(p::integral(raw, "id"));
                p::Ref args(PyTuple_Pack(1, id.get()));
                try {
                    builtin(bigworld.get(), "destroyEntity", args.get());
                } catch (const std::exception& error) {
                    PyErr_Clear();
                    log(std::string("Native detached turret retirement failed: ") + error.what());
                    continue;
                }
            }
            removeIdentity(retiring.get(), raw);
            ++retired;
        }
        return retired;
    }
    void add(PyObject* raw) {
        if (find(raw)) return;
        auto e = std::make_shared<Entry>(raw, tuning);
        e->id = p::integral(raw, "id");
        e->vehicle = p::integral(raw, "vehicle_id");
        e->row = visualRow(raw);
        e->started = p::numeric(raw, "started");
        e->created = p::numeric(raw, "created");
        e->canonical = field(raw, "canonical_key").get() != Py_None;
        e->settled = p::truth(raw, "settled");
        e->impacted = p::truth(raw, "impacted");
        entries.push_back(std::move(e));
    }
    void push(PyObject* raw, PyObject* wire, double now, double age) {
        auto e = find(raw);
        if (!e) { add(raw); e = find(raw); }
        Row next;
        if (!detached::readRow(wire, next)) throw p::Error("native turret canonical presentation row is invalid");
        e->row = std::move(next);
        e->started = p::numeric(raw, "started");
        e->settled = p::truth(raw, "settled");
        e->impacted = p::truth(raw, "impacted");
        e->failed = false;
        e->buffer.push(e->row, now, age);
    }
    void attach(Battle& b) {
        if (generation == b.generation) return;
        generation = b.generation;
        accepted.clear();
        accepted_order.clear();
        const Actor* local = b.localVehicle();
        auto entity = local ? b.entity(local->engine_id) : p::retain(Py_None);
        auto descriptor = field(entity.get(), "typeDescriptor"), type = field(descriptor.get(), "type"), tags = field(type.get(), "tags");
        auto tag = p::text("SPG");
        spg = tags.get() != Py_None && PySequence_Contains(tags.get(), tag.get()) == 1;
        auto input = field(b.engine.modules.get(), "avatar_input_handler"), modes = field(input.get(), "_CTRL_MODE");
        strategic = p::string(field(modes.get(), "STRATEGIC").get());
        arty = p::string(field(modes.get(), "ARTY").get());
    }
    // Accepted rows in acceptance order; an unchanged row keeps its raw dictionary and presentation progress.
    void ingest(const DetachedTurrets& turrets) {
        std::vector<std::string> order;
        for (const auto& key : turrets.order) {
            const auto row = turrets.rows.find(key);
            if (row == turrets.rows.end()) continue;
            order.push_back(key);
            const auto found = accepted.find(key);
            if (found != accepted.end() && found->second->row.motion_sequence == row->second.motion_sequence &&
                found->second->row.created_time_ms == row->second.created_time_ms)
                continue;
            p::Ref raw(detached::packRow(row->second));
            auto fresh = std::make_shared<Accepted>(raw.get(), row->second);
            if (found != accepted.end()) fresh->visual_revision = found->second->visual_revision;
            accepted[key] = std::move(fresh);
        }
        for (auto at = accepted.begin(); at != accepted.end();)
            if (std::find(order.begin(), order.end(), at->first) == order.end()) at = accepted.erase(at);
            else ++at;
        accepted_order = std::move(order);
    }
    p::Ref optional(Accepted& row, PyObject* object, const char* method, PyObject* args) {
        try { return call(object, method, args); }
        catch (const std::exception& error) {
            PyErr_Clear();
            if (!row.failed) { row.failed = true; log(std::string("Native turret ") + method + ": " + error.what()); }
            return p::retain(Py_None);
        }
    }
    bool accepted_still(const std::string& key, const std::shared_ptr<Accepted>& record) const {
        const auto at = accepted.find(key);
        return at != accepted.end() && at->second == record;
    }
    void schedule(Battle& b, World& w, double now, double server) {
        if (!active()) return;
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
        if (spg) {
            auto handler = field(b.engine.avatar.get(), "inputHandler"), mode = field(handler.get(), "_AvatarInputHandler__ctrlModeName");
            const auto name = p::string(mode.get());
            spg_view = name == strategic || name == arty;
        }
        const auto order = accepted_order;
        for (const auto& key : order) {
            if (!active()) break;
            const auto found = accepted.find(key);
            if (found == accepted.end()) continue;
            auto record = found->second;
            auto* actor = b.vehicles->find(key);
            if (!actor || !actor->ready || actor->tombstone || actor->missing) continue;
            auto entity = resolve(actor->engine_id);
            if (entity.get() == Py_None) continue;
            // Visual-only debris owns no ray, hull, navigation or contact geometry.
            if (!record->geometry && !record->row.flight.body) { w.turrets.geometry.insert(key); record->geometry = true; }
            if (!record->geometry && actor->geometry && record->attempted_geometry != actor->geometry) {
                record->attempted_geometry = actor->geometry;
                auto* descriptor = stock_native_client_query::descriptor(*actor);
                if (w.turret_obstacles && descriptor) {
                    auto identity = p::text(key);
                    p::Ref args(PyTuple_Pack(3, identity.get(), record->raw.get(), descriptor));
                    auto result = optional(*record, w.turret_obstacles.get(), "add", args.get());
                    if (!active()) break;
                    if (!accepted_still(key, record)) continue;
                    if (p::flag(result.get())) { w.turrets.geometry.insert(key); record->geometry = true; }
                }
            }
            if (!p::flag(py::optional(entity.get(), "isTurretDetached").get()) ||
                (!spg_view && std::hypot(origin[0] - record->row.flight.rest[0], origin[2] - record->row.flight.rest[2]) > aoi))
                continue;
            bool resident = false;
            for (const auto& e : entries) if (e->vehicle == actor->engine_id) { resident = true; break; }
            if (!resident) {
                auto retiring = field(manager(), "_retiring");
                for (Py_ssize_t i = 0; i < p::size(retiring.get()); ++i) {
                    auto value = p::item(retiring.get(), i);
                    if (p::integral(value.get(), "vehicle_id") == actor->engine_id) { resident = true; break; }
                }
            }
            if (resident && record->visual_revision >= record->row.motion_sequence) continue;
            const std::string retry_key = "visual:" + key;
            const auto due = w.turrets.visual_retry.find(retry_key);
            if (!resident && due != w.turrets.visual_retry.end() && now < due->second) continue;
            w.turrets.visual_retry[retry_key] = now + .25;
            p::Ref prepare_args(PyTuple_Pack(2, entity.get(), record->raw.get()));
            auto plan = optional(*record, manager(), "prepare_canonical", prepare_args.get());
            if (!active()) break;
            if (!accepted_still(key, record) || !PyDict_Check(plan.get())) continue;
            auto time = p::number(now), age = p::number(std::max(0., (server - double(record->row.created_time_ms)) / 1000.));
            p::Ref launch_args(PyTuple_Pack(4, plan.get(), record->raw.get(), time.get(), age.get()));
            auto launched = optional(*record, manager(), "launch_canonical", launch_args.get());
            if (!active()) break;
            if (!accepted_still(key, record)) continue;
            if (p::flag(launched.get())) {
                record->visual_revision = record->row.motion_sequence;
                w.turrets.visual_revisions[key] = record->visual_revision;
            }
        }
    }
    p::Ref vector(const Point& v) {
        auto args = p::tupleNumbers(v);
        return builtin(math.get(), "Vector3", args.get());
    }
    void writeMatrix(const Entry& e, const present::Pose& pose) {
#if defined(WOT_NATIVE_BATTLE_HOST_BUILD)
        auto angles = p::tupleNumbers(pose.attitude);
        p::Ref args(PyTuple_Pack(1, angles.get()));
        builtin(e.matrix->get(), "setRotateYPR", args.get());
        put(e.matrix->get(), "translation", vector(pose.position));
#else
        if (!BW::PyMatrix::Check(e.matrix->get())) throw p::Error("native turret provider is not a writable Matrix");
        auto* matrix = static_cast<BW::PyMatrix*>(e.matrix->get());
        matrix->setRotate(float(pose.attitude[0]), float(pose.attitude[1]), float(pose.attitude[2]));
        matrix->translation(BW::Vector3(float(pose.position[0]), float(pose.position[1]), float(pose.position[2])));
#endif
    }
    void impact(const Entry& e, const present::Pose& pose) {
        auto raw = field(e.raw.get(), "flight");
        if (!pose.body) {
            p::Ref args(PyTuple_Pack(2, e.entity->get(), raw.get()));
            call(manager(), "_report_impact", args.get());
            return;
        }
        const auto& body = *pose.body;
        if (!body.impact) return;
        p::Ref flight(PyDict_Copy(raw.get()));
        p::set(flight.get(), "landed", p::boolean(true));
        p::set(flight.get(), "contact", p::numbers(body.impact->point));
        p::set(flight.get(), "energy", p::number(body.impact->energy));
        auto hit_frame = p::dict(), hit = p::dict();
        p::set(hit.get(), "normal", p::numbers(body.impact->normal));
        p::set(hit_frame.get(), "impact", hit);
        p::set(flight.get(), "body", hit_frame);
        p::Ref args(PyTuple_Pack(2, e.entity->get(), flight.get()));
        call(manager(), "_report_impact", args.get());
    }
    void failedLoad(const std::shared_ptr<Entry>& e, double now) {
        entries.erase(std::remove(entries.begin(), entries.end(), e), entries.end());
        auto time = p::number(now);
        p::Ref args(PyTuple_Pack(2, e->raw.get(), time.get()));
        call(manager(), "_retire_failed_load", args.get());
    }
    std::size_t advance(double now) {
        retirePoll();
        if (!active()) return 0;
        std::size_t written = 0;
        const auto active_entries = entries;
        for (const auto& e : active_entries) {
            if (!active()) break;
            try {
                auto entity = resolve(e->id);
                if (entity.get() == Py_None) {
                    if (e->entity) erase(e);
                    else if (e->canonical && now - e->created >= timeout) failedLoad(e, now);
                    continue;
                }
                if ((e->entity && e->entity->get() != entity.get()) || !matches(entity.get(), e->raw.get())) { erase(e); continue; }
                if (!e->entity) { e->entity = std::make_unique<p::Ref>(p::retain(entity.get())); put(e->raw.get(), "entity", entity); }
                auto model = field(entity.get(), "model");
                if (model.get() == Py_None) {
                    if (e->canonical && now - e->created >= timeout) failedLoad(e, now);
                    continue;
                }
                if (e->settled) continue;
                if (!e->matrix) {
                    p::Ref args(PyTuple_Pack(2, entity.get(), e->raw.get()));
                    auto bound = call(manager(), "_bind", args.get());
                    if (!active()) break;
                    if (!p::flag(bound.get())) {
                        auto retiring = field(manager(), "_retiring");
                        p::append(retiring.get(), e->raw);
                        erase(e);
                        retirePoll();
                        continue;
                    }
                    auto matrix = field(e->raw.get(), "matrix");
                    if (matrix.get() == Py_None) throw p::Error("native turret Matrix binding is unavailable");
                    e->matrix = std::make_unique<p::Ref>(std::move(matrix));
                }
                const double elapsed = std::max(0., now - e->started);
                auto buffered = e->buffer.pose(now);
                const auto pose = buffered ? *buffered : present::poseAt(e->row, elapsed, tuning);
                writeMatrix(*e, pose);
                if (!active()) break;
                ++written;
                const bool was_settled = e->settled;
                const bool landed = pose.body ? bool(pose.body->impact) : elapsed >= e->row.flight.duration;
                if (pose.body) e->settled = pose.body->sleeping && (!buffered || *e->buffer.cursor() >= e->buffer.samples().back().time);
                else if (landed) e->settled = true;
                if (e->settled != was_settled) put(e->raw.get(), "settled", p::boolean(e->settled));
                if (landed) {
                    if (pose.body && pose.body->impact_serial > e->presented_impact) {
                        e->presented_impact = pose.body->impact_serial;
                        put(e->raw.get(), "presented_impact_serial", p::integer(e->presented_impact));
                        impact(*e, pose);
                    } else if (!pose.body && !e->impacted) {
                        e->impacted = true;
                        put(e->raw.get(), "impacted", p::boolean(true));
                        impact(*e, pose);
                    }
                }
            } catch (const std::exception& error) {
                PyErr_Clear();
                if (!e->failed) { e->failed = true; log("Native detached turret " + std::to_string(e->id) + ": " + error.what()); }
            }
        }
        return written;
    }
    std::size_t clear() {
        closed = true;
        auto* m = manager();
        put(m, "_closed", p::boolean(true));
        auto rows = field(m, "_turrets"), retiring = field(m, "_retiring");
        if (!PyList_Check(rows.get())) throw p::Error("native active turret list is invalid");
        for (Py_ssize_t i = 0; i < PyList_GET_SIZE(rows.get()); ++i) p::append(retiring.get(), p::retain(PyList_GET_ITEM(rows.get(), i)));
        entries.clear();
        put(m, "_turrets", p::list());
        return retirePoll();
    }
};

void destroy(PyObject* c) {
    auto* o = static_cast<Owner*>(PyCapsule_GetPointer(c, capsule_name));
    if (o) delete o;
    else PyErr_Clear();
}
Owner& owner(PyObject* c) {
    auto* o = static_cast<Owner*>(PyCapsule_GetPointer(c, capsule_name));
    if (!o) throw p::Error("native turret presentation capsule is invalid");
    return *o;
}
// The owner the stock manager carries as _native_turret_presentation, created on first use.
Owner* managerOwner(PyObject* manager) {
    auto held = field(manager, "_native_turret_presentation");
    if (held.get() == Py_None) {
        p::Ref capsule(PyCapsule_New(new Owner(manager), capsule_name, destroy));
        put(manager, "_native_turret_presentation", capsule);
        return &owner(capsule.get());
    }
    return &owner(held.get());
}
PyObject* fail(const std::exception& e) {
    if (!PyErr_Occurred()) PyErr_SetString(PyExc_ValueError, e.what());
    return nullptr;
}
PyObject* create(PyObject*, PyObject* m) {
    try { return PyCapsule_New(new Owner(m), capsule_name, destroy); } catch (const std::exception& e) { return fail(e); }
}
PyObject* adopt(PyObject*, PyObject* args) {
    PyObject *c, *raw;
    if (!PyArg_ParseTuple(args, "OO", &c, &raw)) return nullptr;
    try { owner(c).add(raw); Py_RETURN_NONE; } catch (const std::exception& e) { return fail(e); }
}
PyObject* push(PyObject*, PyObject* args) {
    PyObject *c, *raw, *row;
    double now, age;
    if (!PyArg_ParseTuple(args, "OOOdd", &c, &raw, &row, &now, &age)) return nullptr;
    try { owner(c).push(raw, row, now, age); Py_RETURN_NONE; } catch (const std::exception& e) { return fail(e); }
}
PyObject* advance(PyObject*, PyObject* args) {
    PyObject* c;
    double now;
    if (!PyArg_ParseTuple(args, "Od", &c, &now)) return nullptr;
    try { return p::integer(owner(c).advance(now)).release(); } catch (const std::exception& e) { return fail(e); }
}
PyObject* retireOwner(PyObject*, PyObject* c) {
    try { return p::integer(owner(c).retirePoll()).release(); } catch (const std::exception& e) { return fail(e); }
}
PyObject* clearOwner(PyObject*, PyObject* c) {
    try { return p::integer(owner(c).clear()).release(); } catch (const std::exception& e) { return fail(e); }
}
// Called by the stock DetachedTurretPresentation manager through its native_client_turret_presentation facade.
[[maybe_unused]] PyMethodDef methods[] = {
    {"create", create, METH_O, nullptr},
    {"adopt", adopt, METH_VARARGS, nullptr},
    {"push", push, METH_VARARGS, nullptr},
    {"advance", advance, METH_VARARGS, nullptr},
    {"retire", retireOwner, METH_O, nullptr},
    {"clear", clearOwner, METH_O, nullptr},
    {nullptr, nullptr, 0, nullptr}};
}

void presentTurretRows(Battle& b) {
    auto& w = *b.world;
    if (b.worker || !w.turret_manager) return;
    auto* o = managerOwner(w.turret_manager.get());
    o->attach(b);
    o->ingest(w.turrets);
}

void advanceTurretPresentation(Battle& b, double now) {
    auto& w = *b.world;
    if (!w.turret_manager) return;
    auto* o = managerOwner(w.turret_manager.get());
    if (o->generation != b.generation) { o->attach(b); o->ingest(w.turrets); }
    const auto server = projectiles::serverTimeMs(b, now);
    if (!server) return;
    o->schedule(b, w, now, std::max(0., std::trunc(*server)));
    o->advance(now);
}

void retireTurretPresentation(Battle& b) {
    auto& w = *b.world;
    if (!w.turret_manager) return;
    // The stock manager clears its entities through this owner (destroy_all -> clear).
    auto destroy_all = py::optional(w.turret_manager.get(), "destroy_all");
    if (!py::callable(destroy_all)) return;
    PyObject* done = PyObject_CallFunctionObjArgs(destroy_all.get(), nullptr);
    if (done) Py_DECREF(done);
    else PyErr_Clear();
}
}
}

#if !defined(WOT_NATIVE_BATTLE_HOST_BUILD)
namespace {
class TurretPresentationModule final : public BW::Script::InitTimeJob {
public:
    TurretPresentationModule() : BW::Script::InitTimeJob(1) {}
    void init() override {
        if (!Py_InitModule("offline_client_turret_presentation_native", native_battle::world::methods)) {
            PyErr_Print();
            CRITICAL_MSG("Native visible turret presentation initialization failed\n");
        }
    }
};
TurretPresentationModule turretPresentationModule;
}
#endif
