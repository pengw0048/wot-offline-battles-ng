// The battle space's native destructible owner: the catalog identities staged for live proof, the shared
// registry/scene/backend/events/pipeline, tree prewarm and the destructible presentation seam.
#if !defined(WOT_NATIVE_BATTLE_HOST_BUILD)
#include "pch.hpp"
#endif
#include "stock_native_battle_world_parts.hpp"
#include "stock_native_bot_world_adapter.hpp"
#include "stock_native_destructible_backend.hpp"
#include "stock_native_world_events.hpp"
#include "space/space_manager.hpp"
#include <algorithm>
#include <climits>
#include <cmath>
#include <random>

namespace native_battle {
namespace sw = stock_native_world;
namespace sd = stock_native_destructible;
using Catalog = DestructibleCatalog;

// One per battle space. Member order is construction order: the backend's sweep veto reads `veto`.
struct WorldSpace {
    // TRIANGLE_NOCOLLIDE | TRIANGLE_WATER (collision_flags.VEHICLE_SKIP_FLAGS).
    static constexpr std::uint16_t vehicle_skip_flags = 0x10 | 0x40;
    BW::ClientSpacePtr space;
    std::uint32_t id = 0;
    std::function<bool(const sw::TurretSweep&)> veto;
    sw::Registry registry;
    sd::NativeBackend backend;
    sw::NativeScene scene;
    sw::WorldEvents events;
    sw::Pipeline pipeline;
    std::optional<double> last_frame;
    WorldSpace(BW::ClientSpacePtr s, std::uint32_t space_id)
        : space(s), id(space_id),
          backend(s, registry, [this](const sw::TurretSweep& sweep) { return !veto || veto(sweep); }),
          scene(s, registry, backend), events(registry, backend, scene, space_id),
          pipeline(registry, scene, vehicle_skip_flags) {}
    ~WorldSpace() { scene.retire(); backend.retire(); }
};

namespace world {
namespace {
constexpr double chunk_metres = 100.;
constexpr double forward_probe_metres = 6.;

[[noreturn]] void invalid(const std::string& what) { throw p::Error(what); }
// A descriptor value outside the installer's material domain fails the whole world, as the DTO installer did.
struct DomainError : p::Error { using p::Error::Error; };
WorldSpace* current(Battle& b) {
    if (!b.world || !b.world->space || !b.world->space->scene.current()) return nullptr;
    return b.world->space.get();
}
std::uint32_t requireU32(std::int64_t v, const char* what) {
    if (v < 0 || std::uint64_t(v) > 0xffffffffULL) invalid(what);
    return std::uint32_t(v);
}
double finiteFloat(PyObject* v) {
    p::Ref number(PyNumber_Float(v));
    const double out = PyFloat_AsDouble(number.get());
    if (!std::isfinite(out)) throw p::Error("descriptor number is not finite");
    return out;
}
// descriptor[name], or null when absent (descriptor.get(name)).
PyObject* entry(PyObject* descriptor, const char* name) { return PyDict_GetItemString(descriptor, name); }
PyObject* required(PyObject* descriptor, const char* name) {
    PyObject* v = entry(descriptor, name);
    if (!v) throw p::Error(std::string("descriptor lacks ") + name);
    return v;
}
int moduleMaterial(std::int64_t value, const char* what) {
    if (value != -1 && (value < 73 || value >= 86)) throw DomainError(what);
    return int(value);
}
std::map<std::int64_t, std::vector<std::int64_t>> dependencies(PyObject* raw) {
    std::map<std::int64_t, std::vector<std::int64_t>> out;
    if (!raw || !p::flag(raw)) return out;
    if (!PyDict_Check(raw)) throw p::Error("Native structure dependency table is not a dictionary");
    Py_ssize_t cursor = 0;
    PyObject *key, *values;
    while (PyDict_Next(raw, &cursor, &key, &values)) {
        const auto module = p::wide(p::Ref(PyNumber_Int(key)).get());
        if (module < 73 || module >= 86) throw DomainError("Native dependency module is invalid");
        p::Ref list(PySequence_List(values));
        if (PyList_GET_SIZE(list.get()) > 13) throw DomainError("Native dependency span is invalid");
        std::vector<std::int64_t> links;
        for (Py_ssize_t i = 0; i < PyList_GET_SIZE(list.get()); ++i) {
            const auto next = p::wide(p::Ref(PyNumber_Int(PyList_GET_ITEM(list.get(), i))).get());
            if (next < 73 || next >= 86) throw DomainError("Native dependency target is invalid");
            links.push_back(next);
        }
        out[module] = std::move(links);
    }
    return out;
}

struct Prepared {
    sw::Key key;
    sw::FrozenIdentity identity;
    bool tree = false;
    sd::Parameters parameters;
    Object fracture, touchdown;   // null for None
};

// The descriptor-derived exact values of one placement (the pinned world snapshot builder). A contradictory
// descriptor cannot grant native admission; the placement stays so unresolved geometry can still be proved.
void readDescriptor(Prepared& out, const Catalog::Placement& placement, PyObject* descriptor, PyObject* cache_module) {
    auto& value = out.identity.instance;
    const auto expected = int(placement.kind);
    bool valid = descriptor && PyDict_Check(descriptor) && entry(descriptor, "type") &&
                 PyObject_RichCompareBool(entry(descriptor, "type"), p::integer(expected).get(), Py_EQ) == 1;
    PyErr_Clear();
    std::map<std::int64_t, double> scaled, reference;
    std::map<std::int64_t, std::vector<std::int64_t>> depends, inverse;
    double correction = 0., tree_health = 0.;
    std::optional<std::array<double, 7>> parameters;
    Object fracture, touchdown;
    auto scale = [&](double health) {
        p::Ref args(Py_BuildValue("(dd)", value.item_scale, health));
        auto result = p::call(cache_module, "scaledDestructibleHealth", args.get());
        return finiteFloat(result.get());
    };
    if (valid) {
        try {
            if (placement.kind == Catalog::Kind::Structure) {
                PyObject* modules = entry(descriptor, "modules");
                valid = modules && PyDict_Check(modules) && PyDict_Size(modules) > 0;
                if (valid) {
                    Py_ssize_t cursor = 0;
                    PyObject *material, *module;
                    while (PyDict_Next(modules, &cursor, &material, &module)) {
                        const auto mat = moduleMaterial(p::wide(p::Ref(PyNumber_Int(material)).get()), "Native health material is invalid");
                        p::Ref health_key(PyString_FromString("health"));
                        p::Ref raw(PyObject_GetItem(module, health_key.get()));
                        const double health = finiteFloat(raw.get());
                        reference[mat] = health;
                        scaled[mat] = scale(health);
                    }
                    depends = dependencies(entry(descriptor, "destroyDepends"));
                    inverse = dependencies(entry(descriptor, "inversedDestroyDepends"));
                }
            } else if (placement.kind == Catalog::Kind::Tree) {
                tree_health = finiteFloat(required(descriptor, "health"));
                std::array<double, 7> values{};
                const char* names[] = {"height", "mass", "springAngle", "springStiffnes", "springResist", "airResist", "buryDepth"};
                for (unsigned i = 0; i < 7; ++i) values[i] = finiteFloat(required(descriptor, names[i]));
                parameters = values;
                if (PyObject* v = entry(descriptor, "fractureEffect")) fracture = borrow(v);
                if (PyObject* v = entry(descriptor, "touchdownEffect")) touchdown = borrow(v);
            } else {
                const double health = finiteFloat(required(descriptor, "health"));
                reference[-1] = health;
                scaled[-1] = scale(health);
                correction = finiteFloat(required(descriptor, "kineticDamageCorrection"));
            }
        } catch (const DomainError&) {
            throw;
        } catch (const std::exception&) {
            // KeyError/TypeError/ValueError while reading the descriptor: no native admission.
            PyErr_Clear();
            valid = false;
        }
    }
    if (!valid) {
        scaled.clear(); reference.clear(); depends.clear(); inverse.clear();
        correction = tree_health = 0.;
        parameters.reset();
        fracture = Object();
        touchdown = Object();
    }
    value.scaled_health = std::move(scaled);
    value.reference_health = std::move(reference);
    value.dependencies = std::move(depends);
    value.inverse_dependencies = std::move(inverse);
    value.kinetic_correction = correction;
    value.tree_health = tree_health;
    value.descriptor_valid = valid;
    if (parameters) {
        const auto& v = *parameters;
        out.parameters.height = v[0]; out.parameters.mass = v[1]; out.parameters.spring_angle = v[2];
        out.parameters.stiffness = v[3]; out.parameters.spring_resist = v[4]; out.parameters.air_resist = v[5];
        out.parameters.bury_depth = v[6];
    }
    out.fracture = std::move(fracture);
    out.touchdown = std::move(touchdown);
}

// Every catalog identity, validated completely before the fresh registry is touched.
void install(Battle& b, WorldSpace& owner, const Catalog* catalog) {
    auto& registry = owner.registry;
    if (!registry.instances.empty() || !registry.baked_instances.empty()) invalid("Native world installation requires a fresh space owner");
    auto area = py::attr(b.engine.modules.get(), "area_destructibles");
    auto cache_module = py::attr(b.engine.modules.get(), "destructibles_cache");
    auto cache = py::optional(area.get(), "g_cache");
    if (cache.get() == Py_None) invalid("Native world installation requires the #1513 resource cache");
    const double unit = p::scalar(py::attr(cache.get(), "unitVehicleMass").get());
    if (!(unit > 0.)) invalid("Native unit vehicle mass is invalid");
    std::vector<Prepared> prepared;
    if (catalog) {
        std::map<std::string, Object> descriptors;
        for (const auto& row : catalog->placements) {
            const auto& placement = row.second;
            if (row.first.second > 0xffffu) invalid("Native world identity exceeds the #1513 slot width");
            if (placement.descriptor.empty()) invalid("Native world descriptor filename is empty");
            if (!(placement.item_scale > 0.)) invalid("Native world item scale is invalid");
            auto found = descriptors.find(placement.descriptor);
            if (found == descriptors.end()) {
                p::Ref name(PyString_FromStringAndSize(placement.descriptor.data(), Py_ssize_t(placement.descriptor.size())));
                PyObject* raw = PyObject_CallMethod(cache.get(), const_cast<char*>("getDescByFilename"), const_cast<char*>("O"), name.get());
                if (!raw) PyErr_Clear();
                else if (raw == Py_None) { Py_DECREF(raw); raw = nullptr; }
                found = descriptors.emplace(placement.descriptor, Object(raw)).first;
            }
            Prepared item;
            item.key = row.first;
            item.tree = placement.kind == Catalog::Kind::Tree;
            auto& value = item.identity.instance;
            value.kind = sw::Kind(int(placement.kind));
            value.admission = sw::Admission::Pending;
            value.filename = placement.descriptor;
            value.boxes = placement.boxes;
            for (const auto& box : value.boxes) moduleMaterial(box.material, "Native world OBB material is invalid");
            value.item_scale = placement.item_scale;
            value.broad_phase_margin = placement.margin;
            value.origin = {double(placement.signature[0]) / Catalog::quantization, double(placement.signature[1]) / Catalog::quantization,
                            double(placement.signature[2]) / Catalog::quantization};
            readDescriptor(item, placement, found->second.get(), cache_module.get());
            if (!item.tree) {
                const auto& resource = catalog->resources.at(placement.resource);
                for (std::size_t i = 0; i < resource.boxes.size(); ++i) {
                    if (placement.kind != Catalog::Kind::Structure && placement.box_index && i != *placement.box_index) continue;
                    const auto& box = resource.boxes[i];
                    sw::Box local;
                    local.center = {(box[0] + box[3]) * .5, (box[1] + box[4]) * .5, (box[2] + box[5]) * .5};
                    local.axes = {{(box[3] - box[0]) * .5, 0., 0.}, {0., (box[4] - box[1]) * .5, 0.}, {0., 0., (box[5] - box[2]) * .5}};
                    local.material = resource.materials[i];
                    item.identity.local_boxes.push_back(std::move(local));
                }
                if (!resource.retained.empty())
                    value.retained_collision = placement.kind == Catalog::Kind::Structure ||
                                               (placement.box_index && resource.retained.count(*placement.box_index));
            }
            for (unsigned i = 0; i < 12; ++i) item.identity.locator_signature[i] = placement.signature[i];
            item.identity.quantization = double(Catalog::quantization);
            item.identity.canonical_ambiguous = !item.tree && catalog->ambiguous.count(placement.signature);
            item.identity.native_pending = true;
            item.identity.category = int(placement.kind);
            prepared.push_back(std::move(item));
        }
    }
    registry.catalog_available = catalog != nullptr;
    registry.unit_vehicle_mass = unit;
    if (catalog) registry.skin.excluded_instances.insert(catalog->excluded.begin(), catalog->excluded.end());
    for (auto& item : prepared) {
        registry.putBaked(item.key, item.identity.instance);
        if (item.tree && item.identity.instance.descriptor_valid)
            owner.backend.installTree(item.key, item.parameters, item.fracture ? item.fracture.get() : Py_None,
                                      item.touchdown ? item.touchdown.get() : Py_None);
        if (!owner.scene.install(item.key, std::move(item.identity))) invalid("Native world identity could not be staged");
    }
    registry.syncBaked();
}

// DestructiblesCache.chunkIDFromPosition over a binary32 Math.Vector3 sample; null outside the chunk grid.
std::optional<std::uint32_t> chunkId(double x, double z) {
    const double gx = std::floor(double(float(x)) * .01) + 127., gz = std::floor(double(float(z)) * .01) + 127.;
    if (!std::isfinite(gx) || !std::isfinite(gz) || gx < 0. || gz < 0. || gx > 253. || gz > 253.) return std::nullopt;
    return (std::uint32_t(gx) << 8) | std::uint32_t(gz);
}
}

void openSpace(Battle& b) {
    if (!b.world) invalid("the native battle world is not started");
    auto& w = *b.world;
    if (w.space) invalid("the native battle space is already open");
    const auto id = p::wide(py::attr(b.engine.avatar.get(), "spaceID").get());
    if (id <= 0 || id > INT_MAX) invalid("Native battle space identity is invalid");
    auto space = BW::SpaceManager::instance().space(BW::SpaceID(id));
    if (!space || space->cleared()) invalid("Native battle space is unavailable");
    auto owner = std::make_shared<WorldSpace>(space, std::uint32_t(id));
    install(b, *owner, w.destructibles_disabled ? nullptr : w.catalog.get());
    if (!owner->backend.installPresenter()) {
        PyErr_Clear();
        invalid("Native destructible presentation could not be installed");
    }
    if (!owner->events.install(b.engine.shell.get())) {
        const auto error = describe(p::Error("Native world event owner could not be installed"));
        invalid(error);
    }
    w.space = std::move(owner);
}

void closeSpace(Battle& b) {
    if (!b.world || !b.world->space) return;
    // Consumers that still hold the lifetime see a retired scene from here on.
    b.world->space->scene.retire();
    b.world->space->backend.retire();
    b.world->space.reset();
}

SceneAccess sceneAccess(Battle& b) {
    SceneAccess out;
    auto* owner = current(b);
    if (!owner) return out;
    out.lifetime = b.world->space;
    out.registry = &owner->registry;
    out.scene = &owner->scene;
    out.pipeline = &owner->pipeline;
    out.backend = &owner->backend;
    out.events = &owner->events;
    return out;
}

void spaceFrame(Battle& b, double now) {
    auto* owner = current(b);
    if (!owner) return;
    if (!std::isfinite(now)) invalid("Native world frame time is invalid");
    const double elapsed = owner->last_frame ? std::max(0., now - *owner->last_frame) : 0.;
    owner->last_frame = now;
    if (!owner->events.mappingChanged(now)) invalid(describe(p::Error("Native world stream remapping failed")));
    owner->backend.runtime().update(elapsed, now);
}

void setTurretSweep(Battle& b, std::function<bool(const TurretSweep&)> veto) {
    if (!b.world || !b.world->space) invalid("the native battle space is not open");
    b.world->space->veto = std::move(veto);
}

void withdrawCatalog(Battle& b) {
    auto* owner = current(b);
    if (!owner || !owner->registry.catalog_available) return;
    owner->registry.catalog_available = false;
    ++owner->registry.generation;
}

bool ingressDestructible(Battle& b, const Value& event) {
    // Canonical rows always reach the space (WorldEvents records a tree logically when no catalog proves it).
    auto* owner = current(b);
    if (!owner) {
        if (!b.world || b.world->destructibles_disabled || !b.featureEnabled("destructible interactions")) return false;
        invalid("#1513 destructible runtime is unavailable");
    }
    p::Ref raw(cs::write(event));
    const bool accepted = owner->events.ingress(raw.get(), b.clock());
    if (!accepted && PyErr_Occurred()) {
        const auto error = describe(p::Error("Canonical native world event was rejected"));
        // A bad canonical row is local to that row on the Bot authority; the visible client reports it.
        if (b.worker) { b.warn("canonical destructible event", error, false); return false; }
        invalid(error);
    }
    return accepted;
}

p::Ref packDestructibleEvents(Battle& b) {
    auto* owner = current(b);
    if (!owner) return p::list();
    return p::Ref(owner->events.pack(b.engine.shell.get()));
}

bool acknowledgeDestructibleEvents(Battle& b, std::uint64_t sequence) {
    auto* owner = current(b);
    if (!owner) return false;
    const bool done = owner->events.ack(sequence);
    if (!done && PyErr_Occurred()) invalid(describe(p::Error("Native destructible acknowledgement failed")));
    return done;
}

QueryStatus ground(Battle& b, const Point& start, const Point& end, Hit& out) {
    auto* owner = current(b);
    return owner ? owner->pipeline.ground(start, end, out) : QueryStatus::Failed;
}
QueryStatus horizontal(Battle& b, const Point& start, const Point& end, Hit& out) {
    auto* owner = current(b);
    return owner ? owner->pipeline.horizontal(start, end, out) : QueryStatus::Failed;
}
QueryStatus navigation(Battle& b, const Point& start, const Point& end, Hit& out) {
    auto* owner = current(b);
    return owner ? owner->pipeline.navigation(start, end, out) : QueryStatus::Failed;
}

std::size_t prewarmTrees(Battle& b, const Point& position, double yaw) {
    auto* owner = current(b);
    if (!owner) return 0;
    // Bind a stale cross-battle stock manager to this space before its chunk loads are consumed.
    auto area = py::optional(b.engine.modules.get(), "area_destructibles");
    auto manager = py::optional(area.get(), "g_destructiblesManager");
    if (manager.get() != Py_None) {
        auto space = p::wide(py::call(manager.get(), "getSpaceID").get());
        if (space != std::int64_t(owner->id)) {
            auto id = p::integer(owner->id);
            py::call(manager.get(), "startSpace", id.get());
        }
    }
    std::set<std::uint32_t> chunks;
    for (double dx : {-chunk_metres, 0., chunk_metres})
        for (double dz : {-chunk_metres, 0., chunk_metres})
            if (const auto id = chunkId(position[0] + dx, position[2] + dz)) chunks.insert(*id);
    if (const auto id = chunkId(position[0] + std::sin(yaw) * forward_probe_metres, position[2] + std::cos(yaw) * forward_probe_metres))
        chunks.insert(*id);
    const std::vector<std::uint32_t> required(chunks.begin(), chunks.end());
    if (!owner->scene.warmTreeChunks(required)) return 0;
    std::size_t ready = 0;
    for (const auto id : required) ready += owner->registry.ready_tree_chunks.count(id);
    return ready;
}

TreeIdentity trustedTreeStatus(Battle& b, std::uint32_t chunk_id, std::uint32_t item_index) {
    auto* owner = current(b);
    if (!owner) return TreeIdentity::Pending;
    return owner->events.trustedTreeStatus({chunk_id, item_index});
}

bool isolated(const Battle& b, std::uint32_t chunk_id, std::uint32_t item_index) {
    if (!b.world || !b.world->space) return false;
    return b.world->space->registry.isolated({chunk_id, item_index});
}

std::map<std::string, std::int64_t> registryCounts(const Battle& b) {
    std::map<std::string, std::int64_t> out;
    if (!b.world || !b.world->space) return out;
    const auto& r = b.world->space->registry;
    out["instances"] = std::int64_t(r.instances.size());
    out["baked_instances"] = std::int64_t(r.baked_instances.size());
    out["contact_bins"] = std::int64_t(r.skin.contact_bins.size());
    out["baked_shot_bins"] = std::int64_t(r.skin.baked_shot_bins.size());
    out["pending"] = std::int64_t(r.pending.size());
    out["journal"] = std::int64_t(r.journal.size());
    out["destroyed"] = std::int64_t(r.skin.destroyed.size());
    out["speculative"] = std::int64_t(r.skin.speculative.size());
    out["isolated_slots"] = std::int64_t(r.skin.isolated_slots.size());
    out["ready_tree_chunks"] = std::int64_t(r.ready_tree_chunks.size());
    out["bodies"] = std::int64_t(b.world->space->backend.runtime().bodies().size());
    out["generation"] = std::int64_t(r.generation);
    return out;
}

namespace {
// The original #1513 effect resources at the native destruction presentation seam (stock_native_destructible_present):
// TerrainEffects, the stock manager's lifetime effects and WWISE light sounds. No destruction law runs here.
p::Ref module(const char* name) { return p::Ref(PyImport_ImportModule(name)); }
// BigWorld.player() outside the account screen, or null.
Object battlePlayer() {
    auto player = py::call(module("BigWorld").get(), "player");
    if (player.get() == Py_None || p::flag(py::call(module("helpers").get(), "isPlayerAccount").get())) return Object();
    return own(std::move(player));
}
p::Ref vector(PyObject* xyz) {
    p::Ref args(PySequence_Tuple(xyz));
    return p::Ref(PyObject_CallObject(py::attr(module("Math").get(), "Vector3").get(), args.get()));
}
PyObject* failed(const std::exception& e) {
    if (!PyErr_Occurred()) PyErr_SetString(PyExc_RuntimeError, e.what());
    return nullptr;
}
PyObject* emit(PyObject*, PyObject* args) {
    PyObject *resource, *category, *position, *direction, *scale, *havok;
    if (!PyArg_ParseTuple(args, "OOOOOO", &resource, &category, &position, &direction, &scale, &havok)) return nullptr;
    try {
        const Object player = battlePlayer();
        if (!player) Py_RETURN_NONE;
        Object variants = borrow(resource);
        if (PyString_Check(resource) || PyUnicode_Check(resource)) {
            const auto name = p::string(resource);
            if (name.empty() || name == "none") Py_RETURN_NONE;
            auto area = module("AreaDestructibles");
            auto cache = py::attr(area.get(), "g_cache");
            variants = own(p::Ref(PyObject_CallMethod(cache.get(), const_cast<char*>("_getEffect"), const_cast<char*>("OOO"), resource, category, Py_False)));
        }
        if (variants.get() == Py_None) Py_RETURN_NONE;
        const auto count = PySequence_Size(variants.get());
        if (count <= 0) throw p::Error("destructible effect variants are empty");
        static std::mt19937 random(std::random_device{}());
        p::Ref effect(PySequence_GetItem(variants.get(), Py_ssize_t(std::uniform_int_distribution<Py_ssize_t>(0, count - 1)(random))));
        auto keywords = p::dict();
        p::set(keywords.get(), "dir", vector(direction));
        p::set(keywords.get(), "scale", p::retain(scale));
        if (havok != Py_None) p::set(keywords.get(), "havokEnabled", p::retain(havok));
        auto effects = py::attr(player.get(), "terrainEffects");
        auto add = py::attr(effects.get(), "addNew");
        auto list = py::attr(effect.get(), "effectsList"), points = py::attr(effect.get(), "keyPoints");
        p::Ref call_args(PyTuple_Pack(4, vector(position).get(), list.get(), points.get(), Py_None));
        p::Ref done(PyObject_Call(add.get(), call_args.get(), keywords.get()));
        Py_RETURN_NONE;
    } catch (const std::exception& e) { return failed(e); }
}
PyObject* stopLifetime(PyObject*, PyObject* args) {
    PyObject *space, *chunk, *item, *module_index;
    if (!PyArg_ParseTuple(args, "OOOO", &space, &chunk, &item, &module_index)) return nullptr;
    try {
        auto area = module("AreaDestructibles");
        auto manager = py::attr(area.get(), "g_destructiblesManager");
        if (manager.get() == Py_None) Py_RETURN_NONE;
        if (PyObject_RichCompareBool(py::call(manager.get(), "getSpaceID").get(), space, Py_EQ) != 1) { PyErr_Clear(); Py_RETURN_NONE; }
        auto code = p::Ref(PyObject_CallMethod(area.get(), const_cast<char*>("_encodeModule"), const_cast<char*>("OOO"), chunk, item, module_index));
        auto effects = py::attr(manager.get(), "_DestructiblesManager__lifetimeEffects");
        p::Ref effect(PyObject_CallMethod(effects.get(), const_cast<char*>("pop"), const_cast<char*>("OO"), code.get(), Py_None));
        const Object player = effect.get() != Py_None ? battlePlayer() : Object();
        if (player) {
            auto terrain = py::attr(player.get(), "terrainEffects");
            py::call(terrain.get(), "stop", effect.get());
        }
        Py_RETURN_NONE;
    } catch (const std::exception& e) { return failed(e); }
}
PyObject* removeLightSound(PyObject*, PyObject* args) {
    PyObject *space, *chunk, *item;
    if (!PyArg_ParseTuple(args, "OOO", &space, &chunk, &item)) return nullptr;
    try {
        py::call(module("WWISE").get(), "WG_lightSoundRemove", space, chunk, item);
        Py_RETURN_NONE;
    } catch (const std::exception& e) { return failed(e); }
}
PyMethodDef presentation_methods[] = {
    {"emit", emit, METH_VARARGS, nullptr},
    {"stop_lifetime", stopLifetime, METH_VARARGS, nullptr},
    {"remove_light_sound", removeLightSound, METH_VARARGS, nullptr},
    {nullptr, nullptr, 0, nullptr}};
}

std::array<Object, 3> presentationCallables() {
    std::array<Object, 3> out;
    for (std::size_t i = 0; i < 3; ++i) out[i] = own(p::Ref(PyCFunction_New(&presentation_methods[i], nullptr)));
    return out;
}
}
}
