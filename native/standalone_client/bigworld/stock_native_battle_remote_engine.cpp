// The BigWorld engine boundary of remote presentation: Vector3/Matrix writes and entity ownership.
#if !defined(WOT_NATIVE_BATTLE_HOST_BUILD)
#include "pch.hpp"
#include "pyscript/script_math.hpp"
#include "connection_control.hpp"
#include "entity.hpp"
#include "py_entity.hpp"
#include "stock_native_client_drive_presentation_python.hpp"
#endif
#include "stock_native_battle_remote_internal.hpp"

namespace native_battle {
namespace remote {
#if defined(WOT_NATIVE_BATTLE_HOST_BUILD)
// Host syntax builds have no engine; these mirror the Python Math surface.
PyObject* nativeVector(const Point& v) { return Py_BuildValue("(ddd)", v[0], v[1], v[2]); }
Point nativeXYZ(PyObject* o) {
    return {p::scalar(py::attr(o, "x").get()), p::scalar(py::attr(o, "y").get()), p::scalar(py::attr(o, "z").get())};
}
void nativeMatrix(PyObject* m, const Point& v, const Point& r, bool rotate) {
    if (rotate) py::call(m, "setRotateYPR", p::Ref(Py_BuildValue("(ddd)", r[0], r[1], r[2])).get());
    py::set(m, "translation", p::Ref(Py_BuildValue("(ddd)", v[0], v[1], v[2])).get());
}
bool engineOwnsEntity(std::int64_t, PyObject* python) { return python && python != Py_None; }
void applyTrackMode(PyObject* appearance, std::array<int, 2> mode) {
    p::Ref value(Py_BuildValue("(ii)", mode[0], mode[1]));
    py::call(appearance, "changeEngineMode", value.get(), Py_True);
}
void applyTrackScroll(PyObject* appearance, double left, double right) {
    py::call(appearance, "updateTracksScroll", p::number(left).get(), p::number(right).get());
}
#else
void applyTrackMode(PyObject* appearance, std::array<int, 2> mode) {
    stock_native_client_drive::applyAppearanceTrackMode(appearance, {mode[0], mode[1]});
}
void applyTrackScroll(PyObject* appearance, double left, double right) {
    stock_native_client_drive::applyTrackScroll(appearance, left, right);
}
PyObject* nativeVector(const Point& v) { return BW::Script::getData(BW::Vector3(float(v[0]), float(v[1]), float(v[2]))); }
Point nativeXYZ(PyObject* o) {
    BW::Vector3 v;
    if (BW::Script::setData(o, v) != 0) throw p::Error("Native remote Vector3 is invalid");
    return {v.x, v.y, v.z};
}
void nativeMatrix(PyObject* o, const Point& v, const Point& r, bool rotate) {
    if (!BW::PyMatrix::Check(o)) throw p::Error("Native remote provider is not a writable Matrix");
    auto* m = static_cast<BW::PyMatrix*>(o);
    if (rotate) m->setRotate(float(r[0]), float(r[1]), float(r[2]));
    m->translation(BW::Vector3(float(v[0]), float(v[1]), float(v[2])));
}
bool engineOwnsEntity(std::int64_t id, PyObject* python) {
    auto* c = BW::ConnectionControl::pInstance();
    auto* e = c ? c->findEntity(static_cast<BW::EntityID>(id)) : nullptr;
    if (!e || e->isDestroyed() || !e->isInWorld()) return false;
    p::Ref actual(e->pPyEntity().newRef());
    if (actual.get() != python) return false;
    auto key = p::text("isStarted");
    PyObject* started = p::directAttribute(python, key.get());
    if (!started) { PyErr_Clear(); return false; }
    p::Ref held(started);
    return p::flag(started);
}
#endif
}
}
