// Exercise real critical ingress and stock HUD delivery over transient controllers.
#include "stock_native_battle_subsystems.hpp"
#include "stock_native_battle_vehicles_internal.hpp"

namespace native_battle {
struct VisibilityCore {};
struct LocalCriticalOwner {};
struct RemoteVehicle {};
struct ReplicaBinding {};
struct FallenFoliage {};
struct DestructibleCatalog {};
class ReloadSound {};
Visibility::~Visibility() = default;
Critical::Critical() = default;
Critical::~Critical() = default;
Remote::~Remote() = default;
World::~World() = default;
LocalGun::~LocalGun() = default;
Projectiles::~Projectiles() = default;
Battle::Battle() = default;
Battle::~Battle() = default;
p::Ref Battle::entity(std::int64_t id) const { return py::call(engine.bigworld.get(), "entity", p::integer(id).get()); }
Actor* Battle::vehicle(const std::string& key) const { return vehicles->find(key); }
void Battle::warn(const std::string& name, const std::string& reason, bool disable) {
    if (disable) disabled_features.insert(name);
    py::call(engine.modules.get(), "warning", p::text(name).get(), p::text(reason).get());
}
std::string describe(const std::exception& error) {
    std::string out = error.what();
    if (PyErr_Occurred()) {
        PyObject *type, *value, *trace;
        PyErr_Fetch(&type, &value, &trace);
        if (value) { p::Ref text(PyObject_Str(value)); if (text.get()) out += ": " + p::string(text.get()); }
        Py_XDECREF(type); Py_XDECREF(value); Py_XDECREF(trace);
    }
    return out;
}
void log(const std::string&) {}
namespace critical {
bool reconcileAuthority(Battle&, Actor& actor, const Value& source, Value*) {
    const auto revision = std::int64_t(source.get("critical_revision").numeric());
    if (revision <= actor.presented.critical_revision) return false;
    actor.presented.critical_revision = revision;
    return true;
}
}
namespace gun { void edge(Battle&) {} void descriptorChanged(Battle&, PyObject*) {} }
namespace remote { void destroyAll(Battle&) {} }
namespace vehicles {
bool syncFireEffect(Battle& b, PyObject*, std::optional<bool>) {
    py::call(b.engine.modules.get(), "fire_effect");
    return true;
}
@LIFECYCLE@
}
}

namespace {
namespace b = native_battle;
b::Battle* active = nullptr;
PyObject* reenter(PyObject*, PyObject* args) {
    PyObject* payload;
    long long revision;
    if (!PyArg_ParseTuple(args,"OL",&payload,&revision)) return nullptr;
    try {
        if (!active) throw b::p::Error("critical HUD fixture retired");
        auto* actor = active->vehicle("player:1");
        if (!actor) throw b::p::Error("critical HUD actor retired");
        auto authority = b::p::dict();
        b::p::set(authority.get(),"critical_revision",b::p::integer(revision));
        auto source = b::cs::read(authority.get());
        b::vehicles::applyCritical(*active,*actor,b::cs::read(payload),&source);
        Py_RETURN_NONE;
    } catch (const std::exception& error) {
        if (!PyErr_Occurred()) PyErr_SetString(PyExc_RuntimeError,error.what());
        return nullptr;
    }
}
PyObject* exercise(PyObject*, PyObject* args) {
    try {
        PyObject *avatar, *modules, *world, *commands;
        int replay = 0;
        if (!PyArg_ParseTuple(args,"OOOO|i",&avatar,&modules,&world,&commands,&replay)) return nullptr;
        b::Battle battle;
        struct Active {
            b::Battle* previous;
            explicit Active(b::Battle& battle) : previous(active) { active=&battle; }
            ~Active() { active=previous; }
        } guard(battle);
        battle.phase = b::Phase::Running;
        battle.round_id = 1;
        battle.live = true;
        battle.replay = replay != 0;
        battle.engine.avatar = b::borrow(avatar);
        battle.engine.modules = b::borrow(modules);
        battle.engine.bigworld = b::borrow(world);
        battle.engine.constants = b::own(b::py::attr(modules,"constants"));
        battle.critical = std::make_unique<b::Critical>();
        battle.roster = std::make_unique<b::Vehicles>();
        battle.vehicles = std::make_shared<b::cs::State>();
        auto actor = std::make_shared<b::Actor>();
        actor->key = "player:1";
        actor->kind = "player";
        actor->id = 1;
        actor->engine_id = 91;
        actor->local = actor->ready = true;
        battle.vehicles->actors[actor->key] = actor;
        auto rows = b::p::list(), held = b::p::list(), errors = b::p::list();
        for (Py_ssize_t i=0;i<PyList_Size(commands);++i) {
            auto command = b::p::item(commands,i);
            b::py::call(modules,"before",command.get());
            auto action = b::cs::read(command.get());
            const auto kind = action.get("kind").resolved().text;
            try {
                if (kind == "apply" || kind == "death_blow") {
                    if (kind == "death_blow") actor->alive = false;
                    b::vehicles::applyCritical(battle,*actor,action.get("payload"),&action);
                    if (kind == "death_blow") actor->presented.health = std::array<std::int64_t,4>{0,0,0,0};
                } else if (kind == "silent" || kind == "notify_before_cache") {
                    auto entity = battle.entity(actor->engine_id);
                    auto events = b::critical::applyPayload(battle,entity.get(),action.get("payload"));
                    if (kind == "notify_before_cache") {
                        b::vehicles::presentCritical(battle,*actor,events,17);
                        actor->presented.critical = action.get("payload");
                    }
                } else if (kind == "retire") {
                    b::vehicles::retire(battle,*actor);
                    battle.vehicles->actors.erase(actor->key);
                } else if (kind == "restart") {
                    b::vehicles::quiesce(battle);
                    ++battle.vehicles->epoch;
                    ++battle.round_id;
                } else if (kind == "dead") {
                    actor->presented.health = std::array<std::int64_t,4>{0,0,0,0};
                } else if (kind == "descriptor") {
                    ++actor->descriptor_revision;
                } else if (kind == "engine") {
                    actor->engine_id = 92;
                } else if (kind == "epoch") {
                    ++battle.vehicles->epoch;
                } else if (kind == "missing") {
                    actor->missing = true;
                } else if (kind == "stopped") {
                    battle.phase = b::Phase::Stopped;
                } else if (kind == "loading") {
                    battle.phase = b::Phase::Loading;
                } else if (kind == "running") {
                    battle.phase = b::Phase::Running;
                } else if (kind == "finished") {
                    battle.phase = b::Phase::Finished;
                }
                b::vehicles::flushCriticalPresentations(battle);
            } catch (const std::exception& error) {
                b::p::append(errors.get(),b::p::text(b::describe(error)));
            }
            b::p::append(rows.get(),b::p::integer(actor->presented.critical_hud_pending.size()));
            b::p::append(held.get(),b::p::boolean(bool(actor->presented.critical_hud_entity)));
        }
        auto result = b::p::dict();
        b::p::set(result.get(),"pending",rows);
        b::p::set(result.get(),"entity_held",held);
        b::p::set(result.get(),"errors",errors);
        b::p::set(result.get(),"disabled",b::p::boolean(!battle.disabled_features.empty()));
        return result.release();
    } catch (const std::exception& error) {
        if (!PyErr_Occurred()) PyErr_SetString(PyExc_RuntimeError,error.what());
        return nullptr;
    }
}
PyMethodDef methods[]={{"exercise",exercise,METH_VARARGS,""},{"reenter",reenter,METH_VARARGS,""},{nullptr,nullptr,0,nullptr}};
}
PyMODINIT_FUNC initbattle_critical_hud_test() { Py_InitModule("battle_critical_hud_test",methods); }
