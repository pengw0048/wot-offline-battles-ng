// Stock presentation gates of native remote Vehicles: the pose overlay, draw/attachment/ground gates, engine audio.
#if !defined(WOT_NATIVE_BATTLE_HOST_BUILD)
#include "pch.hpp"
#endif
#include "stock_native_battle_remote_internal.hpp"
#include "stock_native_battle_compat.hpp"
#include <algorithm>
#include <cstdlib>
#include <functional>
#include <structmember.h>

namespace native_battle {
namespace remote {
namespace {
// A Python callable backed by a C++ function: stock events, data links and wrapped stock functions.
struct Callback {
    PyObject_HEAD
    std::function<PyObject*(PyObject*, PyObject*)>* body;
};
void callbackDealloc(PyObject* self) {
    delete reinterpret_cast<Callback*>(self)->body;
    PyObject_Del(self);
}
PyObject* callbackCall(PyObject* self, PyObject* args, PyObject* kwargs) {
    try { return (*reinterpret_cast<Callback*>(self)->body)(args, kwargs); }
    catch (const std::exception& e) {
        if (!PyErr_Occurred()) PyErr_SetString(PyExc_RuntimeError, e.what());
        return nullptr;
    }
}
PyTypeObject CallbackType = {PyVarObject_HEAD_INIT(nullptr, 0) "offline_native_battle.RemoteCallback"};

// `value` of a stock speedInfo whose movement columns come from the copied pose overlay.
struct SpeedInfo {
    PyObject_HEAD
    PyObject* source;
    PyObject* vehicle;   // identity key only, never dereferenced
};
void speedInfoDealloc(PyObject* self) {
    Py_XDECREF(reinterpret_cast<SpeedInfo*>(self)->source);
    PyObject_Del(self);
}
PyObject* speedInfoGet(PyObject* self, PyObject* key) {
    auto* s = reinterpret_cast<SpeedInfo*>(self);
    if (!PyString_Check(key) || std::string(PyString_AS_STRING(key)) != "value") return PyObject_GenericGetAttr(self, key);
    try {
        auto original = py::attr(s->source, "value");
        Overlay* row = overlay(s->vehicle);
        const double speed = row ? row->speed : 0., turn = row ? row->turn_speed : 0.;
        auto fourth = p::item(original.get(), 3);
        return Py_BuildValue("(dddO)", speed, turn, speed, fourth.get());
    } catch (const std::exception& e) {
        if (!PyErr_Occurred()) PyErr_SetString(PyExc_RuntimeError, e.what());
        return nullptr;
    }
}
PyTypeObject SpeedInfoType = {PyVarObject_HEAD_INIT(nullptr, 0) "offline_native_battle.CopiedSpeedInfo"};

// A plain attribute holder (CompoundAppearance.setupGunMatrixTargets reads turretMatrix/gunMatrix).
struct Holder {
    PyObject_HEAD
    PyObject* dict;
};
void holderDealloc(PyObject* self) {
    Py_XDECREF(reinterpret_cast<Holder*>(self)->dict);
    PyObject_Del(self);
}
PyTypeObject HolderType = {PyVarObject_HEAD_INIT(nullptr, 0) "offline_native_battle.AimTarget"};

// Every slot is set before PyType_Ready.
void initTypes() {
    static bool done = false;
    if (done) return;
    CallbackType.tp_basicsize = sizeof(Callback);
    CallbackType.tp_dealloc = callbackDealloc;
    CallbackType.tp_call = callbackCall;
    CallbackType.tp_flags = Py_TPFLAGS_DEFAULT;
    SpeedInfoType.tp_basicsize = sizeof(SpeedInfo);
    SpeedInfoType.tp_dealloc = speedInfoDealloc;
    SpeedInfoType.tp_getattro = speedInfoGet;
    SpeedInfoType.tp_flags = Py_TPFLAGS_DEFAULT;
    HolderType.tp_basicsize = sizeof(Holder);
    HolderType.tp_dealloc = holderDealloc;
    HolderType.tp_dictoffset = offsetof(Holder, dict);
    HolderType.tp_getattro = PyObject_GenericGetAttr;
    HolderType.tp_setattro = PyObject_GenericSetAttr;
    HolderType.tp_flags = Py_TPFLAGS_DEFAULT;
    if (PyType_Ready(&CallbackType) < 0 || PyType_Ready(&SpeedInfoType) < 0 || PyType_Ready(&HolderType) < 0)
        throw p::Error("native remote type initialization failed");
    done = true;
}

std::map<PyObject*, Overlay> overlays;
const char* const pose_fields[] = {"_pose_active", "position", "yaw", "matrix", "steady_rotation_matrix", "stabilised_matrix",
                                   "speed", "turn_speed", "speedInfo", "velocity", "acceleration", "_collision_filter"};

PyObject* module(const char* name) {
    PyObject* loaded = PyDict_GetItemString(PyImport_GetModuleDict(), name);
    if (loaded) { Py_INCREF(loaded); return loaded; }
    return PyImport_ImportModule(name);
}
std::string text(PyObject* error) {
    PyObject* raw = PyObject_Str(error);
    if (!raw) { PyErr_Clear(); return "error"; }
    p::Ref held(raw);
    return p::string(held.get());
}

// One bounded log line per optional stock surface.
std::set<std::string> reported_gates;
void reportGate(const std::string& kind, const std::string& error) {
    if (!reported_gates.insert(kind).second) return;
    std::string message = error.substr(0, 240);
    native_battle::log("hidden vehicle ground " + kind + " gate degraded: " + (message.empty() ? "error" : message));
}
void reportOnce(const std::string& key, const std::string& line) {
    if (reported_gates.insert(key).second) native_battle::log(line);
}
std::string pending(const std::exception& e) { return describe(e); }

// ---- ground and attachment gates --------------------------------------------------------------------------
struct DecalGate {
    Object detached_model, detached_splodge, detached_hull;   // splodge we detached from the hull node
    Object disabled_model, disabled_splodge;                  // a splodge whose native ownership is unknown
};
std::map<PyObject*, DecalGate> decal_gates;

bool setModelAttachmentVisibility(PyObject* model, bool visible) {
    if (!model || model == Py_None) return false;
    PyObject* probe = PyObject_GetAttrString(model, "visibleAttachments");
    if (probe) {
        Py_DECREF(probe);
        if (PyObject_SetAttrString(model, "visibleAttachments", visible ? Py_True : Py_False) == 0) return true;
    }
    if (!PyErr_ExceptionMatches(PyExc_AttributeError) && !PyErr_ExceptionMatches(PyExc_TypeError) &&
        !PyErr_ExceptionMatches(PyExc_ReferenceError)) throw p::Error("visibleAttachments");
    PyErr_Clear();
    reportOnce("attachments", "compound model has no writable visibleAttachments gate; hidden vehicle effects may still draw");
    return false;
}

}

// Stock ground and trace gates of one appearance (also the compat hooks for an undrawn LAN remote).
bool stopGroundEffects(PyObject* appearance) {
    auto manager = py::optional(appearance, "customEffectManager");
    if (manager.get() == Py_None) return false;
    auto selectors = py::optional(manager.get(), "_CustomEffectManager__selectors");
    if (selectors.get() == Py_None) {
        reportOnce("selectors", "custom effect manager exposes no selector list; hidden vehicle dust and exhaust may still draw");
        return false;
    }
    PyObject* list = PySequence_List(selectors.get());
    if (!list) {
        if (!PyErr_ExceptionMatches(PyExc_Exception)) throw p::Error("selectors");
        reportGate("effects", describe(p::Error("selector list")));
        return false;
    }
    p::Ref rows(list);
    bool reached = false;
    for (Py_ssize_t i = 0; i < PyList_GET_SIZE(list); ++i) {
        PyObject* selector = PyList_GET_ITEM(list, i);
        try {
            auto enabled = py::optional(selector, "_enabled");
            if (enabled.get() != Py_None && !p::flag(enabled.get())) { reached = true; continue; }
            py::call(selector, "stop");
            reached = true;
        } catch (const std::exception& e) {
            if (PyErr_Occurred() && !PyErr_ExceptionMatches(PyExc_Exception)) throw;
            reportGate("effects", pending(e));
        }
    }
    return reached;
}

bool setGroundDecalVisibility(PyObject* appearance, bool visible) {
    if (!appearance || appearance == Py_None) return false;
    bool reached = false, attempted = false;
    p::Ref model(p::retain(Py_None)), decal(p::retain(Py_None)), hull(p::retain(Py_None)), splodge(p::retain(Py_None));
    try {
        replace(model, py::optional(appearance, "compoundModel"));
        replace(decal, py::optional(appearance, "_CompoundAppearance__chassisDecal"));
        replace(hull, py::optional(decal.get(), "_VehicleDecal__hullParent"));
        replace(splodge, py::optional(appearance, "_CompoundAppearance__splodge"));
    } catch (const std::exception& e) {
        if (PyErr_Occurred() && !PyErr_ExceptionMatches(PyExc_Exception)) throw;
        reportGate("decal", pending(e));
        return false;
    }
    auto& gate = decal_gates[appearance];
    bool disabled = bool(gate.disabled_splodge);
    if (disabled) {
        const bool matches = gate.disabled_model.get() == model.get() && gate.disabled_splodge.get() == splodge.get();
        const bool reused = !matches && gate.disabled_splodge.get() == splodge.get();
        if (!matches) {
            if (reused && model.get() != Py_None && splodge.get() != Py_None) gate.disabled_model = borrow(model.get());
            else { gate.disabled_model = Object(); gate.disabled_splodge = Object(); disabled = false; }
        }
    }
    bool detached = bool(gate.detached_splodge);
    if (detached) {
        const bool matches = gate.detached_model.get() == model.get() && gate.detached_splodge.get() == splodge.get() &&
                             (hull.get() == Py_None || gate.detached_hull.get() == hull.get());
        const bool reused = !matches && gate.detached_splodge.get() == splodge.get();
        if (!matches) {
            // Never dereference a retired hull node after a compound refresh.
            gate.detached_model = gate.detached_splodge = gate.detached_hull = Object();
            detached = false;
            if (reused && model.get() != Py_None && splodge.get() != Py_None) {
                gate.disabled_model = borrow(model.get());
                gate.disabled_splodge = borrow(splodge.get());
                disabled = true;
            }
        }
    }
    if (!disabled && splodge.get() != Py_None) {
        if (visible && detached) {
            attempted = true;
            Object node = gate.detached_hull;
            gate.detached_model = gate.detached_splodge = gate.detached_hull = Object();
            gate.disabled_model = borrow(model.get());
            gate.disabled_splodge = borrow(splodge.get());
            try {
                py::call(node.get(), "attach", splodge.get());
                gate.disabled_model = gate.disabled_splodge = Object();
                reached = true;
            } catch (const std::exception& e) {
                if (PyErr_Occurred() && !PyErr_ExceptionMatches(PyExc_Exception)) throw;
                reportGate("decal", pending(e));
            }
        } else if (!visible && !detached && hull.get() != Py_None) {
            attempted = true;
            gate.disabled_model = borrow(model.get());
            gate.disabled_splodge = borrow(splodge.get());
            try {
                py::call(hull.get(), "detach", splodge.get());
                gate.disabled_model = gate.disabled_splodge = Object();
                gate.detached_model = borrow(model.get());
                gate.detached_splodge = borrow(splodge.get());
                gate.detached_hull = borrow(hull.get());
                reached = true;
            } catch (const std::exception& e) {
                if (PyErr_Occurred() && !PyErr_ExceptionMatches(PyExc_Exception)) throw;
                reportGate("decal", pending(e));
            }
        } else if (detached) reached = true;
    } else if (disabled) attempted = true;
    try {
        auto change = py::optional(decal.get(), visible ? "attach" : "detach");
        if (py::callable(change)) {
            attempted = true;
            py::invoke(change.get());
            reached = true;
        }
    } catch (const std::exception& e) {
        if (PyErr_Occurred() && !PyErr_ExceptionMatches(PyExc_Exception)) throw;
        reportGate("decal", pending(e));
    }
    if (!reached && !attempted)
        reportOnce("decal-owner", "compound appearance exposes no ground decal owner; a hidden vehicle may still cast its occlusion decal");
    return reached;
}

bool setVehicleTracesVisibility(PyObject* appearance, bool visible) {
    if (!appearance || appearance == Py_None) return false;
    PyObject* raw = PyObject_GetAttrString(appearance, "vehicleTraces");
    if (!raw) {
        if (!PyErr_ExceptionMatches(PyExc_AttributeError)) throw p::Error("vehicleTraces");
        PyErr_Clear();
        reportOnce("traces-absent", "compound appearance exposes no vehicle traces component; a hidden vehicle may still print track marks");
        return false;
    }
    p::Ref traces(raw);
    if (!visible) {
        if (traces.get() == Py_None) return true;
        if (PyObject_SetAttrString(appearance, "vehicleTraces", Py_None) < 0) {
            if (!PyErr_ExceptionMatches(PyExc_Exception)) throw p::Error("vehicleTraces");
            PyObject *t, *v, *tb;
            PyErr_Fetch(&t, &v, &tb);
            PyErr_NormalizeException(&t, &v, &tb);
            p::Ref value(v ? v : p::retain(Py_None).release());
            Py_XDECREF(t); Py_XDECREF(tb);
            reportGate("traces", text(value.get()));
            return false;
        }
        return true;
    }
    if (traces.get() != Py_None) return true;
    auto damage = py::optional(appearance, "damageState");
    auto damaged = py::optional(damage.get(), "isCurrentModelDamaged");
    auto filter = py::optional(appearance, "filter");
    auto lod = py::optional(py::optional(appearance, "lodCalculator").get(), "lodStateLink");
    if (damaged.get() == Py_None || filter.get() == Py_None || lod.get() == Py_None) {
        reportOnce("traces-rebuild", "compound appearance cannot rebuild its vehicle traces; a revealed vehicle may print no track marks");
        return false;
    }
    if (p::flag(damaged.get())) return true;
    try {
        p::Ref assembler(module("vehicle_systems.model_assembler"));
        py::call(assembler.get(), "assembleVehicleTraces", appearance, filter.get(), lod.get());
    } catch (const std::exception& e) {
        if (PyErr_Occurred() && !PyErr_ExceptionMatches(PyExc_Exception)) throw;
        reportGate("traces", pending(e));
        return false;
    }
    return true;
}

bool stopBoundVisualEffects(PyObject* appearance) {
    if (!appearance || appearance == Py_None) return false;
    bool reached = false;
    if (py::optional(appearance, "_CompoundAppearance__effectsPlayer").get() != Py_None) {
        try { py::call(appearance, "_CompoundAppearance__stopEffects"); reached = true; }
        catch (const std::exception& e) {
            if (PyErr_Occurred() && !PyErr_ExceptionMatches(PyExc_Exception)) throw;
            reportGate("effects", pending(e));
        }
    }
    auto bound = py::optional(appearance, "boundEffects");
    if (bound.get() != Py_None) {
        try {
            auto effects = py::optional(bound.get(), "_effects");
            const Py_ssize_t count = effects.get() == Py_None ? 0 : PyObject_Size(effects.get());
            if (count < 0) throw p::Error("boundEffects");
            if (count) py::call(bound.get(), "stop");
            reached = true;
        } catch (const std::exception& e) {
            if (PyErr_Occurred() && !PyErr_ExceptionMatches(PyExc_Exception)) throw;
            reportGate("effects", pending(e));
        }
    }
    return reached;
}

void clearDecalGate(PyObject* appearance) { decal_gates.erase(appearance); }
void clearGroundDecalVisibilityState(PyObject* appearance) { clearDecalGate(appearance); }

namespace {

// ---- engine audio relay -----------------------------------------------------------------------------------
struct Relay;
struct AppearanceAudio {
    Object weak;                                   // weakref to the appearance
    std::shared_ptr<Relay> relay;
    Object muted_detailed, muted_model;            // a hidden owner's (DetailedEngineState, compound) generation
    bool muted = false;
    Object motion_speed, motion_rotation;
    bool restore_reported = false;
};
std::map<PyObject*, AppearanceAudio> audio;
std::map<std::uintptr_t, std::weak_ptr<Relay>> relays;   // by id(DetailedEngineState)
Object original_assemble, original_subscribe, installed_assemble, installed_subscribe;

Object makeCallback(std::function<PyObject*(PyObject*, PyObject*)> body) {
    initTypes();
    auto* value = PyObject_New(Callback, &CallbackType);
    if (!value) throw p::Error("native remote callback allocation failed");
    value->body = new std::function<PyObject*(PyObject*, PyObject*)>(std::move(body));
    return Object(reinterpret_cast<PyObject*>(value));
}

AppearanceAudio* audioOf(PyObject* appearance, bool create) {
    auto found = audio.find(appearance);
    if (found != audio.end()) {
        PyObject* alive = PyWeakref_GetObject(found->second.weak.get());
        if (alive == appearance) return &found->second;
        audio.erase(found);
    }
    if (!create) return nullptr;
    PyObject* weak = PyWeakref_NewRef(appearance, nullptr);
    if (!weak) throw p::Error("engine audio appearance weakref failed");
    auto& row = audio[appearance];
    row.weak = Object(weak);
    return &row;
}

struct Relay : std::enable_shared_from_this<Relay> {
    Object appearance_weak;
    std::uintptr_t state_id = 0;
    bool audible = true, closed = false;
    std::optional<std::uintptr_t> sink_id;
    Object last_start_args, last_start_kwargs, last_state_args, last_state_kwargs;
    bool has_start = false, has_state = false;
    Object start_callback, state_callback;

    PyObject* current() const {
        if (closed) return nullptr;
        PyObject* a = PyWeakref_GetObject(appearance_weak.get());
        if (!a || a == Py_None) return nullptr;
        PyObject* d = PyObject_GetAttrString(a, "detailedEngineState");
        if (!d) { PyErr_Clear(); return nullptr; }
        const bool match = d != Py_None && std::uintptr_t(d) == state_id;
        Py_DECREF(d);
        return match ? a : nullptr;
    }
    p::Ref sink() const {
        PyObject* a = current();
        if (!a || !audible) return p::retain(Py_None);
        return py::optional(a, "engineAudition");
    }
    PyObject* forward(const char* method, PyObject* args, PyObject* kwargs, bool start) {
        (start ? last_start_args : last_state_args) = borrow(args);
        (start ? last_start_kwargs : last_state_kwargs) = borrow(kwargs ? kwargs : Py_None);
        (start ? has_start : has_state) = true;
        auto target = sink();
        if (target.get() != Py_None) {
            auto fn = py::attr(target.get(), method);
            PyObject* result = PyObject_Call(fn.get(), args, kwargs);
            if (!result) return nullptr;
            Py_DECREF(result);
        }
        Py_RETURN_NONE;
    }
    void replay(PyObject* audition, const char* method, const Object& args, const Object& kwargs) {
        auto fn = py::attr(audition, method);
        PyObject* kw = kwargs.get() != Py_None ? kwargs.get() : nullptr;
        p::Ref unused(PyObject_Call(fn.get(), args.get(), kw));
    }
    void suspend() { audible = false; sink_id.reset(); }
    void bind(PyObject* audition, PyObject* detailed) {
        PyObject* a = current();
        if (!a || std::uintptr_t(detailed) != state_id) throw p::Error("engine lifecycle belongs to another appearance");
        const auto old = sink_id;
        auto* row = audioOf(a, false);
        audible = !(row && row->muted);
        if (audition != Py_None) sink_id = std::uintptr_t(audition); else sink_id.reset();
        py::set(detailed, "onEngineStart", start_callback.get());
        py::set(detailed, "onStateChanged", state_callback.get());
        if (row && row->motion_rotation && audition != Py_None) {
            auto filter = py::optional(a, "filter");
            if (filter.get() != Py_None) py::call(audition, "setSpeedInfo", row->motion_rotation.get(), strafeLink(filter.get()).get());
        }
        if (audible && audition != Py_None && old != sink_id) {
            if (has_start) replay(audition, "onEngineStart", last_start_args, last_start_kwargs);
            if (has_state) replay(audition, "onEngineStateChanged", last_state_args, last_state_kwargs);
        }
    }
    void close() {
        if (PyObject* a = current()) {
            auto d = py::attr(a, "detailedEngineState");
            for (auto* entry : {&start_callback, &state_callback}) {
                const char* name = entry == &start_callback ? "onEngineStart" : "onStateChanged";
                auto installed = py::optional(d.get(), name);
                if (installed.get() == entry->get()) py::set(d.get(), name, Py_None);
            }
        }
        closed = true;
        audible = false;
        has_start = has_state = false;
        last_start_args = last_start_kwargs = last_state_args = last_state_kwargs = Object();
        sink_id.reset();
        auto found = relays.find(state_id);
        if (found != relays.end() && found->second.lock().get() == this) relays.erase(found);
    }
    static Object strafeLink(PyObject* filter) {
        Object held = borrow(filter);
        return makeCallback([held](PyObject*, PyObject*) -> PyObject* { return PyObject_GetAttrString(held.get(), "strafeSpeed"); });
    }
};

std::shared_ptr<Relay> registerRelay(PyObject* appearance) {
    auto detailed = py::optional(appearance, "detailedEngineState");
    if (detailed.get() == Py_None) return {};
    auto* row = audioOf(appearance, true);
    auto owner = row->relay;
    if (!owner || owner->closed || owner->state_id != std::uintptr_t(detailed.get())) {
        if (owner) owner->close();
        owner = std::make_shared<Relay>();
        owner->appearance_weak = borrow(row->weak.get());
        owner->state_id = std::uintptr_t(detailed.get());
        std::weak_ptr<Relay> weak = owner;
        owner->start_callback = makeCallback([weak](PyObject* args, PyObject* kwargs) -> PyObject* {
            auto relay = weak.lock();
            if (!relay) Py_RETURN_NONE;
            return relay->forward("onEngineStart", args, kwargs, true);
        });
        owner->state_callback = makeCallback([weak](PyObject* args, PyObject* kwargs) -> PyObject* {
            auto relay = weak.lock();
            if (!relay) Py_RETURN_NONE;
            return relay->forward("onEngineStateChanged", args, kwargs, false);
        });
        relays[owner->state_id] = owner;
        row->relay = owner;
    }
    return owner;
}

void suspendAudio(PyObject* appearance) {
    auto owner = registerRelay(appearance);
    if (!owner) return;
    auto detailed = py::attr(appearance, "detailedEngineState");
    py::set(detailed.get(), "onEngineStart", owner->start_callback.get());
    py::set(detailed.get(), "onStateChanged", owner->state_callback.get());
    owner->suspend();
}

bool alive(PyObject* entity) {
    auto value = py::optional(entity, "isAlive");
    if (PyCallable_Check(value.get())) return p::flag(py::invoke(value.get()).get());
    return p::flag(value.get());
}
bool flagOf(PyObject* o, const char* name) { return p::flag(py::optional(o, name).get()); }

bool ownerIsCurrent(PyObject* entity, PyObject* appearance, PyObject* audition, PyObject* detailed, PyObject* model) {
    auto* row = audioOf(appearance, false);
    return alive(entity) && flagOf(entity, "inWorld") && flagOf(entity, "isStarted") &&
           py::optional(entity, "appearance").get() == appearance && py::optional(appearance, "engineAudition").get() == audition &&
           py::optional(appearance, "detailedEngineState").get() == detailed && py::optional(appearance, "compoundModel").get() == model &&
           !(row && row->muted);
}

// The stock hook for a vehicle first presented mid-battle, or its prebattle engine notification.
bool activateRevealedEngine(PyObject* detailed) {
    p::Ref bigworld(module("BigWorld"));
    auto avatar = py::call(bigworld.get(), "player");
    auto arena = py::optional(avatar.get(), "arena");
    if (detailed == Py_None || arena.get() == Py_None) return false;
    p::Ref constants(module("constants"));
    auto periods = py::attr(constants.get(), "ARENA_PERIOD");
    auto period = py::attr(arena.get(), "period");
    p::Ref engine_state(module("vehicle_systems.components.engine_state"));
    if (PyObject_RichCompareBool(period.get(), py::attr(periods.get(), "BATTLE").get(), Py_EQ) == 1)
        py::call(engine_state.get(), "checkEngineStart", detailed, period.get());
    else if (PyObject_RichCompareBool(period.get(), py::attr(periods.get(), "PREBATTLE").get(), Py_EQ) == 1)
        py::call(engine_state.get(), "notifyEngineOnArenaPeriodChange", detailed, period.get(), py::attr(arena.get(), "periodEndTime").get(),
                 py::call(bigworld.get(), "serverTime").get());
    else return false;
    return true;
}

// Activates only a freshly assembled receiver on an already active parent; failure keeps the receiver.
void activateNewAudioOwner(PyObject* entity, PyObject* appearance, PyObject* audition) {
    const char* mode = std::getenv("OFFLINE_LAN_0922_CLIENT_MODE");
    if (mode && std::string(mode) == "simulation_worker") return;
    if (flagOf(entity, "isPlayerVehicle")) return;
    if (py::optional(appearance, "engineAudition").get() != audition) return;
    if (!flagOf(appearance, "activated")) return;
    auto activate = py::optional(audition, "activate");
    if (!py::callable(activate)) return;
    PyObject* done = PyObject_CallFunctionObjArgs(activate.get(), nullptr);
    if (done) Py_DECREF(done);
    else if (PyErr_ExceptionMatches(PyExc_Exception)) PyErr_Clear();
    else throw p::Error("audition activate");
}
}

Overlay* overlay(PyObject* vehicle, bool create) {
    auto found = overlays.find(vehicle);
    if (found != overlays.end()) return &found->second;
    return create ? &overlays[vehicle] : nullptr;
}

// compat.set_vehicle_pose_overlay: one copied-physics pose for stock camera, gun and collision consumers.
void publishOverlay(PyObject* vehicle, const OverlayPose& pose) {
    auto& row = overlays[vehicle];
    row.pose_active = true;
    row.speed = pose.speed;
    row.turn_speed = pose.turn_speed;
    row.fields["_pose_active"] = borrow(Py_True);
    row.fields["position"] = borrow(pose.position);
    row.fields["yaw"] = own(p::number(pose.yaw));
    row.fields["matrix"] = borrow(pose.matrix);
    row.fields["steady_rotation_matrix"] = borrow(pose.steady_rotation_matrix ? pose.steady_rotation_matrix : pose.matrix);
    row.fields["stabilised_matrix"] = borrow(pose.stabilised_matrix ? pose.stabilised_matrix : pose.matrix);
    row.fields["speed"] = own(p::number(pose.speed));
    row.fields["turn_speed"] = own(p::number(pose.turn_speed));
    if (!row.fields.count("speedInfo")) {
        Battle* b = current();
        if (b && b->compat && b->compat->active) {
            p::Ref stock(p::retain(Py_None));
            try { replace(stock, compat::nativeVehicleAttribute(vehicle, "speedInfo")); }
            catch (const std::exception&) {
                // A partially constructed Vehicle has no stock provider yet.
                if (!PyErr_ExceptionMatches(PyExc_AttributeError)) throw p::Error("speedInfo");
                PyErr_Clear();
            }
            if (stock.get() != Py_None) {
                initTypes();
                auto* info = PyObject_New(SpeedInfo, &SpeedInfoType);
                if (!info) throw p::Error("speedInfo allocation");
                info->source = stock.release();
                info->vehicle = vehicle;
                row.fields["speedInfo"] = Object(reinterpret_cast<PyObject*>(info));
            }
        }
    }
    if (pose.velocity) row.fields["velocity"] = borrow(pose.velocity);
    if (pose.acceleration) row.fields["acceleration"] = borrow(pose.acceleration);
}

void clearOverlay(PyObject* vehicle) {
    auto found = overlays.find(vehicle);
    if (found == overlays.end()) return;
    for (const char* name : pose_fields) found->second.fields.erase(name);
    found->second.pose_active = false;
    found->second.speed = found->second.turn_speed = 0.;
    if (found->second.fields.empty()) overlays.erase(found);
}
void clearOverlays() {
    overlays.clear();
    decal_gates.clear();
}

Object aimTarget(Battle& b) {
    initTypes();
    auto* holder = PyObject_New(Holder, &HolderType);
    if (!holder) throw p::Error("aim target allocation failed");
    holder->dict = PyDict_New();
    Object target(reinterpret_cast<PyObject*>(holder));
    for (const char* name : {"turretMatrix", "gunMatrix"}) {
        auto matrix = py::call(b.engine.math.get(), "Matrix");
        py::call(matrix.get(), "setIdentity");
        py::set(target.get(), name, matrix.get());
    }
    return target;
}

Object callback(std::function<PyObject*(PyObject*, PyObject*)> body) { return makeCallback(std::move(body)); }

bool closePresentationExtras(PyObject* appearance, bool visible) {
    if (!appearance || appearance == Py_None) return false;
    bool reached = setGroundDecalVisibility(appearance, visible);
    reached = setVehicleTracesVisibility(appearance, visible) || reached;
    if (!visible) {
        reached = stopGroundEffects(appearance) || reached;
        reached = stopBoundVisualEffects(appearance) || reached;
    }
    return reached;
}

void clearAppearanceState(PyObject* appearance) {
    clearDecalGate(appearance);
    if (appearance && appearance != Py_None) releaseEngineAudio(appearance);
}

// set_engine_audible: retire a hidden sound owner and assemble a fresh one on reveal.
bool setEngineAudible(PyObject* entity, bool audible) {
    if (flagOf(entity, "isPlayerVehicle")) return false;
    auto appearance = py::optional(entity, "appearance");
    if (appearance.get() == Py_None) return false;
    PyObject* a = appearance.get();
    auto* row = audioOf(a, true);
    auto audition = py::optional(a, "engineAudition");
    auto detailed = py::optional(a, "detailedEngineState");
    if (!audible) {
        if (audition.get() == Py_None) return row->muted;
        // Publish ownership before the ComponentDescriptor can re-enter.
        row->muted = true;
        row->muted_detailed = borrow(detailed.get());
        row->muted_model = own(py::optional(a, "compoundModel"));
        if (detailed.get() != Py_None) suspendAudio(a);
        py::set(a, "engineAudition", Py_None);
        return true;
    }
    if (!row->muted) return false;
    const Object saved_detailed = row->muted_detailed, saved_model = row->muted_model;
    auto model = py::optional(a, "compoundModel");
    if (!alive(entity) || !flagOf(entity, "inWorld") || audition.get() != Py_None || detailed.get() != saved_detailed.get() ||
        model.get() != saved_model.get()) {
        // Death or a model rebuild may already have installed a new owner.
        row->muted = false;
        row->muted_detailed = row->muted_model = Object();
        return false;
    }
    if (!flagOf(entity, "isStarted")) return false;
    row->muted = false;
    row->muted_detailed = row->muted_model = Object();
    p::Ref built(p::retain(Py_None));
    try {
        p::Ref assembler(module("vehicle_systems.model_assembler"));
        p::Ref links(module("DataLinks"));
        auto sensor = py::attr(a, "waterSensor");
        auto weapon_energy = py::attr(a, "_CompoundAppearance__weaponEnergy");
        if (detailed.get() == Py_None || model.get() == Py_None || sensor.get() == Py_None)
            throw p::Error("stock engine-audition inputs are unavailable");
        auto underwater = py::call(links.get(), "createBoolLink", sensor.get(), p::text("isUnderWater").get());
        auto in_water = py::call(links.get(), "createBoolLink", sensor.get(), p::text("isInWater").get());
        try {
            py::call(assembler.get(), "assembleVehicleAudition", Py_False, a);
        } catch (...) {
            PyObject *t, *v, *tb;
            PyErr_Fetch(&t, &v, &tb);
            replace(built, py::optional(a, "engineAudition"));
            PyErr_Restore(t, v, tb);
            throw;
        }
        replace(built, py::attr(a, "engineAudition"));
        py::call(built.get(), "setIsUnderwaterInfo", underwater.get());
        py::call(built.get(), "setIsInWaterInfo", in_water.get());
        activateNewAudioOwner(entity, a, built.get());
        if (!ownerIsCurrent(entity, a, built.get(), detailed.get(), model.get())) return false;
        py::call(built.get(), "setWeaponEnergy", weapon_energy.get());
        py::call(built.get(), "attachToModel", model.get());
        if (!ownerIsCurrent(entity, a, built.get(), detailed.get(), model.get())) return false;
        py::call(assembler.get(), "subscribeEngineAuditionToEngineState", built.get(), detailed.get());
        // Seed the new sound owner after model/water attachment from the retained native events.
        auto relay = registerRelay(a);
        if (relay) {
            relay->bind(built.get(), detailed.get());
            if (!relay->has_start) activateRevealedEngine(detailed.get());
        }
    } catch (const std::exception& e) {
        if (PyErr_Occurred() && !PyErr_ExceptionMatches(PyExc_Exception)) throw;
        const std::string error = describe(e);
        // A Python-side assembly failure is local to this sound owner; retire any partial component and retry on the next reveal.
        auto current_audition = py::optional(a, "engineAudition");
        if (py::optional(entity, "appearance").get() == a && py::optional(a, "detailedEngineState").get() == detailed.get() &&
            py::optional(a, "compoundModel").get() == model.get() && flagOf(entity, "inWorld") && flagOf(entity, "isStarted") &&
            (current_audition.get() == built.get() || current_audition.get() == Py_None)) {
            if (detailed.get() != Py_None) suspendAudio(a);
            py::set(a, "engineAudition", Py_None);
            auto* again = audioOf(a, true);
            again->muted = true;
            again->muted_detailed = saved_detailed;
            again->muted_model = saved_model;
        }
        auto* again = audioOf(a, true);
        if (!again->restore_reported) {
            again->restore_reported = true;
            native_battle::log("remote engine sound rebuild failed: " + error);
        }
        return false;
    }
    return true;
}

// set_draw_visibility: the stock compound gate plus the attachment, ground and audio gates it does not cover.
void setDrawVisibility(Battle&, PyObject* entity, bool visible) {
    auto show = py::optional(entity, "show");
    auto appearance = py::optional(entity, "appearance");
    auto change = py::optional(appearance.get(), "changeVisibility");
    if (!py::callable(show) || !py::callable(change)) throw p::Error("#1513 native vehicle visibility gate is unavailable");
    if (!visible) closePresentationExtras(appearance.get(), false);
    setEngineAudible(entity, visible);
    py::invoke(show.get(), visible ? Py_True : Py_False);
    py::invoke(change.get(), visible ? Py_True : Py_False);
    setModelAttachmentVisibility(py::optional(appearance.get(), "compoundModel").get(), visible);
    if (visible) closePresentationExtras(appearance.get(), true);
}

bool bindEngineMotion(PyObject* appearance, PyObject* speed_link, PyObject* rotation_link) {
    auto detailed = py::optional(appearance, "detailedEngineState");
    if (detailed.get() == Py_None) return false;
    py::set(detailed.get(), "vehicleSpeedLink", speed_link);
    py::set(detailed.get(), "rotationSpeedLink", rotation_link);
    auto model = py::optional(appearance, "compoundModel");
    // This input was captured by native assembly before the copied poses.
    if (model.get() != Py_None) py::set(detailed.get(), "vehicleMatrixLink", py::attr(model.get(), "root").get());
    auto audition = py::optional(appearance, "engineAudition");
    if (audition.get() != Py_None) {
        if (model.get() != Py_None) py::call(audition.get(), "attachToModel", model.get());
        auto filter = py::optional(appearance, "filter");
        if (filter.get() != Py_None) py::call(audition.get(), "setSpeedInfo", rotation_link, Relay::strafeLink(filter.get()).get());
    }
    auto* row = audioOf(appearance, true);
    row->motion_speed = borrow(speed_link);
    row->motion_rotation = borrow(rotation_link);
    registerRelay(appearance);
    return true;
}

void releaseEngineAudio(PyObject* appearance) {
    auto* row = audioOf(appearance, false);
    if (!row) return;
    if (row->relay) row->relay->close();
    audio.erase(appearance);
}

// engine_audio.install: wrap stock audition assembly so every relay sees its sound owner.
bool installEngineAudio() {
    if (installed_assemble) return false;
    p::Ref assembler(module("vehicle_systems.model_assembler"));
    original_assemble = own(py::attr(assembler.get(), "assembleVehicleAudition"));
    original_subscribe = own(py::attr(assembler.get(), "subscribeEngineAuditionToEngineState"));
    Object assemble = original_assemble, subscribe = original_subscribe;
    installed_assemble = makeCallback([assemble](PyObject* args, PyObject* kwargs) -> PyObject* {
        PyObject* result = PyObject_Call(assemble.get(), args, kwargs);
        if (!result) return nullptr;
        p::Ref held(result);
        if (PyTuple_GET_SIZE(args) >= 2) registerRelay(PyTuple_GET_ITEM(args, 1));
        return held.release();
    });
    installed_subscribe = makeCallback([subscribe](PyObject* args, PyObject* kwargs) -> PyObject* {
        PyObject* result = PyObject_Call(subscribe.get(), args, kwargs);
        if (!result) return nullptr;
        p::Ref held(result);
        if (PyTuple_GET_SIZE(args) >= 2) {
            PyObject* detailed = PyTuple_GET_ITEM(args, 1);
            auto found = relays.find(std::uintptr_t(detailed));
            if (found != relays.end())
                if (auto relay = found->second.lock()) relay->bind(PyTuple_GET_ITEM(args, 0), detailed);
        }
        return held.release();
    });
    py::set(assembler.get(), "assembleVehicleAudition", installed_assemble.get());
    py::set(assembler.get(), "subscribeEngineAuditionToEngineState", installed_subscribe.get());
    return true;
}

void uninstallEngineAudio() {
    for (auto& row : audio) if (row.second.relay) row.second.relay->close();
    audio.clear();
    relays.clear();
    if (!installed_assemble) return;
    p::Ref assembler(module("vehicle_systems.model_assembler"));
    if (py::attr(assembler.get(), "assembleVehicleAudition").get() == installed_assemble.get())
        py::set(assembler.get(), "assembleVehicleAudition", original_assemble.get());
    if (py::attr(assembler.get(), "subscribeEngineAuditionToEngineState").get() == installed_subscribe.get())
        py::set(assembler.get(), "subscribeEngineAuditionToEngineState", original_subscribe.get());
    installed_assemble = installed_subscribe = original_assemble = original_subscribe = Object();
}

// ---- descriptor collision at the drawn pose -------------------------------------------------------------------
namespace {
// _component_value: a dict entry or an attribute, else None.
p::Ref componentValue(PyObject* component, const char* name) {
    if (!component || component == Py_None) return p::retain(Py_None);
    if (PyDict_Check(component)) return p::value(component, name);
    return py::optional(component, name);
}
p::Ref matrix(PyObject* math, PyObject* source = nullptr) {
    return source ? py::call(math, "Matrix", source) : py::call(math, "Matrix");
}
p::Ref zero(PyObject* math) {
    auto v = p::number(0.);
    return py::call(math, "Vector3", v.get(), v.get(), v.get());
}
struct PoseComponent { p::Ref component, matrix; bool attached; };
// remote_vehicle._pose_components: exact #1513 (compDescr, compMatrix, isAttached) triples below the body pose.
std::vector<PoseComponent> poseComponents(PyObject* vehicle, PyObject* math) {
    auto descriptor = py::attr(vehicle, "typeDescriptor");
    const bool turret_attached = !p::flag(py::optional(vehicle, "isTurretDetached").get());
    std::vector<PoseComponent> out;
    auto chassis = py::attr(descriptor.get(), "chassis"), hull_descr = py::attr(descriptor.get(), "hull");
    auto turret_descr = py::attr(descriptor.get(), "turret"), gun_descr = py::attr(descriptor.get(), "gun");
    auto identity = matrix(math);
    py::call(identity.get(), "setIdentity");
    out.push_back({p::retain(chassis.get()), std::move(identity), true});
    auto hull_offset = componentValue(chassis.get(), "hullPosition");
    if (hull_offset.get() == Py_None) replace(hull_offset, zero(math));
    p::Ref negative_hull(PyNumber_Negative(hull_offset.get()));
    auto hull = matrix(math);
    py::call(hull.get(), "setTranslate", negative_hull.get());
    out.push_back({p::retain(hull_descr.get()), std::move(hull), true});
    auto positions = componentValue(hull_descr.get(), "turretPositions");
    p::Ref turret_offset(p::flag(positions.get()) ? p::item(positions.get(), 0) : zero(math));
    p::Ref turret_translation(PyNumber_Subtract(negative_hull.get(), turret_offset.get()));
    auto turret = matrix(math);
    py::call(turret.get(), "setTranslate", turret_translation.get());
    auto appearance = py::attr(vehicle, "appearance");
    const double turret_yaw = p::scalar(py::attr(matrix(math, py::attr(appearance.get(), "turretMatrix").get()).get(), "yaw").get());
    auto yaw_rotation = matrix(math);
    py::call(yaw_rotation.get(), "setRotateY", p::number(-turret_yaw).get());
    py::call(turret.get(), "postMultiply", yaw_rotation.get());
    auto gun_offset = componentValue(turret_descr.get(), "gunPosition");
    if (gun_offset.get() == Py_None) replace(gun_offset, zero(math));
    p::Ref negative_gun(PyNumber_Negative(gun_offset.get()));
    auto gun = matrix(math);
    py::call(gun.get(), "setTranslate", negative_gun.get());
    const double gun_pitch = p::scalar(py::attr(matrix(math, py::attr(appearance.get(), "gunMatrix").get()).get(), "pitch").get());
    auto pitch_rotation = matrix(math);
    py::call(pitch_rotation.get(), "setRotateX", p::number(-gun_pitch).get());
    py::call(gun.get(), "postMultiply", pitch_rotation.get());
    py::call(gun.get(), "preMultiply", turret.get());
    out.push_back({p::retain(turret_descr.get()), std::move(turret), turret_attached});
    out.push_back({p::retain(gun_descr.get()), std::move(gun), turret_attached});
    return out;
}
}

p::Ref collideAtMatrix(Battle& b, PyObject* vehicle, PyObject* vehicle_matrix, PyObject* start, PyObject* end) {
    PyObject* math = b.engine.math.get();
    p::Ref module(PyImport_ImportModule("gui.mods.offline_lan_0922.entities.remote_vehicle"));
    auto result_type = py::attr(module.get(), "_SegmentCollisionResultExt");
    auto world_to_vehicle = matrix(math, vehicle_matrix);
    py::call(world_to_vehicle.get(), "invert");
    auto body_start = py::call(world_to_vehicle.get(), "applyPoint", start);
    auto body_end = py::call(world_to_vehicle.get(), "applyPoint", end);
    std::vector<std::pair<double, Object>> hits;
    for (auto& entry : poseComponents(vehicle, math)) {
        // Exact #1513 Vehicle.__collideSegment skips an unattached component before its hit tester.
        if (!entry.attached) continue;
        auto tester = componentValue(entry.component.get(), "hitTester");
        auto local_hit_test = py::optional(tester.get(), "localHitTest");
        if (!py::callable(local_hit_test)) continue;
        auto component_start = py::call(entry.matrix.get(), "applyPoint", body_start.get());
        auto component_end = py::call(entry.matrix.get(), "applyPoint", body_end.get());
        auto collisions = py::invoke(local_hit_test.get(), component_start.get(), component_end.get());
        if (!p::flag(collisions.get())) continue;
        p::Ref rows(PySequence_List(collisions.get()));
        for (Py_ssize_t i = 0; i < PyList_GET_SIZE(rows.get()); ++i) {
            PyObject* collision = PyList_GET_ITEM(rows.get(), i);
            // dist, local_normal, angle_cos, material_kind = collision
            PyObject* fields = PySequence_Fast(collision, "collision");
            if (!fields) {
                if (!PyErr_ExceptionMatches(PyExc_TypeError)) throw p::Error("collision row");
                PyErr_Clear();
                continue;
            }
            p::Ref held(fields);
            if (PySequence_Fast_GET_SIZE(fields) != 4) continue;
            PyObject* dist = PySequence_Fast_GET_ITEM(fields, 0);
            PyObject* angle_cos = PySequence_Fast_GET_ITEM(fields, 2);
            PyObject* material_kind = PySequence_Fast_GET_ITEM(fields, 3);
            auto materials = componentValue(entry.component.get(), "materials");
            p::Ref material(p::retain(Py_None));
            if (p::flag(materials.get())) replace(material, py::call(materials.get(), "get", material_kind));
            auto name = componentValue(entry.component.get(), "itemTypeName");
            if (!PyString_Check(name.get()) && !PyUnicode_Check(name.get())) throw p::Error("#1513 collision component has no itemTypeName");
            const double distance = p::scalar(dist);
            auto row = py::invoke(result_type.get(), p::number(distance).get(), p::number(p::scalar(angle_cos)).get(), material.get(), name.get());
            hits.emplace_back(distance, own(std::move(row)));
        }
    }
    std::stable_sort(hits.begin(), hits.end(), [](const auto& a, const auto& c) { return a.first < c.first; });
    p::Ref out(PyList_New(0));
    for (const auto& hit : hits) p::append(out.get(), p::retain(hit.second.get()));
    return out;
}

void finaliseScriptOwners() {
    // A failed stock unsubscribe must not strand all other Python owners.
    std::string error;
    try { uninstallEngineAudio(); } catch (const std::exception& e) { error = describe(e); }
    audio.clear();
    relays.clear();
    overlays.clear();
    decal_gates.clear();
    installed_assemble = installed_subscribe = original_assemble = original_subscribe = Object();
    if (!error.empty()) throw p::Error(error);
}

}
}
