// The pinned battle_runtime._FrameDiagnostics as a native type: callback rows and window aggregates
// are C++ state; PERF lines and snapshot() use the pinned format strings and dict construction.
#if !defined(WOT_CLIENT_LIFECYCLE_HOST_BUILD)
#include "pch.hpp"
#endif
#include "stock_native_client_lifecycle_python.hpp"
#include <deque>
#include <memory>

namespace stock_native_client_lifecycle {
namespace frame_diagnostics {
namespace {
using namespace py;

const char kPrefix[] = "[Offline LAN 0.9.22] PERF ";

p::Ref flt(double v) { return p::Ref(PyFloat_FromDouble(v)); }
p::Ref none() { return p::retain(Py_None); }
p::Ref share(const p::Ref& r) { return p::retain(r.get()); }
p::Ref boolean(bool v) { return p::retain(v ? Py_True : Py_False); }
// Python max(0.0, v) keeps 0.0 unless v is larger.
double floor0(double v) { return v > 0.0 ? v : 0.0; }
double ms(double v) { return floor0(v) * 1000.0; }
// float(value)
double toDouble(PyObject* v) {
    if (PyFloat_CheckExact(v)) return PyFloat_AS_DOUBLE(v);
    p::Ref f(PyNumber_Float(v));
    return PyFloat_AS_DOUBLE(f.get());
}
p::Ref msOf(PyObject* v) { return flt(ms(toDouble(v))); }
// dict(value) and dict(value or {})
p::Ref dictOf(PyObject* v) {
    if (PyDict_CheckExact(v)) return p::Ref(PyDict_Copy(v));
    return p::Ref(PyObject_CallFunctionObjArgs(reinterpret_cast<PyObject*>(&PyDict_Type), v, nullptr));
}
p::Ref dictOr(PyObject* v) { return p::flag(v) ? dictOf(v) : p::dict(); }
// mapping.get(key, fallback) on the exact dicts this class creates.
p::Ref lookup(PyObject* d, PyObject* key, PyObject* fallback) {
    if (PyDict_CheckExact(d)) {
        PyObject* v = PyDict_GetItem(d, key);
        return p::retain(v ? v : fallback);
    }
    return method(d, "get", key, fallback);
}
p::Ref lookup(PyObject* d, const char* key, PyObject* fallback = Py_None) {
    auto k = p::text(key);
    return lookup(d, k.get(), fallback);
}
p::Ref lookupAttr(PyObject* o, const char* name, PyObject* fallback) {
    PyObject* v = PyObject_GetAttrString(o, name);
    if (v) return p::Ref(v);
    if (!PyErr_ExceptionMatches(PyExc_AttributeError)) throw p::Error(name);
    PyErr_Clear();
    return p::retain(fallback);
}
bool equalsText(PyObject* v, const char* text) { return equal(v, p::text(text).get()); }
// round(value * 1000.0, 3) through CPython 2.7's builtin_round.
p::Ref round3(double v) {
    if (!std::isfinite(v) || v == 0.0) return flt(v);
    return p::Ref(_Py_double_round(v, 3));
}
p::Ref roundMs(PyObject* v) {
    if (PyFloat_CheckExact(v)) return round3(PyFloat_AS_DOUBLE(v) * 1000.0);
    p::Ref product(PyNumber_Multiply(v, flt(1000.0).get()));
    const double x = PyFloat_AsDouble(product.get());
    if (x == -1.0 && PyErr_Occurred()) throw p::Error("round() argument");
    return round3(x);
}
template<std::size_t N> p::Ref fmt(const char* format, p::Ref (&items)[N]) {
    p::Ref tuple(PyTuple_New(Py_ssize_t(N)));
    for (std::size_t i = 0; i < N; ++i) PyTuple_SET_ITEM(tuple.get(), Py_ssize_t(i), items[i].release());
    p::Ref f(PyString_FromString(format));
    return p::Ref(PyString_Format(f.get(), tuple.get()));
}
p::Ref concat(std::initializer_list<PyObject*> parts) {
    auto out = p::text("");
    for (PyObject* part : parts) replace(out, p::Ref(PyNumber_Add(out.get(), part)));
    return out;
}
p::Ref join(const char* separator, PyObject* items) {
    auto sep = p::text(separator);
    return p::Ref(_PyString_Join(sep.get(), items));
}
// `or 'none'`
p::Ref orText(p::Ref value, const char* fallback) { return p::flag(value.get()) ? std::move(value) : p::text(fallback); }

struct LanTotals {
    p::Ref generation{none()};
    PyObject* source = nullptr;   // id() of the getter's owner; compared, never dereferenced
    std::vector<double> wall;
    std::vector<std::int64_t> calls;
};
struct LanDelta { std::vector<double> wall; std::vector<std::int64_t> calls; };

struct Pending {
    p::Ref cause{none()};
    double entry_wall = 0., exec = 0., tick_dt = 0., motion_dt = 0.;
    bool has_end_cpu = false, has_thread_cpu = false, emitted = false;
    double end_cpu = 0., thread_cpu = 0.;
    p::Ref window_state{none()}, stages{none()}, probes{none()}, probe_durations{none()}, projectile{none()},
        combat{none()}, context{none()};
    std::shared_ptr<LanTotals> lan_totals;
};
struct Row {
    std::shared_ptr<const Pending> base;
    p::Ref next{none()}, window_before{none()}, window_after{none()};
    double wall_gap = 0., raw_dt = 0., offframe = 0., outside = 0., bw_minus_wall = 0., between_wall = 0.;
    bool has_between_cpu = false, has_lan = false;
    double between_cpu = 0., outside_without_lan = 0.;
    LanDelta lan;
    bool linked = false;   // a worker row: carries previous/following compact frames
    std::vector<p::Ref> previous, following;
};

struct State {
    p::Ref globals{none()}, clock{none()}, cpu_clock{none()}, window_clock{none()}, writer{none()};
    p::Ref enabled{p::retain(Py_True)}, lan_getter{none()};
    std::vector<p::Ref> stage_names, probe_names, lan_names, projectile_names;
    std::size_t stage_count = 0;   // _FRAME_STAGE_NAMES precede _FRAME_DETAIL_NAMES in stage_names
    std::size_t lan_poll = 0, lan_materialize = 0, lan_handle = 0, lan_notify = 0;
    std::size_t top_frames = 3, percentile_cap = 4096;
    double steady_window = 30., initial_window = 30., window_seconds = 30.;

    std::shared_ptr<const Pending> pending;
    bool has_cpu_entry = false;
    double cpu_entry = 0.;
    std::deque<p::Ref> recent;
    std::int64_t frame_id = 0, window_id = 0;
    p::Ref last_snapshot{p::dict()};

    std::int64_t samples = 0, cpu_samples = 0, between_cpu_samples = 0, lan_samples = 0, distribution_samples = 0;
    double window_elapsed = 0., gap_sum = 0., gap_max = 0., raw_sum = 0., raw_max = 0., exec_sum = 0., exec_max = 0.;
    double cpu_sum = 0., cpu_max = 0., cpu_wall_sum = 0., between_cpu_sum = 0., between_cpu_max = 0., between_cpu_wall_sum = 0.;
    double outside_sum = 0., outside_max = 0., outside_without_lan_sum = 0., outside_without_lan_max = 0.;
    double offframe_sum = 0., offframe_max = 0.;
    std::vector<double> lan_sums, lan_maxima;
    std::vector<std::int64_t> lan_calls;
    std::deque<double> gap_samples, exec_samples, outside_samples;
    p::Ref load_busiest{p::Ref(PyTuple_New(0))}, collections{p::dict()}, worker_runtime{p::dict()};
    std::vector<double> stage_sums, stage_maxima, probe_duration_sums, probe_duration_maxima, projectile_sums, projectile_maxima;
    std::vector<std::int64_t> probe_sums, probe_maxima;
    std::vector<std::shared_ptr<Row>> slow;
    p::Ref combat_completed{p::list()};
    std::int64_t over_50 = 0, over_67 = 0, over_100 = 0, sim_caps = 0, clock_regressions = 0, authority_frames = 0;
    p::Ref last_context{p::dict()};
    bool emit_due = false;

    void resetWindow() {
        samples = cpu_samples = between_cpu_samples = lan_samples = distribution_samples = 0;
        window_elapsed = gap_sum = gap_max = raw_sum = raw_max = exec_sum = exec_max = 0.;
        cpu_sum = cpu_max = cpu_wall_sum = between_cpu_sum = between_cpu_max = between_cpu_wall_sum = 0.;
        outside_sum = outside_max = outside_without_lan_sum = outside_without_lan_max = offframe_sum = offframe_max = 0.;
        lan_sums.assign(lan_names.size(), 0.); lan_maxima.assign(lan_names.size(), 0.); lan_calls.assign(lan_names.size(), 0);
        gap_samples.clear(); exec_samples.clear(); outside_samples.clear();
        replace(load_busiest, p::Ref(PyTuple_New(0)));
        replace(collections, p::dict());
        replace(worker_runtime, p::dict());
        stage_sums.assign(stage_names.size(), 0.); stage_maxima.assign(stage_names.size(), 0.);
        probe_sums.assign(probe_names.size(), 0); probe_maxima.assign(probe_names.size(), 0);
        probe_duration_sums.assign(probe_names.size(), 0.); probe_duration_maxima.assign(probe_names.size(), 0.);
        projectile_sums.assign(projectile_names.size(), 0.); projectile_maxima.assign(projectile_names.size(), 0.);
        slow.clear();
        replace(combat_completed, p::list());
        over_50 = over_67 = over_100 = sim_caps = clock_regressions = authority_frames = 0;
        replace(last_context, p::dict());
        emit_due = false;
    }
    void reset() {
        pending.reset();
        has_cpu_entry = false;
        replace(lan_getter, none());
        recent.clear();
        frame_id = window_id = 0;
        window_seconds = initial_window;
        replace(last_snapshot, p::dict());
        resetWindow();
    }
    void disable() {
        replace(enabled, p::retain(Py_False));
        pending.reset();
        slow.clear();
    }
};

struct Diagnostics { PyObject_HEAD State* state; };

// `except Exception:` around a body; BaseException still propagates.
template<class Body> bool guarded(Body body) {
    try { body(); return true; }
    catch (const std::exception& e) {
        if (!PyErr_Occurred()) PyErr_SetString(PyExc_RuntimeError, e.what());
        if (!PyErr_ExceptionMatches(PyExc_Exception)) throw;
        PyErr_Clear();
        return false;
    }
}
std::vector<p::Ref> names(PyObject* g, const char* name) {
    auto tuple = global(g, name);
    std::vector<p::Ref> out;
    p::Ref iterator(PyObject_GetIter(tuple.get()));
    while (PyObject* item = PyIter_Next(iterator.get())) out.emplace_back(item);
    if (PyErr_Occurred()) throw p::Error(name);
    return out;
}
std::size_t indexOf(const std::vector<p::Ref>& list, const char* name) {
    for (std::size_t i = 0; i < list.size(); ++i) if (equalsText(list[i].get(), name)) return i;
    PyErr_SetObject(PyExc_KeyError, p::text(name).get());
    throw p::Error(name);
}

void readCpu(State& s, bool& has, double& out) {
    has = false;
    if (s.cpu_clock.get() == Py_None) return;
    guarded([&] {
        p::Ref value(PyObject_CallFunctionObjArgs(s.cpu_clock.get(), nullptr));
        if (value.get() == Py_None) return;
        const double v = toDouble(value.get());
        if (v >= 0.0 && !std::isnan(v) && !std::isinf(v)) { has = true; out = v; }
    });
}
p::Ref readWindow(State& s) {
    if (s.window_clock.get() == Py_None) return none();
    p::Ref result = none();
    guarded([&] {
        p::Ref value(PyObject_CallFunctionObjArgs(s.window_clock.get(), nullptr));
        if (value.get() == Py_None || PyObject_Size(value.get()) != 3) { if (PyErr_Occurred()) throw p::Error("len"); return; }
        p::Ref items(PyList_New(0));
        p::Ref iterator(PyObject_GetIter(value.get()));
        while (PyObject* item = PyIter_Next(iterator.get())) {
            p::Ref held(item);
            p::append(items.get(), p::Ref(PyNumber_Int(item)));
        }
        if (PyErr_Occurred()) throw p::Error("window state");
        p::Ref tuple(PyList_AsTuple(items.get()));
        for (Py_ssize_t i = 0; i < PyTuple_GET_SIZE(tuple.get()); ++i) {
            PyObject* item = PyTuple_GET_ITEM(tuple.get(), i);
            if (!equal(item, integer(-1).get()) && !equal(item, integer(0).get()) && !equal(item, integer(1).get())) return;
        }
        replace(result, std::move(tuple));
    });
    return result;
}
// A missing transport sample never disables frame diagnostics.
std::shared_ptr<LanTotals> readLanPoll(State& s) {
    std::shared_ptr<LanTotals> result;
    guarded([&] {
        if (!PyCallable_Check(s.lan_getter.get())) return;
        p::Ref snapshot(PyObject_CallFunctionObjArgs(s.lan_getter.get(), nullptr));
        if (!PyDict_Check(snapshot.get())) return;
        auto integral = [](PyObject* v) { return !PyBool_Check(v) && (PyInt_Check(v) || PyLong_Check(v)); };
        p::Ref generation(PyObject_GetItem(snapshot.get(), p::text("generation").get()));
        if (!integral(generation.get())) return;
        const int negative = PyObject_RichCompareBool(generation.get(), integer(0).get(), Py_LT);
        if (negative < 0) throw p::Error("generation");
        if (negative) return;
        auto owner = optionalAttr(s.lan_getter.get(), "__self__");
        if (owner.get() == Py_None) replace(owner, optionalAttr(s.lan_getter.get(), "im_self"));
        auto totals = std::make_shared<LanTotals>();
        replace(totals->generation, std::move(generation));
        totals->source = owner.get() != Py_None ? owner.get() : s.lan_getter.get();
        for (const auto& name : s.lan_names) {
            const std::string stage = p::string(name.get());
            p::Ref count(PyObject_GetItem(snapshot.get(), p::text(stage + "_calls").get()));
            if (!integral(count.get())) return;
            const int below = PyObject_RichCompareBool(count.get(), integer(0).get(), Py_LT);
            if (below < 0) throw p::Error("calls");
            if (below) return;
            p::Ref wall(PyObject_GetItem(snapshot.get(), p::text(stage + "_wall_seconds").get()));
            const double value = toDouble(wall.get());
            if (value < 0.0 || std::isnan(value) || std::isinf(value)) return;
            totals->wall.push_back(value);
            totals->calls.push_back(strictInt(count.get()));
        }
        result = std::move(totals);
    });
    return result;
}
bool lanDelta(const State& s, const LanTotals* previous, const LanTotals* current, double outside, LanDelta& out) {
    if (!previous || !current || previous->source != current->source ||
        !equal(previous->generation.get(), current->generation.get())) return false;
    out.wall.clear(); out.calls.clear();
    for (std::size_t i = 0; i < s.lan_names.size(); ++i) {
        const double wall = current->wall[i] - previous->wall[i];
        if (wall < 0) return false;
        const std::int64_t calls = current->calls[i] - previous->calls[i];
        if (calls < 0) return false;
        out.wall.push_back(wall); out.calls.push_back(calls);
    }
    // Child timers are included in their parents; reject over-budget samples beyond clock roundoff.
    const double tolerance = 1e-6;
    if (out.wall[s.lan_poll] > outside + tolerance ||
        out.wall[s.lan_materialize] + out.wall[s.lan_handle] > out.wall[s.lan_poll] + tolerance ||
        out.wall[s.lan_notify] > out.wall[s.lan_handle] + tolerance) return false;
    return true;
}

// _compact_frame: plain bounded data without the neighbour rows.
p::Ref compact(const State& s, const Row& row) {
    const Pending& b = *row.base;
    auto out = literal(16);
    put(out.get(), "cause", share(b.cause));
    put(out.get(), "next", share(row.next));
    put(out.get(), "authority_time", lookup(b.context.get(), "authority_time"));
    put(out.get(), "gap_ms", round3(row.wall_gap * 1000.0));
    put(out.get(), "exec_ms", round3(b.exec * 1000.0));
    put(out.get(), "thread_cpu_ms", b.has_thread_cpu ? round3(b.thread_cpu * 1000.0) : none());
    put(out.get(), "between_callbacks_cpu_ms", row.has_between_cpu ? round3(row.between_cpu * 1000.0) : none());
    put(out.get(), "between_callbacks_wall_ms", round3(row.between_wall * 1000.0));
    put(out.get(), "outside_ms", round3(row.outside * 1000.0));
    if (row.has_lan) {
        auto lan = p::dict();
        for (std::size_t i = 0; i < s.lan_names.size(); ++i)
            if (PyDict_SetItem(lan.get(), s.lan_names[i].get(), round3(row.lan.wall[i] * 1000.0).get()) < 0) throw p::Error("lan");
        put(out.get(), "lan_poll_ms", lan);
    } else put(out.get(), "lan_poll_ms", none());
    put(out.get(), "outside_without_lan_poll_ms", row.has_lan ? round3(row.outside_without_lan * 1000.0) : none());
    put(out.get(), "offframe_ms", round3(row.offframe * 1000.0));
    for (const char* key : {"stages_ms", "details_ms"}) {
        const bool stages = key[0] == 's';
        auto values = p::dict();
        PyObject *name, *value;
        Py_ssize_t position = 0;
        while (PyDict_Next(b.stages.get(), &position, &name, &value)) {
            bool listed = false;
            for (std::size_t i = stages ? 0 : s.stage_count; i < (stages ? s.stage_count : s.stage_names.size()); ++i)
                if (equal(name, s.stage_names[i].get())) { listed = true; break; }
            if (listed && PyDict_SetItem(values.get(), name, roundMs(value).get()) < 0) throw p::Error("stages");
        }
        put(out.get(), key, values);
    }
    put(out.get(), "projectile", dictOr(b.projectile.get()));
    put(out.get(), "detail", share(b.combat));
    return out;
}

void add(State& s, const std::shared_ptr<Row>& row) {
    const Pending& b = *row->base;
    auto role = lookup(b.context.get(), "role");
    if (equalsText(role.get(), "worker")) {
        auto frame = compact(s, *row);
        for (const auto& previous : s.slow)
            if (previous->linked && previous->following.size() < 3) previous->following.push_back(share(frame));
        row->linked = true;
        for (const auto& recent : s.recent) row->previous.push_back(share(recent));
        s.recent.push_back(std::move(frame));
        if (s.recent.size() > 3) s.recent.pop_front();
    }
    s.samples += 1;
    const double gap = row->wall_gap, raw_dt = row->raw_dt, execution = b.exec, outside = row->outside;
    s.window_elapsed += gap;
    s.gap_sum += gap; if (gap > s.gap_max) s.gap_max = gap;
    s.raw_sum += raw_dt; if (raw_dt > s.raw_max) s.raw_max = raw_dt;
    s.exec_sum += execution; if (execution > s.exec_max) s.exec_max = execution;
    if (row->has_between_cpu) {
        s.between_cpu_samples += 1;
        s.between_cpu_sum += row->between_cpu;
        if (row->between_cpu > s.between_cpu_max) s.between_cpu_max = row->between_cpu;
        s.between_cpu_wall_sum += row->between_wall;
    }
    if (b.has_thread_cpu) {
        s.cpu_sum += b.thread_cpu;
        if (b.thread_cpu > s.cpu_max) s.cpu_max = b.thread_cpu;
        s.cpu_samples += 1;
        s.cpu_wall_sum += execution;
    }
    s.outside_sum += outside; if (outside > s.outside_max) s.outside_max = outside;
    if (row->has_lan) {
        s.lan_samples += 1;
        for (std::size_t i = 0; i < s.lan_names.size(); ++i) {
            s.lan_sums[i] += row->lan.wall[i];
            if (row->lan.wall[i] > s.lan_maxima[i]) s.lan_maxima[i] = row->lan.wall[i];
            s.lan_calls[i] += row->lan.calls[i];
        }
        s.outside_without_lan_sum += row->outside_without_lan;
        if (row->outside_without_lan > s.outside_without_lan_max) s.outside_without_lan_max = row->outside_without_lan;
    }
    for (auto* samples : {&s.gap_samples, &s.exec_samples, &s.outside_samples}) {
        samples->push_back(samples == &s.gap_samples ? gap : samples == &s.exec_samples ? execution : outside);
        if (samples->size() > s.percentile_cap) samples->pop_front();
    }
    s.distribution_samples += 1;
    s.offframe_sum += row->offframe; if (row->offframe > s.offframe_max) s.offframe_max = row->offframe;
    if (gap >= 0.050) s.over_50 += 1;
    if (gap >= 0.067) s.over_67 += 1;
    if (gap >= 0.100) s.over_100 += 1;
    if (raw_dt > 0.100) s.sim_caps += 1;
    if (equalsText(role.get(), "authority")) s.authority_frames += 1;
    auto zero = flt(0.0);
    for (std::size_t i = 0; i < s.stage_names.size(); ++i) {
        const double value = floor0(toDouble(lookup(b.stages.get(), s.stage_names[i].get(), zero.get()).get()));
        s.stage_sums[i] += value;
        if (value > s.stage_maxima[i]) s.stage_maxima[i] = value;
    }
    auto zero_int = integer(0);
    for (std::size_t i = 0; i < s.probe_names.size(); ++i) {
        const std::int64_t count = std::max<std::int64_t>(0, strictInt(lookup(b.probes.get(), s.probe_names[i].get(), zero_int.get()).get()));
        s.probe_sums[i] += count;
        if (count > s.probe_maxima[i]) s.probe_maxima[i] = count;
        const double duration = floor0(toDouble(lookup(b.probe_durations.get(), s.probe_names[i].get(), zero.get()).get()));
        s.probe_duration_sums[i] += duration;
        if (duration > s.probe_duration_maxima[i]) s.probe_duration_maxima[i] = duration;
    }
    auto projectile = dictOr(b.projectile.get());
    for (std::size_t i = 0; i < s.projectile_names.size(); ++i) {
        const double value = floor0(toDouble(lookup(projectile.get(), s.projectile_names[i].get(), zero.get()).get()));
        s.projectile_sums[i] += value;
        if (value > s.projectile_maxima[i]) s.projectile_maxima[i] = value;
    }
    replace(s.last_context, dictOr(b.context.get()));
    // Ranked by (gap, exec); a tie keeps the earlier frame first.
    auto later = std::find_if(s.slow.begin(), s.slow.end(), [&](const std::shared_ptr<Row>& existing) {
        const double existing_gap = existing->wall_gap, existing_exec = existing->base->exec;
        return gap != existing_gap ? gap > existing_gap : execution > existing_exec;
    });
    s.slow.insert(later, row);
    if (s.slow.size() > s.top_frames) s.slow.resize(s.top_frames);
    if (s.window_elapsed >= s.window_seconds) s.emit_due = true;
}

struct Distribution { double p50, p95, p99, max; p::Ref dict{none()}; };
double percentile(const std::vector<double>& sorted, double fraction) {
    if (sorted.empty()) return 0.0;
    if (sorted.size() == 1) return sorted[0];
    const double rank = std::max(0.0, std::min(1.0, fraction)) * double(sorted.size() - 1);
    const std::size_t lower = std::size_t(std::floor(rank)), upper = std::size_t(std::ceil(rank));
    if (lower == upper) return sorted[lower];
    const double weight = rank - double(lower);
    return sorted[lower] * (1.0 - weight) + sorted[upper] * weight;
}
Distribution distribution(const std::deque<double>& samples, double exact_maximum) {
    std::vector<double> values(samples.begin(), samples.end());
    if (std::any_of(values.begin(), values.end(), [](double v) { return std::isnan(v); })) {
        // sorted() order with NaN is Python's own; take it from list.sort.
        p::Ref list(PyList_New(0));
        for (double v : values) p::append(list.get(), flt(v));
        if (PyList_Sort(list.get()) < 0) throw p::Error("sort");
        for (std::size_t i = 0; i < values.size(); ++i) values[i] = PyFloat_AS_DOUBLE(PyList_GET_ITEM(list.get(), Py_ssize_t(i)));
    } else std::stable_sort(values.begin(), values.end());
    Distribution out{ms(percentile(values, 0.50)), ms(percentile(values, 0.95)), ms(percentile(values, 0.99)), ms(exact_maximum)};
    replace(out.dict, literal(4));
    put(out.dict.get(), "p50", flt(out.p50));
    put(out.dict.get(), "p95", flt(out.p95));
    put(out.dict.get(), "p99", flt(out.p99));
    put(out.dict.get(), "max", flt(out.max));
    return out;
}
p::Ref jsonDumps(const State& s, PyObject* value) {
    auto json = global(s.globals.get(), "json");
    p::Ref separators(Py_BuildValue("(ss)", ",", ":"));
    return callKw(json.get(), "dumps", {value}, {{"separators", separators.get()}});
}
// Module helper _combat_log_lines: one native log line below #1513's 8 KiB limit.
p::Ref combatLogLines(const State& s, PyObject* prefix, const char* kind, PyObject* record) {
    auto payload = jsonDumps(s, record);
    auto line = concat({prefix, p::text(kind).get(), p::text(" ").get(), payload.get(), p::text("\n").get()});
    const Py_ssize_t length = PyObject_Size(line.get());
    if (length < 0) throw p::Error("len");
    if (length <= 7168) return line;
    const Py_ssize_t size = PyObject_Size(payload.get());
    std::vector<p::Ref> chunks;
    for (Py_ssize_t index = 0; index < size; index += 2500) chunks.emplace_back(PySequence_GetSlice(payload.get(), index, index + 2500));
    p::Ref parts(PyList_New(0));
    auto kind_part = p::text(std::string(kind) + "_part ");
    for (std::size_t index = 0; index < chunks.size(); ++index) {
        auto record_part = literal(4);
        put(record_part.get(), "schema", integer(2));
        put(record_part.get(), "part", integer(std::int64_t(index) + 1));
        put(record_part.get(), "parts", integer(std::int64_t(chunks.size())));
        put(record_part.get(), "data", share(chunks[index]));
        p::append(parts.get(), concat({prefix, kind_part.get(), jsonDumps(s, record_part.get()).get(), p::text("\n").get()}));
    }
    return join("", parts.get());
}

p::Ref format(State& s) {
    const std::int64_t samples = std::max<std::int64_t>(1, s.samples);
    const double n = double(samples);
    const double elapsed = s.window_elapsed > 1e-9 ? s.window_elapsed : 1e-9;
    auto context = share(s.last_context);
    s.window_id += 1;
    auto gap = distribution(s.gap_samples, s.gap_max);
    auto execution = distribution(s.exec_samples, s.exec_max);
    auto outside = distribution(s.outside_samples, s.outside_max);
    auto stage_snapshot = p::dict();
    for (std::size_t i = 0; i < s.stage_names.size(); ++i) {
        auto row = literal(2);
        put(row.get(), "avg_ms", flt(ms(s.stage_sums[i] / n)));
        put(row.get(), "max_ms", flt(ms(s.stage_maxima[i])));
        if (PyDict_SetItem(stage_snapshot.get(), s.stage_names[i].get(), row.get()) < 0) throw p::Error("stages");
    }
    auto detail_snapshot = p::dict();
    for (std::size_t i = s.stage_count; i < s.stage_names.size(); ++i) {
        PyObject* key = s.stage_names[i].get();
        PyObject* value = PyDict_GetItem(stage_snapshot.get(), key);
        if (!value) { PyErr_SetObject(PyExc_KeyError, key); throw p::Error("pop"); }
        p::Ref held = p::retain(value);
        if (PyDict_DelItem(stage_snapshot.get(), key) < 0 || PyDict_SetItem(detail_snapshot.get(), key, held.get()) < 0) throw p::Error("pop");
    }
    const bool lan = s.lan_samples != 0;
    const double lan_n = double(s.lan_samples);
    auto lan_snapshot = literal(4);
    put(lan_snapshot.get(), "samples", integer(s.lan_samples));
    put(lan_snapshot.get(), "unmatched", integer(s.samples - s.lan_samples));
    put(lan_snapshot.get(), "interval", p::text("previous_finish_to_current_begin"));
    auto lan_stages = p::dict();
    for (std::size_t i = 0; i < s.lan_names.size(); ++i) {
        auto row = literal(4);
        put(row.get(), "avg_ms", lan ? flt(ms(s.lan_sums[i] / lan_n)) : none());
        put(row.get(), "max_ms", lan ? flt(ms(s.lan_maxima[i])) : none());
        put(row.get(), "calls", integer(s.lan_calls[i]));
        put(row.get(), "parent", p::text(i == s.lan_poll ? "outside" : i == s.lan_notify ? "handle" : "poll"));
        if (PyDict_SetItem(lan_stages.get(), s.lan_names[i].get(), row.get()) < 0) throw p::Error("lan");
    }
    put(lan_snapshot.get(), "stages_ms", lan_stages);
    auto outside_without_lan = literal(4);
    put(outside_without_lan.get(), "samples", integer(s.lan_samples));
    put(outside_without_lan.get(), "avg_ms", lan ? flt(ms(s.outside_without_lan_sum / lan_n)) : none());
    put(outside_without_lan.get(), "max_ms", lan ? flt(ms(s.outside_without_lan_max)) : none());
    put(outside_without_lan.get(), "gpu_measured", boolean(false));
    auto probe_snapshot = p::dict();
    for (std::size_t i = 0; i < s.probe_names.size(); ++i) {
        auto row = literal(8);
        put(row.get(), "logical_count", integer(s.probe_sums[i]));
        put(row.get(), "logical_hz", flt(double(s.probe_sums[i]) / elapsed));
        put(row.get(), "logical_per_frame_avg", flt(double(s.probe_sums[i]) / n));
        put(row.get(), "logical_per_frame_max", integer(s.probe_maxima[i]));
        put(row.get(), "timed_ms_total", flt(ms(s.probe_duration_sums[i])));
        put(row.get(), "timed_ms_per_second", flt(ms(s.probe_duration_sums[i] / elapsed)));
        put(row.get(), "timed_ms_per_frame_avg", flt(ms(s.probe_duration_sums[i] / n)));
        put(row.get(), "timed_ms_per_frame_max", flt(ms(s.probe_duration_maxima[i])));
        if (PyDict_SetItem(probe_snapshot.get(), s.probe_names[i].get(), row.get()) < 0) throw p::Error("probes");
    }
    auto dash = p::text("-");
    auto off = p::text("off");
    const std::int64_t kept = std::int64_t(s.gap_samples.size());
    const std::int64_t dropped = std::max<std::int64_t>(0, s.distribution_samples - kept);
    auto snapshot = literal(27);
    put(snapshot.get(), "schema", integer(1));
    put(snapshot.get(), "window", integer(s.window_id));
    put(snapshot.get(), "round", lookup(context.get(), "round", dash.get()));
    put(snapshot.get(), "map", lookup(context.get(), "map", dash.get()));
    put(snapshot.get(), "phase", lookup(context.get(), "phase", dash.get()));
    put(snapshot.get(), "role", lookup(context.get(), "role", dash.get()));
    put(snapshot.get(), "probe_timing", lookup(context.get(), "probe_timing", off.get()));
    put(snapshot.get(), "samples", integer(s.samples));
    put(snapshot.get(), "seconds", flt(s.window_elapsed));
    put(snapshot.get(), "render_callback_fps", flt(double(s.samples) / elapsed));
    put(snapshot.get(), "distribution_samples", integer(kept));
    put(snapshot.get(), "distribution_dropped", integer(dropped));
    put(snapshot.get(), "frame_interval_ms", share(gap.dict));
    put(snapshot.get(), "python_callback_ms", share(execution.dict));
    const bool between = s.between_cpu_samples != 0;
    const double between_n = double(s.between_cpu_samples);
    auto between_cpu = literal(7);
    put(between_cpu.get(), "samples", integer(s.between_cpu_samples));
    put(between_cpu.get(), "avg_ms", between ? flt(ms(s.between_cpu_sum / between_n)) : none());
    put(between_cpu.get(), "max_ms", between ? flt(ms(s.between_cpu_max)) : none());
    put(between_cpu.get(), "matched_wall_avg_ms", between ? flt(ms(s.between_cpu_wall_sum / between_n)) : none());
    put(between_cpu.get(), "interval", p::text("finish_to_begin"));
    put(between_cpu.get(), "includes", p::text("engine,other_callbacks,lan_poll"));
    put(between_cpu.get(), "gpu_measured", boolean(false));
    put(snapshot.get(), "between_callbacks_cpu", between_cpu);
    const bool cpu = s.cpu_samples != 0;
    const double cpu_n = double(s.cpu_samples);
    auto main_cpu = literal(4);
    put(main_cpu.get(), "samples", integer(s.cpu_samples));
    put(main_cpu.get(), "avg_ms", cpu ? flt(ms(s.cpu_sum / cpu_n)) : none());
    put(main_cpu.get(), "max_ms", cpu ? flt(ms(s.cpu_max)) : none());
    put(main_cpu.get(), "matched_wall_avg_ms", cpu ? flt(ms(s.cpu_wall_sum / cpu_n)) : none());
    put(snapshot.get(), "main_thread_cpu", main_cpu);
    put(snapshot.get(), "outside_callback_ms", share(outside.dict));
    put(snapshot.get(), "lan_poll", share(lan_snapshot));
    put(snapshot.get(), "outside_without_lan_poll_ms", share(outside_without_lan));
    put(snapshot.get(), "python_stages_ms", stage_snapshot);
    put(snapshot.get(), "python_details_ms", detail_snapshot);
    put(snapshot.get(), "raw_native_calls_measured", boolean(false));
    put(snapshot.get(), "logical_native_probes", probe_snapshot);
    put(snapshot.get(), "worker_runtime", dictOf(s.worker_runtime.get()));
    put(snapshot.get(), "over_50_67_100", p::Ref(PyTuple_Pack(3, integer(s.over_50).get(), integer(s.over_67).get(), integer(s.over_100).get())));
    put(snapshot.get(), "simulation_caps", integer(s.sim_caps));
    put(snapshot.get(), "clock_regressions", integer(s.clock_regressions));
    replace(s.last_snapshot, std::move(snapshot));

    auto prefix = p::text(kPrefix);
    p::Ref lines(PyList_New(0));
    auto line = [&](p::Ref text) { p::append(lines.get(), concat({prefix.get(), text.get()})); };
    {
        p::Ref a[] = {integer(s.window_id), lookup(context.get(), "round", dash.get()), lookup(context.get(), "map", dash.get()),
                      lookup(context.get(), "phase", dash.get()), lookup(context.get(), "role", dash.get()),
                      lookup(context.get(), "probe_timing", off.get()), integer(s.samples), flt(s.window_elapsed),
                      flt(double(s.samples) / elapsed), integer(s.authority_frames), flt(ms(s.gap_sum / n)), flt(ms(s.gap_max)),
                      flt(ms(s.raw_sum / n)), flt(ms(s.raw_max)), flt(ms(s.exec_sum / n)), flt(ms(s.exec_max)),
                      flt(ms(s.offframe_sum / n)), flt(ms(s.offframe_max)), flt(ms(s.outside_sum / n)), flt(ms(s.outside_max)),
                      flt(gap.p50), flt(gap.p95), flt(gap.p99), flt(gap.max),
                      flt(execution.p50), flt(execution.p95), flt(execution.p99), flt(execution.max),
                      flt(outside.p50), flt(outside.p95), flt(outside.p99), flt(outside.max),
                      integer(kept), integer(dropped), integer(s.over_50), integer(s.over_67), integer(s.over_100),
                      integer(s.sim_caps), integer(s.clock_regressions)};
        line(fmt("summary v=3 window=%d round=%s map=%s phase=%s role=%s "
                 "probe_timing=%s "
                 "samples=%d seconds=%.3f fps=%.2f authority_frames=%d "
                 "gap_ms_avg_max=%.3f/%.3f raw_dt_ms_avg_max=%.3f/%.3f "
                 "exec_ms_avg_max=%.3f/%.3f offframe_ms_avg_max=%.3f/%.3f "
                 "outside_ms_avg_max=%.3f/%.3f "
                 "gap_ms_p50_p95_p99_max=%.3f/%.3f/%.3f/%.3f "
                 "python_ms_p50_p95_p99_max=%.3f/%.3f/%.3f/%.3f "
                 "outside_ms_p50_p95_p99_max=%.3f/%.3f/%.3f/%.3f "
                 "distribution_kept_dropped=%d/%d "
                 "over_50_67_100=%d/%d/%d sim_caps=%d clock_regress=%d\n", a));
    }
    {
        p::Ref planners(PyList_New(0));
        p::Ref iterator(PyObject_GetIter(s.load_busiest.get()));
        while (PyObject* item = PyIter_Next(iterator.get())) {
            p::Ref held(item);
            auto pair = unpack(item, 2);
            p::Ref a[] = {std::move(pair[0]), std::move(pair[1])};
            p::append(planners.get(), fmt("%d=%d", a));
        }
        if (PyErr_Occurred()) throw p::Error("bot planners");
        line(concat({p::text("bot_planners ").get(), orText(join(" ", planners.get()), "none").get(), p::text("\n").get()}));
    }
    {
        p::Ref keys(PyDict_Keys(s.collections.get()));
        if (PyList_Sort(keys.get()) < 0) throw p::Error("sorted");
        p::Ref counts(PyList_New(0));
        for (Py_ssize_t i = 0; i < PyList_GET_SIZE(keys.get()); ++i) {
            PyObject* name = PyList_GET_ITEM(keys.get(), i);
            p::Ref a[] = {p::retain(name), p::Ref(PyObject_GetItem(s.collections.get(), name))};
            p::append(counts.get(), fmt("%s=%d", a));
        }
        line(concat({p::text("collections ").get(), orText(join(" ", counts.get()), "none").get(), p::text("\n").get()}));
    }
    {
        auto empty = [] { return p::dict(); };
        auto part = [&](const char* name) { auto v = lookup(s.worker_runtime.get(), name); return p::flag(v.get()) ? std::move(v) : empty(); };
        auto control = part("control");
        auto presentation = part("presentation");
        auto bots = part("bot_diagnostics");
        auto g = [](const p::Ref& d, const char* name) { return method(d.get(), "get", p::text(name).get()); };
        p::Ref a[] = {g(control, "control_steps"), g(control, "catchup_callbacks"), g(control, "debt_callbacks"),
                      g(control, "control_debt_ms"), g(control, "max_control_step_ms"), g(control, "astar_pending"),
                      g(control, "astar_credit"), g(control, "astar_budget_remaining"),
                      g(control, "astar_budget_exhausted_callbacks"), g(control, "astar_completed"), g(control, "astar_failed"),
                      g(bots, "visibility_queue_depth"), g(bots, "visibility_queue_max_depth"),
                      g(bots, "visibility_oldest_stale_age_ms"), g(bots, "visibility_oldest_stale_max_age_ms"),
                      g(bots, "shot_lane_pending_pairs"), g(bots, "shot_lane_pending_max_pairs"),
                      g(bots, "shot_lane_oldest_due_age_ms"), g(bots, "shot_lane_oldest_due_max_age_ms"),
                      g(bots, "shot_lane_completed_pairs"), g(bots, "shot_lane_budget_deferred_attempts"),
                      g(presentation, "pose_writes"), g(presentation, "pose_skips"), g(presentation, "aim_writes"),
                      g(presentation, "aim_skips")};
        line(fmt("worker control_steps=%s catchup=%s debt_callbacks=%s "
                 "control_debt_ms=%s max_control_step_ms=%s "
                 "astar_pending=%s astar_credit=%s astar_budget_remaining=%s "
                 "astar_budget_exhausted=%s astar_completed=%s astar_failed=%s "
                 "visibility_queue_depth_max=%s/%s "
                 "visibility_oldest_ms_max=%s/%s "
                 "shot_lane_pending_max=%s/%s "
                 "shot_lane_oldest_due_ms_max=%s/%s "
                 "shot_lane_completed_deferred=%s/%s "
                 "pose_writes_skips=%s/%s aim_writes_skips=%s/%s\n", a));
    }
    if (lan) {
        p::Ref head[] = {integer(s.lan_samples), integer(s.samples)};
        p::Ref stages(PyList_New(0));
        for (std::size_t i = 0; i < s.lan_names.size(); ++i) {
            p::Ref a[] = {share(s.lan_names[i]), flt(ms(s.lan_sums[i] / lan_n)), flt(ms(s.lan_maxima[i])), share(s.lan_names[i]),
                          integer(s.lan_calls[i])};
            p::append(stages.get(), fmt("%s=%.3f/%.3f %s_calls=%d", a));
        }
        p::Ref tail[] = {flt(ms(s.outside_without_lan_sum / lan_n)), flt(ms(s.outside_without_lan_max))};
        line(concat({fmt("lan_poll samples=%d/%d interval=finish_to_begin "
                         "materialize_handle_parent=poll notify_parent=handle "
                         "wall_ms_avg_max ", head).get(),
                     join(" ", stages.get()).get(),
                     fmt(" outside_without_lan_poll_ms_avg_max=%.3f/%.3f "
                         "gpu_measured=0\n", tail).get()}));
    } else {
        p::Ref a[] = {integer(s.samples)};
        line(fmt("lan_poll samples=0/%d unavailable=1\n", a));
    }
    if (cpu) {
        p::Ref a[] = {integer(s.cpu_samples), integer(s.samples), flt(ms(s.cpu_sum / cpu_n)), flt(ms(s.cpu_max)),
                      flt(ms(s.cpu_wall_sum / cpu_n))};
        line(fmt("thread_cpu samples=%d/%d cpu_ms_avg_max=%.3f/%.3f "
                 "matched_callback_wall_ms_avg=%.3f\n", a));
    }
    auto named = [&](const char* head, std::size_t first, std::size_t last, const std::vector<p::Ref>& list,
                     const char* format, auto value_a, auto value_b) {
        p::Ref values(PyList_New(0));
        for (std::size_t i = first; i < last; ++i) {
            p::Ref a[] = {share(list[i]), value_a(i), value_b(i)};
            p::append(values.get(), fmt(format, a));
        }
        line(concat({p::text(head).get(), join(" ", values.get()).get(), p::text("\n").get()}));
    };
    auto stage_avg = [&](std::size_t i) { return flt(ms(s.stage_sums[i] / n)); };
    auto stage_max = [&](std::size_t i) { return flt(ms(s.stage_maxima[i])); };
    named("stages_ms_avg_max ", 0, s.stage_count, s.stage_names, "%s=%.3f/%.3f", stage_avg, stage_max);
    named("details_ms_avg_max parent=local ", s.stage_count, s.stage_names.size(), s.stage_names, "%s=%.3f/%.3f", stage_avg, stage_max);
    const std::size_t probes = s.probe_names.size();
    named("logical_probes_avg_max ", 0, probes, s.probe_names, "%s=%.2f/%d",
          [&](std::size_t i) { return flt(double(s.probe_sums[i]) / n); }, [&](std::size_t i) { return integer(s.probe_maxima[i]); });
    named("logical_probes_hz_total ", 0, probes, s.probe_names, "%s=%.2f/%d",
          [&](std::size_t i) { return flt(double(s.probe_sums[i]) / elapsed); }, [&](std::size_t i) { return integer(s.probe_sums[i]); });
    named("logical_probe_ms_avg_max ", 0, probes, s.probe_names, "%s=%.3f/%.3f",
          [&](std::size_t i) { return flt(ms(s.probe_duration_sums[i] / n)); },
          [&](std::size_t i) { return flt(ms(s.probe_duration_maxima[i])); });
    named("logical_probe_ms_per_s_total ", 0, probes, s.probe_names, "%s=%.3f/%.3f",
          [&](std::size_t i) { return flt(ms(s.probe_duration_sums[i] / elapsed)); },
          [&](std::size_t i) { return flt(ms(s.probe_duration_sums[i])); });
    {
        auto at = [&](const char* name) { return indexOf(s.projectile_names, name); };
        const std::size_t active = at("active"), chords = at("chords"), debt = at("debt"), advance = at("advance"),
                          terminals = at("terminals"), scans = at("scans"), candidates = at("candidates");
        p::Ref a[] = {flt(s.projectile_sums[active] / n), flt(s.projectile_maxima[active]),
                      flt(s.projectile_sums[chords] / n), flt(s.projectile_maxima[chords]),
                      flt(ms(s.projectile_sums[debt] / n)), flt(ms(s.projectile_maxima[debt])),
                      flt(ms(s.projectile_sums[advance] / n)), flt(ms(s.projectile_maxima[advance])),
                      flt(s.projectile_sums[terminals] / n), flt(s.projectile_maxima[terminals]),
                      flt(s.projectile_sums[scans] / n), flt(s.projectile_maxima[scans]),
                      flt(s.projectile_sums[candidates] / n), flt(s.projectile_maxima[candidates])};
        line(fmt("projectile_avg_max active=%.2f/%.0f chords=%.2f/%.0f "
                 "debt_ms=%.3f/%.3f advance_ms=%.3f/%.3f "
                 "terminal=%.2f/%.0f scans=%.2f/%.0f "
                 "candidates=%.2f/%.0f\n", a));
    }
    auto zero = flt(0.0);
    auto zero_int = integer(0);
    std::int64_t rank = 0;
    for (const auto& row : s.slow) {
        ++rank;
        const Pending& b = *row->base;
        PyObject* stages = b.stages.get();
        PyObject* probe_counts = b.probes.get();
        PyObject* probe_durations = b.probe_durations.get();
        auto projectile = p::flag(b.projectile.get()) ? share(b.projectile) : p::dict();
        auto row_context = p::flag(b.context.get()) ? share(b.context) : p::dict();
        auto unavailable = [] { return p::text("unavailable"); };
        auto three = [](double v) { p::Ref a[] = {flt(v)}; return fmt("%.3f", a); };
        auto joined = [&](std::size_t first, std::size_t last, const std::vector<p::Ref>& list, const char* format, auto value) {
            p::Ref values(PyList_New(0));
            for (std::size_t i = first; i < last; ++i) {
                p::Ref a[] = {share(list[i]), value(i)};
                p::append(values.get(), fmt(format, a));
            }
            return join(",", values.get());
        };
        auto projectile_int = [&](const char* name) { return toInt(lookup(projectile.get(), name, zero_int.get()).get()); };
        p::Ref projectile_text = [&] {
            p::Ref a[] = {projectile_int("active"), projectile_int("chords"),
                          msOf(lookup(projectile.get(), "debt", zero.get()).get()),
                          msOf(lookup(projectile.get(), "advance", zero.get()).get()),
                          projectile_int("terminals"), projectile_int("scans"), projectile_int("candidates")};
            return fmt("active:%d,chords:%d,debt_ms:%.3f,"
                       "advance_ms:%.3f,terminal:%d,scans:%d,"
                       "candidates:%d", a);
        }();
        auto stage_ms = [&](std::size_t i) { return msOf(lookup(stages, s.stage_names[i].get(), zero.get()).get()); };
        p::Ref a[] = {
            integer(rank), share(b.cause), share(row->next), flt(ms(row->wall_gap)), flt(row->raw_dt * 1000.0),
            flt(row->bw_minus_wall * 1000.0), flt(ms(b.exec)), flt(ms(row->outside)),
            row->has_lan ? three(ms(row->lan.wall[s.lan_poll])) : unavailable(),
            row->has_lan ? three(ms(row->outside_without_lan)) : unavailable(),
            flt(ms(b.tick_dt)), flt(ms(b.motion_dt)),
            flt(toDouble(lookup(row_context.get(), "pose_step", zero.get()).get())),
            flt(toDouble(lookup(row_context.get(), "speed", zero.get()).get())),
            flt(toDouble(lookup(row_context.get(), "camera_speed", zero.get()).get())),
            integer(p::flag(lookup(row_context.get(), "airborne").get())),
            toInt(lookup(row_context.get(), "grind", zero_int.get()).get()),
            toInt(lookup(row_context.get(), "bot_count", zero_int.get()).get()),
            toInt(lookup(row_context.get(), "outgoing_count", zero_int.get()).get()),
            integer(p::flag(lookup(row_context.get(), "transitioned").get())),
            integer(b.emitted),
            std::move(projectile_text),
            joined(0, s.stage_count, s.stage_names, "%s:%.3f", stage_ms),
            joined(s.stage_count, s.stage_names.size(), s.stage_names, "%s:%.3f", stage_ms),
            joined(0, s.probe_names.size(), s.probe_names, "%s:%d",
                   [&](std::size_t i) { return toInt(lookup(probe_counts, s.probe_names[i].get(), zero_int.get()).get()); }),
            joined(0, s.probe_names.size(), s.probe_names, "%s:%.3f",
                   [&](std::size_t i) { return msOf(lookup(probe_durations, s.probe_names[i].get(), zero.get()).get()); })};
        line(fmt("slow rank=%d cause=%d next=%d gap_ms=%.3f "
                 "raw_dt_ms=%.3f bw_minus_wall_ms=%.3f "
                 "prev_exec_ms=%.3f outside_ms=%.3f "
                 "lan_poll_ms=%s outside_without_lan_poll_ms=%s "
                 "cause_tick_ms=%.3f cause_motion_ms=%.3f "
                 "pose_step_m=%.4f speed_mps=%.3f camera_mps=%.3f "
                 "airborne=%d grind=%d bots=%d outgoing=%d "
                 "transition=%d prev_emit=%d "
                 "projectile=%s stages_ms=%s details_ms=%s logical_probes=%s "
                 "logical_probe_ms=%s\n", a));
        p::Ref between[] = {integer(rank), share(b.cause), share(row->next), flt(ms(row->between_wall)),
                            row->has_between_cpu ? three(ms(row->between_cpu)) : unavailable(),
                            jsonDumps(s, row->window_before.get()), jsonDumps(s, row->window_after.get())};
        line(fmt("between_callbacks rank=%d cause=%d next=%d wall_ms=%.3f cpu_ms=%s window_before=%s window_after=%s "
                 "window_fields=focused,minimized,known includes=engine,other_callbacks,lan_poll gpu_measured=0\n", between));
        if (equalsText(lookup(row_context.get(), "role").get(), "worker")) {
            struct Frame { const char* relation; std::int64_t index; p::Ref frame; };
            std::vector<Frame> frames;
            frames.push_back({"focus", 0, compact(s, *row)});
            if (rank == 1) {
                for (std::size_t i = 0; i < row->previous.size(); ++i) frames.push_back({"previous", std::int64_t(i), share(row->previous[i])});
                for (std::size_t i = 0; i < row->following.size(); ++i) frames.push_back({"following", std::int64_t(i), share(row->following[i])});
            }
            for (const auto& frame : frames) {
                auto record = literal(8);
                put(record.get(), "schema", integer(2));
                put(record.get(), "window", integer(s.window_id));
                put(record.get(), "rank", integer(rank));
                put(record.get(), "round", lookup(row_context.get(), "round"));
                put(record.get(), "map", lookup(row_context.get(), "map"));
                put(record.get(), "relation", p::text(frame.relation));
                put(record.get(), "index", integer(frame.index));
                put(record.get(), "frame", share(frame.frame));
                p::append(lines.get(), combatLogLines(s, prefix.get(), "combat_trace", record.get()));
            }
        }
    }
    auto summary = [&](const char* kind, PyObject* capture) {
        auto record = literal(4);
        put(record.get(), "schema", integer(2));
        put(record.get(), "round", lookup(s.last_context.get(), "round"));
        put(record.get(), "map", lookup(s.last_context.get(), "map"));
        put(record.get(), "capture", p::retain(capture));
        p::append(lines.get(), combatLogLines(s, prefix.get(), kind, record.get()));
    };
    for (Py_ssize_t i = 0; i < PyList_GET_SIZE(s.combat_completed.get()); ++i)
        summary("combat_summary", PyList_GET_ITEM(s.combat_completed.get(), i));
    auto checkpoint = lookup(s.worker_runtime.get(), "combat_checkpoint");
    if (checkpoint.get() != Py_None) summary("combat_checkpoint", checkpoint.get());
    return join("", lines.get());
}

State& state(PyObject* self) { return *reinterpret_cast<Diagnostics*>(self)->state; }
bool enabled(State& s) { return p::flag(s.enabled.get()); }

PyObject* reset(PyObject* self, PyObject*) {
    try { state(self).reset(); Py_RETURN_NONE; } catch (const std::exception& e) { return fail(e); }
}

// begin(entry_wall, raw_dt, offframe=0.0, lan_poll_getter=None): seal the previous callback.
PyObject* beginFrame(PyObject* self, PyObject* args, PyObject* kwargs) {
    static const char* keywords[] = {"entry_wall", "raw_dt", "offframe", "lan_poll_getter", nullptr};
    PyObject *entry_wall, *raw_dt, *offframe = nullptr, *getter = Py_None;
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "OO|OO:begin", const_cast<char**>(keywords), &entry_wall, &raw_dt, &offframe, &getter))
        return nullptr;
    try {
        State& s = state(self);
        s.frame_id += 1;
        const std::int64_t frame_id = s.frame_id;
        if (!enabled(s)) return integer(frame_id).release();
        readCpu(s, s.has_cpu_entry, s.cpu_entry);
        replace(s.lan_getter, p::retain(getter));
        auto lan = readLanPoll(s);
        if (!guarded([&] {
                if (!s.pending) return;
                auto pending = s.pending;
                auto row = std::make_shared<Row>();
                row->base = pending;
                double wall_gap = toDouble(entry_wall) - pending->entry_wall;
                if (wall_gap < 0.0) { wall_gap = 0.0; s.clock_regressions += 1; }
                const double observed_raw = toDouble(raw_dt);
                if (observed_raw < 0.0) s.clock_regressions += 1;
                const double off = floor0(offframe ? toDouble(offframe) : 0.0);
                replace(row->next, integer(frame_id));
                row->wall_gap = wall_gap;
                row->raw_dt = observed_raw;
                row->offframe = off;
                row->outside = floor0(wall_gap - pending->exec - off);
                row->bw_minus_wall = observed_raw - wall_gap;
                if (s.has_cpu_entry && pending->has_end_cpu && s.cpu_entry >= pending->end_cpu) {
                    row->has_between_cpu = true;
                    row->between_cpu = s.cpu_entry - pending->end_cpu;
                }
                row->between_wall = floor0(wall_gap - pending->exec);
                replace(row->window_before, share(pending->window_state));
                replace(row->window_after, wall_gap >= .05 ? readWindow(s) : none());
                row->has_lan = lanDelta(s, pending->lan_totals.get(), lan.get(), row->outside, row->lan);
                if (row->has_lan) row->outside_without_lan = floor0(row->outside - row->lan.wall[s.lan_poll]);
                add(s, row);
            }))
            s.disable();
        return integer(frame_id).release();
    } catch (const std::exception& e) { return fail(e); }
}

PyObject* finishFrame(PyObject* self, PyObject* args, PyObject* kwargs) {
    static const char* keywords[] = {"frame_id", "entry_wall", "tick_dt", "motion_dt", "stages", "probes", "context",
                                     "probe_durations", "projectile", "combat", nullptr};
    PyObject *frame_id, *entry_wall, *tick_dt, *motion_dt, *stages_in, *probes_in, *context;
    PyObject *probe_durations = Py_None, *projectile = Py_None, *combat = Py_None;
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "OOOOOOO|OOO:finish", const_cast<char**>(keywords), &frame_id, &entry_wall,
                                     &tick_dt, &motion_dt, &stages_in, &probes_in, &context, &probe_durations, &projectile, &combat))
        return nullptr;
    try {
        State& s = state(self);
        if (!enabled(s)) Py_RETURN_NONE;
        if (!guarded([&] {
                auto stages = dictOr(stages_in);
                auto probes = dictOr(probes_in);
                bool emitted = false;
                p::Ref emit_seconds = flt(0.0);
                if (s.emit_due && s.samples) {
                    const double emit_start = toDouble(p::Ref(PyObject_CallFunctionObjArgs(s.clock.get(), nullptr)).get());
                    auto payload = format(s);
                    p::Ref(PyObject_CallFunctionObjArgs(s.writer.get(), payload.get(), nullptr));
                    const double spent = toDouble(p::Ref(PyObject_CallFunctionObjArgs(s.clock.get(), nullptr)).get()) - emit_start;
                    if (spent > 0.0) replace(emit_seconds, flt(spent));
                    emitted = true;
                    s.window_seconds = s.steady_window;
                    s.resetWindow();
                }
                put(stages.get(), "diag_emit", emit_seconds);
                // Captured after all callback work, including diagnostic logging.
                auto lan = readLanPoll(s);
                auto window_state = readWindow(s);
                bool has_end_cpu = false;
                double end_cpu = 0.;
                readCpu(s, has_end_cpu, end_cpu);
                const double end_wall = toDouble(p::Ref(PyObject_CallFunctionObjArgs(s.clock.get(), nullptr)).get());
                auto pending = std::make_shared<Pending>();
                replace(pending->cause, p::Ref(PyNumber_Int(frame_id)));
                pending->entry_wall = toDouble(entry_wall);
                pending->exec = floor0(end_wall - toDouble(entry_wall));
                pending->has_end_cpu = has_end_cpu;
                pending->end_cpu = end_cpu;
                replace(pending->window_state, std::move(window_state));
                if (has_end_cpu && s.has_cpu_entry && end_cpu >= s.cpu_entry) {
                    pending->has_thread_cpu = true;
                    pending->thread_cpu = end_cpu - s.cpu_entry;
                }
                pending->tick_dt = floor0(toDouble(tick_dt));
                pending->motion_dt = floor0(toDouble(motion_dt));
                replace(pending->stages, std::move(stages));
                replace(pending->probes, std::move(probes));
                replace(pending->probe_durations, dictOr(probe_durations));
                replace(pending->projectile, dictOr(projectile));
                replace(pending->combat, p::retain(combat));
                pending->lan_totals = std::move(lan);
                replace(pending->context, dictOr(context));
                pending->emitted = emitted;
                s.pending = std::move(pending);
            }))
            s.disable();
        Py_RETURN_NONE;
    } catch (const std::exception& e) { return fail(e); }
}

// Publish the last sealed intervals when a round ends early.
PyObject* flush(PyObject* self, PyObject*) {
    try {
        State& s = state(self);
        if (!enabled(s) || !s.samples) Py_RETURN_NONE;
        if (!guarded([&] {
                auto payload = format(s);
                p::Ref(PyObject_CallFunctionObjArgs(s.writer.get(), payload.get(), nullptr));
                s.resetWindow();
            }))
            s.disable();
        Py_RETURN_NONE;
    } catch (const std::exception& e) { return fail(e); }
}

PyObject* emitDue(PyObject* self, PyObject*) {
    try {
        State& s = state(self);
        return boolean(enabled(s) && s.emit_due && s.samples).release();
    } catch (const std::exception& e) { return fail(e); }
}
PyObject* snapshot(PyObject* self, PyObject*) {
    try { return dictOf(state(self).last_snapshot.get()).release(); } catch (const std::exception& e) { return fail(e); }
}
PyObject* noteCollections(PyObject* self, PyObject* counts) {
    try {
        State& s = state(self);
        if (!enabled(s) || !PyDict_Check(counts)) Py_RETURN_FALSE;
        auto out = p::dict();
        auto items = method(counts, "items");
        p::Ref iterator(PyObject_GetIter(items.get()));
        while (PyObject* item = PyIter_Next(iterator.get())) {
            p::Ref held(item);
            auto pair = unpack(item, 2);
            if (PyDict_SetItem(out.get(), str(pair[0].get()).get(), toInt(pair[1].get()).get()) < 0) throw p::Error("collections");
        }
        if (PyErr_Occurred()) throw p::Error("collections");
        replace(s.collections, std::move(out));
        Py_RETURN_TRUE;
    } catch (const std::exception& e) { return fail(e); }
}
PyObject* noteBotLoad(PyObject* self, PyObject* report) {
    try {
        State& s = state(self);
        if (!enabled(s) || !PyDict_Check(report)) Py_RETURN_FALSE;
        auto busiest = method(report, "get", p::text("busiest").get());
        replace(s.load_busiest, p::flag(busiest.get()) ? p::Ref(PySequence_Tuple(busiest.get())) : p::Ref(PyTuple_New(0)));
        Py_RETURN_TRUE;
    } catch (const std::exception& e) { return fail(e); }
}
PyObject* noteWorkerRuntime(PyObject* self, PyObject* report) {
    try {
        State& s = state(self);
        if (!enabled(s) || !PyDict_Check(report)) Py_RETURN_FALSE;
        replace(s.worker_runtime, dictOf(report));
        Py_RETURN_TRUE;
    } catch (const std::exception& e) { return fail(e); }
}
PyObject* noteCombatCaptures(PyObject* self, PyObject* captures) {
    try {
        State& s = state(self);
        if (enabled(s)) p::Ref(_PyList_Extend(reinterpret_cast<PyListObject*>(s.combat_completed.get()), captures));
        Py_RETURN_NONE;
    } catch (const std::exception& e) { return fail(e); }
}

PyObject* getEnabled(PyObject* self, void*) { return share(state(self).enabled).release(); }
int setEnabled(PyObject* self, PyObject* value, void*) {
    if (!value) { PyErr_SetString(PyExc_AttributeError, "enabled"); return -1; }
    replace(state(self).enabled, p::retain(value));
    return 0;
}

int traverse(PyObject* self, visitproc visit, void* arg) {
    State* s = reinterpret_cast<Diagnostics*>(self)->state;
    if (!s) return 0;
    auto all = [&](const p::Ref& r) { return r.get() ? visit(r.get(), arg) : 0; };
    auto pending = [&](const Pending& b) {
        for (const p::Ref* r : {&b.cause, &b.window_state, &b.stages, &b.probes, &b.probe_durations, &b.projectile, &b.combat, &b.context})
            if (int v = all(*r)) return v;
        return b.lan_totals ? all(b.lan_totals->generation) : 0;
    };
    for (const p::Ref* r : {&s->globals, &s->clock, &s->cpu_clock, &s->window_clock, &s->writer, &s->enabled, &s->lan_getter,
                            &s->last_snapshot, &s->load_busiest, &s->collections, &s->worker_runtime, &s->combat_completed,
                            &s->last_context})
        if (int v = all(*r)) return v;
    for (const auto& r : s->recent) if (int v = all(r)) return v;
    if (s->pending) if (int v = pending(*s->pending)) return v;
    for (const auto& row : s->slow) {
        if (int v = pending(*row->base)) return v;
        for (const p::Ref* r : {&row->next, &row->window_before, &row->window_after}) if (int v = all(*r)) return v;
        for (const auto& r : row->previous) if (int v = all(r)) return v;
        for (const auto& r : row->following) if (int v = all(r)) return v;
    }
    return 0;
}
int clear(PyObject* self) {
    Diagnostics* d = reinterpret_cast<Diagnostics*>(self);
    State* s = d->state;
    d->state = nullptr;
    delete s;
    return 0;
}
void dealloc(PyObject* self) {
    PyObject_GC_UnTrack(self);
    clear(self);
    PyObject_GC_Del(self);
}
// A cleared instance (a collected cycle) answers like a disabled one.
template<PyObject* (*Body)(PyObject*, PyObject*)> PyObject* live(PyObject* self, PyObject* arg) {
    if (!reinterpret_cast<Diagnostics*>(self)->state) { PyErr_SetString(PyExc_RuntimeError, "frame diagnostics were cleared"); return nullptr; }
    return Body(self, arg);
}
template<PyObject* (*Body)(PyObject*, PyObject*, PyObject*)> PyObject* liveKw(PyObject* self, PyObject* args, PyObject* kwargs) {
    if (!reinterpret_cast<Diagnostics*>(self)->state) { PyErr_SetString(PyExc_RuntimeError, "frame diagnostics were cleared"); return nullptr; }
    return Body(self, args, kwargs);
}

PyCFunction keywordMethod(PyCFunctionWithKeywords fn) { return reinterpret_cast<PyCFunction>(reinterpret_cast<void (*)()>(fn)); }
PyMethodDef diagnosticMethods[] = {
    {"reset", live<reset>, METH_NOARGS, nullptr},
    {"begin", keywordMethod(liveKw<beginFrame>), METH_VARARGS | METH_KEYWORDS, nullptr},
    {"finish", keywordMethod(liveKw<finishFrame>), METH_VARARGS | METH_KEYWORDS, nullptr},
    {"flush", live<flush>, METH_NOARGS, nullptr},
    {"emit_due", live<emitDue>, METH_NOARGS, nullptr},
    {"snapshot", live<snapshot>, METH_NOARGS, nullptr},
    {"note_collections", live<noteCollections>, METH_O, nullptr},
    {"note_bot_load", live<noteBotLoad>, METH_O, nullptr},
    {"note_worker_runtime", live<noteWorkerRuntime>, METH_O, nullptr},
    {"note_combat_captures", live<noteCombatCaptures>, METH_O, nullptr},
    {nullptr, nullptr, 0, nullptr}};
PyGetSetDef diagnosticFields[] = {
    {const_cast<char*>("enabled"), reinterpret_cast<getter>(getEnabled), reinterpret_cast<setter>(setEnabled), nullptr, nullptr},
    {nullptr, nullptr, nullptr, nullptr, nullptr}};
PyTypeObject DiagnosticsType = {PyVarObject_HEAD_INIT(nullptr, 0) "offline_client_lifecycle_native.FrameDiagnostics"};

// FrameDiagnostics(globals, clock=None, writer=None, window_seconds=DIAGNOSTIC_WINDOW_SECONDS,
//                  initial_window_seconds=None, cpu_clock=None, window_clock=None)
PyObject* create(PyObject*, PyObject* args, PyObject* kwargs) {
    static const char* keywords[] = {"globals", "clock", "writer", "window_seconds", "initial_window_seconds", "cpu_clock",
                                     "window_clock", nullptr};
    PyObject *g, *clock = Py_None, *writer = Py_None, *window_seconds = nullptr, *initial = Py_None;
    PyObject *cpu_clock = Py_None, *window_clock = Py_None;
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O!|OOOOOO:FrameDiagnostics", const_cast<char**>(keywords), &PyDict_Type, &g,
                                     &clock, &writer, &window_seconds, &initial, &cpu_clock, &window_clock))
        return nullptr;
    try {
        if (!DiagnosticsType.tp_flags) {
            DiagnosticsType.tp_basicsize = sizeof(Diagnostics);
            DiagnosticsType.tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC;
            DiagnosticsType.tp_dealloc = dealloc;
            DiagnosticsType.tp_traverse = traverse;
            DiagnosticsType.tp_clear = clear;
            DiagnosticsType.tp_methods = diagnosticMethods;
            DiagnosticsType.tp_getset = diagnosticFields;
            if (PyType_Ready(&DiagnosticsType) < 0) throw p::Error("frame diagnostics type failed");
        }
        auto state = std::unique_ptr<State>(new State());
        State& s = *state;
        replace(s.globals, p::retain(g));
        replace(s.clock, p::flag(clock) ? p::retain(clock) : global(g, "_PROFILE_CLOCK"));
        replace(s.cpu_clock, p::retain(cpu_clock));
        replace(s.window_clock, p::retain(window_clock));
        if (p::flag(writer)) replace(s.writer, p::retain(writer));
        else {
            PyObject* stream = PySys_GetObject(const_cast<char*>("stdout"));
            if (!stream) throw p::Error("sys.stdout is unavailable");
            replace(s.writer, attr(stream, "write"));
        }
        auto steady = window_seconds ? p::retain(window_seconds) : global(g, "DIAGNOSTIC_WINDOW_SECONDS");
        const double steady_value = toDouble(steady.get());
        s.steady_window = steady_value > 0.25 ? steady_value : 0.25;
        const double initial_value = initial == Py_None ? s.steady_window : toDouble(initial);
        s.initial_window = initial_value > 0.25 ? initial_value : 0.25;
        s.stage_names = names(g, "_FRAME_STAGE_NAMES");
        s.stage_count = s.stage_names.size();
        for (auto& detail : names(g, "_FRAME_DETAIL_NAMES")) s.stage_names.push_back(std::move(detail));
        s.probe_names = names(g, "PROBE_KINDS");
        s.lan_names = names(g, "_LAN_POLL_STAGES");
        s.projectile_names = names(g, "_PROJECTILE_METRIC_NAMES");
        s.lan_poll = indexOf(s.lan_names, "poll");
        s.lan_materialize = indexOf(s.lan_names, "materialize");
        s.lan_handle = indexOf(s.lan_names, "handle");
        s.lan_notify = indexOf(s.lan_names, "notify");
        s.top_frames = std::size_t(strictInt(global(g, "DIAGNOSTIC_TOP_FRAMES").get()));
        s.percentile_cap = std::size_t(strictInt(global(g, "DIAGNOSTIC_PERCENTILE_SAMPLES").get()));
        s.reset();
        auto* object = PyObject_GC_New(Diagnostics, &DiagnosticsType);
        if (!object) throw p::Error("frame diagnostics allocation failed");
        object->state = state.release();
        PyObject_GC_Track(object);
        return reinterpret_cast<PyObject*>(object);
    } catch (const std::exception& e) { return fail(e); }
}

// Module helper _xyz: a dict, an indexable triple, or x/y/z attributes, each through _number.
p::Ref xyz(PyObject* value) {
    if (PyDict_Check(value)) {
        p::Ref a[] = {flt(number(method(value, "get", p::text("x").get()).get())), flt(number(method(value, "get", p::text("y").get()).get())),
                      flt(number(method(value, "get", p::text("z").get()).get()))};
        return p::Ref(PyTuple_Pack(3, a[0].get(), a[1].get(), a[2].get()));
    }
    std::vector<p::Ref> items;
    for (Py_ssize_t i = 0; i < 3; ++i) {
        PyObject* item = PyObject_GetItem(value, integer(i).get());
        if (!item) {
            if (!PyErr_ExceptionMatches(PyExc_TypeError) && !PyErr_ExceptionMatches(PyExc_IndexError)) throw p::Error("_xyz");
            PyErr_Clear();
            items.clear();
            break;
        }
        items.push_back(flt(number(p::Ref(item).get())));
    }
    if (items.size() == 3) return p::Ref(PyTuple_Pack(3, items[0].get(), items[1].get(), items[2].get()));
    auto zero = flt(0.0);
    p::Ref a[] = {flt(number(lookupAttr(value, "x", zero.get()).get())), flt(number(lookupAttr(value, "y", zero.get()).get())),
                  flt(number(lookupAttr(value, "z", zero.get()).get()))};
    return p::Ref(PyTuple_Pack(3, a[0].get(), a[1].get(), a[2].get()));
}
p::Ref formatXyz(PyObject* value) {
    auto f = p::text("(%.2f, %.2f, %.2f)");
    return p::Ref(PyString_Format(f.get(), xyz(value).get()));
}
// Module helper _format_axes: the three basis lengths, which read as the matrix scale.
p::Ref formatAxes(PyObject* matrix) {
    auto axis = optionalAttr(matrix, "applyToAxis");
    if (!PyCallable_Check(axis.get())) return p::text("unreadable");
    std::vector<p::Ref> lengths;
    for (long index = 0; index < 3; ++index) {
        bool readable = false;
        guarded([&] {
            p::Ref vector(PyObject_CallFunction(axis.get(), const_cast<char*>("l"), index));
            lengths.push_back(flt(number(attr(vector.get(), "length").get())));
            readable = true;
        });
        if (!readable) return p::text("unreadable");
    }
    p::Ref a[] = {std::move(lengths[0]), std::move(lengths[1]), std::move(lengths[2])};
    return fmt("%.3f/%.3f/%.3f", a);
}

// BattleRuntime._report_local_decals(): the decal transforms the player's tank was built on.
PyObject* reportLocalDecals(PyObject*, PyObject* args) {
    PyObject *battle, *g;
    if (!PyArg_ParseTuple(args, "OO", &battle, &g)) return nullptr;
    try {
        auto server = attr(battle, "_server");
        auto entity = server.get() != Py_None ? method(battle, "_server_entity", attr(server.get(), "vehicle_id").get()) : none();
        auto descriptor = optionalAttr(entity.get(), "typeDescriptor");
        if (descriptor.get() == Py_None) Py_RETURN_FALSE;
        for (const char* part_name : {"chassis", "hull", "turret"}) {
            auto part = optionalAttr(descriptor.get(), part_name);
            auto decals = optionalAttr(part.get(), "AODecals");
            if (!p::flag(decals.get())) replace(decals, p::Ref(PyTuple_New(0)));
            p::Ref iterator(PyObject_GetIter(decals.get()));
            std::int64_t index = 0;
            while (PyObject* item = PyIter_Next(iterator.get())) {
                p::Ref transform(item);
                p::Ref a[] = {p::text(part_name), integer(index++), formatXyz(optionalAttr(transform.get(), "translation").get()),
                              formatAxes(transform.get())};
                writeStdout(fmt("[Offline LAN 0.9.22] AODECAL %s[%d] at=%s axes=%s\n", a).get());
            }
            if (PyErr_Occurred()) throw p::Error("AODecals");
        }
        auto chassis = optionalAttr(descriptor.get(), "chassis");
        auto appearance = optionalAttr(entity.get(), "appearance");
        p::Ref a[] = {formatXyz(optionalAttr(chassis.get(), "hullPosition").get()),
                      boolean(optionalAttr(appearance.get(), "_CompoundAppearance__splodge").get() != Py_None)};
        writeStdout(fmt("[Offline LAN 0.9.22] AODECAL hullPosition=%s splodge=%s\n", a).get());
        Py_RETURN_TRUE;
    } catch (const std::exception& e) { return fail(e); }
}

// BattleRuntime._report_local_compound(now): name a degenerate transform under the player's own compound.
PyObject* reportLocalCompound(PyObject*, PyObject* args) {
    PyObject *battle, *now, *g;
    if (!PyArg_ParseTuple(args, "OOO", &battle, &now, &g)) return nullptr;
    try {
        auto matrix = method(battle, "_local_body_pose");
        if (matrix.get() == Py_None) Py_RETURN_FALSE;
        auto target = optionalAttr(attr(attr(battle, "_runtime").get(), "bigworld").get(), "target");
        auto axes = formatAxes(matrix.get());
        p::Ref targeting[] = {optionalAttr(target.get(), "isEnabled"), optionalAttr(target.get(), "isFull"),
                              optionalAttr(target.get(), "selectionFovDegrees"), optionalAttr(target.get(), "maxDistance"),
                              optionalAttr(target.get(), "skeletonCheckEnabled")};
        p::Ref signature(PyTuple_New(6));
        PyTuple_SET_ITEM(signature.get(), 0, share(axes).release());
        for (Py_ssize_t i = 0; i < 5; ++i) PyTuple_SET_ITEM(signature.get(), i + 1, p::Ref(PyObject_Repr(targeting[i].get())).release());
        auto compare = [](PyObject* a, PyObject* b, int op) {
            const int result = PyObject_RichCompareBool(a, b, op);
            if (result < 0) throw p::Error("compound report comparison");
            return result != 0;
        };
        if (compare(signature.get(), attr(battle, "_compound_report_signature").get(), Py_EQ) ||
            compare(attr(battle, "_compound_reports").get(), attr(battle, "_COMPOUND_REPORT_LIMIT").get(), Py_GE) ||
            compare(now, attr(battle, "_next_compound_report").get(), Py_LT))
            Py_RETURN_FALSE;
        setAttr(battle, "_compound_report_signature", signature.get());
        setAttr(battle, "_next_compound_report", p::Ref(PyNumber_Add(now, attr(battle, "_COMPOUND_REPORT_SECONDS").get())).get());
        setAttr(battle, "_compound_reports", p::Ref(PyNumber_InPlaceAdd(attr(battle, "_compound_reports").get(), integer(1).get())).get());
        if (equal(attr(battle, "_compound_reports").get(), integer(1).get()))
            dispatch(battle, "_report_local_decals", "report_local_decals", {}, {g});
        {
            p::Ref a[] = {formatXyz(attr(matrix.get(), "translation").get()), std::move(axes)};
            writeStdout(fmt("[Offline LAN 0.9.22] COMPOUND at=%s axes=%s\n", a).get());
        }
        // PyTarget.entity dereferences the picked entity with no null check; calling the object is #1513's guarded read.
        auto entity = PyCallable_Check(target.get()) ? p::Ref(PyObject_CallFunctionObjArgs(target.get(), nullptr)) : none();
        p::Ref a[] = {std::move(targeting[0]), std::move(targeting[1]), std::move(targeting[2]), std::move(targeting[3]),
                      std::move(targeting[4]), optionalAttr(entity.get(), "id")};
        writeStdout(fmt("[Offline LAN 0.9.22] TARGETING enabled=%s full=%s fov=%s max=%s "
                        "skeleton=%s entity=%s\n", a).get());
        Py_RETURN_TRUE;
    } catch (const std::exception& e) { return fail(e); }
}

PyMethodDef methods[] = {
    {"FrameDiagnostics", keywordMethod(create), METH_VARARGS | METH_KEYWORDS,
     "The pinned _FrameDiagnostics with its state in C++."},
    {"report_local_compound", reportLocalCompound, METH_VARARGS, "Report the player's compound transform when it changes."},
    {"report_local_decals", reportLocalDecals, METH_VARARGS, "Report the player's AO decal transforms once."},
    {nullptr, nullptr, 0, nullptr}};
}
}

PyMethodDef* diagnostics_methods() { return frame_diagnostics::methods; }
}
