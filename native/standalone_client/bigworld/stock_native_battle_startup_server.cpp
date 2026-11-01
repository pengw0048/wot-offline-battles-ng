// The stock-facing Avatar/Vehicle mailbox: #1513 calls these as Avatar.base/.cell server methods through compat.
#if !defined(WOT_NATIVE_BATTLE_HOST_BUILD)
#include "pch.hpp"
#endif
#include "stock_native_battle_startup_internal.hpp"

namespace native_battle {
namespace startup {
namespace {
struct EnterState { std::string phase, message; };

// The mailbox state; one object per round, fenced by the battle generation that created it.
struct Server {
    PyObject_HEAD
    std::uint64_t generation;
    std::int64_t vehicle_id, bound_vehicle_id;
    bool arena_vehicle_added, enter_started, enter_completed, ready_requested, client_ready, ready_publish_started;
    bool destroyed, leave_requested;
    std::string* enter_error;
    std::string* ready_publish_error;
    std::map<std::int64_t, EnterState>* enter_states;
    PyObject* radio;
    PyObject* client_context;
};
struct RadioSender { PyObject_HEAD std::uint64_t generation; };

PyTypeObject ServerType = {PyVarObject_HEAD_INIT(nullptr, 0) "offline_native_battle.AvatarServer"};
PyTypeObject SenderType = {PyVarObject_HEAD_INIT(nullptr, 0) "offline_native_battle.RadioSender"};

Server* self(PyObject* o) { return reinterpret_cast<Server*>(o); }
// The battle this mailbox belongs to, or null once that round is over.
Battle* owner(Server* s) {
    Battle* b = current();
    return b && b->generation == s->generation ? b : nullptr;
}
Battle& live(Server* s) {
    Battle* b = owner(s);
    if (!b) throw p::Error("Avatar server is destroyed");
    return *b;
}
PyObject* ok(bool v) { return p::boolean(v).release(); }
PyObject* none() { Py_RETURN_NONE; }
template<class Body> PyObject* guarded(Body body) {
    try { return body(); }
    catch (const std::exception& e) { if (!PyErr_Occurred()) PyErr_SetString(PyExc_RuntimeError, e.what()); return nullptr; }
}
void resetEnter(Server* s) {
    s->enter_started = s->enter_completed = s->ready_requested = s->client_ready = s->ready_publish_started = false;
    s->enter_error->clear();
    s->ready_publish_error->clear();
}
bool bindOnce(Battle& b, Server* s, std::int64_t vehicle_id) {
    if (s->bound_vehicle_id == vehicle_id) return false;
    if (s->bound_vehicle_id) throw p::Error("Avatar is already bound to another Vehicle");
    binding::avatarSelectVehicle(b, vehicle_id);
    s->bound_vehicle_id = vehicle_id;
    return true;
}
p::Ref sendInput(Server* s, const char* kind, PyObject* payload) {
    Battle& b = live(s);
    if (!s->vehicle_id || !s->client_ready) throw p::Error("Vehicle is not ready");
    return p::boolean(local::control(b, kind, payload));
}
p::Ref payload(std::initializer_list<std::pair<const char*, PyObject*>> items) {
    auto out = p::dict();
    for (const auto& item : items) PyDict_SetItemString(out.get(), item.first, item.second);
    return out;
}

void serverDealloc(PyObject* o) {
    auto* s = self(o);
    delete s->enter_error;
    delete s->ready_publish_error;
    delete s->enter_states;
    Py_XDECREF(s->radio);
    Py_XDECREF(s->client_context);
    PyObject_Del(o);
}

// Stock local entry: install client-server pose state before PlayerAvatar.vehicle_onEnterWorld runs.
PyObject* prepareVehicleEnter(PyObject* o, PyObject* args) {
    PyObject* vehicle;
    if (!PyArg_ParseTuple(args, "O", &vehicle)) return nullptr;
    return guarded([&] {
        auto* s = self(o);
        Battle* b = owner(s);
        if (!b || s->destroyed) return ok(false);
        const std::int64_t id = asInt(py::attr(vehicle, "id").get());
        if (s->vehicle_id && id != s->vehicle_id) return ok(false);
        local::preparePresentation(*b, vehicle);
        return ok(true);
    });
}
PyObject* acceptVehicleEnter(PyObject* o, PyObject* args) {
    PyObject* raw;
    if (!PyArg_ParseTuple(args, "O", &raw)) return nullptr;
    return guarded([&] {
        auto* s = self(o);
        if (!owner(s) || s->destroyed) return ok(false);
        const std::int64_t id = asInt(raw);
        auto found = s->enter_states->find(id);
        if (found == s->enter_states->end() || found->second.phase == "started") (*s->enter_states)[id] = {"started", ""};
        if (!s->vehicle_id) s->vehicle_id = id;
        else if (s->vehicle_id != id) return ok(false);
        s->enter_started = true;
        return ok(true);
    });
}
PyObject* completeVehicleEnter(PyObject* o, PyObject* args) {
    PyObject* raw;
    if (!PyArg_ParseTuple(args, "O", &raw)) return nullptr;
    return guarded([&] {
        auto* s = self(o);
        if (!owner(s) || s->destroyed) return ok(false);
        const std::int64_t id = asInt(raw);
        auto found = s->enter_states->find(id);
        if (found == s->enter_states->end() || found->second.phase == "failed") return ok(false);
        found->second = {"completed", ""};
        if (id != s->vehicle_id || !s->enter_started) return ok(true);
        if (!s->enter_error->empty()) return ok(false);
        s->enter_completed = true;
        return ok(true);
    });
}
PyObject* failVehicleEnter(PyObject* o, PyObject* args) {
    PyObject *raw, *error;
    if (!PyArg_ParseTuple(args, "OO", &raw, &error)) return nullptr;
    return guarded([&] {
        auto* s = self(o);
        if (!owner(s) || s->destroyed) return ok(false);
        const std::int64_t id = asInt(raw);
        const std::string message = p::string(str(error).get());
        (*s->enter_states)[id] = {"failed", message};
        if (id == s->vehicle_id) *s->enter_error = message;
        return ok(true);
    });
}
PyObject* vehicleEnterStatusPy(PyObject* o, PyObject* args) {
    PyObject* raw;
    if (!PyArg_ParseTuple(args, "O", &raw)) return nullptr;
    return guarded([&] {
        auto* s = self(o);
        auto found = s->enter_states->find(asInt(raw));
        if (found == s->enter_states->end()) return Py_BuildValue("(sO)", "pending", Py_None);
        if (found->second.message.empty()) return Py_BuildValue("(sO)", found->second.phase.c_str(), Py_None);
        return Py_BuildValue("(ss)", found->second.phase.c_str(), found->second.message.c_str());
    });
}
PyObject* forgetVehicleEnterPy(PyObject* o, PyObject* args) {
    PyObject* raw;
    if (!PyArg_ParseTuple(args, "O", &raw)) return nullptr;
    return guarded([&] { self(o)->enter_states->erase(asInt(raw)); return none(); });
}
PyObject* bindToVehicle(PyObject* o, PyObject* args) {
    PyObject* raw;
    if (!PyArg_ParseTuple(args, "O", &raw)) return nullptr;
    return guarded([&] {
        auto* s = self(o);
        Battle* b = owner(s);
        if (!b || s->destroyed) return ok(false);
        const std::int64_t id = asInt(raw);
        if (!s->vehicle_id) s->vehicle_id = id;
        if (id != s->vehicle_id) throw p::Error("cannot bind unknown Vehicle");
        bindOnce(*b, s, id);
        return ok(true);
    });
}
// StrategicCamera/ArtyCamera need this mailbox admitted; the offline Avatar owns no server position.
PyObject* moveTo(PyObject* o, PyObject* args) {
    PyObject* position;
    if (!PyArg_ParseTuple(args, "O", &position)) return nullptr;
    return guarded([&] {
        auto* s = self(o);
        if (!owner(s) || s->destroyed) return ok(false);
        for (Py_ssize_t i = 0; i < 3; ++i)
            if (!std::isfinite(asFloat(p::item(position, i).get()))) throw p::Error("Avatar move position is invalid");
        return ok(true);
    });
}
PyObject* switchViewPointOrBindToVehicle(PyObject* o, PyObject* args) {
    PyObject *viewpoint, *id;
    if (!PyArg_ParseTuple(args, "OO", &viewpoint, &id)) return nullptr;
    return guarded([&] {
        auto* s = self(o);
        Battle* b = owner(s);
        if (!b || s->destroyed) return ok(false);
        return ok(feedback::switchPostmortemViewpoint(*b, p::flag(viewpoint), asInt(id)));
    });
}
// Vehicle.onEnterWorld calls this before its engine callback returns: record the request only.
PyObject* setClientReady(PyObject* o, PyObject*) {
    auto* s = self(o);
    if (!owner(s) || s->destroyed || s->client_ready || s->ready_requested) return ok(false);
    s->ready_requested = true;
    return ok(true);
}
PyObject* flushClientReadyPy(PyObject* o, PyObject*) {
    return guarded([&] { Battle* b = owner(self(o)); return ok(b ? flushClientReady(*b) : false); });
}
PyObject* sendStateToOwnClient(PyObject* o, PyObject*) {
    if (!self(o)->vehicle_id) { PyErr_SetString(PyExc_RuntimeError, "Vehicle state requested before binding"); return nullptr; }
    Py_RETURN_NONE;
}
PyObject* syncVehicleAttrs(PyObject* o, PyObject* args) {
    PyObject* attrs;
    if (!PyArg_ParseTuple(args, "O", &attrs)) return nullptr;
    return guarded([&] {
        if (!PyDict_Check(attrs)) throw p::Error("attrs must be a dict");
        Battle& b = live(self(o));
        py::call(b.engine.avatar.get(), "syncVehicleAttrs", p::Ref(PyDict_Copy(attrs)).get());
        return none();
    });
}
// PlayerAvatar.moveVehicle already notified the native filter; the mailbox owns only the LAN relay.
PyObject* vehicleMoveWith(PyObject* o, PyObject* args) {
    PyObject* flags;
    if (!PyArg_ParseTuple(args, "O", &flags)) return nullptr;
    return guarded([&] { return sendInput(self(o), "move", payload({{"flags", p::integer(asInt(flags)).get()}}).get()).release(); });
}
PyObject* setCruiseControlMode(PyObject* o, PyObject* args) {
    PyObject* mode;
    if (!PyArg_ParseTuple(args, "O", &mode)) return nullptr;
    return guarded([&] { return sendInput(self(o), "cruise", payload({{"mode", p::integer(asInt(mode)).get()}}).get()).release(); });
}
PyObject* vehicleChangeSetting(PyObject* o, PyObject* args) {
    PyObject *code, *value;
    if (!PyArg_ParseTuple(args, "OO", &code, &value)) return nullptr;
    return guarded([&] {
        auto* s = self(o);
        Battle& b = live(s);
        if (local::changeVehicleSetting(b, int(asInt(code)), value)) return none();
        py::invoke(need(b.engine.avatar.get(), "updateVehicleSetting").get(), p::integer(s->vehicle_id).get(), code, value);
        return none();
    });
}
PyObject* trackWorld(PyObject* o, PyObject* args) {
    PyObject* point;
    if (!PyArg_ParseTuple(args, "O", &point)) return nullptr;
    return guarded([&] { return sendInput(self(o), "track_world", payload({{"point", point}}).get()).release(); });
}
PyObject* trackRelative(PyObject* o, PyObject* args) {
    PyObject* point;
    if (!PyArg_ParseTuple(args, "O", &point)) return nullptr;
    return guarded([&] { return sendInput(self(o), "track_relative", payload({{"point", point}}).get()).release(); });
}
PyObject* stopTracking(PyObject* o, PyObject* args) {
    PyObject *yaw, *pitch;
    if (!PyArg_ParseTuple(args, "OO", &yaw, &pitch)) return nullptr;
    return guarded([&] {
        return sendInput(self(o), "stop_tracking",
                         payload({{"turret_yaw", p::number(asFloat(yaw)).get()}, {"gun_pitch", p::number(asFloat(pitch)).get()}}).get()).release();
    });
}
// A locally rejected trigger cancels PlayerAvatar's acknowledgement wait through the sender.
PyObject* vehicleShoot(PyObject* o, PyObject*) {
    return guarded([&] {
        auto* s = self(o);
        auto accepted = sendInput(s, "shoot", p::dict().get());
        if (accepted.get() == Py_False) local::rejectNativeShotWait(live(s));
        return accepted.release();
    });
}
PyObject* setDevelopmentFeature(PyObject* o, PyObject* args) {
    PyObject *name, *value, *data;
    if (!PyArg_ParseTuple(args, "OOO", &name, &value, &data)) return nullptr;
    return guarded([&] {
        const std::string feature = p::string(name);
        if (feature == "pickup") {
            p::Ref pair(PyTuple_Pack(2, value, data));
            return sendInput(self(o), "development", payload({{"name", name}, {"args", pair.get()}}).get()).release();
        }
        if (feature == "server_marker") return none();
        PyErr_Format(PyExc_AttributeError, "unsupported development feature: %s", feature.c_str());
        return static_cast<PyObject*>(nullptr);
    });
}
PyObject* ignored(PyObject*, PyObject*) { Py_RETURN_NONE; }
PyObject* falsy(PyObject*, PyObject*) { Py_RETURN_FALSE; }
PyObject* chatAction(PyObject* o, PyObject* args) {
    auto* s = self(o);
    if (!owner(s) || s->destroyed || !s->radio) Py_RETURN_FALSE;
    auto handler = p::Ref(PyObject_GetAttrString(s->radio, "handle_client_action"));
    return PyObject_CallObject(handler.get(), args);
}
PyObject* requestToken(PyObject* o, PyObject* args) {
    PyObject *request, *kind;
    if (!PyArg_ParseTuple(args, "OO", &request, &kind)) return nullptr;
    return guarded([&] {
        Battle& b = live(self(o));
        auto callback = py::optional(b.engine.avatar.get(), "onTokenReceived");
        if (py::callable(callback)) py::invoke(callback.get(), request, kind, p::text("").get());
        return none();
    });
}
PyObject* sendAccountStats(PyObject* o, PyObject* args) {
    PyObject *request, *names;
    if (!PyArg_ParseTuple(args, "OO", &request, &names)) return nullptr;
    return guarded([&] {
        Battle& b = live(self(o));
        auto callback = py::optional(b.engine.avatar.get(), "receiveAccountStats");
        if (!py::callable(callback)) return none();
        auto values = p::dict();
        p::Ref iterator(PyObject_GetIter(names));
        while (PyObject* raw = PyIter_Next(iterator.get())) { p::Ref name(raw); PyDict_SetItem(values.get(), name.get(), p::integer(0).get()); }
        if (PyErr_Occurred()) throw p::Error("account stat names");
        py::invoke(callback.get(), request, pickle(values.get()).get());
        return none();
    });
}
PyObject* monitorVehicleDamagedDevices(PyObject* o, PyObject* args) {
    PyObject* raw;
    if (!PyArg_ParseTuple(args, "O", &raw)) return nullptr;
    return guarded([&] {
        auto* s = self(o);
        Battle* b = owner(s);
        if (!b || s->destroyed) return ok(false);
        return ok(visibility::monitorDamagedDevices(*b, asInt(raw)));
    });
}
PyObject* setClientCtx(PyObject* o, PyObject* args) {
    PyObject* value;
    if (!PyArg_ParseTuple(args, "O", &value)) return nullptr;
    auto* s = self(o);
    Py_INCREF(value);
    Py_XDECREF(s->client_context);
    s->client_context = value;
    Py_RETURN_NONE;
}
// Stock leaveArena: the session owns the round exit. Like the server round trip it replaces, the exit runs
// one callback later: the caller is usually a Flash button whose movie the teardown destroys.
PyObject* leaveArena(PyObject* o, PyObject* args) {
    return guarded([&] {
        auto* s = self(o);
        Battle* b = owner(s);
        if (!b || s->destroyed || s->leave_requested) return ok(false);
        if (!b->startup || !b->startup->on_local_leave || b->startup->on_local_leave.get() == Py_None) return ok(false);
        b->startup->leave_callback = own(schedule(*b, 0., [](Battle& battle) {
            battle.startup->leave_callback = Object();
            py::invoke(battle.startup->on_local_leave.get());
        }));
        s->leave_requested = true;
        return ok(true);
    });
}
std::int64_t accountCommand(Battle& b, const char* name) {
    return asInt(need(need(b.engine.modules.get(), "account_commands").get(), name).get());
}
bool knownAccountCommand(Battle& b, std::int64_t command) {
    for (const char* name : {"CMD_GET_AVATAR_SYNC", "CMD_ADD_INT_USER_SETTINGS", "CMD_DEL_INT_USER_SETTINGS"})
        if (accountCommand(b, name) == command) return true;
    return false;
}
void ackCommand(Battle& b, PyObject* request, std::int64_t command, PyObject* result, PyObject* error) {
    if (!knownAccountCommand(b, command)) {
        PyErr_Format(PyExc_AttributeError, "unsupported account command: %lld", (long long)command);
        throw p::Error("account command");
    }
    py::call(b.engine.avatar.get(), "onCmdResponse", request, result, error);
}
PyObject* doCmdStr(PyObject* o, PyObject* args) {
    PyObject *request, *command, *string;
    if (!PyArg_ParseTuple(args, "OOO", &request, &command, &string)) return nullptr;
    return guarded([&] { ackCommand(live(self(o)), request, asInt(command), p::integer(0).get(), p::text("").get()); return none(); });
}
PyObject* doCmdIntArr(PyObject* o, PyObject* args) {
    PyObject *request, *command, *values;
    if (!PyArg_ParseTuple(args, "OOO", &request, &command, &values)) return nullptr;
    return guarded([&] {
        Battle& b = live(self(o));
        if (!knownAccountCommand(b, asInt(command))) {
            PyErr_Format(PyExc_AttributeError, "unsupported account command: %lld", (long long)asInt(command));
            throw p::Error("account command");
        }
        auto dispatched = compat::dispatchAccountIntCommand(b, command, values);
        ackCommand(b, request, asInt(command), dispatched.first.get(), dispatched.second.get());
        return none();
    });
}
PyObject* startTeamChatPy(PyObject* o, PyObject*) {
    auto* s = self(o);
    if (!owner(s) || s->destroyed || !s->radio) Py_RETURN_FALSE;
    return PyObject_CallMethod(s->radio, const_cast<char*>("start_team_chat"), nullptr);
}
PyObject* closeTeamChatPy(PyObject* o, PyObject*) {
    auto* s = self(o);
    if (s->destroyed || !s->radio) Py_RETURN_FALSE;
    return PyObject_CallMethod(s->radio, const_cast<char*>("close_team_chat"), nullptr);
}
PyObject* vehicleIdGet(PyObject* o, void*) {
    auto* s = self(o);
    if (!s->vehicle_id) Py_RETURN_NONE;
    return PyLong_FromLongLong(s->vehicle_id);
}
PyObject* voipGet(PyObject* o, void*) { Py_INCREF(o); return o; }

PyMethodDef serverMethods[] = {
    {"prepareVehicleEnter", prepareVehicleEnter, METH_VARARGS, nullptr},
    {"acceptVehicleEnter", acceptVehicleEnter, METH_VARARGS, nullptr},
    {"completeVehicleEnter", completeVehicleEnter, METH_VARARGS, nullptr},
    {"failVehicleEnter", failVehicleEnter, METH_VARARGS, nullptr},
    {"vehicleEnterStatus", vehicleEnterStatusPy, METH_VARARGS, nullptr},
    {"forgetVehicleEnter", forgetVehicleEnterPy, METH_VARARGS, nullptr},
    {"bindToVehicle", bindToVehicle, METH_VARARGS, nullptr},
    {"moveTo", moveTo, METH_VARARGS, nullptr},
    {"switchViewPointOrBindToVehicle", switchViewPointOrBindToVehicle, METH_VARARGS, nullptr},
    {"setClientReady", setClientReady, METH_NOARGS, nullptr},
    {"flushClientReady", flushClientReadyPy, METH_NOARGS, nullptr},
    {"sendStateToOwnClient", sendStateToOwnClient, METH_NOARGS, nullptr},
    {"syncVehicleAttrs", syncVehicleAttrs, METH_VARARGS, nullptr},
    {"vehicle_moveWith", vehicleMoveWith, METH_VARARGS, nullptr},
    {"setCruiseControlMode", setCruiseControlMode, METH_VARARGS, nullptr},
    {"vehicle_changeSetting", vehicleChangeSetting, METH_VARARGS, nullptr},
    {"vehicle_trackWorldPointWithGun", trackWorld, METH_VARARGS, nullptr},
    {"trackRelativePointWithGun", trackRelative, METH_VARARGS, nullptr},
    {"vehicle_trackRelativePointWithGun", trackRelative, METH_VARARGS, nullptr},
    {"vehicle_stopTrackingWithGun", stopTracking, METH_VARARGS, nullptr},
    {"vehicle_shoot", vehicleShoot, METH_NOARGS, nullptr},
    {"setDevelopmentFeature", setDevelopmentFeature, METH_VARARGS, nullptr},
    {"setVehicleDevelopmentFeature", ignored, METH_VARARGS, nullptr},
    {"controlAnotherVehicle", ignored, METH_VARARGS, nullptr},
    {"vehicle_teleport", ignored, METH_VARARGS, nullptr},
    {"vehicle_replenishAmmo", ignored, METH_NOARGS, nullptr},
    {"confirmBattleResultsReceiving", ignored, METH_NOARGS, nullptr},
    {"makeDenunciation", ignored, METH_VARARGS, nullptr},
    {"messenger_onActionByClient_chat2", chatAction, METH_VARARGS, nullptr},
    {"start_team_chat", startTeamChatPy, METH_NOARGS, nullptr},
    {"close_team_chat", closeTeamChatPy, METH_NOARGS, nullptr},
    {"banUnbanUser", ignored, METH_VARARGS, nullptr},
    {"requestToken", requestToken, METH_VARARGS, nullptr},
    {"sendAccountStats", sendAccountStats, METH_VARARGS, nullptr},
    {"logStreamCorruption", ignored, METH_VARARGS, nullptr},
    {"autoAim", ignored, METH_VARARGS, nullptr},
    {"switchObserverFPV", ignored, METH_VARARGS, nullptr},
    {"switchObserverFPVControlMode", ignored, METH_VARARGS, nullptr},
    {"setRemoteCamera", ignored, METH_VARARGS, nullptr},
    {"activateEquipment", ignored, METH_VARARGS, nullptr},
    {"monitorVehicleDamagedDevices", monitorVehicleDamagedDevices, METH_VARARGS, nullptr},
    {"invalidateMicrophoneMute", ignored, METH_NOARGS, nullptr},
    {"isReady", falsy, METH_NOARGS, nullptr},
    {"isVOIPEnabled", falsy, METH_NOARGS, nullptr},
    {"isVivox", falsy, METH_NOARGS, nullptr},
    {"isYY", falsy, METH_NOARGS, nullptr},
    {"isPlayerSpeaking", falsy, METH_VARARGS, nullptr},
    {"setMicrophoneMute", ignored, METH_VARARGS, nullptr},
    {"setClientCtx", setClientCtx, METH_VARARGS, nullptr},
    {"leaveArena", leaveArena, METH_VARARGS, nullptr},
    {"doCmdStr", doCmdStr, METH_VARARGS, nullptr},
    {"doCmdIntArr", doCmdIntArr, METH_VARARGS, nullptr},
    {nullptr, nullptr, 0, nullptr}};
PyGetSetDef serverProperties[] = {
    {const_cast<char*>("vehicle_id"), vehicleIdGet, nullptr, nullptr, nullptr},
    {const_cast<char*>("voipController"), voipGet, nullptr, nullptr, nullptr},
    {nullptr, nullptr, nullptr, nullptr, nullptr}};

// The radio adapter's sender: team commands and chat relayed over the LAN client.
PyObject* senderCommand(PyObject* o, PyObject* args) {
    PyObject* request;
    if (!PyArg_ParseTuple(args, "O", &request)) return nullptr;
    return guarded([&] {
        Battle* b = current();
        if (!b || b->generation != reinterpret_cast<RadioSender*>(o)->generation || b->worker || !b->engine.avatar || !b->running() ||
            !PyDict_Check(request))
            return ok(false);
        p::Ref target_kind(p::retain(Py_None)), target_id(p::retain(Py_None));
        PyObject* stock = PyDict_GetItemString(request, "stock_target_id");
        if (stock && stock != Py_None) {
            const std::int64_t engine = asInt(stock);
            Actor* match = nullptr;
            std::size_t count = 0;
            for (auto& entry : b->vehicles->actors) {
                Actor& v = *entry.second;
                if (v.engine_id == engine && v.ready && !v.tombstone) { match = &v; ++count; }
            }
            if (count != 1 || (match->kind != "bot" && match->kind != "player")) return ok(false);
            replace(target_kind, p::text(match->kind == "player" ? "human" : "bot"));
            replace(target_id, p::integer(match->presented.network_id ? match->presented.network_id : match->id));
        }
        auto get = [&](const char* name) { PyObject* v = PyDict_GetItemString(request, name); return p::retain(v ? v : Py_None); };
        auto send = py::attr(b->engine.client.get(), "send_team_command");
        auto kw = p::dict();
        p::set(kw.get(), "target_kind", target_kind);
        p::set(kw.get(), "target_id", target_id);
        p::set(kw.get(), "cell_index", get("cell_index"));
        p::set(kw.get(), "details", get("details"));
        p::Ref positional(PyTuple_Pack(1, get("command").get()));
        return PyObject_Call(send.get(), positional.get(), kw.get());
    });
}
PyObject* senderChat(PyObject* o, PyObject* args) {
    PyObject* request;
    if (!PyArg_ParseTuple(args, "O", &request)) return nullptr;
    return guarded([&] {
        Battle* b = current();
        if (!b || b->generation != reinterpret_cast<RadioSender*>(o)->generation || b->worker || !b->engine.avatar || !b->running() ||
            !PyDict_Check(request))
            return ok(false);
        PyObject* text_value = PyDict_GetItemString(request, "text");
        return py::call(b->engine.client.get(), "send_team_chat", text_value ? text_value : Py_None).release();
    });
}
PyMethodDef senderMethods[] = {
    {"send_team_command", senderCommand, METH_VARARGS, nullptr},
    {"send_team_chat", senderChat, METH_VARARGS, nullptr},
    {nullptr, nullptr, 0, nullptr}};

void ready(PyTypeObject& type, std::size_t size, destructor dealloc, PyMethodDef* methods, PyGetSetDef* properties) {
    if (type.tp_basicsize) return;
    type.tp_basicsize = Py_ssize_t(size);
    type.tp_dealloc = dealloc;
    type.tp_flags = Py_TPFLAGS_DEFAULT;
    type.tp_methods = methods;
    type.tp_getset = properties;
    if (PyType_Ready(&type) < 0) throw p::Error("native Avatar server type failed");
}
Server* serverOf(Battle& b) {
    if (!b.engine.server || Py_TYPE(b.engine.server.get()) != &ServerType) return nullptr;
    return self(b.engine.server.get());
}
}

p::Ref createServer(Battle& b) {
    ready(ServerType, sizeof(Server), serverDealloc, serverMethods, serverProperties);
    ready(SenderType, sizeof(RadioSender), [](PyObject* o) { PyObject_Del(o); }, senderMethods, nullptr);
    auto* s = PyObject_New(Server, &ServerType);
    if (!s) throw p::Error("native Avatar server allocation failed");
    s->generation = b.generation;
    s->vehicle_id = s->bound_vehicle_id = 0;
    s->arena_vehicle_added = s->destroyed = s->leave_requested = false;
    s->enter_error = new std::string();
    s->ready_publish_error = new std::string();
    s->enter_states = new std::map<std::int64_t, EnterState>();
    s->radio = nullptr;
    s->client_context = nullptr;
    resetEnter(s);
    p::Ref object(reinterpret_cast<PyObject*>(s));
    auto* sender = PyObject_New(RadioSender, &SenderType);
    if (!sender) throw p::Error("native radio sender allocation failed");
    sender->generation = b.generation;
    p::Ref sender_object(reinterpret_cast<PyObject*>(sender));
    auto radio_type = py::attr(module("gui.mods.offline_lan_0922.tactical_radio").get(), "BattleRadioAdapter");
    auto kw = p::dict();
    p::set(kw.get(), "player_getter", py::attr(b.engine.bigworld.get(), "player"));
    p::Ref positional(PyTuple_Pack(2, b.engine.avatar.get(), sender_object.get()));
    s->radio = PyObject_Call(radio_type.get(), positional.get(), kw.get());
    if (!s->radio) throw p::Error("battle radio adapter failed");
    return object;
}

// BattleRuntime's first addVehicleToArena: create the player's Vehicle, publish its roster row, select it.
std::int64_t addVehicleToArena(Battle& b, PyObject* properties, PyObject* position, PyObject* rotation, PyObject* mission_ids, std::int64_t crew_group) {
    Server* s = serverOf(b);
    if (!s || s->destroyed) throw p::Error("Avatar server is destroyed");
    if (s->vehicle_id) throw p::Error("Vehicle already exists");
    auto created = binding::createVehicle(b, properties, position, rotation);
    if (created.get() == Py_None) throw p::Error("createEntity returned no Vehicle id");
    s->vehicle_id = asInt(created.get());
    bool selection_attempted = false;
    try {
        // A re-entrant onEnterWorld skipped the own-vehicle matrix initialization; tear it down instead.
        if (s->enter_started) throw p::Error("Vehicle entered before createEntity returned");
        binding::arenaVehicleAdded(b, s->vehicle_id, properties, false, mission_ids, crew_group);
        s->arena_vehicle_added = true;
        selection_attempted = true;
        bindOnce(b, s, s->vehicle_id);
    } catch (...) {
        PyObject *type, *value, *trace;
        PyErr_Fetch(&type, &value, &trace);
        const bool was_bound = s->bound_vehicle_id != 0;
        try { binding::destroyEntity(b, s->vehicle_id); } catch (const std::exception&) { PyErr_Clear(); }
        s->vehicle_id = s->bound_vehicle_id = 0;
        s->arena_vehicle_added = false;
        s->enter_states->clear();
        resetEnter(s);
        if (was_bound || selection_attempted) {
            try { binding::avatarSelectVehicle(b, 0); } catch (const std::exception&) { PyErr_Clear(); }
        }
        PyErr_Restore(type, value, trace);
        throw;
    }
    return s->vehicle_id;
}

// Native readiness, published from a BigWorld callback outside Vehicle.onEnterWorld.
bool flushClientReady(Battle& b) {
    Server* s = serverOf(b);
    if (!s || s->destroyed) return false;
    if (!s->ready_publish_error->empty()) throw p::Error("player Vehicle ready failed: " + *s->ready_publish_error);
    if (!s->enter_error->empty()) throw p::Error("player Vehicle enter failed: " + *s->enter_error);
    if (!s->ready_requested || s->client_ready || !s->vehicle_id || !s->arena_vehicle_added || s->bound_vehicle_id != s->vehicle_id ||
        !s->enter_completed)
        return false;
    if (!binding::isVehicleReady(b, s->vehicle_id) || s->ready_publish_started) return false;
    s->ready_publish_started = true;
    try {
        binding::avatarVehicleEntered(b);
        binding::avatarClientReady(b);
        binding::avatarReady(b);
        // ClientArena stays WAITING; the server publishes the sole PREBATTLE after every client is ready.
        s->client_ready = true;
    } catch (const std::exception& e) {
        s->client_ready = false;
        *s->ready_publish_error = describe(e);
        throw p::Error(*s->ready_publish_error);
    }
    s->ready_requested = false;
    onClientReady(b);
    return true;
}

void closeTeamChat(Battle& b) {
    Server* s = serverOf(b);
    if (!s || s->destroyed || !s->radio) return;
    py::call(s->radio, "close_team_chat");
}

void destroyServer(Battle& b) {
    Server* s = serverOf(b);
    if (!s) return;
    if (s->destroyed && !s->vehicle_id) return;
    s->destroyed = true;
    if (s->radio) py::call(s->radio, "close");
    if (!s->vehicle_id) { s->enter_states->clear(); return; }
    const std::int64_t vehicle_id = s->vehicle_id;
    s->arena_vehicle_added = false;
    binding::destroyEntity(b, vehicle_id);
    s->vehicle_id = s->bound_vehicle_id = 0;
    s->enter_states->clear();
    resetEnter(s);
}

namespace server {
PyObject* object(Battle& b) { return serverOf(b) ? b.engine.server.get() : nullptr; }
std::int64_t vehicleId(Battle& b) { Server* s = serverOf(b); return s ? s->vehicle_id : 0; }
std::pair<std::string, std::string> vehicleEnterStatus(Battle& b, std::int64_t vehicle_id) {
    Server* s = serverOf(b);
    if (!s) return {"pending", ""};
    auto found = s->enter_states->find(vehicle_id);
    if (found == s->enter_states->end()) return {"pending", ""};
    return {found->second.phase, found->second.message};
}
void forgetVehicleEnter(Battle& b, std::int64_t vehicle_id) { if (Server* s = serverOf(b)) s->enter_states->erase(vehicle_id); }
// Team radio receipts reach the stock UI through the adapter.
PyObject* radio(Battle& b) { Server* s = serverOf(b); return s && !s->destroyed ? s->radio : nullptr; }
}
}
}
