// Visible-client targeting: the cursor outline, the Expert damaged-device monitor and target-lock lifetime.
#if !defined(WOT_NATIVE_BATTLE_HOST_BUILD)
#include "pch.hpp"
#include "connection_control.hpp"
#include "entity.hpp"
#include "py_entity.hpp"
#include "entity_picker.hpp"
#include "pyscript/script_math.hpp"
#endif
#include "stock_native_battle_visibility_core.hpp"

namespace native_battle {
namespace targeting = stock_native_client_targeting;
namespace query = stock_native_client_query;
using ID = std::int64_t;

namespace visibility {
struct CursorRay { Point start{}, direction{}; };
#if defined(WOT_NATIVE_BATTLE_HOST_BUILD)
bool enginePresent(ID, PyObject*);
CursorRay sampleMouse(VisibilityCore&);
#endif
namespace {
constexpr double outline_maximum = 710.;
constexpr double outline_fov = 80. * .5;
constexpr double occlusion_epsilon = 1.e-3;

#if !defined(WOT_NATIVE_BATTLE_HOST_BUILD)
// The engine entity is in the world and is this presentation (or a LAN presentation standing in for it).
bool enginePresent(ID id, PyObject* vehicle) {
    auto* connection = BW::ConnectionControl::pInstance();
    auto* entity = connection ? connection->findEntity(static_cast<BW::EntityID>(id)) : nullptr;
    if (!entity || entity->isDestroyed() || !entity->isInWorld()) return false;
    p::Ref native(entity->pPyEntity().newRef());
    auto lan = p::value(vehicle, "_offlineLANPresentation");
    return p::flag(lan.get()) || native.get() == vehicle;
}
// The ray #1513 gives BigWorld.target: the mouse targeting matrix of this round.
CursorRay sampleMouse(VisibilityCore& c) {
    if (!c.mouse) {
        // PyObjectPlus creation owns one reference; adopt it for this round.
        auto* matrix = new BW::MatrixProviderPtr(new BW::MouseTargetingMatrix, BW::MatrixProviderPtr::STEAL_REFERENCE);
        c.mouse = std::shared_ptr<void>(matrix, [](void* raw) { delete static_cast<BW::MatrixProviderPtr*>(raw); });
    }
    BW::Matrix matrix;
    (*static_cast<BW::MatrixProviderPtr*>(c.mouse.get()))->matrix(matrix);
    auto start = matrix.applyToOrigin(), direction = matrix.applyToUnitAxisVector(2);
    direction.normalise();
    if (direction.length() <= 0.f) throw p::Error("#1513 mouse targeting ray is empty");
    return {{start.x, start.y, start.z}, {direction.x, direction.y, direction.z}};
}
#endif

p::Ref field(PyObject* o, const char* name) {
    if (!o || o == Py_None) return p::retain(Py_None);
    if (PyDict_Check(o)) return p::value(o, name);
    auto key = p::text(name);
    PyObject* v = p::directAttribute(o, key.get());
    if (!v && PyErr_ExceptionMatches(PyExc_AttributeError)) { PyErr_Clear(); return p::retain(Py_None); }
    return p::Ref(v);
}
bool truth(PyObject* o, const char* name, bool fallback = false) {
    auto v = field(o, name);
    return v.get() == Py_None ? fallback : p::flag(v.get());
}
void put(PyObject* o, const char* name, PyObject* value) {
    auto key = p::text(name);
    if (PyObject_GenericSetAttr(o, key.get(), value) < 0) throw p::Error(std::string("#1513 targeting state write failed: ") + name);
}
p::Ref callChecked(PyObject* o, const char* name, PyObject* args) {
    auto fn = field(o, name);
    if (!PyCallable_Check(fn.get())) throw p::Error(std::string("#1513 targeting presentation boundary is unavailable: ") + name);
    return p::Ref(PyObject_CallObject(fn.get(), args));
}
p::Ref indexTuple(const std::vector<int>& values) {
    p::Ref out(PyTuple_New(Py_ssize_t(values.size())));
    for (std::size_t i = 0; i < values.size(); ++i) PyTuple_SET_ITEM(out.get(), Py_ssize_t(i), p::integer(values[i]).release());
    return out;
}
std::shared_ptr<Actor> actorByEngine(const Battle& b, ID id) {
    for (const auto& row : b.vehicles->actors)
        if (row.second && row.second->engine_id == id) return row.second;
    return {};
}
// The presented remote vehicle the query registered for this engine id, or None.
p::Ref entity(Battle& b, VisibilityCore& c, ID id) {
    if (!c.query) return p::retain(Py_None);
    auto* vehicle = query::registeredVehicle(c.query.get(), id);
    if (!vehicle || !enginePresent(id, vehicle)) return p::retain(Py_None);
    return p::retain(vehicle);
}
bool capable(Battle& b, VisibilityCore& c) {
    return b.visibility->local.expert && !c.expert_disabled && gun::livingSkillCount(b, "commander_expert") > 0;
}
targeting::ExpertActor expertActor(const Actor& a, PyObject* e) {
    targeting::ExpertActor out;
    out.engine_id = a.engine_id; out.local = a.local; out.tombstone = a.tombstone; out.visible = a.visible;
    out.exists = e != Py_None; out.alive = a.alive;
    return out;
}
p::Ref feedback(Battle& b) {
    auto provider = field(b.engine.avatar.get(), "guiSessionProvider"), shared = field(provider.get(), "shared");
    return field(shared.get(), "feedback");
}
void hidden(Battle& b, const targeting::ExpertHide& value) {
    auto consumer = feedback(b);
    auto fn = field(consumer.get(), "hideVehicleDamagedDevices");
    if (!PyCallable_Check(fn.get())) return;
    auto id = p::integer(value.engine_id);
    py::invoke(fn.get(), id.get());
}
bool monitor(Battle& b, VisibilityCore& c, ID target, const std::string& reason) {
    auto a = actorByEngine(b, target);
    auto e = a ? entity(b, c, target) : p::retain(Py_None);
    const auto input = a ? std::optional<targeting::ExpertActor>{expertActor(*a, e.get())} : std::nullopt;
    const bool allowed = b.phase == Phase::Loading || b.phase == Phase::Running;
    const auto previous = c.expert.target();
    const double now = target > 0 && target != previous ? b.clock() : 0.;
    const auto changes = c.expert.monitor(target, now, target <= 0 ? true : capable(b, c), allowed, input ? &*input : nullptr, localTeam(b),
                                          a ? ID(a->state.get("team").numeric()) : 0, reason);
    if (changes.hide) hidden(b, *changes.hide);
    return changes.accepted;
}
const std::map<std::string, int>& extraMap(VisibilityCore& c, const std::shared_ptr<Actor>& a, PyObject* e) {
    auto resource = field(e, "typeDescriptor");
    if (resource.get() == Py_None) throw p::Error("Native Expert descriptor is unavailable");
    auto& cache = c.extras[a->engine_id];
    if (cache.actor == a && cache.revision == a->descriptor_revision && cache.descriptor.get() == resource.get()) return cache.values;
    struct Extra { int index; PyObject* object; std::string name; };
    std::vector<Extra> rows;
    auto raw = field(resource.get(), "extras");
    if (PyDict_Check(raw.get())) {
        Py_ssize_t cursor = 0; PyObject *key, *value;
        while (PyDict_Next(raw.get(), &cursor, &key, &value)) {
            auto name = field(value, "name");
            rows.push_back({int(p::wide(key)), value, p::string(name.get())});
        }
    } else if (raw.get() != Py_None)
        for (Py_ssize_t i = 0; i < p::size(raw.get()); ++i) {
            auto item = p::item(raw.get(), i), name = field(item.get(), "name");
            rows.push_back({int(i), item.get(), p::string(name.get())});
        }
    cache.values.clear();
    for (const auto& row : rows) cache.values.emplace(row.name, row.index);
    auto aliases = field(resource.get(), "extrasDict");
    if (PyDict_Check(aliases.get())) {
        Py_ssize_t cursor = 0; PyObject *key, *selected;
        while (PyDict_Next(aliases.get(), &cursor, &key, &selected)) {
            const auto name = p::string(key);
            for (const auto& row : rows)
                if (row.object == selected || row.name == name) { cache.values[name] = row.index; break; }
        }
    }
    cache.actor = a;
    cache.revision = a->descriptor_revision;
    cache.descriptor = borrow(resource.get());
    return cache.values;
}
bool highlighterEnabled(PyObject* value) {
    auto status = field(value, "_Highlighter__highlightStatus"), off = field(value, "HIGHLIGHT_OFF");
    if (status.get() == Py_None || off.get() == Py_None) throw p::Error("#1513 highlighter state is unavailable");
    return p::wide(status.get()) != p::wide(off.get());
}
p::Ref highlighter(PyObject* e) {
    auto appearance = field(e, "appearance");
    return field(appearance.get(), "_CompoundAppearance__highlighter");
}
void focus(Battle& b, PyObject* value) {
    put(b.engine.avatar.get(), "target", value);
    auto consumer = feedback(b);
    const ID id = value == Py_None ? 0 : p::integral(value, "id");
    auto object = p::integer(id), present = p::boolean(value != Py_None);
    p::Ref args(PyTuple_Pack(2, object.get(), present.get()));
    callChecked(consumer.get(), "setTargetInFocus", args.get());
}
void candidate(Battle& b, PyObject* value) {
    auto compatibility = b.engine.compatibility.get();
    if (value != Py_None) {
        if (!truth(compatibility, "_battle_active")) throw p::Error("Target-lock candidate requires a battle");
        const bool lan = truth(value, "_offlineLANPresentation");
        if (!lan && !truth(value, "_offlineNativeRemote")) throw p::Error("Target-lock candidate is not a remote Vehicle");
        if (lan && field(value, "bw_entity").get() == Py_None) throw p::Error("Target-lock candidate has no visual entity");
    }
    put(compatibility, "_target_lock_candidate", value);
}
void refresh(Battle& b, VisibilityCore& c) {
    const auto revision = c.outline_revision;
    if (!c.outlined_vehicle || !truth(c.outlined_vehicle.get(), "_offlineNativeRemote")) return;
    auto component = highlighter(c.outlined_vehicle.get());
    if (component.get() == Py_None) throw p::Error("#1513 native highlighter is unavailable");
    auto actual = field(c.outlined_vehicle.get(), "model");
    if (actual.get() == Py_None) return;
    if (highlighterEnabled(component.get())) {
        if (c.outlined_model.get() != actual.get()) c.outlined_model = borrow(actual.get());
        return;
    }
    // A CompoundAppearance refresh re-created the Highlighter; draw the edge again through its owner.
    auto component_vehicle = field(component.get(), "_Highlighter__vehicle");
    if (component_vehicle.get() == Py_None) return;
    p::Ref args(PyTuple_Pack(1, Py_False));
    callChecked(c.outlined_vehicle.get(), "drawEdge", args.get());
    if (!current(b, &c) || c.outline_revision != revision || !highlighterEnabled(component.get())) return;
    c.outlined_model = borrow(actual.get());
}
bool clear(Battle& b, VisibilityCore& c) {
    if (c.clearing) return false;
    auto& v = *b.visibility;
    if (!v.outlined_engine_id && !c.outlined_visual) return true;
    struct Guard { bool& value; explicit Guard(bool& flag) : value(flag) { value = true; } ~Guard() { value = false; } } guard(c.clearing);
    const auto revision = ++c.outline_revision;
    candidate(b, Py_None);
    focus(b, Py_None);
    if (!current(b, &c) || c.outline_revision != revision) return false;
    if (!c.outlined_visual || !c.outlined_vehicle) throw p::Error("Native outlined vehicle lost its entity before removal");
    const bool native = truth(c.outlined_vehicle.get(), "_offlineNativeRemote");
    auto expected = native ? p::retain(c.outlined_vehicle.get()) : field(c.outlined_vehicle.get(), "bw_entity");
    if (expected.get() != c.outlined_visual.get()) throw p::Error("Native outlined vehicle changed its visual before removal");
    if (native) {
        auto component = highlighter(c.outlined_vehicle.get());
        if (component.get() == Py_None) throw p::Error("#1513 native highlighter is unavailable");
        if (highlighterEnabled(component.get())) {
            if (field(c.outlined_visual.get(), "model").get() == Py_None) throw p::Error("Native outlined vehicle lost its model before removal");
            p::Ref args(PyTuple_Pack(1, Py_False));
            callChecked(c.outlined_vehicle.get(), "removeEdge", args.get());
        }
    } else {
        auto actual = field(c.outlined_vehicle.get(), "model"), native_model = field(c.outlined_visual.get(), "model");
        if (!c.outlined_model || actual.get() != c.outlined_model.get() || native_model.get() == Py_None)
            throw p::Error("Native outlined compound changed before removal");
        p::Ref args(PyTuple_Pack(1, c.outlined_visual.get()));
        callChecked(b.engine.bigworld.get(), "wgDelEdgeDetectEntity", args.get());
    }
    if (!current(b, &c) || c.outline_revision != revision) return false;
    v.outlined_engine_id.reset();
    c.outlined_visual = Object();
    c.outlined_vehicle = Object();
    c.outlined_model = Object();
    monitor(b, c, 0, "target_clear");
    return current(b, &c);
}
void add(Battle& b, VisibilityCore& c, ID id) {
    const auto revision = ++c.outline_revision;
    auto selected = entity(b, c, id);
    if (selected.get() == Py_None) throw p::Error("Outlined remote vehicle is unavailable");
    const bool native = truth(selected.get(), "_offlineNativeRemote");
    auto image = native ? p::retain(selected.get()) : field(selected.get(), "bw_entity");
    if (image.get() == Py_None) throw p::Error("Outlined remote vehicle has no visual entity");
    auto actual_model = field(selected.get(), "model");
    // Record the owner before crossing the native boundary, so cleanup can release a partial registration.
    b.visibility->outlined_engine_id = id;
    c.outlined_vehicle = borrow(selected.get());
    c.outlined_visual = borrow(image.get());
    c.outlined_model = borrow(actual_model.get());
    if (native) {
        // The stock Highlighter owns the EdgeDrawer entry across CompoundAppearance refresh and retirement.
        auto component = highlighter(c.outlined_vehicle.get());
        auto component_vehicle = field(component.get(), "_Highlighter__vehicle");
        if (component_vehicle.get() != Py_None) {
            p::Ref args(PyTuple_Pack(1, Py_False));
            callChecked(c.outlined_vehicle.get(), "drawEdge", args.get());
        }
    } else {
        auto a = actorByEngine(b, id);
        const int color = a && ID(a->state.get("team").numeric()) == localTeam(b) ? 2 : 1;
        p::Ref args(Py_BuildValue("(OiiO)", c.outlined_visual.get(), color, 0, Py_False));
        callChecked(b.engine.bigworld.get(), "wgAddEdgeDetectEntity", args.get());
    }
    if (!current(b, &c) || c.outline_revision != revision) return;
    candidate(b, c.outlined_vehicle.get());
    focus(b, c.outlined_vehicle.get());
    if (current(b, &c) && c.outline_revision == revision) monitor(b, c, id, "target_clear");
}
}

void outline(Battle& b, double now) {
    auto* c = core(b);
    if (!c || !c->outline_clock.begin(now, b.visibility->outline_blocked)) return;
    if (!b.engine.remote_factory || !c->query) { clear(b, *c); return; }
    const auto cursor = sampleMouse(*c);
    const auto server = projectiles::serverTimeMs(b, now);
    const auto pick = query::pickOutline(c->query.get(), cursor.start, cursor.direction, outline_maximum, outline_fov,
                                         b.visibility->outlined_engine_id, p::integral(b.engine.avatar.get(), "spaceID"),
                                         server ? std::max(0., std::trunc(*server)) : -1., occlusion_epsilon);
    if (!pick.available) { clear(b, *c); return; }
    for (const auto& error : pick.errors) {
        auto& last = c->query_failures[error.entity];
        if (last != error.error) { last = error.error; log("Native targeting query " + error.entity + ": " + error.error); }
    }
    const auto change = targeting::outlineChanges(b.visibility->outlined_engine_id, pick);
    if (change.unchanged) {
        if (change.refresh) {
            const auto held = b.visibility->outlined_engine_id;
            const auto revision = c->outline_revision;
            refresh(b, *c);
            if (current(b, c) && c->outline_revision == revision && b.visibility->outlined_engine_id == held && held)
                monitor(b, *c, *held, "target_clear");
        }
        return;
    }
    if (!clear(b, *c) || !current(b, c)) return;
    if (change.add) add(b, *c, *change.add);
}

void expert(Battle& b, double now) {
    auto* c = core(b);
    if (!c) return;
    auto a = actorByEngine(b, c->expert.target());
    auto e = a ? entity(b, *c, a->engine_id) : p::retain(Py_None);
    const auto input = a ? std::optional<targeting::ExpertActor>{expertActor(*a, e.get())} : std::nullopt;
    const bool enabled = capable(b, *c);
    static const Value empty;
    static const std::map<std::string, int> none;
    const bool eligible = enabled && a && !a->local && !a->tombstone && a->visible && a->alive && e.get() != Py_None && now >= c->expert.due();
    const auto& indices = eligible ? extraMap(*c, a, e.get()) : none;
    const auto changes = c->expert.tick(now, enabled, input ? &*input : nullptr, a ? a->state.get("critical") : empty, indices);
    if (changes.hide) hidden(b, *changes.hide);
    if (!changes.show || !current(b, c)) return;
    // Avatar.showOtherVehicleDamagedDevices rechecks BigWorld.target(); publish through the same feedback consumer instead.
    auto consumer = feedback(b);
    auto id = p::integer(changes.show->engine_id), damaged = indexTuple(changes.show->devices.first), destroyed = indexTuple(changes.show->devices.second);
    p::Ref args(PyTuple_Pack(3, id.get(), damaged.get(), destroyed.get()));
    callChecked(consumer.get(), "showVehicleDamagedDevices", args.get());
    if (current(b, c)) c->expert.shown(changes.show->engine_id, changes.show->devices);
}

bool monitorExpert(Battle& b, PyObject* vehicle_id, const std::string& reason) {
    PyObject* raw = PyNumber_Long(vehicle_id);
    if (!raw) { PyErr_Clear(); return false; }
    p::Ref parsed(raw);
    const long long target = PyLong_AsLongLong(parsed.get());
    if (PyErr_Occurred()) { PyErr_Clear(); return false; }
    auto* c = core(b);
    if (!c) return target <= 0;
    return monitor(b, *c, target > 0 ? target : 0, reason);
}

bool enableExpertVisibility(Battle& b) {
    auto& v = *b.visibility;
    if (!v.local.expert || v.expert_visibility_enabled || b.worker || !b.engine.avatar) return false;
    if (auto* c = core(b); c && c->expert_disabled) return false;
    auto statuses = field(b.engine.constants.get(), "VEHICLE_MISC_STATUS");
    auto status = field(statuses.get(), "OTHER_VEHICLE_DAMAGED_DEVICES_VISIBLE");
    auto callback = field(b.engine.avatar.get(), "updateVehicleMiscStatus");
    if (status.get() == Py_None || !PyCallable_Check(callback.get())) throw p::Error("#1513 Expert visibility boundary is unavailable");
    // Stock targetBlur clears the cell monitor; the offline target loop owns the four-second delay instead.
    // PlayerAvatar reads floatArgs[0] even for this integer-only status.
    auto vehicle = field(b.engine.avatar.get(), "playerVehicleID");
    p::Ref status_value(PyNumber_Int(status.get()));
    auto zero = p::integer(0);
    p::Ref float_args(Py_BuildValue("(d)", 0.));
    py::invoke(callback.get(), vehicle.get(), status_value.get(), zero.get(), float_args.get());
    v.expert_visibility_enabled = true;
    return true;
}

void disableExpert(Battle& b) {
    auto* c = core(b);
    b.visibility->expert_visibility_enabled = false;
    if (!c) return;
    c->expert_disabled = true;
    monitor(b, *c, 0, "feature_disabled");
}

void disableOutline(Battle& b) {
    if (auto* c = core(b)) {
        try { clear(b, *c); } catch (const std::exception&) { PyErr_Clear(); }
    }
    b.visibility->outline_blocked = true;
}

bool clearOutline(Battle& b) {
    auto* c = core(b);
    return !c || clear(b, *c);
}

bool releaseTargetLock(Battle& b, std::int64_t engine_id) {
    if (!b.engine.avatar || !b.engine.compatibility) return false;
    auto release = field(b.engine.compatibility.get(), "release_target_lock");
    if (!PyCallable_Check(release.get())) return false;
    auto id = p::integer(engine_id);
    auto result = py::invoke(release.get(), b.engine.avatar.get(), id.get());
    return p::flag(result.get());
}

// Releases the stock auto-aim lock once its vehicle stopped being a live, visible, presented target.
void validateTargetLock(Battle& b) {
    auto compatibility = b.engine.compatibility.get();
    auto* avatar = b.engine.avatar.get();
    if (!compatibility || !avatar || !truth(compatibility, "_battle_active")) return;
    auto* c = core(b);
    if (!c) return;
    auto raw = field(avatar, "_PlayerAvatar__autoAimVehID");
    if (raw.get() == Py_None || !p::flag(raw.get())) return;
    bool holds = false;
    try {
        const auto id = p::wide(raw.get());
        auto a = actorByEngine(b, id);
        auto target = entity(b, *c, id);
        targeting::LockStatus status;
        if (target.get() != Py_None) {
            status.exists = true;
            status.alive = a ? a->alive : truth(target.get(), "isCrewActive", true) && p::numeric(target.get(), "health") > 0.;
            status.visible = a ? a->visible : truth(target.get(), "_spot_visible", true);
            status.lan = truth(target.get(), "_offlineLANPresentation");
            if (status.lan) {
                status.visual = field(target.get(), "bw_entity").get() != Py_None;
                status.model = field(target.get(), "model").get() != Py_None;
                status.in_world = truth(target.get(), "inWorld");
            }
        }
        holds = targeting::lockHolds(status);
    } catch (const std::exception&) {
        PyErr_Clear();
    }
    if (holds) return;
    auto fn = field(compatibility, "_original_avatar_auto_aim");
    if (!PyCallable_Check(fn.get())) throw p::Error("#1513 target lock release boundary is unavailable");
    py::invoke(fn.get(), avatar, Py_None);
}
}
}
