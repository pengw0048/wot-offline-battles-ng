// The round lifecycle: start, stock map creation, the player's Vehicle, readiness, the live boundary, stop/fail/cleanup.
#if !defined(WOT_NATIVE_BATTLE_HOST_BUILD)
#include "pch.hpp"
#endif
#include "stock_native_battle_startup_internal.hpp"
#include "stock_native_battle_local.hpp"
#include "stock_native_battle_vehicles.hpp"
#include "stock_native_battle_projectiles.hpp"
#include "stock_native_battle_remote.hpp"
#include <algorithm>
#include <cstdlib>

namespace native_battle {
namespace startup {
namespace {
constexpr double kFrameSeconds = 0.0;
constexpr double kSpaceVisibilityCheckSeconds = 0.5;
constexpr double kPrebattleSeconds = 10.0, kBattleSeconds = 900.0;
constexpr const char* kStandardGameplay = "ctf";

Value boolean(bool v) { Value x; x.kind = Value::Kind::Boolean; x.boolean = v; return x; }
Value number(double v) { Value x; x.kind = Value::Kind::Number; x.number = v; return x; }
Value& writable(Value& v) {
    if (v.reference) { Value copy = *v.reference; v = std::move(copy); }
    if (v.kind != Value::Kind::Object) v.kind = Value::Kind::Object;
    return v;
}
PyObject* modules(Battle& b) { return b.engine.modules.get(); }
p::Ref runtimeModule(Battle& b, const char* name) { return need(modules(b), name); }
double configNumber(const Battle& b, const char* name, double fallback) {
    const auto& v = field(b.config, name);
    const double n = v.resolved().kind == Value::Kind::Null ? fallback : v.numeric(fallback);
    return std::isfinite(n) ? n : fallback;
}
// A native Python callable around one C++ body for stock patch points.
struct Native {
    PyObject_HEAD
    std::function<PyObject*(PyObject*, PyObject*)>* body;
};
void nativeDealloc(PyObject* o) { delete reinterpret_cast<Native*>(o)->body; PyObject_Del(o); }
PyObject* nativeCall(PyObject* o, PyObject* args, PyObject* kwargs) {
    try { return (*reinterpret_cast<Native*>(o)->body)(args, kwargs); }
    catch (const std::exception& e) { if (!PyErr_Occurred()) PyErr_SetString(PyExc_RuntimeError, e.what()); return nullptr; }
}
PyTypeObject NativeType = {PyVarObject_HEAD_INIT(nullptr, 0) "offline_native_battle.Native"};
// A method-like descriptor: class patches need an unbound function, so wrap it in staticmethod at the patch site.
p::Ref native(std::function<PyObject*(PyObject*, PyObject*)> body) {
    if (!NativeType.tp_call) {
        NativeType.tp_basicsize = sizeof(Native);
        NativeType.tp_dealloc = nativeDealloc;
        NativeType.tp_call = nativeCall;
        NativeType.tp_flags = Py_TPFLAGS_DEFAULT;
        if (PyType_Ready(&NativeType) < 0) throw p::Error("native callable type failed");
    }
    auto* o = PyObject_New(Native, &NativeType);
    if (!o) throw p::Error("native callable allocation failed");
    o->body = new std::function<PyObject*(PyObject*, PyObject*)>(std::move(body));
    return p::Ref(reinterpret_cast<PyObject*>(o));
}
// The raw class-dict attribute, avoiding Python 2 bound-method wrappers.
p::Ref classAttribute(PyObject* type, const char* name) {
    auto dict = py::attr(type, "__dict__");
    PyObject* v = PyMapping_Check(dict.get()) ? PyMapping_GetItemString(dict.get(), const_cast<char*>(name)) : nullptr;
    if (v) return p::Ref(v);
    PyErr_Clear();
    return p::retain(Py_None);
}
// Restore `name` on `owner` only while it is still our patch.
void restorePatch(PyObject* owner, const char* name, PyObject* ours, PyObject* original, bool had) {
    if (classAttribute(owner, name).get() != ours) return;
    if (had) PyObject_SetAttrString(owner, name, original);
    else if (PyObject_DelAttrString(owner, name) < 0) PyErr_Clear();
}
std::int64_t guiSpaceId(Battle& b) {
    auto state = py::optional(runtimeModule(b, "app_loader").get(), "_AppLoader__state");
    auto getter = py::optional(state.get(), "getSpaceID");
    if (!py::callable(getter)) throw p::Error("actual battle GUI state is unavailable");
    return asInt(py::invoke(getter.get()).get());
}
std::int64_t spaceIdConstant(Battle& b, const char* name) { return asInt(py::attr(runtimeModule(b, "gui_global_space_id").get(), name).get()); }
p::Ref engineVector(Battle& b, const Point& at) {
    return py::call(b.engine.math.get(), "Vector3", p::number(at[0]).get(), p::number(at[1]).get(), p::number(at[2]).get());
}
}

p::Ref callKw(PyObject* fn, std::initializer_list<PyObject*> positional, std::initializer_list<std::pair<const char*, PyObject*>> keywords) {
    p::Ref args(PyTuple_New(Py_ssize_t(positional.size())));
    Py_ssize_t i = 0;
    for (PyObject* v : positional) { Py_INCREF(v); PyTuple_SET_ITEM(args.get(), i++, v); }
    auto kw = p::dict();
    for (const auto& item : keywords) PyDict_SetItemString(kw.get(), item.first, item.second);
    return p::Ref(PyObject_Call(fn, args.get(), kw.get()));
}

std::string stateName(const Battle* b) {
    if (!b) return "idle";
    switch (b->phase) {
    case Phase::Idle: return "idle";
    case Phase::Loading: return b->startup && b->startup->stage == Stage::LoadingEntities ? "loading_entities" : "creating_map";
    case Phase::Running: case Phase::Finished: return "running";
    case Phase::Failed: return "failed";
    case Phase::Stopped: return "stopped";
    case Phase::Leaving: return "leaving";
    }
    return "idle";
}

double prebattleSeconds(const Battle& b) { return std::max(0., configNumber(b, "prebattleCountdownSeconds", kPrebattleSeconds)); }
double battleSeconds(const Battle& b) { return std::max(1., configNumber(b, "battleDurationSeconds", kBattleSeconds)); }

void scheduleFrame(Battle& b, double delay) {
    b.startup->frame_callback = own(schedule(b, delay, [](Battle& battle) {
        battle.startup->frame_callback = Object();
        frame(battle);
    }));
}

// ---------------------------------------------------------------------------------------------------- start

bool start(PyObject* shell, PyObject* runtime_modules, PyObject* config, PyObject* message, PyObject* client, PyObject* on_local_leave) {
    if (Battle* previous = current()) {
        if (previous->phase != Phase::Idle && previous->phase != Phase::Stopped && previous->phase != Phase::Failed) return false;
        if (previous->startup && previous->startup->restore_pending) return false;
    }
    if (client == Py_None) { PyErr_SetString(PyExc_ValueError, "LAN client is required"); throw p::Error("LAN client is required"); }
    auto battle = std::make_unique<Battle>();
    Battle& b = *battle;
    b.engine.shell = borrow(shell);
    b.engine.modules = borrow(runtime_modules);
    b.engine.bigworld = own(need(runtime_modules, "bigworld"));
    b.engine.constants = own(need(runtime_modules, "constants"));
    b.engine.math = own(need(runtime_modules, "math"));
    b.engine.vehicles = own(need(runtime_modules, "vehicles"));
    b.engine.compatibility = own(need(runtime_modules, "compatibility"));
    b.engine.client = borrow(client);
    b.config = fromPython(PyDict_Check(config) ? config : p::dict().get());
    b.worker = field(b.config, "worker_mode").truth();
    b.replay = p::flag(py::optional(client, "is_offline_replay").get());
    if (b.worker) {
        writable(b.config).object["native_remote_vehicles"] = boolean(false);
        writable(b.config).object["bot_track_animation"] = boolean(false);
    }
    b.start = fromPython(PyDict_Check(message) ? message : p::dict().get());
    b.result = field(b.start, "battle_result");
    b.local_player = asInt(py::attr(client, "player_id").get());
    b.round_id = integer(field(b.start, "round_id"));
    b.startup = std::make_unique<Startup>();
    b.diagnostics = std::make_unique<Diagnostics>();
    b.local = std::make_unique<LocalVehicle>();
    b.roster = std::make_unique<Vehicles>();
    b.vehicles->local_player_id = b.local_player;
    b.startup->on_local_leave = borrow(on_local_leave);
    b.live = false;
    b.phase = Phase::Loading;
    b.startup->stage = Stage::CreatingMap;
    install(std::move(battle));
    try {
        compat::install(b);
        local::applyPhysicsTuning(b, field(b.config, "physics_tuning"));
        projectiles::applyHeTuning(b, field(b.config, "he_tuning"));
        projectiles::start(b);
        b.startup->deadline = b.clock() + configNumber(b, "startupTimeoutSeconds", 30.);
        // The standard (CTF) arena of this map.
        const std::string map_name = text(field(b.config, "map"));
        auto maps = module("gui.mods.offline_lan_0922.ai.maps");
        auto wanted = py::call(maps.get(), "normalize_map_name", p::text(map_name).get());
        auto cache = runtimeModule(b, "arena_cache");
        auto items = py::call(cache.get(), "items");
        p::Ref rows(PyObject_GetIter(items.get()));
        while (PyObject* raw = PyIter_Next(rows.get())) {
            p::Ref row(raw);
            auto arena = p::item(row.get(), 1);
            auto geometry = py::call(maps.get(), "normalize_map_name", py::optional(arena.get(), "geometryName").get());
            if (PyObject_RichCompareBool(geometry.get(), wanted.get(), Py_EQ) == 1 &&
                p::string(py::optional(arena.get(), "gameplayName").get()) == kStandardGameplay) {
                b.startup->arena_type = own(std::move(arena));
                break;
            }
        }
        if (PyErr_Occurred()) throw p::Error("arena cache");
        if (!b.startup->arena_type) throw p::Error("standard arena definition is unavailable");
        // The official red-border rectangle.
        try {
            auto box = py::attr(b.startup->arena_type.get(), "boundingBox");
            auto corner = [&](Py_ssize_t i) -> std::array<double, 2> {
                auto point = p::item(box.get(), i);
                PyObject* x = PySequence_Check(point.get()) ? PySequence_GetItem(point.get(), 0) : nullptr;
                if (x) {
                    p::Ref xs(x), zs(PySequence_GetItem(point.get(), 1));
                    return {asFloat(xs.get()), asFloat(zs.get())};
                }
                PyErr_Clear();
                return {asFloat(py::attr(point.get(), "x").get()), asFloat(py::attr(point.get(), "y").get())};
            };
            const auto low = corner(0), high = corner(1);
            if (std::isfinite(low[0]) && std::isfinite(low[1]) && std::isfinite(high[0]) && std::isfinite(high[1]) && low[0] < high[0] &&
                low[1] < high[1])
                b.startup->arena_bounds = std::array<double, 4>{low[0], low[1], high[0], high[1]};
        } catch (const std::exception&) { PyErr_Clear(); }
        // The validated navigation graph, narrowed to the arena once before any consumer sees it.
        auto navigation = module("gui.mods.offline_lan_0922.prebaked_navigation");
        auto loader = py::optional(modules(b), "navigation_graph_loader");
        if (!py::callable(loader)) replace(loader, py::attr(navigation.get(), "load_graph"));
        b.startup->navigation_graph = own(py::invoke(loader.get(), p::text(map_name).get()));
        auto short_name = py::call(navigation.get(), "_short_map_name", p::text(map_name).get());
        const int supported = PySequence_Contains(py::attr(navigation.get(), "SUPPORTED_MAPS").get(), short_name.get());
        if (supported < 0) throw p::Error("supported maps");
        if (supported && b.startup->navigation_graph.get() == Py_None)
            throw p::Error("validated navigation graph is unavailable for " + p::string(short_name.get()));
        if (b.startup->navigation_graph.get() != Py_None) {
            auto bounds = b.startup->arena_bounds ? p::Ref(Py_BuildValue("(dddd)", (*b.startup->arena_bounds)[0], (*b.startup->arena_bounds)[1],
                                                                         (*b.startup->arena_bounds)[2], (*b.startup->arena_bounds)[3]))
                                                  : p::retain(Py_None);
            const std::int64_t retired = asInt(py::call(navigation.get(), "clip_graph_to_arena", b.startup->navigation_graph.get(), bounds.get()).get());
            if (retired) log("retired " + std::to_string(retired) + " baked navigation cell(s) outside the " + p::string(short_name.get()) + " arena rectangle");
        }
        world::start(b, p::string(py::call(navigation.get(), "mod_dir").get()));
        observeDestructiblesDisabled(b, b.start);
        prepareSpawn(b);
        const Value local_identity = localState(b);
        compat::setBattleNetworkClient(b, client);
        const bool training = text(field(b.start, "battle_mode")) == "training";
        auto gui_types = need(b.engine.constants.get(), "ARENA_GUI_TYPE"), bonus_types = need(b.engine.constants.get(), "ARENA_BONUS_TYPE");
        auto gui_type = py::optional(gui_types.get(), training ? "TRAINING" : "RANDOM");
        auto bonus_type = py::optional(bonus_types.get(), training ? "TRAINING" : "REGULAR");
        const std::string name = has(local_identity, "name") ? text(field(local_identity, "name")) : p::string(py::attr(client, "name").get());
        const std::int64_t team = has(local_identity, "team") ? integer(field(local_identity, "team")) : asInt(py::attr(client, "team").get());
        compat::configureBattle(b, gui_type.get() == Py_None ? p::integer(0).get() : gui_type.get(),
                                bonus_type.get() == Py_None ? p::integer(0).get() : bonus_type.get(), name, team,
                                py::optional(b.startup->arena_type.get(), "id").get());
        // Validate destructive lobby boundaries before changing GUI state.
        auto clear = py::optional(b.engine.bigworld.get(), "clearEntitiesAndSpaces");
        if (!py::callable(clear)) throw p::Error("BigWorld.clearEntitiesAndSpaces is unavailable");
        auto hangar = py::optional(runtimeModule(b, "hangar_space").get(), "g_hangarSpace");
        if (hangar.get() == Py_None) throw p::Error("hangar space owner is unavailable");
        if (!p::flag(py::optional(hangar.get(), "inited").get()) || !p::flag(py::optional(hangar.get(), "spaceInited").get()))
            throw p::Error("hangar space is not ready for battle transition");
        // Freeze every lobby-owned item before the Account retires.
        freezeGarage(b);
        if (!b.worker) {
            b.startup->effective_params = acceptedEffectiveParams(b, local_identity);
            const auto& skills = field(b.startup->effective_params, "skills");
            b.startup->has_sixth_sense = field(skills, "sixth_sense").truth();
            b.startup->has_expert = field(skills, "expert").truth();
            b.startup->has_deadeye = field(skills, "deadeye").truth();
        }
        installGuiGuard(b);
        // Dispose the live lobby before retiring its Account owner.
        if (guiSpaceId(b) != spaceIdConstant(b, "LOBBY")) throw p::Error("battle GUI is not in the lobby state");
        if (!p::flag(py::call(runtimeModule(b, "app_loader").get(), "showBattleLoading").get()))
            throw p::Error("battle loading GUI transition was rejected");
        if (guiSpaceId(b) != spaceIdConstant(b, "BATTLE_LOADING")) throw p::Error("battle loading GUI transition did not finish");
        // Cross the Account-to-Avatar boundary the #1513 observer crosses.
        b.startup->lobby_retire_started = true;
        if (!compat::retireCurrentPlayer(b)) throw p::Error("lobby Account retirement did not run");
        if (p::flag(py::optional(hangar.get(), "inited").get()) || p::flag(py::optional(hangar.get(), "spaceInited").get()))
            throw p::Error("Account retirement did not destroy the hangar space");
        py::invoke(clear.get());
        PyObject* player = PyObject_CallMethod(b.engine.bigworld.get(), const_cast<char*>("player"), nullptr);
        if (!player) {
            if (!PyErr_ExceptionMatches(PyExc_ReferenceError)) throw p::Error("BigWorld.player failed");
            PyErr_Clear();
        } else {
            p::Ref held(player);
            if (player != Py_None) throw p::Error("lobby Account survived battle transition");
        }
        b.startup->map_create_attempted = true;
        createNativeBattleMap(b, map_name);
        auto creator = runtimeModule(b, "offline_map_creator");
        if (!p::flag(py::call(creator.get(), "Active").get())) throw p::Error("stock OfflineMapCreator rejected the map");
        b.engine.avatar = own(py::call(b.engine.bigworld.get(), "player"));
        if (b.engine.avatar.get() == Py_None) throw p::Error("stock OfflineMapCreator created no Avatar");
        configureSpaceVisibility(b);
        if (!p::flag(py::optional(b.engine.avatar.get(), "_offlineLANInitComplete").get()))
            throw p::Error("stock OfflineMapCreator returned a partial Avatar");
        if (!p::flag(py::optional(b.engine.avatar.get(), "_offlineLANPlayerReady").get()))
            throw p::Error("stock OfflineMapCreator did not promote its Avatar");
        if (!b.worker) local::installRamContactHook(b);
        world::openSpace(b);
        if (has(b.start, "destructibles")) world::applyDestructibleState(b, field(b.start, "destructibles"));
        // From here every stock Avatar branch must see a real battle, not the map viewer.
        py::call(creator.get(), "SetActive", Py_False);
        createEntities(b);
        return b.phase != Phase::Failed;
    } catch (const std::exception& e) {
        b.fail(describe(e));
        return false;
    }
}

// OfflineMapCreator owns space creation, geometry mapping and Avatar properties; suppress its viewer-only UI.
void createNativeBattleMap(Battle& b, const std::string& map_name) {
    auto creator = runtimeModule(b, "offline_map_creator");
    const char* setup_name = "_OfflineMapCreator__setupCamera";
    if (!py::callable(py::optional(creator.get(), setup_name))) throw p::Error("OfflineMapCreator viewer-camera boundary is unavailable");
    auto creator_dict = py::optional(creator.get(), "__dict__");
    PyObject* raw_setup = PyDict_Check(creator_dict.get()) ? PyDict_GetItemString(creator_dict.get(), setup_name) : nullptr;
    const bool had_setup = raw_setup != nullptr;
    p::Ref original_setup(p::retain(raw_setup ? raw_setup : Py_None));
    auto app_loader = runtimeModule(b, "app_loader");
    if (!py::callable(py::optional(app_loader.get(), "showBattlePage"))) throw p::Error("OfflineMapCreator battle-page boundary is unavailable");
    p::Ref loader_type(p::retain(reinterpret_cast<PyObject*>(Py_TYPE(app_loader.get()))));
    auto original_page = classAttribute(loader_type.get(), "showBattlePage");
    const bool had_page = original_page.get() != Py_None;
    PyObject* bigworld = b.engine.bigworld.get();
    auto game = runtimeModule(b, "game");
    const char* mapping_name = "addSpaceGeometryMapping";
    auto bigworld_dict = py::optional(bigworld, "__dict__");
    PyObject* raw_mapping = PyDict_Check(bigworld_dict.get()) ? PyDict_GetItemString(bigworld_dict.get(), mapping_name) : nullptr;
    const bool had_mapping = raw_mapping != nullptr;
    p::Ref original_instance_mapping(p::retain(raw_mapping ? raw_mapping : Py_None));
    auto original_add = py::optional(bigworld, mapping_name);
    if (!py::callable(original_add)) throw p::Error("BigWorld.addSpaceGeometryMapping boundary is unavailable");
    auto original_abort = py::optional(game.get(), "abort");
    if (!py::callable(original_abort)) throw p::Error("game.abort boundary is unavailable");
    PyObject* add = original_add.get();
    Py_INCREF(add);
    Object add_holder(add);
    auto defer_page = native([](PyObject*, PyObject*) -> PyObject* { Py_RETURN_NONE; });
    p::Ref defer_static(PyStaticMethod_New(defer_page.get()));
    auto finish_setup = native([bigworld](PyObject*, PyObject*) -> PyObject* {
        // Restore normal battle GUI visibility at the stock setup-camera boundary, without the viewer camera.
        auto set_watcher = py::optional(bigworld, "setWatcher");
        if (py::callable(set_watcher)) py::invoke(set_watcher.get(), p::text("Visibility/GUI").get(), Py_True);
        Py_RETURN_NONE;
    });
    auto reject_abort = native([](PyObject*, PyObject*) -> PyObject* {
        PyErr_SetString(PyExc_RuntimeError, "native Avatar requested game.abort during battle start");
        return nullptr;
    });
    std::uint64_t generation = b.generation;
    auto add_mapping = native([generation, add_holder](PyObject* args, PyObject* kwargs) -> PyObject* {
        Battle* battle = current();
        if (!battle || battle->generation != generation) throw p::Error("map mapping outside its round");
        auto space = p::item(args, 0);
        // Latch the CTF bit before the exact native mapping call.
        const auto contract = visibilityContract(*battle);
        if (contract.first != 0x1) throw p::Error("#1513 standard gameplay visibility bit is not 0x1");
        battle->startup->space_visibility = std::make_pair(asInt(space.get()), contract.first);
        battle->startup->next_space_visibility_check = battle->clock() + kSpaceVisibilityCheckSeconds;
        auto masked = py::optional(battle->engine.modules.get(), "call_with_standard_gameplay_mask");
        if (!py::callable(masked)) throw p::Error("#1513 native mapping-mask boundary is unavailable");
        auto keywords = kwargs ? p::retain(kwargs) : p::dict();
        return py::invoke(masked.get(), add_holder.get(), args, keywords.get()).release();
    });
    std::string first_error;
    bool raised = false;
    py::set(loader_type.get(), "showBattlePage", defer_static.get());
    try {
        py::set(game.get(), "abort", reject_abort.get());
        py::set(bigworld, mapping_name, add_mapping.get());
        py::set(creator.get(), setup_name, finish_setup.get());
        try {
            py::call(creator.get(), "create", p::text(map_name).get());
        } catch (const std::exception& e) { raised = true; first_error = describe(e); }
        auto dict = py::optional(bigworld, "__dict__");
        if (PyDict_Check(dict.get()) && PyDict_GetItemString(dict.get(), mapping_name) == add_mapping.get()) {
            if (had_mapping) PyObject_SetAttrString(bigworld, mapping_name, original_instance_mapping.get());
            else if (PyObject_DelAttrString(bigworld, mapping_name) < 0) PyErr_Clear();
        }
        auto cdict = py::optional(creator.get(), "__dict__");
        if (PyDict_Check(cdict.get()) && PyDict_GetItemString(cdict.get(), setup_name) == finish_setup.get()) {
            if (had_setup) PyObject_SetAttrString(creator.get(), setup_name, original_setup.get());
            else if (PyObject_DelAttrString(creator.get(), setup_name) < 0) PyErr_Clear();
        }
    } catch (const std::exception& e) { if (!raised) { raised = true; first_error = describe(e); } }
    if (py::optional(game.get(), "abort").get() == reject_abort.get()) PyObject_SetAttrString(game.get(), "abort", original_abort.get());
    restorePatch(loader_type.get(), "showBattlePage", defer_static.get(), original_page.get(), had_page);
    PyErr_Clear();
    if (raised) throw p::Error(first_error);
}

// Keep #1513 GUI transitions ordered: space loading and roster polling may arrive in either order.
void installGuiGuard(Battle& b) {
    auto& s = *b.startup;
    if (s.gui_loader_type) return;
    auto app_loader = runtimeModule(b, "app_loader");
    p::Ref type(p::retain(reinterpret_cast<PyObject*>(Py_TYPE(app_loader.get()))));
    auto original_loading = classAttribute(type.get(), "showBattleLoading");
    auto original_page = classAttribute(type.get(), "showBattlePage");
    if (!PyCallable_Check(original_loading.get()) || !PyCallable_Check(original_page.get()))
        throw p::Error("battle GUI state boundaries are unavailable");
    const std::int64_t lobby = spaceIdConstant(b, "LOBBY"), loading = spaceIdConstant(b, "BATTLE_LOADING"), battle_id = spaceIdConstant(b, "BATTLE");
    if (guiSpaceId(b) != lobby) throw p::Error("battle GUI is not in the lobby state");
    PyObject* ours = app_loader.get();
    Object loading_fn = borrow(original_loading.get()), page_fn = borrow(original_page.get()), owner = borrow(ours);
    auto actual = [owner](PyObject* loader) -> std::int64_t {
        auto state = py::optional(loader, "_AppLoader__state");
        auto getter = py::optional(state.get(), "getSpaceID");
        if (!py::callable(getter)) throw p::Error("actual battle GUI state is unavailable");
        return asInt(py::invoke(getter.get()).get());
    };
    auto ordered_loading = native([=](PyObject* args, PyObject*) -> PyObject* {
        auto loader = p::item(args, 0);
        if (loader.get() != owner.get()) return PyObject_CallObject(loading_fn.get(), args);
        if (actual(loader.get()) != lobby) Py_RETURN_NONE;
        auto result = p::Ref(PyObject_CallObject(loading_fn.get(), args));
        if (!p::flag(result.get()) || actual(loader.get()) != loading) Py_RETURN_NONE;
        return result.release();
    });
    Object loading_wrapper = borrow(ordered_loading.get());
    auto ordered_page = native([=](PyObject* args, PyObject*) -> PyObject* {
        auto loader = p::item(args, 0);
        if (loader.get() != owner.get()) return PyObject_CallObject(page_fn.get(), args);
        std::int64_t now = actual(loader.get());
        if (now == battle_id) Py_RETURN_NONE;
        if (now == lobby) {
            auto entered = p::Ref(PyObject_CallObject(loading_wrapper.get(), args));
            if (!p::flag(entered.get())) Py_RETURN_NONE;
            now = actual(loader.get());
        }
        // Never hand an illegal transition to Scaleform; the startup timeout recovers the lobby.
        if (now != loading) Py_RETURN_NONE;
        auto result = p::Ref(PyObject_CallObject(page_fn.get(), args));
        if (!p::flag(result.get()) || actual(loader.get()) != battle_id) Py_RETURN_NONE;
        return result.release();
    });
    // A plain callable class attribute would bind as a method in neither form; the wrappers take the loader positionally.
    p::Ref loading_attr(PyMethod_New(ordered_loading.get(), nullptr, type.get()));
    p::Ref page_attr(PyMethod_New(ordered_page.get(), nullptr, type.get()));
    py::set(type.get(), "showBattleLoading", loading_attr.get());
    py::set(type.get(), "showBattlePage", page_attr.get());
    s.gui_loader_type = own(std::move(type));
    s.gui_loading_original = std::move(loading_fn);
    s.gui_page_original = std::move(page_fn);
    s.gui_loading_wrapper = own(std::move(loading_attr));
    s.gui_page_wrapper = own(std::move(page_attr));
}

void restoreGuiGuard(Battle& b) {
    auto& s = *b.startup;
    if (!s.gui_loader_type) return;
    PyObject* type = s.gui_loader_type.get();
    auto current_loading = classAttribute(type, "showBattleLoading");
    auto current_page = classAttribute(type, "showBattlePage");
    if (current_loading.get() == s.gui_loading_wrapper.get()) PyObject_SetAttrString(type, "showBattleLoading", s.gui_loading_original.get());
    if (current_page.get() == s.gui_page_wrapper.get()) PyObject_SetAttrString(type, "showBattlePage", s.gui_page_original.get());
    PyErr_Clear();
    s.gui_loader_type = s.gui_loading_original = s.gui_page_original = s.gui_loading_wrapper = s.gui_page_wrapper = Object();
}

// (selected gameplay bit, server mask) of #1513's client visibility flags.
std::pair<std::int64_t, std::int64_t> visibilityContract(Battle& b) {
    auto flags = py::optional(modules(b), "client_visibility_flags");
    auto gameplay_mask = py::optional(modules(b), "arena_visibility_mask");
    if (flags.get() == Py_None || !py::callable(gameplay_mask)) throw p::Error("#1513 space visibility boundary is unavailable");
    const std::uint64_t client_bits = PyLong_AsUnsignedLongLongMask(py::attr(flags.get(), "CLIENT_MASK").get());
    const std::uint64_t server_bits = PyLong_AsUnsignedLongLongMask(py::attr(flags.get(), "SERVER_MASK").get());
    const std::int64_t gameplay = asInt(py::attr(b.startup->arena_type.get(), "gameplayID").get());
    const std::int64_t selected = asInt(py::invoke(gameplay_mask.get(), p::integer(gameplay).get()).get());
    if (PyErr_Occurred()) throw p::Error("#1513 space visibility contract is invalid");
    if ((client_bits & server_bits) || ((client_bits | server_bits) & 0xffffffffu) != 0xffffffffu || selected <= 0 ||
        (selected & (selected - 1)) || (std::uint64_t(selected) & ~server_bits))
        throw p::Error("#1513 gameplay visibility mask is invalid");
    return {selected, std::int64_t(server_bits)};
}

// Best-effort typed gameplay bit on the live space; a pending publication finishes in the frame guard.
void configureSpaceVisibility(Battle& b, std::optional<std::int64_t> space_id) {
    try {
        const auto contract = visibilityContract(b);
        const std::int64_t id = space_id ? *space_id : asInt(py::attr(b.engine.avatar.get(), "spaceID").get());
        b.startup->space_visibility = std::make_pair(id, contract.first);
        b.startup->next_space_visibility_check = b.clock() + kSpaceVisibilityCheckSeconds;
        auto spaces = py::optional(b.engine.bigworld.get(), "spaces");
        if (spaces.get() == Py_None) throw p::Error("#1513 live space visibility data is unavailable");
        PyObject* raw = PyObject_GetItem(spaces.get(), p::integer(id).get());
        if (!raw) {
            if (PyErr_ExceptionMatches(PyExc_KeyError)) { PyErr_Clear(); return; }
            throw p::Error("live space");
        }
        p::Ref space(raw);
        if (asInt(py::attr(space.get(), "itemsVisibilityMask").get()) != contract.first)
            py::set(space.get(), "itemsVisibilityMask", p::integer(contract.first).get());
        const std::int64_t actual = asInt(py::attr(space.get(), "itemsVisibilityMask").get());
        if (actual != contract.first)
            throw p::Error("#1513 typed gameplay visibility mask was not applied: expected=0x" + std::to_string(contract.first) +
                           " actual=" + std::to_string(actual));
    } catch (const std::exception& e) {
        // Visibility only filters map decoration; a missing contract never discards a playable battle.
        b.startup->space_visibility.reset();
        b.warn("map visibility filtering", describe(e));
    }
}

// The frame guard: the stock may reset the typed bit; restore it at a bounded cadence.
void maintainSpaceVisibility(Battle& b, double now) {
    auto& s = *b.startup;
    if (!s.space_visibility || now < s.next_space_visibility_check || !b.featureEnabled("map visibility filtering")) return;
    s.next_space_visibility_check = now + kSpaceVisibilityCheckSeconds;
    try {
        const auto contract = visibilityContract(b);
        const std::int64_t selected = s.space_visibility->second;
        auto spaces = py::attr(b.engine.bigworld.get(), "spaces");
        PyObject* raw = PyObject_GetItem(spaces.get(), p::integer(s.space_visibility->first).get());
        if (!raw) {
            if (PyErr_ExceptionMatches(PyExc_KeyError)) { PyErr_Clear(); return; }
            throw p::Error("live space");
        }
        p::Ref space(raw);
        const std::uint64_t current_mask = PyLong_AsUnsignedLongLongMask(py::attr(space.get(), "itemsVisibilityMask").get());
        const std::uint64_t server_bits = std::uint64_t(contract.second);
        if ((current_mask & server_bits) == std::uint64_t(selected)) return;
        const std::uint64_t corrected = (current_mask & ~server_bits & 0xffffffffu) | std::uint64_t(selected);
        py::set(space.get(), "itemsVisibilityMask", p::Ref(PyLong_FromUnsignedLongLong(corrected)).get());
        const std::uint64_t actual = PyLong_AsUnsignedLongLongMask(py::attr(space.get(), "itemsVisibilityMask").get());
        if (actual != corrected) throw p::Error("#1513 typed gameplay visibility mask was not applied");
    } catch (const std::exception& e) {
        s.space_visibility.reset();
        b.warn("map visibility filtering", describe(e));
    }
}

bool observeDestructiblesDisabled(Battle& b, const Value& message) {
    if (!field(message, "destructibles_disabled").truth() || field(message, "destructibles_disabled").resolved().kind != Value::Kind::Boolean)
        return false;
    std::string reason = text(field(message, "destructibles_disabled_reason"), "server disabled destructible interactions");
    if (reason.empty()) reason = "server disabled destructible interactions";
    auto& start = writable(b.start);
    start.object["destructibles_disabled"] = boolean(true);
    Value why; why.kind = Value::Kind::Text; why.text = reason;
    start.object["destructibles_disabled_reason"] = why;
    if (b.world && b.world->catalog) world::disableDestructibles(b, "LAN server disabled destructible interactions: " + reason);
    return true;
}

// ---------------------------------------------------------------------------------------------------- entities

void createEntities(Battle& b) {
    try {
        b.startup->stage = Stage::LoadingEntities;
        b.startup->vehicle_ready_deadline = 0.;
        const Value local = localState(b);
        const std::string vehicle = has(local, "vehicle") ? text(field(local, "vehicle")) : text(field(b.config, "vehicle"));
        auto descriptor = localBattleDescriptor(b, vehicle);
        remote::start(b, descriptor.get());
        b.startup->local_descriptor = borrow(descriptor.get());
        // The complete line-up resolves while BattleLoading is still up.
        const bool lineup_ready = bots::prepareVehicleAssignments(b, descriptor.get());
        if ((field(b.start, "bot_lineup").truth() || field(b.start, "bot_excluded_vehicles").truth()) && !lineup_ready)
            throw p::Error("the configured Bot roster is not available in this client");
        prepareSpawnAssignments(b);
        const auto placed = worldPose(b, local);
        b.local->position = placed.first;
        b.local->yaw = placed.second;
        if (remote::prewarmWrecksEnabled(b)) {
            std::set<std::string> names;
            for (const auto& entry : bots::vehicleAssignments(b)) names.insert(entry.second);
            for (const auto& name : names) descriptorFor(b, name);
        }
        b.engine.server = own(createServer(b));
        compat::attachAvatarServer(b, b.engine.avatar.get(), b.engine.server.get());
        auto compact = py::call(descriptor.get(), "makeCompactDescr");
        const std::int64_t team = has(local, "team") ? integer(field(local, "team")) : 1;
        const std::string name = has(local, "name") ? text(field(local, "name")) : text(field(b.config, "name"), "Player");
        auto properties = binding::properties(b, compact.get(), team, name);
        PyObject* info = PyDict_GetItemString(properties.get(), "publicInfo");
        const auto& marks = field(local, "marks_on_gun").resolved();
        const std::int64_t mark_count = marks.kind == Value::Kind::Null ? 0 : std::max<std::int64_t>(0, std::min<std::int64_t>(3, std::int64_t(marks.numeric(0.))));
        p::set(info, "marksOnGun", p::integer(mark_count));
        std::size_t members = 0;
        for (const auto& row : field(b.start, "players").resolved().array) if (integer(field(row, "team")) == team) ++members;
        p::set(info, "prebattleID", p::integer((team == 1 || team == 2) && members >= 2 ? team : 0));
        const std::int64_t max_health = asInt(py::attr(descriptor.get(), "maxHealth").get());
        const std::int64_t health = has(local, "health") ? integer(field(local, "health")) : max_health;
        p::set(properties.get(), "health", p::integer(std::max<std::int64_t>(1, std::min(health, max_health))));
        const auto& loadout = garage(b);
        auto missions = p::list();
        for (auto id : loadout.personal_mission_ids) p::append(missions.get(), p::integer(id));
        p::Ref rotation(Py_BuildValue("(ddd)", 0., 0., placed.second));
        const std::int64_t vehicle_id = addVehicleToArena(b, properties.get(), engineVector(b, placed.first).get(), rotation.get(), missions.get(),
                                                          loadout.crew_group);
        b.startup->local_vehicle_id = vehicle_id;
        // Refresh ArenaDP before marker plugins cache the local vehicle id.
        if (vehicle_id <= 0) throw p::Error("#1513 player vehicle identity is invalid");
        auto player = py::call(b.engine.bigworld.get(), "player");
        if (player.get() != b.engine.avatar.get()) throw p::Error("#1513 BigWorld player changed before ArenaDP refresh");
        const std::int64_t avatar_vehicle = asInt(py::optional(player.get(), "playerVehicleID").get() == Py_None ? p::integer(0).get()
                                                                                                                 : py::attr(player.get(), "playerVehicleID").get());
        if (avatar_vehicle != vehicle_id)
            throw p::Error("#1513 Avatar player identity mismatch before ArenaDP refresh: expected=" + std::to_string(vehicle_id) +
                           " avatar=" + std::to_string(avatar_vehicle));
        const std::int64_t avatar_team = asInt(py::attr(player.get(), "team").get());
        if (avatar_team != 1 && avatar_team != 2) throw p::Error("#1513 Avatar team is invalid before ArenaDP refresh: team=" + std::to_string(avatar_team));
        auto provider = py::optional(b.engine.avatar.get(), "guiSessionProvider");
        auto arena_dp = py::invoke(needCallable(provider.get(), "getArenaDP").get());
        // #1513 caches player id 0 before the local Vehicle exists; isRequiredDataExists re-reads it.
        if (!p::flag(py::invoke(needCallable(arena_dp.get(), "isRequiredDataExists").get()).get()))
            throw p::Error("#1513 ArenaDP player identity is incomplete");
        auto get_id = needCallable(arena_dp.get(), "getPlayerVehicleID");
        if (asInt(py::invoke(get_id.get(), Py_False).get()) != vehicle_id) throw p::Error("#1513 ArenaDP player identity refresh mismatch");
        compat::synchroniseVehicleMarkerIdentity(b, vehicle_id);
        if (asInt(py::attr(b.engine.avatar.get(), "playerVehicleID").get()) != vehicle_id || asInt(py::invoke(get_id.get(), Py_False).get()) != vehicle_id)
            throw p::Error("#1513 player identity mismatch");
        compat::assertVehicleMarkerIdentity(b, vehicle_id);
        // Start stock BattleLoading once the player id and roster are present.
        auto arena_load = py::optional(py::optional(provider.get(), "shared").get(), "arenaLoad");
        py::invoke(needCallable(arena_load.get(), "invalidateArenaInfo").get());
        vehicles::installLocal(b, vehicle_id, local);
        b.startup->frame_callback = own(schedule(b, 0., [](Battle& battle) { waitForClientReady(battle); }));
    } catch (const std::exception& e) {
        b.fail(describe(e));
    }
}

void waitForClientReady(Battle& b) {
    if (b.phase != Phase::Loading || b.startup->stage != Stage::LoadingEntities) return;
    auto again = [&] { b.startup->frame_callback = own(schedule(b, 0.05, [](Battle& battle) { waitForClientReady(battle); })); };
    if (asFloat(py::call(b.engine.bigworld.get(), "spaceLoadStatus").get()) < 1.) {
        if (b.clock() >= b.startup->deadline) { b.fail("map loading timed out"); return; }
        again();
        return;
    }
    if (b.startup->vehicle_ready_deadline <= 0.) b.startup->vehicle_ready_deadline = b.clock() + configNumber(b, "startupTimeoutSeconds", 30.);
    flushClientReady(b);
    if (b.startup->client_ready_received) { finishEntityStartup(b); return; }
    if (b.clock() >= b.startup->vehicle_ready_deadline) { b.fail("player Vehicle did not enter world before startup timeout"); return; }
    again();
}

void onClientReady(Battle& b) {
    b.startup->client_ready_received = true;
    if (!b.feature("Expert damaged-device presentation", [&] { visibility::enableExpert(b); }))
        visibility::disableExpertPresentation(b);
    if (b.running()) {
        local::sendCurrent(b);
        gun::ensureTicking(b);
    }
}

void finishEntityStartup(Battle& b) {
    try {
        if (b.phase != Phase::Loading || b.startup->stage != Stage::LoadingEntities) return;
        // Keep BattleLoading up until raw wreck assets settle, or the vehicle deadline abandons them.
        if (remote::wreckPrewarmPending(b) > 0) {
            if (b.startup->vehicle_ready_deadline <= 0. || b.clock() < b.startup->vehicle_ready_deadline) {
                b.startup->frame_callback = own(schedule(b, 0.05, [](Battle& battle) { finishEntityStartup(battle); }));
                return;
            }
            remote::abandonWreckPrewarm(b);
        }
        if (!b.startup->local_descriptor) throw p::Error("player Vehicle descriptor is unavailable");
        if (!b.localVehicle()) throw p::Error("player Vehicle record is unavailable");
        // #1513 reapplies ClientVisibilityFlags late in PlayerAvatar.__onInitStepCompleted; cross it first.
        configureSpaceVisibility(b);
        vehicles::markLocalReady(b);
        local::attach(b);
        b.feature("persistent Siege mode hint", [&] { local::enableSiegeHints(b); });
        b.feature("initial Siege mode indicator", [&] { if (Actor* own = b.localVehicle()) local::seedSiegeHud(b, *own); });
        PyObject* descriptor = b.startup->local_descriptor.get();
        if (!b.worker) {
            std::uint64_t generation = b.generation;
            auto listener = native([generation](PyObject* args, PyObject*) -> PyObject* {
                Battle* battle = current();
                if (battle && battle->generation == generation) local::controlModeChanged(*battle, PyTuple_GET_SIZE(args) ? PyTuple_GET_ITEM(args, 0) : Py_None);
                Py_RETURN_NONE;
            });
            compat::setControlModeListener(b, listener.get());
            gun::start(b, descriptor);
        }
        // battle_start.bots only reserves the roster; Bots arrive with the authority manifest or first snapshot.
        Value manifest = b.start;
        writable(manifest).object["bots"] = Value();
        writable(manifest).object["bots"].kind = Value::Kind::Array;
        vehicles::receiveManifest(b, manifest);
        projectiles::observe(b, b.start);
        world::reconcileDetachedTurrets(b, b.start);
        auto latest = py::optional(b.engine.client.get(), "last_snapshot");
        std::optional<Value> last;
        if (PyDict_Check(latest.get())) {
            Value row = fromPython(latest.get());
            if (field(row, "round_id") == field(b.start, "round_id")) last = std::move(row);
        }
        if (last) {
            projectiles::observe(b, *last);
            world::reconcileDetachedTurrets(b, *last);
            gun::restoreEquipment(b, *last, true);
            vehicles::receiveSnapshotMessage(b, latest.get(), b.clock());
        }
        bots::start(b);
        if (last) {
            bots::applySnapshot(b, *last);
            world::rememberRamBotSnapshot(b, *last);
        }
        visibility::start(b);
        local::start(b);
        b.phase = Phase::Running;
        b.startup->stage = Stage::None;
        if (!b.worker) {
            startTeamChatWhenRosterReady(b);
            local::bindArcadeCamera(b);
            const double now = b.clock();
            b.feature("engine RPM presentation", [&] { local::publishRpm(b, now, true); });
        }
        b.last_frame = b.clock();
        if (!b.worker) gun::ensureTicking(b);
        if (!b.running()) return;
        if (b.result.resolved().kind != Value::Kind::Null) events::applyBattleResult(b, b.result);
        if (!b.running()) return;
        // Engine-free harnesses have no socket load barrier: keep the local countdown.
        if (!py::callable(py::optional(b.engine.client.get(), "send_battle_ready"))) {
            Value live; live.kind = Value::Kind::Object;
            live.object["countdown_seconds"] = number(prebattleSeconds(b));
            live.object["battle_duration_seconds"] = number(battleSeconds(b));
            battleLive(b, live);
        }
        scheduleFrame(b, kFrameSeconds);
    } catch (const std::exception& e) {
        b.fail(describe(e));
    }
}

// ---------------------------------------------------------------------------------------------------- live boundary

// The one server-owned countdown, after every map is ready.
void battleLive(Battle& b, const Value& message) {
    if (!b.running() || b.live || b.prebattle_deadline >= 0.) return;
    observeDestructiblesDisabled(b, message);
    auto read = [&](const char* name, double fallback) {
        const auto& v = field(message, name).resolved();
        const double n = v.kind == Value::Kind::Null ? fallback : v.numeric(fallback);
        return std::isfinite(n) ? n : fallback;
    };
    double countdown = std::max(0., read("countdown_seconds", prebattleSeconds(b)));
    double duration = std::max(1., read("battle_duration_seconds", battleSeconds(b)));
    auto deadline = py::optional(b.engine.client.get(), "combat_deadline");
    if (deadline.get() != Py_None) {
        auto monotonic = py::call(module("gui.mods.offline_lan_0922.lan_client").get(), "_monotonic_time");
        countdown = std::max(0., asFloat(deadline.get()) - asFloat(monotonic.get()));
    }
    auto network_duration = py::optional(b.engine.client.get(), "combat_duration");
    if (network_duration.get() != Py_None) duration = std::max(1., asFloat(network_duration.get()));
    writable(b.config).object["battleDurationSeconds"] = number(duration);
    if (!b.worker) {
        binding::arenaPeriod(b, "prebattle", countdown);
        resetPrebattleVisuals(b);
        showPrebattleCrosshair(b);
    }
    b.prebattle_deadline = b.clock() + countdown;
    b.last_frame = b.clock();
    if (countdown <= 0. && prebattleTransitionReady(b, b.clock())) beginBattle(b);
}

// A worker waits for the first server snapshot whose timing phase is battle.
bool prebattleTransitionReady(Battle& b, double now) {
    if (b.prebattle_deadline < 0. || now < b.prebattle_deadline) return false;
    if (!b.worker) return true;
    return p::string(py::optional(b.engine.client.get(), "combat_phase").get()) == "battle";
}

void beginBattle(Battle& b) {
    if (b.live) return;
    // Close the final prebattle interval while reload progression is still frozen.
    if (!b.worker) gun::edge(b);
    double duration = battleSeconds(b);
    auto deadline = py::optional(b.engine.client.get(), "combat_end_deadline");
    if (deadline.get() != Py_None) {
        auto monotonic = py::call(module("gui.mods.offline_lan_0922.lan_client").get(), "_monotonic_time");
        duration = std::max(0.1, asFloat(deadline.get()) - asFloat(monotonic.get()));
    }
    const bool refresh_movement = !b.worker && p::flag(py::optional(b.engine.avatar.get(), "_PlayerAvatar__isOnArena").get());
    if (!b.worker) binding::arenaPeriod(b, "battle", duration);
    b.live = true;
    if (refresh_movement) {
        // The countdown reticle raised isOnArena, so stock skips its input refresh; re-read keys and cruise now.
        auto command = py::call(b.engine.avatar.get(), "makeVehicleMovementCommandByKeys");
        py::call(b.engine.avatar.get(), "moveVehicle", command.get(), Py_False);
    }
    if (!b.worker) gun::setLocked(b, false);
    b.prebattle_deadline = -1.;
    b.last_frame = b.clock();
    if (!b.worker) gun::publishReload(b, true);
}

// At the server-owned countdown barrier every remote entity is ready; close stock's pre-attach visual race.
void resetPrebattleVisuals(Battle& b) {
    if (b.worker || !b.engine.client) return;
    const std::int64_t local_team = asInt(py::optional(b.engine.client.get(), "team").get() == Py_None ? p::integer(1).get()
                                                                                                      : py::attr(b.engine.client.get(), "team").get());
    for (auto& entry : b.vehicles->actors) {
        Actor& v = *entry.second;
        auto& d = v.presented;
        if (v.local || !v.native_remote || !v.ready || v.tombstone || !v.engine_id) continue;
        auto vehicle = remote::get(b, v.engine_id);
        if (vehicle.get() == Py_None || py::optional(vehicle.get(), "model").get() == Py_None)
            throw p::Error("prebattle native vehicle presentation is unavailable");
        const auto& team_value = field(d.state, "team").resolved();
        const std::int64_t team = team_value.kind == Value::Kind::Null ? local_team : std::int64_t(team_value.numeric());
        if (team == local_team) {
            if (!d.minimap_rebound) {
                binding::startVehicleMinimap(b, v.engine_id);
                d.minimap_rebound = true;
            }
            continue;
        }
        d.spot_visible = d.spot_marker_visible = d.direct_spot_visible = false;
        d.spot_until = d.radio_spot_until = 0.;
        py::set(vehicle.get(), "_spot_visible", Py_False);
        py::set(vehicle.get(), "_offlineNativeDrawVisible", Py_False);
        remote::setDrawVisibility(b, vehicle.get(), false);
        py::set(vehicle.get(), "targetCaps", p::list().get());
        // A late stock visual callback may have registered the marker again; stop the real adaptor unconditionally.
        binding::stopVehicleVisual(b, v.engine_id, false);
        d.world_marker_started = d.minimap_started = false;
        py::set(vehicle.get(), "_offlineNativeMarkerVisible", Py_False);
    }
}

// A stock PREBATTLE has no reticle; raise both gates so the player aims during the countdown.
void showPrebattleCrosshair(Battle& b) {
    if (b.worker) return;
    auto handler = py::optional(b.engine.avatar.get(), "inputHandler");
    auto rotator = py::optional(b.engine.avatar.get(), "gunRotator");
    if (handler.get() == Py_None || rotator.get() == Py_None) return;
    auto control = py::optional(handler.get(), "_AvatarInputHandler__curCtrl");
    auto set_flag = py::optional(control.get(), "setGunMarkerFlag");
    auto flags = py::optional(py::optional(modules(b), "aih_constants").get(), "GUN_MARKER_FLAG");
    if (!py::callable(set_flag) || flags.get() == Py_None || !PyObject_HasAttrString(flags.get(), "CONTROL_ENABLED"))
        throw p::Error("#1513 gun-marker control gate is unavailable");
    py::set(b.engine.avatar.get(), "_PlayerAvatar__isOnArena", Py_True);
    py::invoke(set_flag.get(), Py_True, py::attr(flags.get(), "CONTROL_ENABLED").get());
    auto show_client = py::optional(handler.get(), "showGunMarker"), show_server = py::optional(handler.get(), "showGunMarker2");
    if (!py::callable(show_client) || !py::callable(show_server)) throw p::Error("#1513 gun-marker boundary is unavailable");
    py::invoke(show_server.get(), Py_False);
    py::invoke(show_client.get(), Py_True);
    py::call(rotator.get(), "start");
    gun::setLocked(b, true);
}

// TeamChannelController needs a teammate in the ArenaDP roster; remote VEHICLE_ADDED may arrive later.
void startTeamChatWhenRosterReady(Battle& b) {
    auto& s = *b.startup;
    if (b.worker || s.team_chat_initialized || !b.running()) return;
    auto provider = py::optional(b.engine.avatar.get(), "guiSessionProvider");
    auto getter = py::optional(provider.get(), "getArenaDP");
    if (!py::callable(getter)) return;
    auto arena = py::invoke(getter.get());
    auto allies = py::optional(arena.get(), "getAlliesVehiclesNumber");
    if (!py::callable(allies)) return;
    PyObject* count = PyObject_CallFunctionObjArgs(allies.get(), nullptr);
    if (!count) { PyErr_Clear(); return; }
    p::Ref held(count);
    PyObject* converted = PyNumber_Int(count);
    if (!converted) { PyErr_Clear(); return; }
    p::Ref number_value(converted);
    if (p::wide(number_value.get()) <= 1) return;
    PyObject* radio = server::radio(b);
    if (!radio) return;
    bool started = false;
    if (b.feature("team chat initialization", [&] { started = p::flag(py::call(radio, "start_team_chat").get()); }) && started)
        s.team_chat_initialized = true;
}

// Open the countdown once the complete line-up has entered.
void maybeSendBattleReady(Battle& b) {
    auto& s = *b.startup;
    if (s.battle_ready_sent || b.live) return;
    std::size_t players = 0, bots_count = 0;
    for (auto& entry : b.vehicles->actors) {
        Actor& v = *entry.second;
        if (!v.engine_id || v.tombstone) continue;
        if (!v.ready) return;
        (v.kind == "player" ? players : bots_count) += 1;
    }
    if (players != field(b.start, "players").resolved().array.size()) return;
    if (bots_count != field(b.start, "bots").resolved().array.size() || !b.roster->pending_order.empty() || !b.roster->pending_bot_creates.empty())
        return;
    auto ready = py::optional(b.engine.client.get(), "send_battle_ready");
    if (!py::callable(ready)) return;
    auto bases = p::dict();
    if (s.spawn) {
        for (int team = 1; team <= 2; ++team) {
            const auto& base = s.spawn->bases[team - 1];
            p::Ref rows(s.spawn->base_radii ? Py_BuildValue("({s:d,s:d,s:d})", "x", base[0], "z", base[1], "radius", (*s.spawn->base_radii)[team - 1])
                                            : Py_BuildValue("((dd))", base[0], base[1]));
            PyDict_SetItem(bases.get(), p::integer(team).get(), rows.get());
        }
    }
    if (!p::flag(py::invoke(ready.get(), bases.get()).get())) throw p::Error("LAN server did not accept battle readiness");
    s.battle_ready_sent = true;
    if (b.roster->bot_create_started) {
        const double now = b.clock();
        const double created = b.roster->bot_create_finished ? std::max(0., *b.roster->bot_create_finished - *b.roster->bot_create_started) : 0.;
        log("LINEUP bots=" + std::to_string(bots_count) + " created_s=" + std::to_string(created) +
            " ready_s=" + std::to_string(std::max(0., now - *b.roster->bot_create_started)));
    }
}

// ---------------------------------------------------------------------------------------------------- teardown

void stop(Battle& b, bool restore_account) {
    finishRecording(b, "battle_stop");
    auto& s = *b.startup;
    auto cancelRestore = [&] {
        cancel(b, s.restore_callback);
        s.restore_pending = false;
        s.retired_owners.clear();
    };
    if (b.phase == Phase::Idle || b.phase == Phase::Stopped) {
        if (!restore_account && s.restore_pending) cancelRestore();
        return;
    }
    if (b.phase == Phase::Failed && s.restore_pending) {
        // _fail already cleaned up; preserve its one queued restore unless shutdown cancels it.
        if (!restore_account) cancelRestore();
        b.phase = Phase::Stopped;
        return;
    }
    ++b.generation;
    cancel(b, s.frame_callback);
    cancel(b, s.ammo_callback);
    std::string error;
    try { cleanup(b); } catch (const std::exception& e) { error = describe(e); }
    b.phase = Phase::Stopped;
    if (!error.empty()) { s.retired_owners.clear(); throw p::Error(error); }
    if (restore_account) scheduleLobbyRestore(b, false, false);
    else s.retired_owners.clear();
}

void teardownAfterFailure(Battle& b) {
    if (!b.startup) { b.phase = Phase::Failed; return; }
    auto& s = *b.startup;
    ++b.generation;
    cancel(b, s.frame_callback);
    cancel(b, s.ammo_callback);
    std::string cleanup_error;
    try { cleanup(b); } catch (const std::exception& e) { cleanup_error = describe(e); b.error += "; cleanup failed: " + cleanup_error; }
    b.phase = Phase::Failed;
    // A replay process never constructs a fresh Account, even after a native playback failure.
    if (b.replay) { reportFailure(b, false); return; }
    if (cleanup_error.empty()) {
        try { scheduleLobbyRestore(b, true, true); return; }
        catch (const std::exception& e) { b.error += "; lobby restore scheduling failed: " + describe(e); }
    }
    s.retired_owners.clear();
    // A failed cleanup cannot stay LOGGED_ON without a valid Account or Avatar.
    try { compat::disconnect(b); } catch (const std::exception& e) { b.error += "; offline disconnect failed: " + describe(e); }
    reportFailure(b, false);
}

void reportFailure(Battle& b, bool lobby_restored) {
    auto callback = py::optional(b.engine.client.get(), "on_event");
    if (py::callable(callback)) {
        try {
            auto payload = p::dict();
            p::set(payload.get(), "message", p::text(b.error));
            p::set(payload.get(), "round_id", toPython(field(b.start, "round_id")));
            p::set(payload.get(), "lobby_restored", p::boolean(lobby_restored));
            py::invoke(callback.get(), p::text("battle_failed").get(), payload.get());
        } catch (const std::exception&) { PyErr_Clear(); }
    }
}

// Construct Account/Hangar one native callback after the battle GUI was destroyed.
void scheduleLobbyRestore(Battle& b, bool report_failure, bool) {
    auto& s = *b.startup;
    s.restore_pending = true;
    log("battle teardown complete; deferring lobby Account restore");
    s.restore_callback = own(schedule(b, 0., [report_failure](Battle& battle) {
        auto& state = *battle.startup;
        if (!state.restore_pending) return;
        state.restore_pending = false;
        state.restore_callback = Object();
        bool restored = false;
        try {
            // Account.showGUI owns the native showLobby transition.
            compat::restoreLobbyAccount(battle);
            restored = true;
            log("deferred lobby Account restored");
        } catch (const std::exception& e) {
            const std::string restore_error = "lobby restore failed: " + describe(e);
            battle.error = battle.error.empty() ? restore_error : battle.error + "; " + restore_error;
            log("deferred lobby restore failed: " + restore_error);
            try { compat::disconnect(battle); } catch (const std::exception& d) { log("offline disconnect after lobby restore failure failed: " + describe(d)); }
        }
        state.retired_owners.clear();
        try {
            p::Ref round(toPython(field(battle.start, "round_id")));
            py::call(module("gui.mods.offline_lan_0922.gc_sweep").get(), "sweep", p::text("round_end").get(), round.get());
        } catch (const std::exception&) { PyErr_Clear(); }
        if (report_failure) {
            try { reportFailure(battle, restored); } catch (const std::exception& e) { log("deferred lobby completion failed: " + describe(e)); }
        }
    }));
}

namespace {
// Close the exact #1513 space even if stock destroy lost its id.
std::string releaseRetainedSpace(Battle& b, std::int64_t space_id, std::int64_t mapping_id) {
    if (space_id <= 0) return {};
    auto is_client_space = py::optional(b.engine.bigworld.get(), "isClientSpace");
    if (!py::callable(is_client_space)) return {};
    std::string first;
    bool retained = false;
    step(first, [&] { retained = p::flag(py::invoke(is_client_space.get(), p::integer(space_id).get()).get()); });
    if (!first.empty() || !retained) return first;
    if (mapping_id > 0) {
        auto remove = py::optional(b.engine.bigworld.get(), "delSpaceGeometryMapping");
        if (py::callable(remove)) step(first, [&] { py::invoke(remove.get(), p::integer(space_id).get(), p::integer(mapping_id).get()); });
    }
    for (const char* name : {"clearSpace", "releaseSpace"}) {
        auto function = py::optional(b.engine.bigworld.get(), name);
        if (!py::callable(function)) { if (first.empty()) first = std::string("BigWorld.") + name + " is unavailable"; continue; }
        step(first, [&] { py::invoke(function.get(), p::integer(space_id).get()); });
    }
    retained = true;
    step(first, [&] { retained = p::flag(py::invoke(is_client_space.get(), p::integer(space_id).get()).get()); });
    if (retained && first.empty()) first = "stock map teardown retained client space " + std::to_string(space_id);
    return first;
}
// (player or null, error)
std::pair<p::Ref, std::string> enginePlayer(Battle& b) {
    PyObject* raw = PyObject_CallMethod(b.engine.bigworld.get(), const_cast<char*>("player"), nullptr);
    if (raw) return {p::Ref(raw), std::string()};
    if (PyErr_ExceptionMatches(PyExc_ReferenceError)) { PyErr_Clear(); return {p::retain(Py_None), std::string()}; }
    return {p::retain(Py_None), describe(p::Error("BigWorld.player failed"))};
}
std::string forceClearEnginePlayer(Battle& b, const std::string& retained_message) {
    std::string first;
    step(first, [&] { compat::retireCurrentPlayer(b); });
    bool found = false;
    p::Ref player(p::retain(Py_None));
    for (const char* name : {"clearEntitiesAndSpaces", "clearAllSpaces"}) {
        auto clear = py::optional(b.engine.bigworld.get(), name);
        if (!py::callable(clear)) continue;
        found = true;
        bool succeeded = false;
        step(first, [&] { py::invoke(clear.get()); succeeded = true; });
        auto read = enginePlayer(b);
        replace(player, std::move(read.first));
        if (!read.second.empty() && first.empty()) first = read.second;
        if (succeeded && read.second.empty() && player.get() == Py_None) return first;
    }
    if (!found) return "no engine entity-clear boundary is available";
    if (player.get() != Py_None) return retained_message;
    return first;
}
}

// Release battle-scoped native owners in the pinned order; keep the first error and finish every step.
void cleanup(Battle& b) {
    auto& s = *b.startup;
    std::string first;
    if (!b.worker) step(first, [&] { visibility::retire(b); });
    step(first, [&] { critical::retire(b); });
    step(first, [&] { projectiles::retire(b); });
    step(first, [&] { gun::retire(b); });
    step(first, [&] { world::retire(b); });
    step(first, [&] { local::retire(b); });
    step(first, [&] { bots::retire(b); });
    step(first, [&] { local::restoreRamContactHook(b); });
    step(first, [&] { compat::setControlModeListener(b, Py_None); });
    // Quiesce battle-scoped native visuals while the Avatar and battle GUI still own them.
    step(first, [&] { closeTeamChat(b); });
    // BigWorld.target.clear() reaches stock targetBlur; run it before any owner retires.
    step(first, [&] { compat::clearTargetFocus(b); });
    step(first, [&] { local::releasePostmortemVisibility(b); });
    step(first, [&] { local::detach(b); });
    step(first, [&] { vehicles::quiesce(b); });
    s.descriptors.clear();
    s.prepared_names.clear();
    s.unusable_reported.clear();
    if (s.map_create_attempted) {
        auto creator = runtimeModule(b, "offline_map_creator");
        std::int64_t space_id = 0, mapping_id = 0;
        step(first, [&] {
            space_id = asInt(py::optional(creator.get(), "_OfflineMapCreator__spaceId").get() == Py_None
                                 ? p::integer(0).get() : py::attr(creator.get(), "_OfflineMapCreator__spaceId").get());
            mapping_id = asInt(py::optional(creator.get(), "_OfflineMapCreator__spaceMappingId").get() == Py_None
                                   ? p::integer(0).get() : py::attr(creator.get(), "_OfflineMapCreator__spaceMappingId").get());
        });
        step(first, [&] { compat::retireCurrentPlayer(b); });
        // Native retirement and stock map ownership are independent boundaries.
        step(first, [&] { py::call(creator.get(), "destroy"); });
        const std::string space_error = releaseRetainedSpace(b, space_id, mapping_id);
        if (!space_error.empty() && first.empty()) first = space_error;
        auto player = enginePlayer(b);
        if (!player.second.empty() && first.empty()) first = player.second;
        if (player.first.get() != Py_None) {
            // OfflineMapCreator.destroy() swallows its own failure and may keep a zombie Avatar.
            const std::string clear_error = forceClearEnginePlayer(b, "stock map teardown retained the Avatar");
            if (!clear_error.empty() && first.empty()) first = clear_error;
        }
    } else if (s.lobby_retire_started) {
        // HangarSpace.destroy() already ran; never leave the old Account alive for restore to reuse.
        const std::string clear_error = forceClearEnginePlayer(b, "lobby teardown retained the Account");
        if (!clear_error.empty() && first.empty()) first = clear_error;
    }
    step(first, [&] { compat::deactivateMap(b); });
    step(first, [&] { restoreGuiGuard(b); });
    step(first, [&] { compat::uninstall(b); });
    s.map_create_attempted = s.lobby_retire_started = false;
    s.space_visibility.reset();
    b.engine.avatar = Object();
    b.engine.binding = Object();
    b.engine.remote_factory = Object();
    b.live = false;
    b.prebattle_deadline = -1.;
    if (!first.empty()) throw p::Error(first);
}
}
}
