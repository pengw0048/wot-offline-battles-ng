// Baked map foliage and the visible client's fallen-tree foliage, owned by the native battle world.
#if !defined(WOT_NATIVE_BATTLE_HOST_BUILD)
#include "pch.hpp"
#endif
#include "stock_native_battle_world_parts.hpp"
#include "stock_native_battle_world_imports.hpp"
#include "stock_native_bot_world_adapter.hpp"
#include "stock_native_destructible_backend.hpp"
#include "stock_native_world_events.hpp"
#include <cmath>
#include <fstream>
#include <sstream>

namespace native_battle {
namespace world {
namespace {
namespace trees = stock_native_bot_foliage;
constexpr const char* format_name = "offline-lan-0922-foliage";
constexpr std::int64_t format_version = 4;

std::string shortMapName(std::string map) {
    for (auto& c : map) if (c == '\\') c = '/';
    while (!map.empty() && map.back() == '/') map.pop_back();
    const auto slash = map.rfind('/');
    return slash == std::string::npos ? map : map.substr(slash + 1);
}
bool exists(const std::string& path) { std::ifstream in(path); return in.good(); }
// One JSON document through the stock interpreter's json module; startup-only.
p::Ref readJson(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw p::Error("foliage data is unavailable: " + path);
    std::stringstream text;
    text << in.rdbuf();
    p::Ref json(PyImport_ImportModule("json"));
    p::Ref raw(PyString_FromStringAndSize(text.str().data(), Py_ssize_t(text.str().size())));
    return p::Ref(PyObject_CallMethod(json.get(), const_cast<char*>("loads"), const_cast<char*>("O"), raw.get()));
}
p::Ref item(PyObject* mapping, const char* name) {
    PyObject* v = PyDict_Check(mapping) ? PyDict_GetItemString(mapping, name) : nullptr;
    return p::retain(v ? v : Py_None);
}
double finite(PyObject* v, const char* what) {
    p::Ref number(PyNumber_Float(v));
    const double out = PyFloat_AsDouble(number.get());
    if (!std::isfinite(out)) throw p::Error(std::string(what) + " is not finite");
    return out;
}
std::int64_t whole(PyObject* v) {
    if (PyBool_Check(v) || (!PyInt_Check(v) && !PyLong_Check(v))) throw p::Error("fallen tree foliage profile is invalid");
    return p::wide(v);
}
std::string text(PyObject* v) { return v == Py_None ? std::string() : p::string(p::Ref(PyObject_Str(v)).get()); }

void checkManifest(const std::string& directory, const std::string& map) {
    const auto path = directory + "/manifest.json";
    if (!exists(path)) return;
    auto manifest = readJson(path);
    if (!PyDict_Check(manifest.get()) || text(item(manifest.get(), "format").get()) != std::string(format_name) + "-manifest" ||
        p::wide(p::Ref(PyNumber_Int(item(manifest.get(), "version").get())).get()) != format_version)
        throw p::Error("foliage manifest is incompatible");
    auto records = item(manifest.get(), "maps");
    if (!PyList_Check(records.get())) throw p::Error("foliage manifest record list is invalid");
    PyObject* selected = nullptr;
    for (Py_ssize_t i = 0; i < PyList_GET_SIZE(records.get()); ++i) {
        PyObject* record = PyList_GET_ITEM(records.get(), i);
        if (!PyDict_Check(record) || shortMapName(text(item(record, "map").get())) != map) continue;
        if (selected) throw p::Error("foliage manifest record is duplicated");
        selected = record;
    }
    if (!selected) throw p::Error("foliage manifest has no record for this map");
    if (text(item(selected, "file").get()) != map + ".json") throw p::Error("foliage manifest record is invalid");
    if (!exists(directory + "/" + map + ".json")) throw p::Error("foliage data is unavailable");
}

// foliage/<map>.json version 4, validated exactly as the pinned loader did, into the native tree state.
trees::TreeState readFoliage(PyObject* data, const std::string& map, bool enabled) {
    if (!PyDict_Check(data)) throw p::Error("foliage root is not an object");
    if (text(item(data, "format").get()) != format_name) throw p::Error("unsupported foliage format");
    if (p::wide(p::Ref(PyNumber_Int(item(data, "version").get())).get()) != format_version) throw p::Error("unsupported foliage version");
    if (shortMapName(text(item(data, "map").get())) != map) throw p::Error("foliage map does not match the battle");
    const double cell_size = finite(item(data, "cell_size").get(), "foliage cell size");
    if (cell_size <= 0.) throw p::Error("foliage cell size is invalid");
    trees::TreeState out;
    auto& foliage = out.foliage;
    foliage.enabled = enabled;
    foliage.cell_size = std::max(1., cell_size);
    auto instances = item(data, "instances");
    if (instances.get() != Py_None) {
        p::Ref rows(PySequence_List(instances.get()));
        for (Py_ssize_t i = 0; i < PyList_GET_SIZE(rows.get()); ++i) {
            PyObject* row = PyList_GET_ITEM(rows.get(), i);
            if ((!PyList_Check(row) && !PyTuple_Check(row)) || PySequence_Size(row) != 10) throw p::Error("foliage instance is invalid");
            native_visibility::Volume volume;
            for (Py_ssize_t j = 0; j < 10; ++j) volume.static_row[j] = finite(p::item(row, j).get(), "foliage instance");
            foliage.instances.push_back(volume);
        }
    }
    auto cells = item(data, "cells");
    if (PyDict_Check(cells.get())) {
        Py_ssize_t cursor = 0;
        PyObject *key, *ids;
        while (PyDict_Next(cells.get(), &cursor, &key, &ids)) {
            std::vector<std::size_t> members;
            p::Ref list(PySequence_List(ids));
            for (Py_ssize_t i = 0; i < PyList_GET_SIZE(list.get()); ++i) {
                const auto id = p::wide(p::Ref(PyNumber_Int(PyList_GET_ITEM(list.get(), i))).get());
                if (id < 0 || std::size_t(id) >= foliage.instances.size()) throw p::Error("foliage cell references an invalid instance");
                members.push_back(std::size_t(id));
            }
            const auto name = text(key);
            const auto comma = name.find(',');
            if (comma == std::string::npos) continue;
            foliage.cells.emplace(native_visibility::Cell{std::stoi(name.substr(0, comma)), std::stoi(name.substr(comma + 1))}, std::move(members));
        }
    }
    auto fallen = item(data, "fallen_trees");
    if (!PyList_Check(fallen.get())) throw p::Error("fallen tree foliage profiles are invalid");
    std::set<std::size_t> standing_seen;
    for (Py_ssize_t i = 0; i < PyList_GET_SIZE(fallen.get()); ++i) {
        PyObject* row = PyList_GET_ITEM(fallen.get(), i);
        if (!PyList_Check(row) || PyList_GET_SIZE(row) != 9) throw p::Error("fallen tree foliage profile is invalid");
        const auto chunk = whole(PyList_GET_ITEM(row, 0)), index = whole(PyList_GET_ITEM(row, 1));
        if (chunk < 0 || index < 0 || chunk > 0xffffffffLL || index > 0xffffLL) throw p::Error("fallen tree foliage profile is invalid");
        const trees::Key key{std::uint32_t(chunk), std::uint32_t(index)};
        if (out.profiles.count(key)) throw p::Error("fallen tree foliage wire is duplicated");
        double bounds[6];
        for (unsigned j = 0; j < 6; ++j) bounds[j] = finite(PyList_GET_ITEM(row, j + 2), "fallen tree foliage profile");
        trees::TreeProfile profile;
        for (unsigned j = 0; j < 3; ++j) {
            if (!(bounds[j] < bounds[j + 3])) throw p::Error("fallen tree foliage profile is invalid");
            profile.center[j] = (bounds[j] + bounds[j + 3]) * .5;
            profile.half_sizes[j] = (bounds[j + 3] - bounds[j]) * .5;
        }
        PyObject* standing = PyList_GET_ITEM(row, 8);
        if (standing != Py_None) {
            const auto id = whole(standing);
            if (id < 0 || std::size_t(id) >= foliage.instances.size() || !standing_seen.insert(std::size_t(id)).second)
                throw p::Error("fallen tree standing foliage reference is invalid");
            profile.standing = std::size_t(id);
        }
        out.profiles.emplace(key, profile);
    }
    for (const auto& profile : out.profiles) {
        if (!profile.second.standing) continue;
        for (const auto& cell : foliage.cells)
            if (std::find(cell.second.begin(), cell.second.end(), *profile.second.standing) != cell.second.end())
                out.standing_cells[*profile.second.standing].push_back(cell.first);
    }
    return out;
}

// The visible native scene as the fallen-tree pose source.
struct ScenePoses final : trees::TreePoses {
    stock_native_client_drive::SceneAccess access;
    explicit ScenePoses(stock_native_client_drive::SceneAccess a) : access(std::move(a)) {}
    bool waiting(std::uint32_t chunk) const override { return access.backend->waiting(chunk); }
    bool loaded(std::uint32_t chunk) const override { return access.backend->loaded(chunk); }
    bool pose(trees::Key key, const trees::Point& center, const trees::Point& half, trees::Point& out, std::array<trees::Point, 3>& axes) override {
        if (!access.scene->current()) return false;
        access.scene->refresh(key, *access.registry);
        return access.backend->pose(key, center, half, out, axes);
    }
};
}

void loadFoliage(Battle& b, const std::string& config_dir) {
    auto& w = *b.world;
    w.foliage.reset();
    const auto map = shortMapName(b.config.get("map").resolved().text);
    if (map.empty()) return;
    const auto directory = config_dir + "/foliage";
    checkManifest(directory, map);
    const auto path = directory + "/" + map + ".json";
    if (!exists(path)) return;
    auto data = readJson(path);
    auto foliage = std::make_unique<FallenFoliage>();
    foliage->trees = std::make_unique<trees::Trees>(readFoliage(data.get(), map, b.featureEnabled("foliage camouflage")));
    w.foliage = std::move(foliage);
}

const stock_native_bot_foliage::TreeState* trees(const Battle& b) {
    if (!b.world || !b.world->foliage || !b.world->foliage->trees || !b.featureEnabled("foliage camouflage")) return nullptr;
    return &b.world->foliage->trees->state();
}

bool activateFallenTree(Battle& b, std::uint32_t chunk_id, std::uint32_t item_index) {
    if (item_index > 0xffffu || !b.world || !b.world->foliage || !b.world->foliage->trees) return false;
    return b.world->foliage->trees->activate({chunk_id, item_index});
}

void refreshFallenFoliage(Battle& b, double now, bool force) {
    if (b.worker || !b.world || !b.world->foliage || !b.world->foliage->trees) return;
    auto& owner = *b.world->foliage->trees;
    ScenePoses source(local::sceneAccess(b));
    if (!source.access.scene || !source.access.scene->current()) return;
    if (source.access.events)
        for (const auto& key : source.access.events->takeActivatedTrees()) owner.activate(key);
    if (owner.state().refreshing.empty() || (!force && now + 1.e-9 < owner.state().next_refresh)) return;
    auto area = py::optional(b.engine.modules.get(), "area_destructibles");
    auto manager = py::optional(area.get(), "g_destructiblesManager");
    const bool no_animation = p::flag(py::optional(manager.get(), "forceNoAnimation").get());
    std::set<trees::Key> bodies;
    for (const auto& body : source.access.backend->runtime().bodies()) bodies.insert(body.order.key);
    offline_visibility::Update changes;
    owner.refresh(now, bodies, source, changes, force, no_animation);
}

void foliage(Battle& b, double now) {
    if (!b.worker) {
        local::driveFrame(b, now);
        try {
            refreshFallenFoliage(b, now, false);
        } catch (const std::exception& e) {
            b.warn("native environment fallen foliage", describe(e), false);
        }
        return;
    }
    if (!bots::enabled(b)) throw p::Error("worker foliage needs the native Bot runtime");
    bots::frame(b, now, false);
}
}
}
