// Exercise the production compact relay boundary and the unchanged contact reader.
#include "stock_native_player_contacts_python.hpp"
#include "stock_native_bot_python.hpp"
#include "stock_native_client_state.hpp"
#include <algorithm>
#include <cmath>
#include <initializer_list>

namespace native_battle {
using Value = stock_native_client_state::Value;
namespace p = stock_native_bot_python;
struct Battle {
    bool worker = true, active = true;
    Value start;
    bool running() const { return active; }
    double clock() const { return 1.; }
};
namespace link {
std::optional<std::int64_t> authorityEpoch(Battle&) { return 3; }
}
namespace bots {
std::size_t calls = 0;
Value observed;
std::vector<stock_native_player_contacts::Request> requests;
std::size_t playerContacts(Battle&, const Value& players, double) {
    ++calls;
    observed = players;
    std::map<std::int64_t, stock_native_bot::HumanDescriptor> descriptors;
    std::uint64_t rejected = 0;
    if (!stock_native_player_contacts::readPlayerContacts(players, descriptors, requests, rejected))
        throw p::Error("Contact reader failed");
    return requests.size();
}
}
namespace world {
using Kind = Value::Kind;
#include "contact_relay_boundary.inc"
}
}
namespace {
PyObject* relay(PyObject*, PyObject* args) {
    PyObject* message; int worker = 1, running = 1;
    if (!PyArg_ParseTuple(args, "O|ii", &message, &worker, &running)) return nullptr;
    namespace b = native_battle;
    b::Battle battle; battle.worker = worker != 0; battle.active = running != 0;
    battle.start = stock_native_client_state::read(b::p::dict().get());
    battle.start.object["round_id"].kind = b::Value::Kind::Integer;
    battle.start.object["round_id"].integer = 7;
    b::bots::calls = 0; b::bots::requests.clear(); b::bots::observed = b::Value();
    try {
        b::world::destructibleContact(battle, stock_native_client_state::read(message));
        auto result = b::p::dict();
        b::p::set(result.get(), "calls", b::p::integer(b::bots::calls));
        b::p::set(result.get(), "requests", b::p::integer(b::bots::requests.size()));
        b::p::set(result.get(), "players", b::p::Ref(stock_native_client_state::write(b::bots::observed)));
        return result.release();
    } catch (const std::exception& error) {
        if (!PyErr_Occurred()) PyErr_SetString(PyExc_ValueError, error.what());
        return nullptr;
    }
}
PyMethodDef methods[] = {{"relay", relay, METH_VARARGS, nullptr}, {nullptr, nullptr, 0, nullptr}};
}
PyMODINIT_FUNC initcontact_relay_test() { Py_InitModule("contact_relay_test", methods); }
