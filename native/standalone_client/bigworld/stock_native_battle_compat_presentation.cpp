// Battle UI hooks: shell tooltip speed, LAN death messages, postmortem templates, effect receipts and the HUD ping.
#if !defined(WOT_NATIVE_BATTLE_HOST_BUILD)
#include "pch.hpp"
#endif
#include "stock_native_battle_compat_internal.hpp"
#include "stock_native_battle_subsystems.hpp"
#include <cmath>
#include <cstdio>
#include <regex>

namespace native_battle {
namespace compat {
namespace {
PyObject* fail(const std::exception& e) {
    if (!PyErr_Occurred()) PyErr_SetString(PyExc_RuntimeError, e.what());
    return nullptr;
}
std::wstring wide(PyObject* text) {
    p::Ref unicode(PyUnicode_Check(text) ? p::retain(text) : p::Ref(PyUnicode_FromObject(text)));
    const Py_ssize_t size = PyUnicode_GET_SIZE(unicode.get());
    std::wstring out(std::size_t(size), L'\0');
    if (size && PyUnicode_AsWideChar(reinterpret_cast<PyUnicodeObject*>(unicode.get()), &out[0], size) < 0) throw p::Error("unicode conversion failed");
    return out;
}
p::Ref unicode(const std::wstring& text) { return p::Ref(PyUnicode_FromWideChar(text.data(), Py_ssize_t(text.size()))); }
const wchar_t* const ascii_space = L" \t\n\r\f\v";
// re.split(separator, text): every field between matches, including empty leading and trailing fields.
std::vector<std::wstring> split(const std::wstring& text, const std::wregex& separator) {
    std::vector<std::wstring> parts;
    auto it = text.cbegin();
    std::wsmatch m;
    while (std::regex_search(it, text.cend(), m, separator)) {
        parts.emplace_back(it, m[0].first);
        it = m[0].second;
    }
    parts.emplace_back(it, text.cend());
    return parts;
}
// re.split(r'(<[^>]*>)', row): text and tags alternate, text first.
std::vector<std::wstring> splitTags(const std::wstring& row) {
    static const std::wregex tag(L"<[^>]*>");
    std::vector<std::wstring> parts;
    auto it = row.cbegin();
    std::wsmatch m;
    while (std::regex_search(it, row.cend(), m, tag)) {
        parts.emplace_back(it, m[0].first);
        parts.emplace_back(m[0].first, m[0].second);
        it = m[0].second;
    }
    parts.emplace_back(it, row.cend());
    return parts;
}
std::wstring strip(const std::wstring& s) {
    const auto first = s.find_first_not_of(ascii_space);
    if (first == std::wstring::npos) return L"";
    return s.substr(first, s.find_last_not_of(ascii_space) - first + 1);
}
std::wstring separatorOf(const std::wstring& body) {
    static const std::wregex separator(L"<br[ \t\n\r\f\v]*/?>|\n", std::regex::icase);
    std::wsmatch m;
    return std::regex_search(body, m, separator) ? m.str(0) : std::wstring(L"\n");
}
const std::wregex& separatorPattern() {
    static const std::wregex separator(L"<br[ \t\n\r\f\v]*/?>|\n", std::regex::icase);
    return separator;
}
std::wstring join(const std::vector<std::wstring>& parts, const std::wstring& by) {
    std::wstring out;
    for (std::size_t i = 0; i < parts.size(); ++i) { if (i) out += by; out += parts[i]; }
    return out;
}

// battle_shell_tooltip._speed_line: reuse the stock parameter row's label/value spans and separator.
std::wstring speedLine(const std::wstring& body, const std::wstring& value) {
    const std::wstring label = L"炮弹速度 (米/秒)";
    const std::wstring separator = separatorOf(body);
    static const std::wregex number(L"[0-9][0-9 \t\n\r\f\v,. ]*");
    static const std::wregex punctuation(L"([:：][ \t\n\r\f\v]*|[ \t\n\r\f\v]+)$");
    for (const auto& row : split(body, separatorPattern())) {
        auto parts = splitTags(row);
        for (std::size_t index = 0; index < parts.size(); index += 2) {
            std::wsmatch found;
            if (!std::regex_search(parts[index], found, number)) continue;
            const std::wstring before = parts[index].substr(0, std::size_t(found.position(0)));
            const std::wstring after = parts[index].substr(std::size_t(found.position(0) + found.length(0)));
            std::vector<std::size_t> labels;
            for (std::size_t i = 0; i <= index; i += 2)
                if (!strip(i == index ? before : parts[i]).empty()) labels.push_back(i);
            if (labels.empty()) continue;
            const std::size_t first = labels.front();
            const std::wstring old_label = first == index ? before : parts[first];
            std::wsmatch tail;
            const std::wstring ending = std::regex_search(old_label, tail, punctuation) ? tail.str(0) : std::wstring(L" ");
            for (std::size_t i : labels) if (i != index) parts[i].clear();
            const std::wstring replacement = label + ending;
            if (first == index) parts[index] = replacement + value + after;
            else { parts[first] = replacement; parts[index] = before + value + after; }
            return separator + join(parts, L"");
        }
    }
    return separator + label + L": " + value;
}

// _shot_field: GunShot exposes attributes only; plain dictionaries come from serialized descriptors.
p::Ref shotField(PyObject* shot, const char* name, PyObject* fallback = Py_None) {
    if (PyDict_Check(shot)) { PyObject* v = PyDict_GetItemString(shot, name); return p::retain(v ? v : fallback); }
    PyObject* v = PyObject_GetAttrString(shot, name);
    if (v) return p::Ref(v);
    if (!PyErr_ExceptionMatches(PyExc_AttributeError)) throw p::Error(name);
    PyErr_Clear();
    return p::retain(fallback);
}
struct NotApplicable {};
// _plain_number: thousands-grouped integer, or trimmed fixed decimals.
std::wstring plainNumber(PyObject* raw, int decimals = 0) {
    PyObject* converted = PyNumber_Float(raw);
    if (!converted) {
        if (PyErr_ExceptionMatches(PyExc_TypeError) || PyErr_ExceptionMatches(PyExc_ValueError) || PyErr_ExceptionMatches(PyExc_OverflowError)) {
            PyErr_Clear();
            throw NotApplicable();
        }
        throw p::Error("shell parameter");
    }
    const double value = PyFloat_AsDouble(p::Ref(converted).get());
    if (std::isnan(value) || std::isinf(value) || value < 0.) throw NotApplicable();
    if (!decimals) {
        std::wstring digits = std::to_wstring(static_cast<long long>(std::round(value)));
        for (std::ptrdiff_t i = std::ptrdiff_t(digits.size()) - 3; i > 0; i -= 3) digits.insert(std::size_t(i), L",");
        return digits;
    }
    wchar_t buffer[64];
    std::swprintf(buffer, 64, L"%.*f", decimals, value);
    std::wstring text = buffer;
    while (!text.empty() && text.back() == L'0') text.pop_back();
    if (!text.empty() && text.back() == L'.') text.pop_back();
    return text;
}
p::Ref item(PyObject* sequence, Py_ssize_t index) {
    PyObject* v = PySequence_GetItem(sequence, index);
    if (v) return p::Ref(v);
    if (PyErr_ExceptionMatches(PyExc_TypeError) || PyErr_ExceptionMatches(PyExc_IndexError) || PyErr_ExceptionMatches(PyExc_KeyError)) {
        PyErr_Clear();
        throw NotApplicable();
    }
    throw p::Error("shell parameter");
}

// battle_shell_tooltip._reference_body: the installed GunShot endpoints in the reference client's layout.
std::optional<std::pair<std::wstring, std::wstring>> referenceBody(const std::wstring& body, PyObject* shot, PyObject* shell) {
    const std::string kind = p::string(shotField(shell, "kind").get());
    static const std::map<std::string, std::wstring> titles = {
        {"ARMOR_PIERCING", L"穿甲弹"}, {"ARMOR_PIERCING_CR", L"合金穿甲弹"},
        {"HOLLOW_CHARGE", L"高爆反坦克弹"}, {"HIGH_EXPLOSIVE", L"高爆弹"}};
    auto title = titles.find(kind);
    if (title == titles.end()) return {};
    std::wstring damage, near, far, speed, radius;
    bool has_radius = false;
    try {
        damage = plainNumber(item(shotField(shell, "damage").get(), 0).get());
        auto piercing = shotField(shot, "piercingPower");
        near = plainNumber(item(piercing.get(), 0).get());
        far = plainNumber(item(piercing.get(), 1).get());
        speed = plainNumber(shotField(shot, "speed").get());
        if (kind == "HIGH_EXPLOSIVE") {
            auto value = shotField(shell, "explosionRadius");
            if (value.get() == Py_None) replace(value, shotField(shotField(shell, "type").get(), "explosionRadius"));
            radius = plainNumber(value.get(), 2);
            has_radius = true;
        }
    } catch (const NotApplicable&) { return {}; }
    const std::wstring separator = separatorOf(body);
    auto rows = split(body, separatorPattern());
    static const std::wregex tags(L"<[^>]*>"), digit(L"[0-9]"), font(L"<font\\b[^>]*>", std::regex::icase);
    std::vector<std::size_t> parameter_rows;
    for (std::size_t i = 0; i < rows.size(); ++i)
        if (std::regex_search(std::regex_replace(rows[i], tags, L""), digit)) parameter_rows.push_back(i);
    const std::wstring templ = parameter_rows.empty() ? L"" : rows[parameter_rows.front()];
    std::vector<std::wstring> fonts;
    for (std::wsregex_iterator it(templ.begin(), templ.end(), font), end; it != end; ++it) fonts.push_back(it->str(0));
    const std::wstring label_font = fonts.empty() ? L"" : fonts.front(), value_font = fonts.empty() ? L"" : fonts.back();
    auto row = [&](const std::wstring& label, const std::wstring& value) {
        if (!fonts.empty()) return label_font + label + L": </font>" + value_font + value + L"</font>";
        return label + L": " + value;
    };
    const bool affected = kind == "ARMOR_PIERCING" || kind == "ARMOR_PIERCING_CR";
    const std::wstring power = near + (affected ? L"-" + far : L"");
    std::vector<std::wstring> result = {
        row(L"平均伤害", damage + L"点"),
        row(L"平均穿深", power + L"毫米*"),
        row(L"炮弹速度", speed + L"米/秒"),
    };
    if (has_radius) result.push_back(row(L"伤害半径", radius + L"米"));
    if (parameter_rows.size() >= 2) result.insert(result.end(), rows.begin() + std::ptrdiff_t(parameter_rows[1] + 1), rows.end());
    const std::wstring note = affected ? L"*受距离影响: 50 - 500米" : L"*此类型炮弹不受距离影响";
    result.push_back(L"");
    result.push_back(fonts.empty() ? note : value_font + note + L"</font>");
    return std::make_pair(join(result, separator), title->second);
}

// battle_shell_tooltip.append_speed: the mounted gun's speed for this shell in the stock tooltip body.
p::Ref appendSpeed(PyObject* tooltip, PyObject* shell, PyObject* vehicle, PyObject* number_format) {
    auto gun = py::optional(vehicle, "gun");
    auto shell_cd = py::optional(shell, "compactDescr");
    double speed = 0.;
    bool found = false;
    p::Ref selected(p::retain(Py_None));
    auto shots = py::optional(gun.get(), "shots");
    if (shots.get() != Py_None) {
        p::Ref rows(PySequence_List(shots.get()));
        for (Py_ssize_t i = 0; i < PyList_GET_SIZE(rows.get()); ++i) {
            PyObject* shot = PyList_GET_ITEM(rows.get(), i);
            auto candidate = shotField(shot, "shell");
            if (candidate.get() == shell ||
                (shell_cd.get() != Py_None && PyObject_RichCompareBool(py::optional(candidate.get(), "compactDescr").get(), shell_cd.get(), Py_EQ) == 1)) {
                speed = p::scalar(p::Ref(PyNumber_Float(shotField(shot, "speed", p::number(0.).get()).get())).get());
                replace(selected, p::retain(shot));
                found = true;
                break;
            }
        }
    }
    if (!found || speed <= 0. || std::isnan(speed) || std::isinf(speed)) return p::retain(tooltip);
    const bool was_bytes = PyString_Check(tooltip);
    std::wstring text;
    if (was_bytes) {
        p::Ref decoded(PyUnicode_DecodeUTF8(PyString_AS_STRING(tooltip), PyString_GET_SIZE(tooltip), nullptr));
        text = wide(decoded.get());
    } else text = wide(tooltip);
    std::wstring value;
    if (number_format && PyCallable_Check(number_format)) {
        auto formatted = py::invoke(number_format, p::integer(static_cast<long long>(std::round(speed))).get());
        if (PyString_Check(formatted.get())) {
            p::Ref decoded(PyUnicode_DecodeUTF8(PyString_AS_STRING(formatted.get()), PyString_GET_SIZE(formatted.get()), nullptr));
            value = wide(decoded.get());
        } else value = wide(formatted.get());
    } else {
        value = std::to_wstring(static_cast<long long>(std::nearbyint(speed)));
    }
    const auto begin_tag = text.find(L"{BODY}"), end_tag = text.find(L"{/BODY}");
    if (begin_tag != std::wstring::npos && end_tag != std::wstring::npos) {
        const std::size_t begin = begin_tag + 6;
        const std::size_t end = text.find(L"{/BODY}", begin);
        if (end == std::wstring::npos) {
            PyErr_SetString(PyExc_ValueError, "substring not found");
            throw p::Error("tooltip body");
        }
        auto reference = referenceBody(text.substr(begin, end - begin), selected.get(), shell);
        if (reference) {
            text = text.substr(0, begin) + reference->first + text.substr(end);
            const auto header = text.find(L"{HEADER}"), header_end = text.find(L"{/HEADER}");
            if (header != std::wstring::npos && header_end != std::wstring::npos) {
                const std::size_t start = header + 8, finish = text.find(L"{/HEADER}", start);
                if (finish == std::wstring::npos) {
                    PyErr_SetString(PyExc_ValueError, "substring not found");
                    throw p::Error("tooltip header");
                }
                text = text.substr(0, start) + reference->second + text.substr(finish);
            }
        } else {
            text = text.substr(0, end) + speedLine(text.substr(begin, end - begin), value) + text.substr(end);
        }
    } else {
        std::wstring line = speedLine(L"", value);
        line.erase(0, line.find_first_not_of(L'\n'));
        text += L"\n/{BODY}" + line + L"{/BODY}";
    }
    auto result = unicode(text);
    if (was_bytes) return p::Ref(PyUnicode_AsUTF8String(result.get()));
    return result;
}
}

// ConsumablesPanel.__makeShellTooltip: the stock tooltip plus the mounted gun's shell speed.
PyObject* shellTooltip(PyObject* args, PyObject* kwargs) {
    try {
        auto& s = stock();
        p::Ref original(PyObject_Call(s.shell_tooltip.get(), args, kwargs));
        Battle* b = battle();
        if (!b) return original.release();
        try {
            auto descriptor = argument(args, 1);
            auto avatar = player();
            auto vehicle = py::call(s.bigworld.get(), "entity", py::attr(avatar.get(), "playerVehicleID").get());
            return appendSpeed(original.get(), descriptor.get(), py::attr(vehicle.get(), "typeDescriptor").get(), s.nice_number_format.get()).release();
        } catch (const std::exception& e) {
            if (PyErr_Occurred() && !PyErr_ExceptionMatches(PyExc_Exception)) throw;
            // The stock formatter above owns ammo initialisation; this extra line must never abort its listeners.
            const std::string error = describe(e);
            if (!b->compat->shell_tooltip_warning_logged) {
                b->compat->shell_tooltip_warning_logged = true;
                log("shell speed tooltip unavailable: " + error);
            }
            return original.release();
        }
    } catch (const std::exception& e) { return fail(e); }
}

// PlayerMessages.showMessage: personal messages stay stock; LAN squad name fields take the squad colour.
PyObject* showPlayerMessage(PyObject* args, PyObject* kwargs) {
    try {
        auto& s = stock();
        auto panel = argument(args, 0);
        auto read = [&](Py_ssize_t index, const char* name) -> p::Ref {
            if (PyTuple_GET_SIZE(args) > index) return p::retain(PyTuple_GET_ITEM(args, index));
            if (kwargs) if (PyObject* v = PyDict_GetItemString(kwargs, name)) return p::retain(v);
            return p::retain(Py_None);
        };
        auto key = read(1, "key"), message_args = read(2, "args"), extra = read(3, "extra"), postfix = read(4, "postfix");
        auto avatar = player();
        auto own = py::optional(avatar.get(), "playerVehicleID");
        if (own.get() == Py_None && !PyObject_HasAttrString(avatar.get(), "playerVehicleID")) replace(own, p::integer(0));
        auto original = [&](PyObject* formatted, PyObject* remaining, PyObject* tail) {
            return call(s.player_message_show, {panel.get(), key.get(), formatted, remaining, tail});
        };
        const std::string key_text = key.get() != Py_None ? p::string(key.get()) : std::string();
        const bool death = !key_text.empty() && key_text.rfind("DEATH_FROM_", 0) == 0 && p::flag(postfix.get());
        std::string attacker, target;
        p::Ref extras(p::flag(extra.get()) ? p::Ref(PySequence_List(extra.get())) : p::Ref(PyList_New(0)));
        if (death) {
            // Own deaths belong to PostmortemPanel, never the right-hand feed.
            for (Py_ssize_t i = 0; i < PyList_GET_SIZE(extras.get()); ++i) {
                PyObject* row = PyList_GET_ITEM(extras.get(), i);
                if (p::string(p::item(row, 0).get()) == "target" && PyObject_RichCompareBool(p::item(row, 1).get(), own.get(), Py_EQ) == 1)
                    Py_RETURN_NONE;
            }
            const std::string relations = p::string(postfix.get());
            const auto underscore = relations.find('_');
            if (underscore != std::string::npos && relations.find('_', underscore + 1) == std::string::npos) {
                attacker = relations.substr(0, underscore);
                target = relations.substr(underscore + 1);
            }
        }
        if (!p::flag(extra.get()) && !death) return original(message_args.get(), extra.get(), postfix.get()).release();
        p::Ref templates(death ? py::attr(panel.get(), "_FadingMessages__messages") : p::dict());
        if (attacker == "UNKNOWN" && (target == "ALLY" || target == "ENEMY") && key_text != "DEATH_FROM_SHOT") {
            // #1513 has no UNKNOWN environmental variants; SUICIDE variants name only the victim.
            const std::string unassisted = target + "_SUICIDE";
            if (PySequence_Contains(templates.get(), p::text(key_text + "_" + unassisted).get()) == 1) replace(postfix, p::text(unassisted));
        }
        p::Ref formatted(message_args.get() != Py_None ? p::Ref(PyObject_CallFunctionObjArgs(reinterpret_cast<PyObject*>(&PyDict_Type), message_args.get(), nullptr))
                                                       : p::retain(Py_None));
        p::Ref remaining(PyList_New(0));
        auto context = py::call(py::attr(panel.get(), "sessionProvider").get(), "getCtx");
        for (Py_ssize_t i = 0; i < PyList_GET_SIZE(extras.get()); ++i) {
            PyObject* row = PyList_GET_ITEM(extras.get(), i);
            auto name = p::item(row, 0), vehicle_id = p::item(row, 1);
            bool squad = false;
            if (p::flag(formatted.get()) && p::flag(py::call(formatted.get(), "get", name.get()).get()) && p::flag(vehicle_id.get()) &&
                PyObject_RichCompareBool(vehicle_id.get(), own.get(), Py_NE) == 1) {
                auto is_squad = py::attr(context.get(), "isSquadMan");
                p::Ref positional(PyTuple_New(0));
                auto keywords = p::dict();
                PyDict_SetItemString(keywords.get(), "vID", vehicle_id.get());
                squad = p::flag(p::Ref(PyObject_Call(is_squad.get(), positional.get(), keywords.get())).get());
            }
            if (squad) {
                auto rgba = py::call(py::attr(py::attr(panel.get(), "app").get(), "colorManager").get(), "getRGBA", p::text("squad").get());
                auto channel = [&](Py_ssize_t index) { return p::Ref(PyNumber_Int(p::item(rgba.get(), index).get())); };
                auto r = channel(0), g = channel(1), bl = channel(2);
                auto value = p::Ref(PyObject_GetItem(formatted.get(), name.get()));
                auto colored = py::call(s.message_color_format.get(), "format", r.get(), g.get(), bl.get(), value.get());
                if (PyObject_SetItem(formatted.get(), name.get(), colored.get()) < 0) throw p::Error("squad color write failed");
            } else {
                p::Ref pair(PyTuple_Pack(2, name.get(), vehicle_id.get()));
                p::append(remaining.get(), pair);
            }
        }
        p::Ref remaining_tuple(PyList_AsTuple(remaining.get()));
        if (key_text == "DEATH_FROM_SHOT" && attacker == "UNKNOWN") {
            // Two #1513 XML entries reference absent translations; reuse the neutral "target destroyed" text.
            auto message_key = p::text(key_text + "_" + p::string(postfix.get()));
            p::Ref previous(PyObject_GetItem(templates.get(), message_key.get()));
            p::Ref neutral(PyObject_GetItem(templates.get(), p::text("DEATH_FROM_SHOT_SELF_ENEMY").get()));
            p::Ref swapped(PyTuple_Pack(2, p::item(neutral.get(), 0).get(), p::item(previous.get(), 1).get()));
            if (PyObject_SetItem(templates.get(), message_key.get(), swapped.get()) < 0) throw p::Error("template write failed");
            PyObject* result = PyObject_CallFunctionObjArgs(s.player_message_show.get(), panel.get(), key.get(), formatted.get(), remaining_tuple.get(), postfix.get(), nullptr);
            PyObject *type, *value, *trace;
            PyErr_Fetch(&type, &value, &trace);
            PyObject_SetItem(templates.get(), message_key.get(), previous.get());
            if (type) PyErr_Restore(type, value, trace);
            return result;
        }
        return original(formatted.get(), remaining_tuple.get(), postfix.get()).release();
    } catch (const std::exception& e) { return fail(e); }
}

// _BasePostmortemPanel._populate: environmental ammunition deaths on the exact #1513 panel contract.
PyObject* postmortemPopulate(PyObject* args, PyObject* kwargs) {
    try {
        p::Ref result(PyObject_Call(stock().postmortem_populate.get(), args, kwargs));
        auto panel = argument(args, 0);
        auto messages = py::attr(panel.get(), "_BasePostmortemPanel__messages");
        const std::string fire = "DEATH_FROM_DEVICE_EXPLOSION_AT_FIRE", collision = "DEATH_FROM_DEVICE_EXPLOSION_AT_WORLD_COLLISION";
        p::Ref keys(PySequence_List(messages.get()));
        auto fire_text = unicode(L"弹药架因燃烧而爆炸");
        for (Py_ssize_t i = 0; i < PyList_GET_SIZE(keys.get()); ++i) {
            PyObject* key = PyList_GET_ITEM(keys.get(), i);
            const std::string text = p::string(key);
            if (text != fire && text.rfind(fire + "_", 0) != 0) continue;
            p::Ref row(PyObject_GetItem(messages.get(), key));
            p::Ref replaced(PyTuple_Pack(2, fire_text.get(), p::item(row.get(), 1).get()));
            if (PyObject_SetItem(messages.get(), key, replaced.get()) < 0) throw p::Error("postmortem template write failed");
        }
        auto templ = py::call(messages.get(), "get", p::text(fire).get());
        if (!p::flag(templ.get())) replace(templ, py::call(messages.get(), "get", p::text("DEATH_FROM_FIRE").get()));
        if (templ.get() != Py_None) {
            auto text = unicode(L"弹药架因坠落而损毁");
            p::Ref row(PyTuple_Pack(2, text.get(), p::item(templ.get(), 1).get()));
            if (PyObject_SetItem(messages.get(), p::text(collision).get(), row.get()) < 0) throw p::Error("postmortem template write failed");
        }
        return result.release();
    } catch (const std::exception& e) { return fail(e); }
}

// CustomEffectManager.update: one completed stock update, observed for vehicles whose track diagnostic is on.
PyObject* effectUpdate(PyObject* args, PyObject* kwargs) {
    try {
        auto& s = stock();
        auto manager = argument(args, 0);
        p::Ref vehicle(p::retain(Py_None));
        PyObject* raw = PyObject_GetAttrString(manager.get(), "_CustomEffectManager__vehicle");
        if (raw) replace(vehicle, p::Ref(raw));
        else if (PyErr_ExceptionMatches(PyExc_AttributeError)) PyErr_Clear();
        else return nullptr;
        PyObject* own = vehicle.get() != Py_None ? PyObject_GetAttrString(vehicle.get(), "__dict__") : nullptr;
        if (!own) PyErr_Clear();
        p::Ref vehicle_dict(own ? own : p::retain(Py_None).release());
        PyObject* flag = PyDict_Check(vehicle_dict.get()) ? PyDict_GetItemString(vehicle_dict.get(), "_offlineTrackEffectDiagnostic") : nullptr;
        const bool enabled = flag && PyObject_IsTrue(flag) > 0;
        if (!enabled) return PyObject_Call(s.effect_update.get(), args, kwargs);
        p::Ref receipt(p::retain(Py_None));
        try {
            auto row = p::dict();
            p::set(row.get(), "at", py::call(s.bigworld.get(), "time"));
            auto values = py::attr(py::attr(vehicle.get(), "speedInfo").get(), "value");
            p::Ref list(PySequence_List(values.get()));
            p::set(row.get(), "speed_info", p::Ref(PyList_GetSlice(list.get(), 0, 4)));
            replace(receipt, std::move(row));
        } catch (const std::exception&) {
            if (!PyErr_ExceptionMatches(PyExc_Exception)) throw;
            PyErr_Clear();
        }
        p::Ref result(PyObject_Call(s.effect_update.get(), args, kwargs));
        if (receipt.get() != Py_None) {
            try {
                auto variables = py::attr(manager.get(), "_CustomEffectManager__variableArgs");
                auto inputs = p::dict();
                for (const char* name : {"speed", "rotSpeed", "direction", "matkindL", "matkindR", "deltaL", "deltaR", "directionL", "directionR", "hullAngle"})
                    p::set(inputs.get(), name, py::call(variables.get(), "get", p::text(name).get()));
                p::set(receipt.get(), "effect_inputs", inputs);
                if (PyDict_SetItemString(vehicle_dict.get(), "_offlineTrackEffectReceipt", receipt.get()) < 0) throw p::Error("receipt");
            } catch (const std::exception&) {
                if (!PyErr_ExceptionMatches(PyExc_Exception)) throw;
                PyErr_Clear();
            }
        }
        return result.release();
    } catch (const std::exception& e) { return fail(e); }
}

// DebugPanel.updateDebugInfo: the LAN transport's ping and lag replace the absent retail server's.
PyObject* debugUpdate(PyObject* args, PyObject* kwargs) {
    try {
        auto& s = stock();
        Battle* b = battle();
        if (!b || !b->compat->network_client) return PyObject_Call(s.debug_update.get(), args, kwargs);
        auto panel = argument(args, 0), fps = argument(args, 2);
        p::Ref replay_fps(PyTuple_GET_SIZE(args) > 4 ? p::retain(PyTuple_GET_ITEM(args, 4)) : p::integer(-1));
        if (kwargs) if (PyObject* v = PyDict_GetItemString(kwargs, "fpsReplay")) replace(replay_fps, p::retain(v));
        PyObject* client = b->compat->network_client.get();
        auto worker_ping = py::optional(client, "worker_ping_display");
        p::Ref ping(p::retain(Py_None)), lagging(p::retain(Py_None));
        if (py::callable(worker_ping)) {
            auto pair = py::invoke(worker_ping.get());
            replace(ping, p::item(pair.get(), 0));
            replace(lagging, p::item(pair.get(), 1));
        } else {
            const bool connected = flagAttribute(client, "connected", false);
            auto sample = py::optional(client, "rtt_ms");
            std::int64_t value = connected ? 0 : 999;
            if (sample.get() != Py_None) {
                PyObject* converted = PyNumber_Float(sample.get());
                if (converted) {
                    const double ms = PyFloat_AsDouble(converted);
                    Py_DECREF(converted);
                    if (!std::isnan(ms)) value = std::int64_t(std::round(std::max(0., std::min(ms, 999.))));
                } else if (PyErr_ExceptionMatches(PyExc_TypeError) || PyErr_ExceptionMatches(PyExc_ValueError) || PyErr_ExceptionMatches(PyExc_OverflowError)) {
                    PyErr_Clear();
                } else return nullptr;
            }
            replace(ping, p::integer(value));
            replace(lagging, p::boolean(!connected));
        }
        return call(s.debug_update, {panel.get(), ping.get(), fps.get(), lagging.get(), replay_fps.get()}).release();
    } catch (const std::exception& e) { return fail(e); }
}
}
}
