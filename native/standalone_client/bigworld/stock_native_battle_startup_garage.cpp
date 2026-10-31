// The frozen garage loadout, descriptors, the map's spawn contract and the player's start identity.
#if !defined(WOT_NATIVE_BATTLE_HOST_BUILD)
#include "pch.hpp"
#endif
#include "stock_native_battle_startup_internal.hpp"
#include "stock_native_battle_remote.hpp"
#include <algorithm>

namespace native_battle {
namespace startup {
namespace {
p::Ref replayRow(Battle& b) {
    auto presentation = module("gui.mods.offline_lan_0922.replay_presentation");
    auto header = py::attr(py::attr(b.engine.client.get(), "reader").get(), "header");
    return py::call(presentation.get(), "recorded_player", header.get());
}
// lan_protocol._canonical_vehicle_compact_descr(raw) decoded to bytes, or None.
p::Ref decodedCompact(PyObject* raw, std::string& encoded) {
    auto protocol = module("gui.mods.offline_lan_0922.lan_protocol");
    auto canonical = py::call(protocol.get(), "_canonical_vehicle_compact_descr", raw);
    if (canonical.get() == Py_None) return p::retain(Py_None);
    encoded = p::string(canonical.get());
    return py::call(module("base64").get(), "b64decode", py::call(canonical.get(), "encode", p::text("ascii").get()).get());
}
p::Ref vehicleDescr(Battle& b, const char* key, PyObject* value) {
    return callKw(py::attr(b.engine.vehicles.get(), "VehicleDescr").get(), {}, {{key, value}});
}
std::string typeName(PyObject* descriptor) {
    auto name = py::optional(py::attr(descriptor, "type").get(), "name");
    return name.get() == Py_None ? std::string() : p::string(str(name.get()).get());
}
// The mounted gui_items.Vehicle on the visible client; a worker never reads its own unrelated garage.
p::Ref garageItem(Battle& b) {
    if (b.worker) return p::retain(Py_None);
    PyObject* current = PyImport_ImportModule("CurrentVehicle");
    if (!current) { PyErr_Clear(); return p::retain(Py_None); }
    p::Ref held(current);
    try {
        auto vehicle = py::attr(current, "g_currentVehicle");
        if (!p::flag(py::call(vehicle.get(), "isPresent").get())) return p::retain(Py_None);
        return py::attr(vehicle.get(), "item");
    } catch (const std::exception&) { PyErr_Clear(); return p::retain(Py_None); }
}
}

std::optional<std::int64_t> outfitSeason(Battle& b) {
    if (!b.startup->arena_type) return {};
    try {
        auto constants = module("items.components.c11n_constants");
        auto season = py::call(py::attr(constants.get(), "SeasonType").get(), "fromArenaKind",
                               py::attr(b.startup->arena_type.get(), "vehicleCamouflageKind").get());
        return asInt(season.get());
    } catch (const std::exception&) { PyErr_Clear(); return {}; }
}

// Copy every garage read a battle needs, before retire_current_player destroys the lobby Account.
void freezeGarage(Battle& b) {
    auto& s = *b.startup;
    if (s.garage_frozen) return;
    auto& g = s.garage;
    if (b.replay) {
        auto row = replayRow(b);
        auto recorded = py::call(row.get(), "get", p::text("effective_params").get());
        if (!p::flag(recorded.get())) replace(recorded, p::dict());
        std::string encoded;
        auto raw = decodedCompact(py::call(row.get(), "get", p::text("vehicle_compact_descr").get()).get(), encoded);
        if (raw.get() == Py_None) throw p::Error("replay local mounted descriptor is missing");
        g.known = g.shells_known = true;
        auto ammo = py::call(recorded.get(), "get", p::text("ammo").get(), p::Ref(PyTuple_New(0)).get());
        p::Ref rows(PyObject_GetIter(ammo.get()));
        while (PyObject* item = PyIter_Next(rows.get())) { p::Ref pair(item); g.shells[asInt(p::item(pair.get(), 0).get())] = asInt(p::item(pair.get(), 1).get()); }
        auto shots = PyObject_GetItem(p::Ref(PyObject_GetItem(recorded.get(), p::text("gun").get())).get(), p::text("shots").get());
        p::Ref shot_list(shots);
        p::Ref shot_rows(PyObject_GetIter(shot_list.get()));
        while (PyObject* item = PyIter_Next(shot_rows.get())) {
            p::Ref shot(item);
            g.shell_order.push_back(asInt(p::Ref(PyObject_GetItem(shot.get(), p::text("compact_descr").get())).get()));
        }
        std::vector<std::int64_t> equipment;
        auto equipped = py::call(recorded.get(), "get", p::text("equipment").get(), p::Ref(PyTuple_New(0)).get());
        p::Ref equipment_rows(PyObject_GetIter(equipped.get()));
        while (PyObject* item = PyIter_Next(equipment_rows.get())) {
            p::Ref entry(item);
            equipment.push_back(asInt(p::Ref(PyObject_GetItem(entry.get(), p::text("compactDescr").get())).get()));
        }
        if (PyErr_Occurred()) throw p::Error("replay loadout");
        g.equipment_ids = equipment;
        g.equipments = g.battle_boosters = g.crew = own(p::Ref(PyTuple_New(0)));
        auto header = py::attr(py::attr(b.engine.client.get(), "reader").get(), "header");
        g.crew_group = asInt(py::call(header.get(), "get", p::text("local_crew_group").get(), p::integer(0).get()).get());
        auto camouflage = py::call(recorded.get(), "get", p::text("camouflage").get());
        g.camouflage_id = own(p::flag(camouflage.get()) ? py::call(camouflage.get(), "get", p::text("camouflage_id").get()) : p::retain(Py_None));
        Value state = fromPython(row.get());
        g.outfit = own(p::text(""));
        if (auto season = outfitSeason(b)) {
            const auto& outfits = field(state, "outfits").resolved();
            const auto& value = outfits.kind == Value::Kind::Object ? outfits.get(std::to_string(*season)).resolved() : outfits;
            if (value.kind == Value::Kind::Text && !value.text.empty()) {
                PyObject* decoded = PyObject_CallMethod(module("base64").get(), const_cast<char*>("b64decode"), const_cast<char*>("s"), value.text.c_str());
                if (decoded) g.outfit = Object(decoded); else PyErr_Clear();
            }
        }
        g.fitting_descr = own(std::move(raw));
        g.fitting_name = p::string(str(p::Ref(PyObject_GetItem(row.get(), p::text("vehicle").get())).get()).get());
        s.garage_frozen = true;
        return;
    }
    auto item = garageItem(b);
    auto equipment = py::optional(item.get(), "equipment");
    auto consumables = py::optional(equipment.get(), "regularConsumables");
    auto boosters = py::optional(equipment.get(), "battleBoosterConsumables");
    g.known = item.get() != Py_None;
    g.shells_known = g.known;
    if (g.known) {
        auto shells = py::optional(item.get(), "shells");
        if (p::flag(shells.get())) {
            p::Ref rows(PyObject_GetIter(shells.get()));
            while (PyObject* raw = PyIter_Next(rows.get())) {
                p::Ref shell(raw);
                try {
                    const std::int64_t compact = asInt(py::attr(shell.get(), "intCD").get());
                    g.shells[compact] = std::max<std::int64_t>(0, asInt(py::attr(shell.get(), "count").get()));
                    g.shell_order.push_back(compact);
                } catch (const std::exception&) { PyErr_Clear(); }
            }
            PyErr_Clear();
        }
    }
    if (consumables.get() != Py_None) {
        std::vector<std::int64_t> ids;
        auto values = py::call(consumables.get(), "getIntCDs", p::integer(0).get());
        p::Ref rows(PyObject_GetIter(values.get()));
        while (PyObject* raw = PyIter_Next(rows.get())) {
            p::Ref id(raw);
            PyObject* converted = p::flag(id.get()) ? PyNumber_Int(id.get()) : PyInt_FromLong(0);
            if (!converted) { PyErr_Clear(); ids.push_back(0); continue; }
            ids.push_back(p::wide(p::Ref(converted).get()));
        }
        if (PyErr_Occurred()) throw p::Error("equipment ids");
        g.equipment_ids = ids;
        g.equipments = own(p::Ref(PySequence_Tuple(py::call(consumables.get(), "getInstalledItems").get())));
    } else g.equipments = own(p::Ref(PyTuple_New(0)));
    g.battle_boosters = own(boosters.get() == Py_None ? p::Ref(PyTuple_New(0))
                                                      : p::Ref(PySequence_Tuple(py::call(boosters.get(), "getInstalledItems").get())));
    auto crew = py::optional(item.get(), "crew");
    g.crew = own(p::Ref(PySequence_Tuple(p::flag(crew.get()) ? crew.get() : p::Ref(PyTuple_New(0)).get())));
    g.crew_group = asInt(py::call(module("gui.mods.offline_lan_0922.crew_voice").get(), "commander_group", g.crew.get()).get());
    // The paint id getClientInvisibility passes to the base law.
    g.camouflage_id = own(p::retain(Py_None));
    auto reader = py::optional(item.get(), "getBonusCamo");
    if (py::callable(reader)) {
        PyObject* camouflage = PyObject_CallFunctionObjArgs(reader.get(), nullptr);
        if (!camouflage) PyErr_Clear();
        else {
            p::Ref held(camouflage);
            if (camouflage != Py_None) g.camouflage_id = own(py::optional(camouflage, "id"));
        }
    }
    // The selected vehicle's native outfit for this arena season, as InventoryRequester parsed it.
    g.outfit = own(p::text(""));
    if (g.known && s.arena_type) {
        try {
            if (auto season = outfitSeason(b)) {
                auto outfit = py::call(item.get(), "getOutfit", p::integer(*season).get());
                if (outfit.get() != Py_None) {
                    auto compact = py::optional(outfit.get(), "strCompactDescr");
                    if (compact.get() != Py_None) g.outfit = own(std::move(compact));
                    else {
                        auto maker = py::optional(outfit.get(), "makeCompDescr");
                        if (py::callable(maker)) g.outfit = own(py::invoke(maker.get()));
                    }
                }
            }
        } catch (const std::exception& e) { log("the garage outfit is unavailable: " + describe(e)); }
    }
    // The mounted compact descriptor carries the fitted modules, devices and camouflage the garage measures.
    g.fitting_descr = own(p::retain(Py_None));
    auto descriptor = py::optional(item.get(), "descriptor");
    auto maker = py::optional(descriptor.get(), "makeCompactDescr");
    if (py::callable(maker)) {
        try {
            g.fitting_descr = own(py::invoke(maker.get()));
            g.fitting_name = typeName(descriptor.get());
        } catch (const std::exception&) { PyErr_Clear(); g.fitting_descr = own(p::retain(Py_None)); }
    }
    // TAB's personal mission is frozen before the Account-to-Avatar transition.
    if (!b.worker && g.known && text(field(b.start, "battle_mode"), "regular") == "regular") {
        try {
            auto state = compat::garageState(b);
            if (state.get() != Py_None) {
                auto ids = py::call(module("gui.mods.offline_lan_0922.battle_missions").get(), "selected_mission_ids",
                                    py::call(state.get(), "snapshot").get(), descriptor.get());
                p::Ref rows(PyObject_GetIter(ids.get()));
                std::string listed;
                while (PyObject* raw = PyIter_Next(rows.get())) {
                    p::Ref id(raw);
                    g.personal_mission_ids.push_back(asInt(id.get()));
                    listed += (listed.empty() ? "" : ", ") + std::to_string(g.personal_mission_ids.back());
                }
                if (PyErr_Occurred()) throw p::Error("personal missions");
                log("battle personal mission ids=[" + listed + "]");
            }
        } catch (const std::exception& e) { b.warn("battle personal mission", describe(e)); }
    }
    s.garage_frozen = true;
}

const GarageLoadout& garage(Battle& b) {
    freezeGarage(b);
    return b.startup->garage;
}

// The player's own descriptor with the garage fitting on it.
p::Ref localBattleDescriptor(Battle& b, const std::string& vehicle_name) {
    if (b.replay) {
        auto row = replayRow(b);
        std::string encoded;
        auto raw = decodedCompact(py::call(row.get(), "get", p::text("vehicle_compact_descr").get()).get(), encoded);
        if (raw.get() == Py_None) throw p::Error("replay local mounted descriptor is missing");
        auto descriptor = vehicleDescr(b, "compactDescr", raw.get());
        if (typeName(descriptor.get()) != vehicle_name) throw p::Error("replay local descriptor does not match recorded vehicle");
        return descriptor;
    }
    const auto& g = garage(b);
    if (g.fitting_descr && g.fitting_descr.get() != Py_None && g.fitting_name == vehicle_name) {
        try { return vehicleDescr(b, "compactDescr", g.fitting_descr.get()); }
        catch (const std::exception& e) { throw p::Error("the mounted vehicle descriptor is unreadable: " + describe(e)); }
    }
    if (!b.worker) throw p::Error("the mounted vehicle descriptor does not match " + vehicle_name);
    return vehicleDescr(b, "typeName", p::text(vehicle_name).get());
}

// One shared prepared descriptor per vehicle type; this round's proven types substitute for an unloadable one.
p::Ref descriptorFor(Battle& b, const std::string& vehicle_name) {
    auto& s = *b.startup;
    auto cached = s.descriptors.find(vehicle_name);
    if (cached != s.descriptors.end()) return p::retain(cached->second.get());
    auto configuration = module("gui.mods.offline_lan_0922.vehicle_configuration");
    auto retired = py::attr(configuration.get(), "RETIRED_BOT_VEHICLES");
    std::vector<std::string> candidates;
    auto offer = [&](const std::string& name) {
        if (name.empty() || std::find(candidates.begin(), candidates.end(), name) != candidates.end()) return;
        // Retired tanks stay legal as the requested human vehicle, never as another slot's substitute.
        if (name != vehicle_name && PySequence_Contains(retired.get(), p::text(name).get()) == 1) return;
        candidates.push_back(name);
    };
    offer(vehicle_name);
    for (const auto& name : s.prepared_names) offer(name);
    offer(text(field(b.config, "vehicle")));
    std::string failure;
    for (const auto& candidate : candidates) {
        try {
            auto descriptor = vehicleDescr(b, "typeName", p::text(candidate).get());
            py::call(configuration.get(), "install_top_modules", descriptor.get());
            auto compact = py::call(descriptor.get(), "makeCompactDescr");
            replace(descriptor, vehicleDescr(b, "compactDescr", compact.get()));
            if (typeName(descriptor.get()) != candidate) throw p::Error("#1513 top vehicle descriptor type mismatch for " + candidate);
            auto prepared = remote::prepareDescriptor(b, descriptor.get());
            s.descriptors[vehicle_name] = borrow(prepared.get());
            if (std::find(s.prepared_names.begin(), s.prepared_names.end(), candidate) == s.prepared_names.end()) s.prepared_names.push_back(candidate);
            return prepared;
        } catch (const std::exception& e) {
            failure = describe(e);
            if (s.unusable_reported.insert(candidate).second) log("vehicle " + candidate + " is unusable in this client: " + failure);
        }
    }
    throw p::Error("#1513 vehicle '" + vehicle_name + "' has no loadable substitute: " + failure);
}

// The exact mounted human descriptor donated at join time.
p::Ref playerDescriptor(Battle& b, const Value& state) {
    std::string encoded;
    p::Ref raw_value(toPython(field(state, "vehicle_compact_descr")));
    auto protocol = module("gui.mods.offline_lan_0922.lan_protocol");
    auto canonical = py::call(protocol.get(), "_canonical_vehicle_compact_descr", raw_value.get());
    if (canonical.get() == Py_None) throw p::Error("player mounted vehicle descriptor is unavailable");
    encoded = p::string(canonical.get());
    const std::string key = "player\x1f" + encoded;
    auto& s = *b.startup;
    auto cached = s.descriptors.find(key);
    if (cached != s.descriptors.end()) return p::retain(cached->second.get());
    p::Ref descriptor(p::retain(Py_None));
    try {
        auto raw = py::call(module("base64").get(), "b64decode", py::call(canonical.get(), "encode", p::text("ascii").get()).get());
        replace(descriptor, vehicleDescr(b, "compactDescr", raw.get()));
    } catch (const std::exception& e) { throw p::Error("player mounted vehicle descriptor is unreadable: " + describe(e)); }
    const std::string expected = text(field(state, "vehicle"));
    if (expected.empty() || typeName(descriptor.get()) != expected) throw p::Error("player mounted vehicle descriptor type mismatch");
    auto prepared = remote::prepareDescriptor(b, descriptor.get());
    s.descriptors[key] = borrow(prepared.get());
    return prepared;
}

// The navigation graph's per-map spawn contract.
void prepareSpawn(Battle& b) {
    auto& s = *b.startup;
    PyObject* graph = s.navigation_graph.get();
    if (!graph || !PyDict_Check(graph)) throw p::Error("validated navigation graph is required for spawn planning");
    SpawnPlan plan;
    PyObject* map = PyDict_GetItemString(graph, "map");
    plan.map = map && map != Py_None ? p::string(str(map).get()) : "<unknown>";
    PyObject* formations = PyDict_GetItemString(graph, "spawn_formations");
    if (!formations || !PyDict_Check(formations)) throw p::Error("spawn formations are missing for map " + plan.map);
    for (int team = 1; team <= 2; ++team) {
        PyObject* raw = PyDict_GetItemString(formations, std::to_string(team).c_str());
        if (!raw) raw = PyDict_GetItem(formations, p::integer(team).get());
        if (!raw || (!PyList_Check(raw) && !PyTuple_Check(raw)) || PySequence_Size(raw) != 15)
            throw p::Error("map " + plan.map + " team " + std::to_string(team) + " requires exactly 15 spawn slots");
        for (Py_ssize_t slot = 0; slot < 15; ++slot) {
            auto point = p::item(raw, slot);
            if ((!PyList_Check(point.get()) && !PyTuple_Check(point.get())) || PySequence_Size(point.get()) != 4)
                throw p::Error("map " + plan.map + " team " + std::to_string(team) + " spawn slot " + std::to_string(slot) + " is invalid");
            for (Py_ssize_t axis = 0; axis < 4; ++axis) {
                const double value = asFloat(p::item(point.get(), axis).get());
                if (!std::isfinite(value)) throw p::Error("map " + plan.map + " spawn slot is not finite");
                plan.formations[team - 1][slot][axis] = value;
            }
        }
    }
    // Every slot keeps 9 m from every other; the contract was validated when baked.
    for (int team = 0; team < 2; ++team)
        for (int slot = 0; slot < 15; ++slot)
            for (int other_team = 0; other_team <= team; ++other_team)
                for (int other = 0; other < (other_team == team ? slot : 15); ++other) {
                    const auto& a = plan.formations[team][slot];
                    const auto& c = plan.formations[other_team][other];
                    if (std::hypot(a[0] - c[0], a[2] - c[2]) < 9.)
                        throw p::Error("map " + plan.map + " spawn overlap: team " + std::to_string(team + 1) + " slot " + std::to_string(slot));
                }
    PyObject* bases = PyDict_GetItemString(graph, "objective_bases");
    if (!bases || (!PyList_Check(bases) && !PyTuple_Check(bases)) || PySequence_Size(bases) != 2)
        throw p::Error("objective bases are missing for map " + plan.map);
    for (Py_ssize_t team = 0; team < 2; ++team) {
        auto point = p::item(bases, team);
        plan.bases[team] = {asFloat(p::item(point.get(), 0).get()), asFloat(p::item(point.get(), 1).get())};
    }
    PyObject* radii = PyDict_GetItemString(graph, "objective_base_radii");
    if (radii && radii != Py_None) {
        std::array<double, 2> values{asFloat(p::item(radii, 0).get()), asFloat(p::item(radii, 1).get())};
        if (values[0] <= 0. || values[1] <= 0.) throw p::Error("objective base radius must be positive");
        plan.base_radii = values;
    }
    s.spawn = plan;
}

// SPGs take the back of each formation; depth follows the axis between the two spawn centroids.
void prepareSpawnAssignments(Battle& b) {
    auto& s = *b.startup;
    std::set<std::pair<std::int64_t, std::int64_t>> artillery;
    const auto& assignments = bots::vehicleAssignments(b);
    for (const char* kind : {"players", "bots"}) {
        for (const auto& row : field(b.start, kind).resolved().array) {
            if (row.resolved().kind != Value::Kind::Object || integer(field(row, "id")) <= 0) continue;
            const std::pair<std::int64_t, std::int64_t> key{integer(field(row, "team"), 1), integer(field(row, "slot"))};
            std::string name;
            if (std::string(kind) == "bots") {
                auto found = assignments.find(key);
                if (found != assignments.end()) name = found->second;
            } else name = text(field(row, "vehicle"));
            if (name.empty()) continue;
            auto descriptor = descriptorFor(b, name);
            auto tags = py::optional(py::attr(descriptor.get(), "type").get(), "tags");
            if (p::flag(tags.get()) && PySequence_Contains(tags.get(), p::text("SPG").get()) == 1) artillery.insert(key);
        }
    }
    s.spawn_slots.clear();
    s.spawn_cache.clear();
    if (!s.spawn) return;
    const auto& f = s.spawn->formations;
    std::array<std::array<double, 2>, 2> centres{};
    for (int team = 0; team < 2; ++team) {
        for (const auto& point : f[team]) { centres[team][0] += point[0] / 15.; centres[team][1] += point[2] / 15.; }
    }
    for (int team = 1; team <= 2; ++team) {
        std::vector<std::int64_t> spg;
        for (std::int64_t slot = 0; slot < 15; ++slot) if (artillery.count({team, slot})) spg.push_back(slot);
        std::map<std::int64_t, std::int64_t> mapping;
        for (std::int64_t slot = 0; slot < 15; ++slot) mapping[slot] = slot;
        if (!spg.empty()) {
            const auto& own_centre = centres[team - 1];
            const auto& enemy = centres[2 - team];
            const double dx = enemy[0] - own_centre[0], dz = enemy[1] - own_centre[1];
            std::vector<std::int64_t> rear(15);
            for (std::int64_t i = 0; i < 15; ++i) rear[i] = i;
            std::stable_sort(rear.begin(), rear.end(), [&](std::int64_t a, std::int64_t c) {
                const double da = (f[team - 1][a][0] - own_centre[0]) * dx + (f[team - 1][a][2] - own_centre[1]) * dz;
                const double dc = (f[team - 1][c][0] - own_centre[0]) * dx + (f[team - 1][c][2] - own_centre[1]) * dz;
                return da < dc || (da == dc && a < c);
            });
            std::set<std::int64_t> reserved(rear.begin(), rear.begin() + std::ptrdiff_t(spg.size()));
            for (std::size_t i = 0; i < spg.size(); ++i) mapping[spg[i]] = rear[i];
            std::vector<std::int64_t> displaced, vacated;
            for (auto slot : reserved) if (!artillery.count({team, slot})) displaced.push_back(slot);
            for (auto slot : spg) if (!reserved.count(slot)) vacated.push_back(slot);
            for (std::size_t i = 0; i < displaced.size() && i < vacated.size(); ++i) mapping[displaced[i]] = vacated[i];
        }
        for (const auto& entry : mapping) s.spawn_slots[{team, entry.first}] = entry.second;
    }
}

std::pair<Point, double> formationPose(Battle& b, std::int64_t team, std::int64_t slot) {
    auto& s = *b.startup;
    const std::pair<std::int64_t, std::int64_t> key{team, slot};
    auto cached = s.spawn_cache.find(key);
    if (cached != s.spawn_cache.end()) return cached->second;
    if (!s.spawn) throw p::Error("spawn planning is unavailable");
    auto physical = s.spawn_slots.find(key);
    const std::int64_t index = physical == s.spawn_slots.end() ? slot : physical->second;
    if (team != 1 && team != 2) throw p::Error("map " + s.spawn->map + " has no spawn team " + std::to_string(team));
    if (index < 0 || index >= 15) throw p::Error("map " + s.spawn->map + " team " + std::to_string(team) + " has no spawn slot " + std::to_string(index));
    const auto& point = s.spawn->formations[team - 1][index];
    const std::pair<Point, double> result{{point[0], point[1], point[2]}, point[3]};
    s.spawn_cache[key] = result;
    return result;
}

std::pair<Point, double> worldPose(Battle& b, const Value& state) {
    std::pair<Point, double> placed;
    if (field(state, "world_pose").truth()) {
        auto number = [&](const char* name) { const double v = field(state, name).numeric(0.); return std::isfinite(v) ? v : 0.; };
        placed = {{number("x"), number("y"), number("z")}, number("yaw")};
    } else {
        return formationPose(b, integer(field(state, "team"), 1), integer(field(state, "slot")));
    }
    if (auto ground = groundY(b, placed.first[0], placed.first[2], placed.first[1], b.startup->navigation_graph.get() == Py_None))
        placed.first[1] = *ground;
    return placed;
}

// Upward-facing support below rejected overhead surfaces: the near-hull band, then a layered wide query.
std::optional<double> groundY(Battle& b, double x, double z, double hint, bool allow_wide) {
    auto filter = world::groundFilter(b, x, z);
    auto vector = [&](double y) {
        return py::call(b.engine.math.get(), "Vector3", p::number(x).get(), p::number(y).get(), p::number(z).get());
    };
    auto collide = [&](double from, double to) { return world::collideDown(b, vector(from).get(), vector(to).get(), filter.get()); };
    double start_y = hint + 8., end_y = hint - 30.;
    for (int layer = 0; layer < 4; ++layer) {
        try {
            auto hit = collide(start_y, end_y);
            if (hit.get() == Py_None) break;
            const double value = asFloat(py::attr(p::item(hit.get(), 0).get(), "y").get());
            if (-14. < value - hint && value - hint < 6. && asFloat(py::attr(p::item(hit.get(), 1).get(), "y").get()) > 0.) return value;
            const double next_y = value - 0.05;
            if (!(end_y < next_y && next_y < start_y)) break;
            start_y = next_y;
        } catch (const std::exception&) { PyErr_Clear(); break; }
    }
    if (!allow_wide) return {};
    double from_y = std::max(1000., hint + 50.);
    std::optional<double> height;
    for (int layer = 0; layer < 4; ++layer) {
        auto hit = collide(from_y, -1000.);
        if (hit.get() == Py_None) return {};
        height = asFloat(py::attr(p::item(hit.get(), 0).get(), "y").get());
        auto below = collide(*height - 0.4, -1000.);
        if (below.get() == Py_None || *height - asFloat(py::attr(p::item(below.get(), 0).get(), "y").get()) < 2.5) return height;
        from_y = *height - 0.4;
    }
    return height;
}

// The player's start row, else one built from the LAN client.
Value localState(Battle& b) {
    for (const auto& row : field(b.start, "players").resolved().array)
        if (integer(field(row, "id"), -1) == b.local_player) return row.resolved();
    PyObject* client = b.engine.client.get();
    auto get = [&](const char* name) { return fromPython(py::attr(client, name).get()); };
    Value out;
    out.kind = Value::Kind::Object;
    out.object["id"] = get("player_id");
    out.object["name"] = get("name");
    out.object["vehicle"] = get("vehicle");
    out.object["team"] = get("team");
    out.object["slot"] = get("slot");
    out.object["health"] = get("max_health");
    out.object["max_health"] = get("max_health");
    Value alive; alive.kind = Value::Kind::Boolean; alive.boolean = true;
    out.object["alive"] = alive;
    auto marks = py::optional(client, "marks_on_gun");
    out.object["marks_on_gun"] = fromPython(marks.get() == Py_None ? p::integer(0).get() : marks.get());
    return out;
}

// The server-published effective parameters of the local tank; the server-echo cache covers a recovery snapshot.
Value acceptedEffectiveParams(Battle& b, const Value& local_state) {
    Value source = field(local_state, "effective_params");
    if (source.resolved().kind == Value::Kind::Null) {
        auto published = py::optional(b.engine.client.get(), "_published_player_effective_params");
        if (PyDict_Check(published.get())) {
            PyObject* row = PyDict_GetItem(published.get(), p::integer(b.local_player).get());
            if (row) source = fromPython(row);
        }
    }
    Value snapshot = params::canonical(b, source);
    if (snapshot.resolved().kind == Value::Kind::Null) throw p::Error("player effective vehicle parameters are unavailable");
    return snapshot;
}

// Conscious carriers of one skill in the accepted round snapshot.
std::int64_t skillCount(Battle& b, const char* skill) {
    if (b.worker || b.startup->effective_params.resolved().kind == Value::Kind::Null) return 0;
    auto crew = critical::localCrewCritical(b);
    return params::livingSkillCount(b, b.startup->effective_params, skill, crew.get());
}
}
}
