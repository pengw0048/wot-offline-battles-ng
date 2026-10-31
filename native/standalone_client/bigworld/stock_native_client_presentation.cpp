// Native ports of the pinned BattleRuntime track, engine and RPM presentation on the visible client.
#if !defined(WOT_CLIENT_LIFECYCLE_HOST_BUILD)
#include "pch.hpp"
#endif
#include "stock_native_client_lifecycle_python.hpp"

namespace stock_native_client_lifecycle {
namespace presentation_steps {
namespace {
using namespace py;

p::Ref none() { return p::retain(Py_None); }
p::Ref flt(double v) { return p::Ref(PyFloat_FromDouble(v)); }
p::Ref boolean(bool v) { return p::retain(v ? Py_True : Py_False); }
bool test(PyObject* a, PyObject* b, int op) {
    const int result = PyObject_RichCompareBool(a, b, op);
    if (result < 0) throw p::Error("presentation comparison failed");
    return result != 0;
}
p::Ref maxOf(PyObject* a, PyObject* b) { return p::retain(test(b, a, Py_GT) ? b : a); }
p::Ref minOf(PyObject* a, PyObject* b) { return p::retain(test(b, a, Py_LT) ? b : a); }
p::Ref getattrOr(PyObject* o, const char* name, PyObject* fallback) {
    PyObject* v = PyObject_GetAttrString(o, name);
    if (v) return p::Ref(v);
    if (!PyErr_ExceptionMatches(PyExc_AttributeError)) throw p::Error(name);
    PyErr_Clear();
    return p::retain(fallback);
}
p::Ref get(PyObject* mapping, const char* name, PyObject* fallback = nullptr) {
    return fallback ? method(mapping, "get", p::text(name).get(), fallback) : method(mapping, "get", p::text(name).get());
}
p::Ref field(PyObject* value, const char* name, PyObject* fallback) {
    return PyDict_Check(value) ? method(value, "get", p::text(name).get(), fallback) : getattrOr(value, name, fallback);
}
p::Ref format(const char* text, std::initializer_list<PyObject*> values) {
    p::Ref tuple(PyTuple_New(Py_ssize_t(values.size())));
    Py_ssize_t i = 0;
    for (PyObject* v : values) { Py_INCREF(v); PyTuple_SET_ITEM(tuple.get(), i++, v); }
    return p::Ref(PyString_Format(p::text(text).get(), tuple.get()));
}
p::Ref absolute(PyObject* v) { return p::Ref(PyNumber_Absolute(v)); }
p::Ref clampScroll(PyObject* g, PyObject* value) {
    auto limits = unpack(global(g, "TRACK_SCROLL_LIMITS").get(), 2);
    return maxOf(limits[0].get(), minOf(limits[1].get(), value).get());
}
p::Ref localScroll(PyObject* battle, PyObject* g) {
    return method(global(g, "vehicle_physics").get(), "track_scroll", attr(battle, "_local_physics").get(), attr(battle, "_local_speed").get(),
                  attr(battle, "_local_turn_speed").get(), attr(battle, "_local_airborne").get(), attr(battle, "_local_drive_throttle").get());
}

// BattleRuntime._simulated_rpm_and_gear(): #1513's three-gear simulated engine law for local physics.
PyObject* simulatedRpmAndGear(PyObject*, PyObject* args) {
    PyObject *battle, *g;
    if (!PyArg_ParseTuple(args, "OO", &battle, &g)) return nullptr;
    try {
        auto descriptor = attr(battle, "_local_descriptor");
        if (descriptor.get() == Py_None) raise("player descriptor is unavailable for RPM");
        auto physics = field(descriptor.get(), "physics", p::dict().get());
        auto raw_limits = field(physics.get(), "speedLimits", p::Ref(PyTuple_New(0)).get());
        auto limits = p::Ref(PySequence_Tuple(truthy(raw_limits) ? raw_limits.get() : p::Ref(PyTuple_New(0)).get()));
        if (PyTuple_GET_SIZE(limits.get()) < 2) raise("#1513 vehicle speed limits are unavailable");
        auto component = [&](Py_ssize_t i) { return std::fabs(PyFloat_AsDouble(p::Ref(PyNumber_Float(PyTuple_GET_ITEM(limits.get(), i))).get())); };
        const double speed_range = (component(0) + component(1)) / 3.0;
        if (speed_range <= 0.0) raise("#1513 vehicle speed range is invalid");
        auto speed = absolute(p::Ref(PyNumber_Float(attr(battle, "_local_speed").get())).get());
        if (attr(battle, "_local_physics").get() != Py_None) {
            // A neutral turn drives the belts without hull translation; feed the belt speed into RPM.
            auto scroll = unpack(localScroll(battle, g).get(), 2);
            replace(speed, maxOf(speed.get(), absolute(scroll[0].get()).get()));
            replace(speed, maxOf(speed.get(), absolute(scroll[1].get()).get()));
        }
        const double value = PyFloat_AsDouble(p::Ref(PyNumber_Float(speed.get())).get());
        if (value < 0.05) return Py_BuildValue("(di)", 0.0, 0);
        double gear = std::ceil(std::floor(value * 50.0) / 50.0 / speed_range);
        gear = gear > 1.0 ? gear : 1.0;
        const double rpm = std::fabs(1.0 + (value - gear * speed_range) / speed_range);
        const double capped = rpm < 1.2 ? rpm : 1.2;
        auto gear_int = p::Ref(PyNumber_Int(flt(gear).get()));
        auto limit = integer(255);
        return PyTuple_Pack(2, flt(capped > 0.0 ? capped : 0.0).get(), minOf(limit.get(), gear_int.get()).get());
    } catch (const std::exception& e) { return fail(e); }
}

// BattleRuntime._publish_rpm(now, force=False): own-vehicle auxiliary physics at the RPM cadence.
PyObject* publishRpm(PyObject*, PyObject* args) {
    PyObject *battle, *now, *force_raw, *g;
    if (!PyArg_ParseTuple(args, "OOOO", &battle, &now, &force_raw, &g)) return nullptr;
    try {
        const bool force = truthy(p::retain(force_raw));
        if (!force && test(now, attr(battle, "_next_rpm_time").get(), Py_LT)) Py_RETURN_FALSE;
        auto pair = unpack(dispatch(battle, "_simulated_rpm_and_gear", "simulated_rpm_and_gear", {}, {g}).get(), 2);
        setAttr(battle, "_next_rpm_time", p::Ref(PyNumber_Add(now, global(g, "RPM_PRESENTATION_SECONDS").get())).get());
        p::Ref left = flt(0.0), right = flt(0.0);
        if (attr(battle, "_local_physics").get() != Py_None) {
            auto scroll = unpack(localScroll(battle, g).get(), 2);
            replace(left, clampScroll(g, scroll[0].get()));
            replace(right, clampScroll(g, scroll[1].get()));
        }
        p::Ref payload(PyTuple_Pack(7, attr(battle, "_local_yaw").get(), attr(battle, "_local_pitch").get(), attr(battle, "_local_roll").get(),
                                    left.get(), right.get(), pair[0].get(), pair[1].get()));
        if (!force && test(payload.get(), attr(battle, "_last_presented_rpm").get(), Py_EQ)) Py_RETURN_FALSE;
        auto publish = getattrOr(attr(battle, "_binding").get(), "avatar_aux_physics", Py_None);
        if (!PyCallable_Check(publish.get())) raise("#1513 own-vehicle auxiliary physics boundary is unavailable");
        p::Ref(PyObject_CallObject(publish.get(), payload.get()));
        setAttr(battle, "_last_presented_rpm", payload.get());
        auto zero = flt(0.0);
        if (test(now, getattrOr(battle, "_engine_audio_report_at", zero.get()).get(), Py_GE)) {
            setAttr(battle, "_engine_audio_report_at", p::Ref(PyNumber_Add(now, flt(5.0).get())).get());
            auto server = attr(battle, "_server");
            auto entity = server.get() != Py_None ? serverEntity(battle, attr(server.get(), "vehicle_id").get()) : none();
            auto appearance = getattrOr(entity.get(), "appearance", Py_None);
            auto detailed = getattrOr(appearance.get(), "detailedEngineState", Py_None);
            p::Ref(PyImport_ImportModule("gui.mods.offline_lan_0922.engine_audio"));
            auto observer = getattrOr(appearance.get(), "_offlineEngineRelay", Py_None);
            if (observer.get() != Py_None) method(observer.get(), "report", p::text("sample").get());
            auto replay = getattrOr(battle, "_replay_mode", Py_False);
            auto owner = boolean(getattrOr(appearance.get(), "engineAudition", Py_None).get() != Py_None);
            auto mode = getattrOr(detailed.get(), "mode", Py_None);
            auto rpm = getattrOr(detailed.get(), "rpm", Py_None);
            writeStdout(format("[Offline LAN 0.9.22] ENGINE_AUDIO local replay=%s owner=%s mode=%s rpm_input=%.3f rpm_state=%s speed=%.3f\n",
                               {replay.get(), owner.get(), mode.get(), pair[0].get(), rpm.get(), attr(battle, "_local_speed").get()}).get());
        }
        Py_RETURN_TRUE;
    } catch (const std::exception& e) { return fail(e); }
}

// BattleRuntime._report_track_surface(entity, owner, receipt, now=None): stock terrain/effect inputs, read only.
PyObject* reportTrackSurface(PyObject*, PyObject* args) {
    PyObject *battle, *entity, *owner, *receipt, *now_raw, *g;
    if (!PyArg_ParseTuple(args, "OOOOOO", &battle, &entity, &owner, &receipt, &now_raw, &g)) return nullptr;
    try {
        auto diagnostics = getattrOr(battle, "_frame_diagnostics", Py_None);
        if (!truthy(getattrOr(diagnostics.get(), "enabled", Py_False))) Py_RETURN_FALSE;
        auto now = now_raw == Py_None ? method(battle, "_clock") : p::retain(now_raw);
        auto zero = flt(0.0);
        if (test(now.get(), get(receipt, "_surface_report_at", zero.get()).get(), Py_LT)) Py_RETURN_FALSE;
        if (PyObject_SetItem(receipt, p::text("_surface_report_at").get(), p::Ref(PyNumber_Add(now.get(), flt(5.0).get())).get()) < 0)
            throw p::Error("receipt");
        try {
            auto errors = p::dict();
            // read_value: a zero-argument PyTrackScroll reader is called; a failure is recorded, not raised.
            auto read = [&](PyObject* obj, const char* key, PyObject* fallback) -> p::Ref {
                try {
                    auto value = getattrOr(obj, key, fallback);
                    return PyCallable_Check(value.get()) ? p::Ref(PyObject_CallFunctionObjArgs(value.get(), nullptr)) : std::move(value);
                } catch (const std::exception& e) {
                    if (PyErr_Occurred() && !PyErr_ExceptionMatches(PyExc_Exception)) throw;
                    auto error = caught(e);
                    put(errors.get(), key, method(battle, "_bounded_failure_reason", error.get()));
                    return p::retain(fallback);
                }
            };
            auto entity_dict = attr(entity, "__dict__");
            if (PyObject_SetItem(entity_dict.get(), p::text("_offlineTrackEffectDiagnostic").get(), Py_True) < 0) throw p::Error("dict");
            auto effect_receipt = method(entity_dict.get(), "get", p::text("_offlineTrackEffectReceipt").get());
            auto appearance = getattrOr(entity, "appearance", Py_None);
            auto sensor = getattrOr(appearance.get(), "terrainMatKindSensor", Py_None);
            auto manager = getattrOr(appearance.get(), "customEffectManager", Py_None);
            auto effect_args = getattrOr(manager.get(), "_CustomEffectManager__variableArgs", p::dict().get());
            auto scroll = getattrOr(appearance.get(), "trackScrollController", Py_None);
            auto materials = getattrOr(attr(battle, "_runtime").get(), "material_kinds", Py_None);
            auto indexes = getattrOr(materials.get(), "EFFECT_MATERIAL_INDEXES_BY_IDS", p::dict().get());
            auto names = getattrOr(materials.get(), "EFFECT_MATERIALS", p::Ref(PyTuple_New(0)).get());
            auto effect_names = p::dict();
            for (const char* side : {"L", "R"}) {
                auto index = method(indexes.get(), "get", method(effect_args.get(), "get", p::text(std::string("matkind") + side).get()).get());
                const Py_ssize_t count = PyObject_Size(names.get());
                if (count < 0) throw p::Error("len");
                const bool valid = index.get() != Py_None && test(integer(0).get(), index.get(), Py_LE) && test(index.get(), integer(count).get(), Py_LT);
                put(effect_names.get(), side, valid ? p::Ref(PyObject_GetItem(names.get(), index.get())) : none());
            }
            auto first4 = [&](p::Ref value) {
                auto list = p::Ref(PySequence_List(truthy(value) ? value.get() : p::Ref(PyTuple_New(0)).get()));
                return p::Ref(PySequence_GetSlice(list.get(), 0, 4));
            };
            auto start = attr(battle, "_start_message");
            auto row = literal(21);
            put(row.get(), "owner", p::retain(owner));
            put(row.get(), "round", get(truthy(start) ? start.get() : p::dict().get(), "round_id"));
            put(row.get(), "entity", getattrOr(entity, "bw_entity_id", getattrOr(entity, "id", Py_None).get()));
            auto position = p::Ref(PyObject_Str(getattrOr(entity, "position", Py_None).get()));
            put(row.get(), "position", p::Ref(PySequence_GetSlice(position.get(), 0, 160)));
            put(row.get(), "alive", boolean(truthy(read(entity, "isAlive", Py_False))));
            put(row.get(), "visible", boolean(truthy(getattrOr(entity, "_offlineNativeDrawVisible", Py_True))));
            put(row.get(), "damaged_model",
                boolean(truthy(read(getattrOr(appearance.get(), "damageState", Py_None).get(), "isCurrentModelDamaged", Py_False))));
            put(row.get(), "sensor_present", boolean(sensor.get() != Py_None));
            put(row.get(), "manager_present", boolean(manager.get() != Py_None));
            put(row.get(), "scroll_present", boolean(scroll.get() != Py_None));
            auto empty = p::Ref(PyTuple_New(0));
            put(row.get(), "speed_info", first4(read(read(entity, "speedInfo", Py_None).get(), "value", empty.get())));
            put(row.get(), "sensor_materials", first4(read(sensor.get(), "matKinds", empty.get())));
            put(row.get(), "appearance_materials", first4(read(appearance.get(), "terrainMatKind", empty.get())));
            put(row.get(), "in_water", boolean(truthy(getattrOr(appearance.get(), "isInWater", Py_False))));
            auto inputs = p::dict();
            for (const char* key : {"matkindL", "matkindR", "deltaL", "deltaR", "directionL", "directionR", "speed", "isUnderWater"})
                put(inputs.get(), key, method(effect_args.get(), "get", p::text(key).get()));
            put(row.get(), "effect_inputs", inputs);
            put(row.get(), "effect_sample_phase", p::text("cached_before_next_stock_update"));
            put(row.get(), "completed_effect_update", p::retain(effect_receipt.get()));
            if (truthy(effect_receipt)) {
                auto age = maxOf(zero.get(), p::Ref(PyNumber_Subtract(now.get(), p::Ref(PyObject_GetItem(effect_receipt.get(), p::text("at").get())).get())).get());
                put(row.get(), "effect_update_age_ms", p::Ref(PyNumber_Multiply(age.get(), flt(1000.0).get())));
            } else {
                put(row.get(), "effect_update_age_ms", none());
            }
            put(row.get(), "effect_materials", effect_names);
            auto scroll_values = p::dict();
            for (const char* key : {"leftScroll", "rightScroll", "leftContact", "rightContact"}) put(scroll_values.get(), key, read(scroll.get(), key, Py_None));
            put(row.get(), "scroll", scroll_values);
            put(row.get(), "read_errors", errors);
            p::Ref separators(Py_BuildValue("(ss)", ",", ":"));
            auto json = callKw(global(g, "json").get(), "dumps", {row.get()}, {{"separators", separators.get()}});
            writeStdout(format("[Offline LAN 0.9.22] TRACK SURFACE %s\n", {json.get()}).get());
        } catch (const std::exception& e) {
            if (PyErr_Occurred() && !PyErr_ExceptionMatches(PyExc_Exception)) throw;
            auto error = caught(e);
            callKw(battle, "_warn_optional_failure", {p::text("track surface diagnostics").get(), error.get()}, {{"disable", Py_False}});
            Py_RETURN_FALSE;
        }
        Py_RETURN_TRUE;
    } catch (const std::exception& e) { return fail(e); }
}

// BattleRuntime._bot_track_params(record, entity): the descriptor-derived belt parameters, once.
PyObject* botTrackParams(PyObject*, PyObject* args) {
    PyObject *battle, *record, *entity, *g;
    if (!PyArg_ParseTuple(args, "OOOO", &battle, &record, &entity, &g)) return nullptr;
    try {
        auto params = get(record, "track_params");
        if (params.get() == Py_None) {
            replace(params, method(global(g, "vehicle_physics").get(), "derive_params", attr(entity, "typeDescriptor").get()));
            if (PyObject_SetItem(record, p::text("track_params").get(), params.get()) < 0) throw p::Error("track_params");
        }
        return params.release();
    } catch (const std::exception& e) { return fail(e); }
}

// BattleRuntime._bot_engine_mode(alive, speed, turn): #1513's (power, movementFlags) for one Bot.
PyObject* botEngineMode(PyObject*, PyObject* args) {
    PyObject *battle, *alive, *speed, *turn, *g;
    if (!PyArg_ParseTuple(args, "OOOOO", &battle, &alive, &speed, &turn, &g)) return nullptr;
    try {
        if (!truthy(p::retain(alive))) return PyTuple_Pack(2, global(g, "ENGINE_MODE_OFF").get(), integer(0).get());
        p::Ref flags = integer(0);
        auto apply = [&](const char* flag) { replace(flags, p::Ref(PyNumber_InPlaceOr(flags.get(), global(g, flag).get()))); };
        auto moving = global(g, "BOT_MOVING_SPEED");
        if (test(speed, moving.get(), Py_GT)) apply("_MOVEMENT_FORWARD");
        else if (test(speed, p::Ref(PyNumber_Negative(moving.get())).get(), Py_LT)) apply("_MOVEMENT_BACKWARD");
        auto turning = global(g, "BOT_TURNING_RATE");
        if (test(turn, p::Ref(PyNumber_Negative(turning.get())).get(), Py_LT)) apply("_MOVEMENT_ROTATE_LEFT");
        else if (test(turn, turning.get(), Py_GT)) apply("_MOVEMENT_ROTATE_RIGHT");
        if (truthy(flags)) return PyTuple_Pack(2, global(g, "ENGINE_MODE_RUNNING").get(), flags.get());
        return PyTuple_Pack(2, global(g, "ENGINE_MODE_IDLE").get(), integer(0).get());
    } catch (const std::exception& e) { return fail(e); }
}

// BattleRuntime._report_bot_tracks(vehicle, left, right, mode, now): what the scroll controller holds.
PyObject* reportBotTracks(PyObject*, PyObject* args) {
    PyObject *battle, *vehicle, *left, *right, *mode, *now, *g;
    if (!PyArg_ParseTuple(args, "OOOOOOO", &battle, &vehicle, &left, &right, &mode, &now, &g)) return nullptr;
    try {
        auto vehicle_dict = attr(vehicle, "__dict__");
        auto remote = p::text("remote");
        dispatch(battle, "_report_track_surface", "report_track_surface", {vehicle, remote.get(), vehicle_dict.get()}, {{"now", now}},
                 {vehicle, remote.get(), vehicle_dict.get(), now, g});
        auto previous = attr(battle, "_track_report_time");
        if (previous.get() != Py_None &&
            test(p::Ref(PyNumber_Subtract(now, previous.get())).get(), global(g, "TRACK_REPORT_SECONDS").get(), Py_LT))
            Py_RETURN_FALSE;
        setAttr(battle, "_track_report_time", now);
        auto appearance = getattrOr(vehicle, "appearance", Py_None);
        auto detailed = getattrOr(appearance.get(), "detailedEngineState", Py_None);
        auto unknown = p::text("?");
        writeStdout(format("[Offline LAN 0.9.22] ENGINE_AUDIO remote id=%s visible=%s owner=%s mode=%s rpm=%s\n",
                           {getattrOr(vehicle, "bw_entity_id", unknown.get()).get(), getattrOr(vehicle, "_offlineNativeDrawVisible", Py_True).get(),
                            boolean(getattrOr(appearance.get(), "engineAudition", Py_None).get() != Py_None).get(),
                            getattrOr(detailed.get(), "mode", Py_None).get(), getattrOr(detailed.get(), "rpm", Py_None).get()}).get());
        auto feed_state = getattrOr(vehicle, "track_feed_state", Py_None);
        std::vector<p::Ref> feed;
        if (PyCallable_Check(feed_state.get())) feed = unpack(p::Ref(PyObject_CallFunctionObjArgs(feed_state.get(), nullptr)).get(), 4);
        else { feed.push_back(none()); feed.push_back(p::text("unavailable")); feed.push_back(none()); feed.push_back(none()); }
        auto readback = method(vehicle, "track_scroll_readback");
        auto error = attr(attr(battle, "_remote_factory").get(), "track_animation_error");
        writeStdout(format("[Offline LAN 0.9.22] bot tracks id=%s mode=%r fed=(%.3f, %.3f) "
                           "wrote=%r outcome=%s hidden=%s engine=%s scroll=%r error=%r\n",
                           {attr(vehicle, "bw_entity_id").get(), mode, left, right, feed[0].get(), feed[1].get(), feed[2].get(), feed[3].get(),
                            readback.get(), error.get()}).get());
        Py_RETURN_TRUE;
    } catch (const std::exception& e) { return fail(e); }
}

PyMethodDef methods[] = {
    {"simulated_rpm_and_gear", simulatedRpmAndGear, METH_VARARGS, "#1513's simulated engine RPM and gear."},
    {"publish_rpm", publishRpm, METH_VARARGS, "Publish own-vehicle auxiliary physics."},
    {"report_track_surface", reportTrackSurface, METH_VARARGS, "Report stock track terrain/effect inputs."},
    {"bot_track_params", botTrackParams, METH_VARARGS, "One Bot's descriptor-derived belt parameters."},
    {"bot_engine_mode", botEngineMode, METH_VARARGS, "One Bot's #1513 engine mode."},
    {"report_bot_tracks", reportBotTracks, METH_VARARGS, "Report what a Bot's scroll controller holds."},
    {nullptr, nullptr, 0, nullptr}};
}
}

PyMethodDef* presentation_steps_methods() { return presentation_steps::methods; }
}
