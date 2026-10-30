#include "stock_wire_replica_store.hpp"
#include <stdexcept>
#include <string>

namespace stock_wire {
namespace {
namespace cs = stock_native_client_state;
using V = cs::Value;
const char* const replica_keys[] = {"full", "own", "player_motion", "bot_motion", "player_states",
                                    "bot_states", "removed_players", "removed_bots",
                                    "bot_equipment_contracts", "type"};

std::int64_t key(const V& row) {
    const V& id = row.get("id");
    if (id.kind != V::Kind::Integer) throw std::runtime_error("replica actor has no integer id");
    return id.integer;
}
const V& list(const V& message, const char* name) {
    const V& value = message.get(name);
    if (value.kind != V::Kind::Array) throw std::runtime_error(std::string("replica field is not a list: ") + name);
    return value;
}
V shared(std::shared_ptr<const V> value) { V out; out.kind = V::Kind::Shared; out.reference = std::move(value); return out; }
}

void ReplicaStore::restate(std::map<std::int64_t, Actor>& actors, const V& states, bool bot) {
    for (const auto& item : states.array) {
        V state = item;
        auto& equipment = state.object["equipment_states"];
        if (bot && equipment.kind == V::Kind::Array) {
            // Indexes refer to the contract table that the server restates with every change.
            for (auto& entry : equipment.array) {
                auto& contract = entry.object["equipment"];
                if (contract.kind != V::Kind::Integer) continue;
                if (contract.integer < 0 || std::size_t(contract.integer) >= contracts_.size())
                    throw std::runtime_error("replica bot equipment contract is unavailable");
                contract = contracts_[std::size_t(contract.integer)];
            }
        } else if (equipment.kind == V::Kind::Null) state.object.erase("equipment_states");
        auto& actor = actors[key(state)];
        actor.state = std::move(state);
        actor.row.reset();
    }
}

void ReplicaStore::move(std::map<std::int64_t, Actor>& actors, const V& motion) {
    for (const auto& item : motion.array) {
        auto found = actors.find(key(item));
        if (found == actors.end()) throw std::runtime_error("replica motion for an actor without state");
        found->second.motion = item;
        found->second.row.reset();
    }
}

std::shared_ptr<const V> ReplicaStore::apply(V message) {
    const bool full = message.get("full").truth();
    if (!full && !valid_) throw std::runtime_error("replica delta before a full snapshot");
    valid_ = false;
    if (full) { players_.clear(); bots_.clear(); contracts_.clear(); }
    const auto& contracts = message.get("bot_equipment_contracts");
    if (contracts.kind == V::Kind::Array) {
        contracts_.clear();
        for (const auto& contract : contracts.array) contracts_.push_back(shared(std::make_shared<const V>(contract)));
    }
    for (const auto& id : list(message, "removed_players").array) players_.erase(id.integer);
    for (const auto& id : list(message, "removed_bots").array) bots_.erase(id.integer);
    restate(players_, list(message, "player_states"), false);
    restate(bots_, list(message, "bot_states"), true);
    move(players_, list(message, "player_motion"));
    move(bots_, list(message, "bot_motion"));
    auto snapshot = std::make_shared<V>();
    snapshot->kind = V::Kind::Object;
    for (const auto& field : message.object) {
        bool replica = false;
        for (const char* name : replica_keys) replica = replica || field.first == name;
        if (!replica) snapshot->object.emplace(field.first, field.second);
    }
    snapshot->object["type"].kind = V::Kind::Text;
    snapshot->object["type"].text = "snapshot";
    auto rows = [](std::map<std::int64_t, Actor>& actors, V& out) {
        out.kind = V::Kind::Array;
        for (auto& entry : actors) {
            auto& actor = entry.second;
            if (actor.state.kind != V::Kind::Object || actor.motion.kind != V::Kind::Object)
                throw std::runtime_error("replica actor lacks state or motion");
            if (!actor.row) {
                V row = actor.state;
                for (const auto& field : actor.motion.object) row.object[field.first] = field.second;
                actor.row = std::make_shared<const V>(std::move(row));
            }
            out.array.push_back(shared(actor.row));
        }
    };
    auto& players = snapshot->object["players"];
    players.kind = V::Kind::Array;
    const bool own = message.get("own").kind == V::Kind::Object;
    if (own) players.array.push_back(shared(std::make_shared<const V>(message.get("own"))));
    snapshot->object["_own"].kind = V::Kind::Boolean;
    snapshot->object["_own"].boolean = own;
    V remote;
    rows(players_, remote);
    for (auto& row : remote.array) players.array.push_back(std::move(row));
    rows(bots_, snapshot->object["bots"]);
    valid_ = true;
    return snapshot;
}

}

namespace stock_wire {
namespace {
const char* snapshot_name = "offline_client_wire_native.Snapshot";
void destroySnapshot(PyObject* capsule) {
    delete static_cast<std::shared_ptr<const V>*>(PyCapsule_GetPointer(capsule, snapshot_name));
}
PyObject* fail(const char* why) { PyErr_SetString(PyExc_ValueError, why); return nullptr; }

// The Python view of a merged snapshot: envelope, the recipient's own row and the capsule.
PyObject* replicaMessage(PyObject*, PyObject* capsule) {
    auto snapshot = snapshot_from_capsule(capsule);
    if (!snapshot) return fail("not a native replica snapshot");
    PyObject* out = PyDict_New();
    if (!out) return nullptr;
    for (const auto& field : snapshot->object) {
        if (field.first == "players" || field.first == "bots" || field.first == "_own") continue;
        PyObject* value = stock_native_client_state::write(field.second);
        if (!value || PyDict_SetItemString(out, field.first.c_str(), value) < 0) { Py_XDECREF(value); Py_DECREF(out); return nullptr; }
        Py_DECREF(value);
    }
    PyObject* players = PyList_New(0);
    PyObject* bots = PyList_New(0);
    const auto& rows = snapshot->get("players");
    if (players && bots && snapshot->get("_own").truth() && !rows.array.empty()) {
        PyObject* own = stock_native_client_state::write(rows.array.front());
        if (!own || PyList_Append(players, own) < 0) { Py_XDECREF(own); Py_DECREF(players); Py_DECREF(bots); Py_DECREF(out); return nullptr; }
        Py_DECREF(own);
    }
    if (!players || !bots || PyDict_SetItemString(out, "players", players) < 0 ||
        PyDict_SetItemString(out, "bots", bots) < 0 || PyDict_SetItemString(out, "_native_snapshot", capsule) < 0) {
        Py_XDECREF(players); Py_XDECREF(bots); Py_DECREF(out); return nullptr;
    }
    Py_DECREF(players); Py_DECREF(bots);
    return out;
}

// Every row as Python values, for the one-off replay header seed.
PyObject* completeSnapshot(PyObject*, PyObject* capsule) {
    auto snapshot = snapshot_from_capsule(capsule);
    if (!snapshot) return fail("not a native replica snapshot");
    PyObject* out = stock_native_client_state::write(*snapshot);
    if (out && PyDict_DelItemString(out, "_own") < 0) { Py_DECREF(out); return nullptr; }
    return out;
}
}

PyObject* replica_view(PyObject* capsule) { return replicaMessage(nullptr, capsule); }

PyObject* snapshot_capsule(std::shared_ptr<const stock_native_client_state::Value> snapshot) {
    return PyCapsule_New(new std::shared_ptr<const V>(std::move(snapshot)), snapshot_name, destroySnapshot);
}

std::shared_ptr<const stock_native_client_state::Value> snapshot_from_capsule(PyObject* capsule) {
    if (!PyCapsule_IsValid(capsule, snapshot_name)) return {};
    return *static_cast<std::shared_ptr<const V>*>(PyCapsule_GetPointer(capsule, snapshot_name));
}

PyMethodDef* replica_methods() {
    static PyMethodDef table[] = {
        {"replica_message", replicaMessage, METH_O, "Python view of a merged replica snapshot."},
        {"complete_snapshot", completeSnapshot, METH_O, "Every row of a merged replica snapshot as Python values."},
        {nullptr, nullptr, 0, nullptr}};
    return table;
}

}
