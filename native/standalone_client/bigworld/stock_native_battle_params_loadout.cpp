// loadout.py battle law: #1513 attribute factors through the stock crew machinery, names, modifiers and spotting inputs.
#if !defined(WOT_NATIVE_BATTLE_HOST_BUILD)
#include "pch.hpp"
#endif
#include "stock_native_battle_params.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <memory>

namespace native_battle {
namespace params {
namespace {
const char* const RATION_MARKERS[] = {"ration", "chocolate", "cola", "coffee", "pudding", "stimulator", "improvedcombatrations",
                                      "extrarations", "strongcoffee", "buchty", "onigiri", "gulaschkanone"};
const char* const BIG_REPAIR_KIT_MARKERS[] = {"largerepairkit"};
const char* const BROTHERHOOD_MARKERS[] = {"brotherhood"};
const char* const SNAP_SHOT_MARKERS[] = {"smoothturret", "snapshot"};
const char* const SMOOTH_RIDE_MARKERS[] = {"smoothdriving", "smoothride"};
const char* const SIXTH_SENSE_MARKERS[] = {"sixthsense"};
const char* const RECON_SKILL = "commander_eagleeye";
const char* const SITUATIONAL_SKILL = "radioman_finder";
const char* const CAMOUFLAGE_SKILL = "camouflage";

template<std::size_t N> bool matches(const std::string& name, const char* const (&markers)[N]) {
    for (const char* marker : markers) if (name.find(marker) != std::string::npos) return true;
    return false;
}
bool contains(const std::string& name, const char* part) { return name.find(part) != std::string::npos; }

// getattr(o, name, None) with the stock object ABI.
p::Ref attribute(PyObject* o, const char* name) { return py::optional(o ? o : Py_None, name); }
bool isNone(const p::Ref& v) { return v.get() == Py_None; }

// loadout._number: float(value), else the default (TypeError/ValueError only).
double number(PyObject* value, double fallback = 0.) {
    PyObject* f = PyNumber_Float(value);
    if (!f) {
        if (PyErr_ExceptionMatches(PyExc_TypeError) || PyErr_ExceptionMatches(PyExc_ValueError)) { PyErr_Clear(); return fallback; }
        throw p::Error("loadout number conversion failed");
    }
    const double out = PyFloat_AS_DOUBLE(f);
    Py_DECREF(f);
    return out;
}
std::string lower(std::string text) {
    for (auto& c : text) c = char(std::tolower(static_cast<unsigned char>(c)));
    return text;
}
// loadout._name_of: name, else descriptor.name, else the value itself, as lowercased text ('' when unprintable).
std::string nameOf(PyObject* value) {
    auto name = attribute(value, "name");
    if (!p::flag(name.get())) replace(name, attribute(attribute(value, "descriptor").get(), "name"));
    if (!p::flag(name.get())) replace(name, p::retain(value));
    PyObject* text = PyObject_Str(name.get());
    if (!text) { PyErr_Clear(); return {}; }
    p::Ref held(text);
    if (!PyString_Check(text)) return {};
    return lower(std::string(PyString_AS_STRING(text), std::size_t(PyString_GET_SIZE(text))));
}
// Iterate a stock sequence (`value or ()`).
template<class Body> void each(PyObject* sequence, Body body) {
    if (!sequence || sequence == Py_None || !p::flag(sequence)) return;
    p::Ref items(PySequence_List(sequence));
    for (Py_ssize_t i = 0; i < PyList_GET_SIZE(items.get()); ++i) body(PyList_GET_ITEM(items.get(), i));
}
// A crew row may be a (slot, tankman) pair.
PyObject* member(PyObject* raw) {
    return PyTuple_Check(raw) && PyTuple_GET_SIZE(raw) == 2 ? PyTuple_GET_ITEM(raw, 1) : raw;
}
p::Ref skillsOf(PyObject* tankman) {
    auto skills = attribute(tankman, "skills");
    if (isNone(skills)) replace(skills, attribute(attribute(tankman, "descriptor").get(), "skills"));
    return skills;
}
// loadout._skill_flag: a required bool flag (a property or a call), or empty when unproved.
std::optional<bool> skillFlag(PyObject* skill, const char* name) {
    PyObject* raw = PyObject_GetAttrString(skill, name);
    if (!raw) {
        if (!PyErr_ExceptionMatches(PyExc_AttributeError)) throw p::Error(name);
        PyErr_Clear();
        return {};
    }
    p::Ref value(raw);
    if (PyCallable_Check(raw)) {
        PyObject* called = PyObject_CallFunctionObjArgs(raw, nullptr);
        if (!called) { PyErr_Clear(); return {}; }
        replace(value, p::Ref(called));
    }
    if (!PyBool_Check(value.get())) return {};
    return value.get() == Py_True;
}
// loadout._skill_level: one member's level in a named skill (0 when absent, inactive or disabled).
double skillLevel(PyObject* tankman, const std::string& wanted) {
    double level = 0.;
    bool found = false;
    each(skillsOf(tankman).get(), [&](PyObject* skill) {
        if (found || nameOf(skill) != wanted) return;
        found = true;
        const auto active = skillFlag(skill, "isActive"), enabled = skillFlag(skill, "isEnable");
        if ((active && !*active) || (enabled && !*enabled)) return;
        auto raw = attribute(skill, "level");
        level = std::max(0., isNone(raw) ? 100. : number(raw.get(), 100.));
    });
    return level;
}

// ---- the stock VehicleDescrCrew skill processors the battle needs, as native callables ------------------------
enum class Processor { SmoothDriving, SmoothTurret, Pedant, Desperado, TidyPerson, Gunsmith };
using BattleFactors = std::map<std::string, double>;
struct ProcessorObject {
    PyObject_HEAD
    Processor kind;
    std::shared_ptr<BattleFactors>* result;
};
void processorDealloc(PyObject* self) {
    delete reinterpret_cast<ProcessorObject*>(self)->result;
    PyObject_Del(self);
}
double configNumber(PyObject* config, const char* name) {
    p::Ref value(PyObject_GetAttrString(config, name));
    return p::scalar(p::Ref(PyNumber_Float(value.get())).get());
}
// processor(crew, index, level, increase, active, fire, config), called by VehicleDescrCrew.callSkillProcessor.
PyObject* processorCall(PyObject* self, PyObject* args, PyObject*) {
    auto* o = reinterpret_cast<ProcessorObject*>(self);
    try {
        if (PyTuple_GET_SIZE(args) < 7) { PyErr_SetString(PyExc_TypeError, "skill processor takes 7 arguments"); return nullptr; }
        const double level = p::scalar(p::Ref(PyNumber_Float(PyTuple_GET_ITEM(args, 2))).get());
        PyObject* increase_raw = PyTuple_GET_ITEM(args, 3);
        const bool active = p::flag(PyTuple_GET_ITEM(args, 4)), fire = p::flag(PyTuple_GET_ITEM(args, 5));
        PyObject* config = PyTuple_GET_ITEM(args, 6);
        auto& result = **o->result;
        auto increase = [&] { return p::scalar(p::Ref(PyNumber_Float(increase_raw)).get()); };
        switch (o->kind) {
        case Processor::SmoothDriving:
            if (active && !fire) result["offline/moveBloom"] = 1. - (level + increase()) * configNumber(config, "shotDispersionFactorPerLevel");
            break;
        case Processor::SmoothTurret:
            if (active && !fire) result["offline/turretBloom"] = 1. - (level + increase()) * configNumber(config, "shotDispersionFactorPerLevel");
            break;
        case Processor::Pedant:
            if (active && level >= 100.) result["offline/ammoBayHealth"] = configNumber(config, "ammoBayHealthFactor");
            break;
        case Processor::Desperado:
            if (active && level >= 100.) {
                result["offline/adrenaline_health_fraction"] = configNumber(config, "vehicleHealthFraction");
                result["offline/adrenaline_reload_factor"] = configNumber(config, "gunReloadTimeFactor");
            }
            break;
        case Processor::TidyPerson:
            if (active && level >= 100.) result["offline/engine_fire_factor"] = configNumber(config, "fireStartingChanceFactor");
            break;
        case Processor::Gunsmith:
            if (active && !fire)
                result["offline/damaged_gun_factor"] = std::max(0.01, 1. - (level + increase()) * configNumber(config, "shotDispersionFactorPerLevel"));
            break;
        }
        Py_RETURN_NONE;
    } catch (const std::exception& e) {
        if (!PyErr_Occurred()) PyErr_SetString(PyExc_RuntimeError, e.what());
        return nullptr;
    }
}
PyTypeObject ProcessorType = {PyVarObject_HEAD_INIT(nullptr, 0) "offline_native_battle.SkillProcessor"};
p::Ref processor(Processor kind, const std::shared_ptr<BattleFactors>& result) {
    if (!ProcessorType.tp_call) {
        ProcessorType.tp_basicsize = sizeof(ProcessorObject);
        ProcessorType.tp_dealloc = processorDealloc;
        ProcessorType.tp_call = processorCall;
        ProcessorType.tp_flags = Py_TPFLAGS_DEFAULT;
        if (PyType_Ready(&ProcessorType) < 0) throw p::Error("skill processor type failed");
    }
    auto* o = PyObject_New(ProcessorObject, &ProcessorType);
    if (!o) throw p::Error("skill processor allocation failed");
    o->kind = kind;
    o->result = new std::shared_ptr<BattleFactors>(result);
    return p::Ref(reinterpret_cast<PyObject*>(o));
}

// loadout._collect_battle_crew_factors: effects the garage VehicleDescrCrew processors omit; empty without processors.
std::shared_ptr<BattleFactors> collectBattleCrewFactors(PyObject* descriptor_crew) {
    auto result = std::make_shared<BattleFactors>(BattleFactors{
        {"offline/moveBloom", 1.}, {"offline/turretBloom", 1.}, {"offline/ammoBayHealth", 1.},
        // crew_battle.DEFAULTS
        {"offline/adrenaline_health_fraction", 0.}, {"offline/adrenaline_reload_factor", 1.},
        {"offline/engine_fire_factor", 1.}, {"offline/damaged_gun_factor", 1.}});
    auto processors = attribute(descriptor_crew, "_skillProcessors");
    if (!PyDict_Check(processors.get())) return std::make_shared<BattleFactors>();
    p::Ref copied(PyDict_Copy(processors.get()));
    py::set(descriptor_crew, "_skillProcessors", copied.get());
    const std::pair<const char*, Processor> installs[] = {
        {"driver_smoothDriving", Processor::SmoothDriving}, {"gunner_smoothTurret", Processor::SmoothTurret},
        {"loader_pedant", Processor::Pedant}, {"loader_desperado", Processor::Desperado},
        {"driver_tidyPerson", Processor::TidyPerson}, {"gunner_gunsmith", Processor::Gunsmith}};
    for (const auto& install : installs)
        if (PyDict_SetItemString(copied.get(), install.first, processor(install.second, result).get()) < 0) throw p::Error("skill processor install failed");
    return result;
}

struct ClientModules {
    p::Ref utils, tankmen, vehicles, aspects, qualifier_type, crew_class, qualifiers_class, copy;
};
// The native #1513 factor machinery; empty off the client.
std::optional<ClientModules> clientModules() {
    auto load = [](const char* name) -> PyObject* { PyObject* m = PyImport_ImportModule(name); if (!m) PyErr_Clear(); return m; };
    PyObject* constants = load("constants");
    PyObject* tankmen = load("items.tankmen");
    PyObject* utils = load("items.utils");
    PyObject* vehicles = load("items.vehicles");
    PyObject* qualifiers = load("items.qualifiers");
    PyObject* crew = load("items.VehicleDescrCrew");
    PyObject* applier = load("VehicleQualifiersApplier");
    PyObject* copy = load("copy");
    p::Ref held[] = {p::retain(constants ? constants : Py_None), p::retain(tankmen ? tankmen : Py_None), p::retain(utils ? utils : Py_None),
                     p::retain(vehicles ? vehicles : Py_None), p::retain(qualifiers ? qualifiers : Py_None), p::retain(crew ? crew : Py_None),
                     p::retain(applier ? applier : Py_None), p::retain(copy ? copy : Py_None)};
    for (PyObject* m : {constants, tankmen, utils, vehicles, qualifiers, crew, applier, copy}) Py_XDECREF(m);
    for (const auto& m : held) if (m.get() == Py_None) return {};
    auto pick = [](PyObject* module, const char* name) -> PyObject* {
        PyObject* v = PyObject_GetAttrString(module, name);
        if (!v) PyErr_Clear();
        return v;
    };
    PyObject* aspects = pick(held[0].get(), "VEHICLE_TTC_ASPECTS");
    PyObject* qualifier_type = pick(held[4].get(), "QUALIFIER_TYPE");
    PyObject* crew_class = pick(held[5].get(), "VehicleDescrCrew");
    PyObject* qualifiers_class = pick(held[6].get(), "VehicleQualifiersApplier");
    if (!aspects || !qualifier_type || !crew_class || !qualifiers_class) {
        Py_XDECREF(aspects); Py_XDECREF(qualifier_type); Py_XDECREF(crew_class); Py_XDECREF(qualifiers_class);
        return {};
    }
    return ClientModules{p::retain(held[2].get()), p::retain(held[1].get()), p::retain(held[3].get()), p::Ref(aspects), p::Ref(qualifier_type),
                         p::Ref(crew_class), p::Ref(qualifiers_class), p::retain(held[7].get())};
}

// loadout.crew_compact_descrs: one compact descriptor per role in slot order; () when any member is unreadable.
p::Ref crewCompactDescrs(PyObject* crew) {
    p::Ref members(PySequence_List(crew && crew != Py_None && p::flag(crew) ? crew : p::Ref(PyTuple_New(0)).get()));
    bool pairs = PyList_GET_SIZE(members.get()) > 0;
    for (Py_ssize_t i = 0; i < PyList_GET_SIZE(members.get()); ++i) {
        PyObject* m = PyList_GET_ITEM(members.get(), i);
        pairs = pairs && PyTuple_Check(m) && PyTuple_GET_SIZE(m) == 2;
    }
    if (pairs) {
        p::Ref sorted(PyList_New(0));
        std::vector<std::pair<PyObject*, PyObject*>> rows;
        for (Py_ssize_t i = 0; i < PyList_GET_SIZE(members.get()); ++i) {
            PyObject* m = PyList_GET_ITEM(members.get(), i);
            rows.emplace_back(PyTuple_GET_ITEM(m, 0), PyTuple_GET_ITEM(m, 1));
        }
        std::stable_sort(rows.begin(), rows.end(), [](const std::pair<PyObject*, PyObject*>& a, const std::pair<PyObject*, PyObject*>& b) {
            const int less = PyObject_RichCompareBool(a.first, b.first, Py_LT);
            if (less < 0) throw p::Error("crew slot comparison failed");
            return less == 1;
        });
        for (const auto& row : rows) p::append(sorted.get(), p::retain(row.second));
        replace(members, std::move(sorted));
    }
    p::Ref result(PyList_New(0));
    for (Py_ssize_t i = 0; i < PyList_GET_SIZE(members.get()); ++i) {
        PyObject* m = PyList_GET_ITEM(members.get(), i);
        if (m == Py_None) return p::Ref(PyTuple_New(0));
        auto value = attribute(m, "strCD");
        if (isNone(value)) {
            auto maker = attribute(attribute(m, "descriptor").get(), "makeCompactDescr");
            if (!isNone(maker)) {
                PyObject* made = PyObject_CallFunctionObjArgs(maker.get(), nullptr);
                if (made) replace(value, p::Ref(made));
                else PyErr_Clear();
            }
        }
        if (isNone(value)) return p::Ref(PyTuple_New(0));
        p::append(result.get(), std::move(value));
    }
    return p::Ref(PyList_AsTuple(result.get()));
}

// loadout._artefact: one mounted item resolved to its items.artefacts object, or None.
p::Ref artefact(PyObject* value, PyObject* vehicles) {
    if (!value || value == Py_None) return p::retain(Py_None);
    auto byCompact = [&](PyObject* compact) {
        PyObject* item = PyObject_CallMethod(vehicles, const_cast<char*>("getItemByCompactDescr"), const_cast<char*>("O"), compact);
        if (!item) { PyErr_Clear(); return p::retain(Py_None); }
        return p::Ref(item);
    };
    if (PyInt_Check(value) || PyLong_Check(value)) {
        // FittingItem.__eq__ reads other.intCD unguarded: never compare a mounted item against 0.
        if (!p::flag(value)) return p::retain(Py_None);
        PyObject* exact = PyNumber_Int(value);
        if (!exact) { PyErr_Clear(); return p::retain(Py_None); }
        p::Ref held(exact);
        return byCompact(exact);
    }
    if (PyObject_HasAttrString(value, "updateVehicleAttrFactors")) return p::retain(value);
    auto descriptor = attribute(value, "descriptor");
    if (!isNone(descriptor) && PyObject_HasAttrString(descriptor.get(), "updateVehicleAttrFactors")) return descriptor;
    auto compact = attribute(value, "intCD");
    if (p::flag(compact.get()) && vehicles) {
        PyObject* exact = PyNumber_Int(compact.get());
        if (!exact) { PyErr_Clear(); return p::retain(Py_None); }
        p::Ref held(exact);
        return byCompact(exact);
    }
    return p::retain(Py_None);
}

// loadout._update_native_attribute_factors: #1513 VehicleDescrCrew with the actual battle crew state.
void updateNativeFactors(const ClientModules& m, PyObject* descriptor, PyObject* compact_descrs, PyObject* equipments, PyObject* factors,
                         PyObject* aspect, PyObject* activity_flags, bool is_fire) {
    p::Ref increase(PyInt_FromLong(0));
    for (Py_ssize_t i = 0; i < PyList_GET_SIZE(equipments); ++i) {
        auto value = attribute(PyList_GET_ITEM(equipments, i), "crewLevelIncrease");
        if (p::flag(value.get())) replace(increase, p::Ref(PyNumber_Add(increase.get(), value.get())));
    }
    if (PyDict_SetItemString(factors, "crewLevelIncrease", increase.get()) < 0) throw p::Error("factors write failed");
    for (Py_ssize_t i = 0; i < PyList_GET_SIZE(equipments); ++i) {
        PyObject* equipment = PyList_GET_ITEM(equipments, i);
        if (equipment != Py_None) py::call(equipment, "updateVehicleAttrFactors", descriptor, factors, aspect);
    }
    each(py::attr(descriptor, "optionalDevices").get(), [&](PyObject* device) {
        if (device != Py_None) py::call(device, "updateVehicleAttrFactors", descriptor, factors, aspect);
    });
    p::Ref applier(PyObject_CallFunctionObjArgs(m.qualifiers_class.get(), p::dict().get(), descriptor, nullptr));
    p::Ref main_skill(PyObject_GetItem(applier.get(), py::attr(m.qualifier_type.get(), "MAIN_SKILL").get()));
    p::Ref positional(PyTuple_Pack(3, descriptor, compact_descrs, main_skill.get()));
    auto keywords = p::dict();
    p::set(keywords.get(), "activityFlags", p::retain(activity_flags));
    p::set(keywords.get(), "isFire", p::boolean(is_fire));
    p::Ref crew(PyObject_Call(m.crew_class.get(), positional.get(), keywords.get()));
    auto battle = collectBattleCrewFactors(crew.get());
    for (Py_ssize_t i = 0; i < PyList_GET_SIZE(equipments); ++i) {
        PyObject* equipment = PyList_GET_ITEM(equipments, i);
        if (equipment == Py_None) continue;
        auto tags = py::attr(equipment, "tags");
        const int boosted = PySequence_Contains(tags.get(), p::text("crewSkillBattleBooster").get());
        if (boosted < 0) throw p::Error("equipment tags lookup failed");
        if (boosted) py::call(crew.get(), "boostSkillBy", equipment);
    }
    py::call(crew.get(), "onCollectFactors", factors);
    for (const auto& item : *battle) p::set(factors, item.first.c_str(), p::number(item.second));
    if (PyDict_SetItemString(factors, "camouflage", py::attr(crew.get(), "camouflageFactor").get()) < 0) throw p::Error("factors write failed");
    p::Ref dispersion(Py_BuildValue("[dd]", 1.0, 0.0));
    py::call(crew.get(), "onCollectShotDispersionFactors", dispersion.get());
    if (PyDict_SetItemString(factors, "shotDispersion", dispersion.get()) < 0) throw p::Error("factors write failed");
}

// loadout._native_attribute_factors: one fresh factor dictionary, both #1513 attribute aspects.
p::Ref nativeFactors(const ClientModules& m, PyObject* descriptor, PyObject* compact_descrs, PyObject* equipments, PyObject* flags, bool is_fire) {
    p::Ref factors(PyObject_CallMethod(m.utils.get(), const_cast<char*>("makeDefaultVehicleAttributeFactors"), nullptr));
    p::Ref crew(PySequence_List(compact_descrs));
    auto default_aspect = py::attr(m.aspects.get(), "DEFAULT"), still_aspect = py::attr(m.aspects.get(), "WHEN_STILL");
    updateNativeFactors(m, descriptor, crew.get(), equipments, factors.get(), default_aspect.get(), flags, is_fire);
    auto still = py::call(m.copy.get(), "deepcopy", factors.get());
    updateNativeFactors(m, descriptor, crew.get(), equipments, still.get(), still_aspect.get(), flags, is_fire);
    auto invisibility = p::dict();
    if (PyDict_SetItem(invisibility.get(), default_aspect.get(), p::Ref(PyObject_GetItem(factors.get(), p::text("invisibility").get())).get()) < 0 ||
        PyDict_SetItem(invisibility.get(), still_aspect.get(), p::Ref(PyObject_GetItem(still.get(), p::text("invisibility").get())).get()) < 0)
        throw p::Error("invisibility aspects write failed");
    p::set(factors.get(), "invisibility", invisibility);
    return factors;
}

// The stock factors dictionary as battle data: every dict key becomes text.
Value readFactors(PyObject* raw) {
    Value out;
    if (raw == Py_None) return out;
    if (PyBool_Check(raw)) { out.kind = Value::Kind::Boolean; out.boolean = raw == Py_True; return out; }
    if (PyInt_Check(raw)) { out.kind = Value::Kind::Integer; out.integer = PyInt_AS_LONG(raw); return out; }
    if (PyLong_Check(raw)) { out.kind = Value::Kind::Integer; out.integer = p::wide(raw); return out; }
    if (PyFloat_Check(raw)) { out.kind = Value::Kind::Number; out.number = PyFloat_AS_DOUBLE(raw); return out; }
    if (PyDict_Check(raw)) {
        out.kind = Value::Kind::Object;
        Py_ssize_t cursor = 0;
        PyObject *key, *value;
        while (PyDict_Next(raw, &cursor, &key, &value)) out.object.emplace(p::string(p::Ref(PyObject_Str(key)).get()), readFactors(value));
        return out;
    }
    if (PyList_Check(raw) || PyTuple_Check(raw)) {
        out.kind = Value::Kind::Array;
        for (Py_ssize_t i = 0; i < PySequence_Size(raw); ++i) out.array.push_back(readFactors(p::Ref(PySequence_GetItem(raw, i)).get()));
        return out;
    }
    out.kind = Value::Kind::Text;
    out.text = p::string(p::Ref(PyObject_Str(raw)).get());
    return out;
}
std::string text(const std::exception& e) {
    if (!PyErr_Occurred()) return e.what();
    return describe(e);
}
}

double Factors::number(const std::string& name, double fallback) const {
    const auto& v = get(name).resolved();
    switch (v.kind) {
    case Value::Kind::Integer: return double(v.integer);
    case Value::Kind::Number: return v.number;
    case Value::Kind::Boolean: return v.boolean ? 1. : 0.;
    case Value::Kind::Text: return v.numeric(fallback);
    default: return fallback;
    }
}

std::optional<Factors> attributeFactors(PyObject* descriptor, PyObject* crew, PyObject* equipments, const std::vector<bool>* activity,
                                        bool is_fire, std::optional<std::int64_t> crew_level) {
    auto modules = clientModules();
    if (!modules || !descriptor || descriptor == Py_None) return {};
    const auto& m = *modules;
    try {
        auto compact = crewCompactDescrs(crew);
        // loadout.default_crew_level: the pinned maximum, or the requested level clamped into #1513's trained range.
        const std::int64_t maximum = p::wide(py::attr(m.tankmen.get(), "MAX_SKILL_LEVEL").get());
        const std::int64_t level = crew_level ? std::max<std::int64_t>(MIN_DEFAULT_CREW_LEVEL, std::min(maximum, *crew_level)) : maximum;
        auto type = py::attr(descriptor, "type");
        const Py_ssize_t roles = PyObject_Size(py::attr(type.get(), "crewRoles").get());
        const bool complete = PyTuple_GET_SIZE(compact.get()) == roles;
        const bool existing = PyTuple_GET_SIZE(compact.get()) > 0 && complete;
        auto generate = [&] { return py::call(m.utils.get(), "generateDefaultCrew", type.get(), p::integer(level).get()); };
        if (!complete) replace(compact, generate());
        const Py_ssize_t members = PyObject_Size(compact.get());
        if (activity && Py_ssize_t(activity->size()) != members) throw Invalid("activity_flags must contain one bool per crew member");
        p::Ref flags(PyList_New(0));
        for (Py_ssize_t i = 0; i < members; ++i) p::append(flags.get(), p::boolean(activity ? (*activity)[std::size_t(i)] : true));
        p::Ref mounted(PyList_New(0));
        each(equipments, [&](PyObject* equipment) {
            auto resolved = artefact(equipment, m.vehicles.get());
            if (!isNone(resolved)) p::append(mounted.get(), std::move(resolved));
        });
        p::Ref factors(p::retain(Py_None));
        try {
            replace(factors, nativeFactors(m, descriptor, compact.get(), mounted.get(), flags.get(), is_fire));
        } catch (const p::Error&) {
            // A complete garage crew of another nation falls back to the generated default crew.
            bool wrong_nation = false;
            if (PyErr_Occurred()) {
                PyObject *t, *v, *tb;
                PyErr_Fetch(&t, &v, &tb);
                PyErr_NormalizeException(&t, &v, &tb);
                PyObject* message = v ? PyObject_Str(v) : nullptr;
                if (message) { wrong_nation = p::string(message).rfind("wrong tankman nation:", 0) == 0; Py_DECREF(message); }
                else PyErr_Clear();
                PyErr_Restore(t, v, tb);
            }
            if (!existing || !wrong_nation) throw;
            PyErr_Clear();
            replace(factors, nativeFactors(m, descriptor, generate().get(), mounted.get(), flags.get(), is_fire));
        }
        Factors out{readFactors(factors.get())};
        Value aspects; aspects.kind = Value::Kind::Array;
        aspects.array.push_back(readFactors(py::attr(m.aspects.get(), "DEFAULT").get()));
        aspects.array.push_back(readFactors(py::attr(m.aspects.get(), "WHEN_STILL").get()));
        out.values.object["_aspects"] = std::move(aspects);
        return out;
    } catch (const std::exception& e) {
        log("vehicle attribute factors unavailable: " + text(e));
        PyErr_Clear();
        return {};
    }
}

std::pair<double, double> invisibilityPair(const Factors* factors, bool still) {
    const std::pair<double, double> fallback{0., 1.};
    if (!factors || !factors->values.truth()) return fallback;
    const auto& aspects = factors->get("_aspects").resolved();
    if (aspects.kind != Value::Kind::Array || aspects.array.size() < 2) return fallback;
    const auto& aspect = aspects.array[still ? 1 : 0].resolved();
    const std::string key = aspect.kind == Value::Kind::Text ? aspect.text : std::to_string(std::int64_t(aspect.numeric()));
    const auto& values = factors->get("invisibility").resolved().get(key).resolved();
    if (values.kind != Value::Kind::Array || values.array.size() < 2) return fallback;
    for (const auto& v : {values.array[0].resolved(), values.array[1].resolved()})
        if (v.kind != Value::Kind::Integer && v.kind != Value::Kind::Number && v.kind != Value::Kind::Boolean) return fallback;
    return {values.array[0].numeric(), values.array[1].numeric()};
}

std::vector<std::string> deviceNames(PyObject* descriptor) {
    std::vector<std::string> result;
    each(attribute(descriptor, "optionalDevices").get(), [&](PyObject* device) {
        if (device == Py_None) return;
        auto name = nameOf(device);
        if (!name.empty()) result.push_back(std::move(name));
    });
    return result;
}

std::vector<std::string> equipmentNames(PyObject* equipments) {
    std::vector<std::string> result;
    each(equipments, [&](PyObject* equipment) {
        if (!p::flag(equipment)) return;
        auto name = nameOf(equipment);
        if (!name.empty()) result.push_back(std::move(name));
    });
    return result;
}

CrewSkills crewSkillNames(PyObject* crew) {
    CrewSkills result;
    each(crew, [&](PyObject* raw) {
        PyObject* tankman = member(raw);
        std::vector<std::string> names;
        if (tankman != Py_None)
            each(skillsOf(tankman).get(), [&](PyObject* skill) {
                auto name = nameOf(skill);
                if (!name.empty()) names.push_back(std::move(name));
            });
        result.push_back(std::move(names));
    });
    return result;
}

double miscFactor(PyObject* misc, const char* name, double fallback) {
    if (!misc || misc == Py_None) return fallback;
    PyObject* raw = PyObject_CallMethod(misc, const_cast<char*>("get"), const_cast<char*>("sO"), name, Py_None);
    if (!raw) {
        if (PyErr_ExceptionMatches(PyExc_AttributeError) || PyErr_ExceptionMatches(PyExc_TypeError)) { PyErr_Clear(); return fallback; }
        throw p::Error("miscAttrs read failed");
    }
    p::Ref held(raw);
    PyObject* f = PyNumber_Float(raw);
    if (!f) {
        if (PyErr_ExceptionMatches(PyExc_TypeError) || PyErr_ExceptionMatches(PyExc_ValueError)) { PyErr_Clear(); return fallback; }
        throw p::Error("miscAttrs factor conversion failed");
    }
    const double value = PyFloat_AS_DOUBLE(f);
    Py_DECREF(f);
    return value > 0. ? value : fallback;
}

Loadout modifiers(PyObject* descriptor, PyObject* equipments, const CrewSkills* crew_skills, const Factors* factors) {
    Loadout out;
    const auto devices = descriptor && descriptor != Py_None ? deviceNames(descriptor) : std::vector<std::string>();
    const auto consumables = equipmentNames(equipments);
    auto anyDevice = [&](const char* part) { return std::any_of(devices.begin(), devices.end(), [&](const std::string& n) { return contains(n, part); }); };
    out.has_rammer = anyDevice("rammer");
    out.has_aim_drives = anyDevice("aimdrives");
    out.has_ventilation = anyDevice("ventilation");
    out.has_stabiliser = anyDevice("stabilizer");
    out.has_rations = std::any_of(consumables.begin(), consumables.end(), [](const std::string& n) { return matches(n, RATION_MARKERS); });
    out.has_big_kit = std::any_of(consumables.begin(), consumables.end(), [](const std::string& n) { return matches(n, BIG_REPAIR_KIT_MARKERS); });
    double repair_share = 0.;
    if (crew_skills && !crew_skills->empty()) {
        out.has_brotherhood = true;
        for (const auto& names : *crew_skills) {
            if (std::any_of(names.begin(), names.end(), [](const std::string& n) { return n == "repair"; })) repair_share += 1. / double(crew_skills->size());
            if (!std::any_of(names.begin(), names.end(), [](const std::string& n) { return matches(n, BROTHERHOOD_MARKERS); })) out.has_brotherhood = false;
            if (std::any_of(names.begin(), names.end(), [](const std::string& n) { return matches(n, SNAP_SHOT_MARKERS); })) out.has_snap_shot = true;
            if (std::any_of(names.begin(), names.end(), [](const std::string& n) { return matches(n, SMOOTH_RIDE_MARKERS); })) out.has_smooth_ride = true;
            if (std::any_of(names.begin(), names.end(), [](const std::string& n) { return matches(n, SIXTH_SENSE_MARKERS); })) out.has_sixth_sense = true;
        }
    }
    double crew_level = BASE_CREW_LEVEL, commander_level = BASE_CREW_LEVEL;
    if (out.has_ventilation) { crew_level += VENTILATION_CREW_BONUS; commander_level += VENTILATION_CREW_BONUS; }
    if (out.has_brotherhood) { crew_level += BROTHERHOOD_CREW_BONUS; commander_level += BROTHERHOOD_CREW_BONUS; }
    if (out.has_rations) { crew_level += RATION_CREW_BONUS; commander_level += RATION_CREW_BONUS; }
    const double effective = crew_level + commander_level * COMMANDER_SHARE;
    // #1513 VehicleDescrCrew._processSkills: one factor per role, 0.57 + 0.43 * efficiency.
    double crew_factor = CREW_FACTOR_BASE + CREW_FACTOR_SLOPE * effective;
    double crew_multiplier = 1. / crew_factor;
    p::Ref misc(p::retain(Py_None));
    if (descriptor && descriptor != Py_None) {
        auto raw = attribute(descriptor, "miscAttrs");
        if (p::flag(raw.get())) replace(misc, std::move(raw));
    }
    double reload = crew_multiplier * miscFactor(misc.get(), "gunReloadTimeFactor", out.has_rammer ? RAMMER_RELOAD_FACTOR : 1.);
    double aim = crew_multiplier * miscFactor(misc.get(), "gunAimingTimeFactor", out.has_aim_drives ? 1. / AIM_DRIVE_DIVISOR : 1.);
    double dispersion = crew_multiplier;
    double repair = CREW_FACTOR_BASE + (1. - CREW_FACTOR_BASE) * repair_share;
    double gun_rotation = crew_factor, vehicle_rotation = 1., radio = 1.;
    std::array<double, 3> terrain{{1., 1., 1.}};
    const bool have_factors = factors && factors->values.truth();
    if (have_factors) {
        crew_factor = factors->number("turret/rotationSpeed");
        crew_multiplier = 1. / std::max(crew_factor, 1e-6);
        gun_rotation = factors->number("gun/rotationSpeed");
        reload = miscFactor(misc.get(), "gunReloadTimeFactor", 1.) * factors->number("gun/reloadTime");
        aim = miscFactor(misc.get(), "gunAimingTimeFactor", 1.) * factors->number("gun/aimingTime");
        const auto& shot = factors->get("shotDispersion").resolved();
        dispersion = shot.kind == Value::Kind::Array && !shot.array.empty() &&
                     (shot.array[0].resolved().kind == Value::Kind::Integer || shot.array[0].resolved().kind == Value::Kind::Number)
                         ? shot.array[0].numeric() : crew_multiplier;
        repair = factors->number("repairSpeed");
        vehicle_rotation = factors->number("vehicle/rotationSpeed");
        radio = factors->number("radio/distance");
        const auto& resist = factors->get("chassis/terrainResistance").resolved();
        if (resist.kind == Value::Kind::Array && resist.array.size() >= 3) terrain = {{resist.array[0].numeric(1.), resist.array[1].numeric(1.), resist.array[2].numeric(1.)}};
    }
    double move = 1., rotation = 1., turret = 1.;
    if (out.has_stabiliser) {
        const double stabiliser = miscFactor(misc.get(), "additiveShotDispersionFactor", STABILISER_BLOOM_FACTOR);
        move *= stabiliser; rotation *= stabiliser; turret *= stabiliser;
    }
    if (have_factors) {
        const double directive = factors->number("additiveShotDispersionFactor");
        move *= directive; rotation *= directive; turret *= directive;
    }
    auto present = [&](const char* name, double fallback) {
        const auto& v = factors->get(name).resolved();
        return v.kind == Value::Kind::Null ? fallback : v.numeric(fallback);
    };
    if (out.has_snap_shot) turret *= have_factors ? present("offline/turretBloom", SNAP_SHOT_TURRET_FACTOR) : SNAP_SHOT_TURRET_FACTOR;
    else if (have_factors) turret *= present("offline/turretBloom", 1.);
    if (out.has_smooth_ride) move *= have_factors ? present("offline/moveBloom", SMOOTH_RIDE_MOVE_FACTOR) : SMOOTH_RIDE_MOVE_FACTOR;
    else if (have_factors) move *= present("offline/moveBloom", 1.);
    out.crew_level = crew_level;
    out.commander_level = commander_level;
    out.effective_crew_level = effective;
    out.crew_multiplier = crew_multiplier;
    out.crew_factor = crew_factor;
    out.gun_rotation_factor = gun_rotation;
    out.reload_factor = reload;
    out.aim_time_factor = aim;
    out.dispersion_factor = dispersion;
    out.repair_factor = repair;
    out.vehicle_rotation_factor = vehicle_rotation;
    out.terrain_resistance_factors = terrain;
    out.radio_factor = radio;
    out.from_client_factors = have_factors;
    out.bloom_move_factor = move;
    out.bloom_rotation_factor = rotation;
    out.bloom_turret_factor = turret;
    return out;
}

double crewLevelIncrease(PyObject* descriptor, PyObject* equipments, const CrewSkills* crew_skills) {
    double increase = 0.;
    auto misc = attribute(descriptor, "miscAttrs");
    if (p::flag(misc.get())) {
        PyObject* raw = PyObject_CallMethod(misc.get(), const_cast<char*>("get"), const_cast<char*>("sd"), "crewLevelIncrease", 0.0);
        if (raw) {
            p::Ref held(raw);
            if (p::flag(raw)) {
                PyObject* f = PyNumber_Float(raw);
                if (f) { increase = PyFloat_AS_DOUBLE(f); Py_DECREF(f); }
                else PyErr_Clear();
            }
        } else PyErr_Clear();
    }
    const auto bundle = modifiers(descriptor, equipments, crew_skills);
    if (bundle.has_brotherhood) increase += BROTHERHOOD_CREW_BONUS;
    if (bundle.has_rations) increase += RATION_CREW_BONUS;
    return increase;
}

SpottingProfile spottingProfile(PyObject* descriptor, PyObject* crew, double level_increase, const Factors* factors) {
    SpottingProfile profile;
    const bool have_factors = factors && factors->values.truth();
    profile.from_client_factors = have_factors;
    if (descriptor && descriptor != Py_None) {
        double optics = 1.;
        auto misc = attribute(descriptor, "miscAttrs");
        if (p::flag(misc.get())) {
            PyObject* raw = PyObject_CallMethod(misc.get(), const_cast<char*>("get"), const_cast<char*>("sd"), "circularVisionRadiusFactor", 1.0);
            if (raw) {
                p::Ref held(raw);
                if (p::flag(raw)) {
                    PyObject* f = PyNumber_Float(raw);
                    if (f) { optics = PyFloat_AS_DOUBLE(f); Py_DECREF(f); }
                    else PyErr_Clear();
                }
            } else PyErr_Clear();
        }
        each(attribute(descriptor, "optionalDevices").get(), [&](PyObject* device) {
            if (device == Py_None) return;
            const auto name = nameOf(device);
            if (contains(name, "stereoscope")) {
                const double factor = number(attribute(device, "circularVisionRadiusFactor").get(), 0.);
                // #1513 divides the descriptor's optics factor out: binoculars replace it instead of stacking.
                if (factor > 0.) profile.binocular_factor = std::max(profile.binocular_factor, factor / std::max(optics, 1e-6));
                profile.has_binoculars = true;
                profile.binocular_delay = number(attribute(device, "activateWhenStillSec").get(), 3.);
            } else if (contains(name, "camouflagenet")) {
                profile.has_camouflage_net = true;
                profile.camouflage_net_delay = number(attribute(device, "activateWhenStillSec").get(), 3.);
            }
        });
        if (profile.has_camouflage_net) {
            auto deltas = attribute(attribute(descriptor, "type").get(), "invisibilityDeltas");
            double bonus = 0.;
            if (p::flag(deltas.get())) {
                PyObject* raw = PyObject_CallMethod(deltas.get(), const_cast<char*>("get"), const_cast<char*>("sd"), "camouflageNetBonus", 0.0);
                if (raw) {
                    p::Ref held(raw);
                    if (p::flag(raw)) {
                        PyObject* f = PyNumber_Float(raw);
                        if (f) { bonus = PyFloat_AS_DOUBLE(f); Py_DECREF(f); }
                        else PyErr_Clear();
                    }
                } else PyErr_Clear();
            }
            profile.camouflage_net_bonus = std::max(0., bonus);
        }
    }
    std::vector<PyObject*> members;
    p::Ref keep(PySequence_List(crew && crew != Py_None && p::flag(crew) ? crew : p::Ref(PyTuple_New(0)).get()));
    for (Py_ssize_t i = 0; i < PyList_GET_SIZE(keep.get()); ++i) {
        PyObject* tankman = member(PyList_GET_ITEM(keep.get(), i));
        if (tankman != Py_None) members.push_back(tankman);
    }
    if (!members.empty()) {
        double camouflage_total = 0.;
        bool commander_seen = false;
        for (PyObject* tankman : members) {
            auto role_raw = attribute(tankman, "role");
            const std::string role = p::flag(role_raw.get()) ? lower(p::string(p::Ref(PyObject_Str(role_raw.get())).get())) : std::string();
            if (role == "commander" || !commander_seen) {
                auto level = attribute(tankman, "roleLevel");
                if (role == "commander" && !isNone(level)) {
                    profile.commander_level = std::max(0., number(level.get(), 100.));
                    commander_seen = true;
                }
            }
            // #1513 takes the single best crewman for a role-specific skill.
            profile.recon_level = std::max(profile.recon_level, skillLevel(tankman, RECON_SKILL));
            profile.situational_level = std::max(profile.situational_level, skillLevel(tankman, SITUATIONAL_SKILL));
            camouflage_total += skillLevel(tankman, CAMOUFLAGE_SKILL);
        }
        // Camouflage is crew-wide: a member without the skill contributes zero.
        profile.camouflage_level = camouflage_total / double(members.size());
    }
    // #1513 adds the increase to every crewman before any efficiency is taken.
    const double increase = std::max(0., level_increase);
    if (increase != 0.) {
        profile.commander_level += increase;
        if (profile.recon_level > 0.) profile.recon_level += increase;
        if (profile.situational_level > 0.) profile.situational_level += increase;
        if (profile.camouflage_level > 0.) profile.camouflage_level += increase;
    }
    if (have_factors) {
        const double binocular = profile.has_binoculars ? profile.binocular_factor : 1.;
        profile.vision_factor = factors->number("circularVisionRadius") / std::max(binocular, 1e-6);
        profile.camouflage_factor = factors->number("camouflage");
        profile.invisibility_moving = invisibilityPair(factors, false);
        profile.invisibility_still = invisibilityPair(factors, true);
    } else {
        double vision = CREW_FACTOR_BASE + (1. - CREW_FACTOR_BASE) * std::min(1.5, std::max(0., profile.commander_level / 100.));
        vision *= 1. + RECON_FACTOR_PER_LEVEL * std::max(0., profile.recon_level);
        vision *= 1. + SITUATIONAL_FACTOR_PER_LEVEL * std::max(0., profile.situational_level);
        profile.vision_factor = vision;
        profile.camouflage_factor = CREW_FACTOR_BASE + (1. - CREW_FACTOR_BASE) * std::min(1., std::max(0., profile.camouflage_level / 100.));
        profile.invisibility_still = {profile.has_camouflage_net ? profile.camouflage_net_bonus : 0., 1.};
    }
    return profile;
}

namespace {
Value num(double v) { Value x; x.kind = Value::Kind::Number; x.number = v; return x; }
Value flag(bool v) { Value x; x.kind = Value::Kind::Boolean; x.boolean = v; return x; }
template<std::size_t N> Value list(const std::array<double, N>& values) {
    Value x; x.kind = Value::Kind::Array;
    for (double v : values) x.array.push_back(num(v));
    return x;
}
double read(const Value& o, const char* name, double fallback) { return o.resolved().get(name).numeric(fallback); }
bool readFlag(const Value& o, const char* name) { return o.resolved().get(name).truth(); }
std::pair<double, double> readPair(const Value& o, const char* name, std::pair<double, double> fallback) {
    const auto& v = o.resolved().get(name).resolved();
    if (v.kind != Value::Kind::Array || v.array.size() < 2) return fallback;
    return {v.array[0].numeric(), v.array[1].numeric()};
}
}

Value Loadout::toValue() const {
    Value o; o.kind = Value::Kind::Object;
    const std::pair<const char*, double> numbers[] = {
        {"crew_level", crew_level}, {"commander_level", commander_level}, {"effective_crew_level", effective_crew_level},
        {"crew_multiplier", crew_multiplier}, {"crew_factor", crew_factor}, {"gun_rotation_factor", gun_rotation_factor},
        {"reload_factor", reload_factor}, {"aim_time_factor", aim_time_factor}, {"dispersion_factor", dispersion_factor},
        {"repair_factor", repair_factor}, {"vehicle_rotation_factor", vehicle_rotation_factor}, {"radio_factor", radio_factor},
        {"bloom_move_factor", bloom_move_factor}, {"bloom_rotation_factor", bloom_rotation_factor}, {"bloom_turret_factor", bloom_turret_factor}};
    for (const auto& n : numbers) o.object[n.first] = num(n.second);
    const std::pair<const char*, bool> flags[] = {
        {"has_big_kit", has_big_kit}, {"from_client_factors", from_client_factors}, {"has_rammer", has_rammer},
        {"has_aim_drives", has_aim_drives}, {"has_ventilation", has_ventilation}, {"has_stabiliser", has_stabiliser},
        {"has_rations", has_rations}, {"has_brotherhood", has_brotherhood}, {"has_snap_shot", has_snap_shot},
        {"has_smooth_ride", has_smooth_ride}, {"has_sixth_sense", has_sixth_sense}};
    for (const auto& f : flags) o.object[f.first] = flag(f.second);
    o.object["terrain_resistance_factors"] = list(terrain_resistance_factors);
    return o;
}

Loadout Loadout::fromValue(const Value& v) {
    Loadout o;
    o.crew_level = read(v, "crew_level", o.crew_level);
    o.commander_level = read(v, "commander_level", o.commander_level);
    o.effective_crew_level = read(v, "effective_crew_level", o.effective_crew_level);
    o.crew_multiplier = read(v, "crew_multiplier", 1.);
    o.crew_factor = read(v, "crew_factor", 1.);
    o.gun_rotation_factor = read(v, "gun_rotation_factor", 1.);
    o.reload_factor = read(v, "reload_factor", 1.);
    o.aim_time_factor = read(v, "aim_time_factor", 1.);
    o.dispersion_factor = read(v, "dispersion_factor", 1.);
    o.repair_factor = read(v, "repair_factor", 1.);
    o.vehicle_rotation_factor = read(v, "vehicle_rotation_factor", 1.);
    o.radio_factor = read(v, "radio_factor", 1.);
    o.bloom_move_factor = read(v, "bloom_move_factor", 1.);
    o.bloom_rotation_factor = read(v, "bloom_rotation_factor", 1.);
    o.bloom_turret_factor = read(v, "bloom_turret_factor", 1.);
    const auto& terrain = v.resolved().get("terrain_resistance_factors").resolved();
    if (terrain.kind == Value::Kind::Array && terrain.array.size() >= 3)
        o.terrain_resistance_factors = {{terrain.array[0].numeric(1.), terrain.array[1].numeric(1.), terrain.array[2].numeric(1.)}};
    o.has_big_kit = readFlag(v, "has_big_kit");
    o.from_client_factors = readFlag(v, "from_client_factors");
    o.has_rammer = readFlag(v, "has_rammer");
    o.has_aim_drives = readFlag(v, "has_aim_drives");
    o.has_ventilation = readFlag(v, "has_ventilation");
    o.has_stabiliser = readFlag(v, "has_stabiliser");
    o.has_rations = readFlag(v, "has_rations");
    o.has_brotherhood = readFlag(v, "has_brotherhood");
    o.has_snap_shot = readFlag(v, "has_snap_shot");
    o.has_smooth_ride = readFlag(v, "has_smooth_ride");
    o.has_sixth_sense = readFlag(v, "has_sixth_sense");
    return o;
}

Value SpottingProfile::toValue() const {
    Value o; o.kind = Value::Kind::Object;
    const std::pair<const char*, double> numbers[] = {
        {"commander_level", commander_level}, {"recon_level", recon_level}, {"situational_level", situational_level},
        {"camouflage_level", camouflage_level}, {"binocular_factor", binocular_factor}, {"binocular_delay", binocular_delay},
        {"camouflage_net_bonus", camouflage_net_bonus}, {"camouflage_net_delay", camouflage_net_delay},
        {"vision_factor", vision_factor}, {"camouflage_factor", camouflage_factor}};
    for (const auto& n : numbers) o.object[n.first] = num(n.second);
    o.object["has_binoculars"] = flag(has_binoculars);
    o.object["has_camouflage_net"] = flag(has_camouflage_net);
    o.object["from_client_factors"] = flag(from_client_factors);
    o.object["invisibility_moving"] = list(std::array<double, 2>{{invisibility_moving.first, invisibility_moving.second}});
    o.object["invisibility_still"] = list(std::array<double, 2>{{invisibility_still.first, invisibility_still.second}});
    return o;
}

SpottingProfile SpottingProfile::fromValue(const Value& v) {
    SpottingProfile o;
    o.commander_level = read(v, "commander_level", o.commander_level);
    o.recon_level = read(v, "recon_level", 0.);
    o.situational_level = read(v, "situational_level", 0.);
    o.camouflage_level = read(v, "camouflage_level", 0.);
    o.binocular_factor = read(v, "binocular_factor", 1.);
    o.binocular_delay = read(v, "binocular_delay", 3.);
    o.camouflage_net_bonus = read(v, "camouflage_net_bonus", 0.);
    o.camouflage_net_delay = read(v, "camouflage_net_delay", 3.);
    o.vision_factor = read(v, "vision_factor", 1.);
    o.camouflage_factor = read(v, "camouflage_factor", CREW_FACTOR_BASE);
    o.has_binoculars = readFlag(v, "has_binoculars");
    o.has_camouflage_net = readFlag(v, "has_camouflage_net");
    o.from_client_factors = readFlag(v, "from_client_factors");
    o.invisibility_moving = readPair(v, "invisibility_moving", {0., 1.});
    o.invisibility_still = readPair(v, "invisibility_still", {0., 1.});
    return o;
}
}
}
