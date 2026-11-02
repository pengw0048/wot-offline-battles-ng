// Drive production native radio relays through the retained stock Python adapter.
#include "../bigworld/stock_native_battle_subsystems.hpp"
#include "../bigworld/stock_native_battle_startup.hpp"
#include "../include/wot/stock_battle_flash_invoke.hpp"

namespace native_battle {
struct VisibilityCore {};
struct LocalCriticalOwner {};
struct RemoteVehicle {};
struct ReplicaBinding {};
struct FallenFoliage {};
struct DestructibleCatalog {};
class ReloadSound {};
Visibility::~Visibility() = default;
Critical::~Critical() = default;
Remote::~Remote() = default;
World::~World() = default;
LocalGun::~LocalGun() = default;
Projectiles::~Projectiles() = default;
Battle::Battle() = default;
Battle::~Battle() = default;
Actor* Battle::vehicle(const std::string& key) const {
    const auto found = vehicles->actors.find(key);
    return found == vehicles->actors.end() ? nullptr : found->second.get();
}
namespace startup { namespace server {
PyObject* radio(Battle& battle) { return battle.engine.server.get(); }
} }
}
namespace {
namespace b = native_battle;
PyObject* relay(PyObject*, PyObject* args) {
    try {
        PyObject *avatar, *radio, *messages;
        int worker = 0;
        if (!PyArg_ParseTuple(args, "OOO|i", &avatar, &radio, &messages, &worker)) return nullptr;
        b::Battle battle;
        battle.phase = b::Phase::Running;
        battle.worker = worker;
        battle.engine.avatar = b::borrow(avatar);
        battle.engine.server = b::borrow(radio);
        battle.engine.client = b::borrow(avatar);
        battle.start = b::cs::read(b::p::Ref(Py_BuildValue("{s:i}", "round_id", 1)).get());
        battle.startup = std::make_unique<b::Startup>();
        battle.vehicles = std::make_shared<b::cs::State>();
        auto actor = std::make_shared<b::Actor>();
        actor->id = 1; actor->kind = "player"; actor->engine_id = 101; actor->ready = true;
        battle.vehicles->actors["player:1"] = actor;
        auto results = b::p::list();
        const auto count = PySequence_Size(messages);
        for (Py_ssize_t i = 0; i < count; ++i) {
            b::p::Ref raw(PySequence_GetItem(messages, i));
            const auto value = b::cs::read(raw.get());
            const auto& kind = value.get("type").text;
            const bool accepted = kind == "team_command" ? b::startup::teamCommand(battle, value) : b::startup::teamChat(battle, value);
            b::p::append(results.get(), b::p::boolean(accepted));
        }
        return results.release();
    } catch (const std::exception& error) {
        if (!PyErr_Occurred()) PyErr_SetString(PyExc_RuntimeError, error.what());
        return nullptr;
    }
}
struct TextAdapter {
    using Value = std::string;
    static void setString(Value& value, const char* text) { value = text; }
    static void setNull(Value& value) { value.clear(); }
    static void setNumber(Value& value, double) { value.clear(); }
    static void setBoolean(Value& value, bool) { value.clear(); }
};
PyObject* flashUtf8(PyObject*, PyObject* args) {
    PyObject* text;
    if (!PyArg_ParseTuple(args, "O", &text)) return nullptr;
    wot::stock_battle_flash::KeepAlive keep;
    TextAdapter::Value value;
    wot::stock_battle_flash::toGFx<TextAdapter>(text, value, keep.objects);
    if (PyErr_Occurred()) return nullptr;
    return PyString_FromStringAndSize(value.data(), Py_ssize_t(value.size()));
}
PyMethodDef methods[] = {{"relay", relay, METH_VARARGS, ""}, {"flash_utf8", flashUtf8, METH_VARARGS, ""}, {nullptr, nullptr, 0, nullptr}};
}
PyMODINIT_FUNC initbattle_radio_test() { Py_InitModule("battle_radio_test", methods); }
