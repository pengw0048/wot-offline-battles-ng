// Real CPython 2.7 host for the production Svarog/WaterSensor sources.
// Water, space collision and D3DX are recorded fixtures, not engine owners.
#pragma once
#include <Python.h>
#include <array>
#include <cassert>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <string>
#include <vector>

#define BW_BEGIN_NAMESPACE namespace BW {
#define BW_END_NAMESPACE }
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

namespace BW {
using string = std::string;
using uint8 = std::uint8_t;
using uint32 = std::uint32_t;
using uint64 = std::uint64_t;
using SpaceID = std::uint32_t;

extern std::vector<std::string> hostErrors;
#define ERROR_MSG(...) do { char text[256]; std::snprintf(text, sizeof(text), __VA_ARGS__); \
    BW::hostErrors.push_back(text); } while (0)

struct Vector2 { float x, y; Vector2(float a = 0, float b = 0) : x(a), y(b) {} };
struct Vector3 {
    float x, y, z;
    Vector3(float a = 0, float b = 0, float c = 0) : x(a), y(b), z(c) {}
};
struct Vector4 { float x = 0, y = 0, z = 0, w = 0; };
struct Matrix { float m[4][4] = {}; };
// Host reference only. Windows uses d3dx9_43!D3DXVec3Transform.
extern std::vector<std::array<float, 3>> hostTransforms;
inline Vector4 * D3DXVec3Transform(Vector4 * out, const Vector3 * v, const Matrix * m) {
    hostTransforms.push_back({{v->x, v->y, v->z}});
    out->x = v->x * m->m[0][0] + v->y * m->m[1][0] + v->z * m->m[2][0] + m->m[3][0];
    out->y = v->x * m->m[0][1] + v->y * m->m[1][1] + v->z * m->m[2][1] + m->m[3][1];
    out->z = v->x * m->m[0][2] + v->y * m->m[1][2] + v->z * m->m[2][2] + m->m[3][2];
    out->w = v->x * m->m[0][3] + v->y * m->m[1][3] + v->z * m->m[2][3] + m->m[3][3];
    return out;
}

class PyObjectPtr {
public:
    enum Steal { STEAL_REFERENCE };
    PyObjectPtr(PyObject * p = nullptr) : p_(p) { Py_XINCREF(p_); }
    PyObjectPtr(PyObject * p, Steal) : p_(p) {}
    PyObjectPtr(const PyObjectPtr & o) : p_(o.p_) { Py_XINCREF(p_); }
    ~PyObjectPtr() { Py_XDECREF(p_); }
    PyObjectPtr & operator=(PyObject * p) {
        PyObject * old = p_; Py_XINCREF(p); p_ = p; Py_XDECREF(old); return *this;
    }
    PyObjectPtr & operator=(const PyObjectPtr & o) { return *this = o.p_; }
    PyObject * get() const { return p_; }
    explicit operator bool() const { return p_ != nullptr; }
    bool operator!() const { return p_ == nullptr; }
private:
    PyObject * p_;
};

class PyObjectPlus : public PyObject {
public:
    explicit PyObjectPlus(PyTypeObject * type) { PyObject_Init(this, type); }
    virtual ~PyObjectPlus() {}
    static void * operator new(std::size_t size) { return PyObject_Malloc(size); }
    static void operator delete(void * p) { PyObject_Free(p); }
    const char * typeName() const { return Py_TYPE(this)->tp_name; }
    static PyTypeObject s_type_;
    static bool Check(PyObject * o) { return o && PyObject_TypeCheck(o, &s_type_); }
    static PyMethodDef * s_getMethodDefs() { static PyMethodDef none[] = {{NULL, NULL, 0, NULL}}; return none; }
    static PyGetSetDef * s_getAttributeDefs() { static PyGetSetDef none[] = {{NULL, NULL, NULL, NULL, NULL}}; return none; }
};
using PyObjectPlusWithWeakReference = PyObjectPlus;

template <class T> class SmartPointer {
public:
    SmartPointer(T * p = nullptr) : p_(p) { Py_XINCREF(static_cast<PyObject *>(p_)); }
    SmartPointer(const SmartPointer & o) : SmartPointer(o.p_) {}
    ~SmartPointer() { Py_XDECREF(static_cast<PyObject *>(p_)); }
    SmartPointer & operator=(const SmartPointer & o) {
        T * old = p_; p_ = o.p_; Py_XINCREF(static_cast<PyObject *>(p_)); Py_XDECREF(static_cast<PyObject *>(old));
        return *this;
    }
    SmartPointer & operator=(T * p) { return *this = SmartPointer(p); }
    T * operator->() const { return p_; }
    T * get() const { return p_; }
    explicit operator bool() const { return p_ != nullptr; }
    bool operator!() const { return p_ == nullptr; }
private:
    T * p_;
};

struct HostFactory { const char * module; const char * name; PyCFunctionWithKeywords function; };
std::vector<HostFactory> & hostFactories();
std::vector<PyTypeObject *> & hostTypes();
PyObject * hostMemberNames(PyObject * self, bool methods);

namespace PyTypeObjectUtil {
template <class T> ternaryfunc callFunction() { return 0; }
}

template <class T> PyTypeObject hostType(const char * name, PyTypeObject * base) {
    PyTypeObject type = { PyVarObject_HEAD_INIT(&PyType_Type, 0) };
    type.tp_name = name;
    type.tp_basicsize = sizeof(T);
    type.tp_dealloc = [](PyObject * o) { delete static_cast<T *>(static_cast<PyObjectPlus *>(o)); };
    type.tp_getattro = PyObject_GenericGetAttr;
    type.tp_setattro = PyObject_GenericSetAttr;
    type.tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE;
    type.tp_methods = T::s_getMethodDefs();
    type.tp_getset = T::s_getAttributeDefs();
    type.tp_base = base;
    type.tp_call = PyTypeObjectUtil::callFunction<T>();
    return type;
}
struct HostTypeRegistration { explicit HostTypeRegistration(PyTypeObject * t) { hostTypes().push_back(t); } };
struct HostFactoryRegistration {
    HostFactoryRegistration(const char * m, const char * n, PyCFunctionWithKeywords f) {
        hostFactories().push_back(HostFactory{m, n, f});
    }
};

#define Py_Header(CLASS, SUPER) \
    public: \
        typedef CLASS This; typedef SUPER Super; \
        static PyTypeObject s_type_; \
        static bool Check(PyObject * o) { return o && PyObject_TypeCheck(o, &s_type_); } \
        static PyMethodDef * s_getMethodDefs(); \
        static PyGetSetDef * s_getAttributeDefs(); \
        static This * getSelf(PyObject * o) { return static_cast<This *>(static_cast<PyObjectPlus *>(o)); } \
    private:
#define PY_METHOD_DECLARE(NAME) \
    PyObject * NAME(PyObject * args); \
    static PyObject * _##NAME(PyObject * self, PyObject * args, PyObject *) { return getSelf(self)->NAME(args); }
#define PY_RO_ATTRIBUTE_SET(NAME) \
    int pySet_##NAME(PyObject *) { \
        PyErr_Format(PyExc_TypeError, "Sorry, the attribute " #NAME " in %s is read-only", this->typeName()); \
        return -1; }
#define PY_RO_ATTRIBUTE_DECLARE(MEMBER, NAME) \
    PyObject * pyGet_##NAME() { return Script::getData(MEMBER); } PY_RO_ATTRIBUTE_SET(NAME)
#define PY_FACTORY_DECLARE() \
    static PyObject * pyNew(PyObject * args); \
    static PyObject * _pyNew(PyObject *, PyObject * args, PyObject *) { return This::pyNew(args); }
#define PY_AUTO_FACTORY_DECLARE(CLASS, ARGS) \
    static PyObject * _pyNew(PyObject *, PyObject * args, PyObject *) { \
        if (!PyArg_ParseTuple(args, ":" #CLASS)) return NULL; return CLASS::New(); }
#define PY_GENERAL_TYPEOBJECT_WITH_BASE_WITH_NAME(CLASS, BASE, NAME) \
    PyTypeObject CLASS::s_type_ = BW::hostType<CLASS>(NAME, BASE); \
    static BW::HostTypeRegistration CLASS##_hostType(&CLASS::s_type_);
#define PY_TYPEOBJECT(CLASS) PY_GENERAL_TYPEOBJECT_WITH_BASE_WITH_NAME(CLASS, &Super::s_type_, #CLASS)
#define PY_TYPEOBJECT_SPECIALISE_CALL(CLASS, CALL) \
    namespace PyTypeObjectUtil { template <> ternaryfunc callFunction<CLASS>() { return CALL; } }
#define PY_BEGIN_METHODS(CLASS) \
    PyMethodDef * CLASS::s_getMethodDefs() { static std::vector<PyMethodDef> defs; if (defs.empty()) {
#define PY_METHOD(NAME) defs.push_back(PyMethodDef{#NAME, (PyCFunction)This::_py_##NAME, METH_VARARGS | METH_KEYWORDS, NULL});
#define PY_END_METHODS() defs.push_back(PyMethodDef{NULL, NULL, 0, NULL}); } return defs.data(); }
#define PY_BEGIN_ATTRIBUTES(CLASS) \
    PyGetSetDef * CLASS::s_getAttributeDefs() { static std::vector<PyGetSetDef> defs; if (defs.empty()) { \
        defs.push_back(PyGetSetDef{(char *)"__members__", [](PyObject * s, void *) { return BW::hostMemberNames(s, false); }, \
            [](PyObject *, PyObject *, void *) { PyErr_SetString(PyExc_TypeError, "Sorry, the attribute __members__ is read-only"); return -1; }, NULL, NULL}); \
        defs.push_back(PyGetSetDef{(char *)"__methods__", [](PyObject * s, void *) { return BW::hostMemberNames(s, true); }, \
            [](PyObject *, PyObject *, void *) { PyErr_SetString(PyExc_TypeError, "Sorry, the attribute __methods__ is read-only"); return -1; }, NULL, NULL});
#define PY_ATTRIBUTE(NAME) { struct A { \
        static PyObject * get(PyObject * s, void *) { return This::getSelf(s)->pyGet_##NAME(); } \
        static int set(PyObject * s, PyObject * v, void *) { \
            if (!v) { PyErr_SetString(PyExc_TypeError, "Cannot delete attribute " #NAME); return -1; } \
            return This::getSelf(s)->pySet_##NAME(v); } }; \
        defs.push_back(PyGetSetDef{(char *)#NAME, &A::get, &A::set, NULL, NULL}); }
#define PY_END_ATTRIBUTES() defs.push_back(PyGetSetDef{NULL, NULL, NULL, NULL, NULL}); } return defs.data(); }
#define PY_FACTORY_NAMED(CLASS, NAME, MODULE) \
    static BW::HostFactoryRegistration CLASS##_hostFactory(#MODULE, NAME, (PyCFunctionWithKeywords)CLASS::_pyNew);
#define PY_FACTORY(CLASS, MODULE) PY_FACTORY_NAMED(CLASS, #CLASS, MODULE)
#define PY_MODULE_FUNCTION(NAME, MODULE)

class MatrixProvider : public PyObjectPlus {
    Py_Header(MatrixProvider, PyObjectPlus)
public:
    MatrixProvider(bool, PyTypeObject * type) : PyObjectPlus(type) {}
    virtual void matrix(Matrix & m) const = 0;
};
using MatrixProviderPtr = SmartPointer<MatrixProvider>;

namespace Script {
struct FiniTimeJob { virtual ~FiniTimeJob() {} virtual void fini() = 0; };
inline int number(PyObject * o, double & value, const char * name, const char * kind) {
    if (PyFloat_Check(o)) value = PyFloat_AsDouble(o);
    else if (PyInt_Check(o)) value = double(PyInt_AsLong(o));
    else if (PyLong_Check(o)) value = PyLong_AsDouble(o);
    else { PyErr_Format(PyExc_TypeError, "%s must be set to a %s", name, kind); return -1; }
    return PyErr_Occurred() ? -1 : 0;
}
inline int setData(PyObject * o, float & value, const char * name = "") {
    double v; if (number(o, v, name, "float")) return -1; value = float(v); return 0;
}
inline int setData(PyObject * o, uint32 & value, const char * name = "") {
    if ((PyInt_Check(o) && PyInt_AsLong(o) >= 0) || PyLong_Check(o)) {
        unsigned long v = PyInt_Check(o) ? (unsigned long)PyInt_AsLong(o) : PyLong_AsUnsignedLong(o);
        if (PyErr_Occurred() || v > 0xffffffffUL) { PyErr_Clear(); PyErr_Format(PyExc_ValueError, "%s is out of range", name); return -1; }
        value = uint32(v); return 0;
    }
    PyErr_Format(PyExc_TypeError, "%s must be set to an unsigned int", name); return -1;
}
inline int setData(PyObject * o, BW::string & value, const char * name = "") {
    if (!PyString_Check(o)) { PyErr_Format(PyExc_TypeError, "%s must be set to a string", name); return -1; }
    value = PyString_AsString(o); return 0;
}
template <std::size_t N> inline int sequence(PyObject * o, float (&out)[N], const char * name) {
    if (!PySequence_Check(o) || PySequence_Size(o) != Py_ssize_t(N)) {
        PyErr_Clear(); PyErr_Format(PyExc_TypeError, "%s must be set to a Vector%d", name, int(N)); return -1;
    }
    for (std::size_t i = 0; i < N; ++i) {
        PyObject * item = PySequence_GetItem(o, Py_ssize_t(i));
        const int failed = setData(item, out[i], name); Py_DECREF(item);
        if (failed) return -1;
    }
    return 0;
}
inline int setData(PyObject * o, Vector2 & value, const char * name = "") {
    float v[2]; if (sequence(o, v, name)) return -1; value = Vector2(v[0], v[1]); return 0;
}
inline int setData(PyObject * o, Vector3 & value, const char * name = "") {
    float v[3]; if (sequence(o, v, name)) return -1; value = Vector3(v[0], v[1], v[2]); return 0;
}
inline PyObject * getData(bool v) { return PyBool_FromLong(v); }
inline PyObject * getData(float v) { return PyFloat_FromDouble(v); }
inline PyObject * getData(const PyObjectPtr & v) {
    PyObject * o = v.get() ? v.get() : Py_None; Py_INCREF(o); return o;
}
inline PyObject * getData(const Vector3 & v) { return Py_BuildValue("(fff)", v.x, v.y, v.z); }
inline PyObject * getData(const Matrix & v) {
    PyObject * t = PyTuple_New(16);
    for (int i = 0; i < 16; ++i) PyTuple_SET_ITEM(t, i, PyFloat_FromDouble(v.m[i / 4][i % 4]));
    return t;
}
inline bool call(PyObject * fn, PyObject * args, const char *) {
    if (!fn) { PyErr_Clear(); Py_XDECREF(args); return false; }
    PyObject * r = PyObject_CallObject(fn, args); Py_DECREF(fn); Py_XDECREF(args);
    if (!r) { PyErr_Print(); return false; }
    Py_DECREF(r); return true;
}
}

namespace Moo {
struct RenderContext { uint32 frame = 0; uint32 frameTimestamp() const { return frame; } };
inline RenderContext & rc() { static RenderContext c; return c; }
}

// Physical fixtures: each records the exact query the production code issued.
struct WorldTriangle { uint32 flags; uint32 collisionFlags() const { return flags & 0xff; } };
struct CollisionObstacle {};
const int COLLIDE_BEFORE = 1;
const int COLLIDE_ALL = 3;
class CollisionCallback {
public:
    virtual ~CollisionCallback() {}
    virtual bool useHighestAvailableLod() { return false; }
    virtual int operator()(const CollisionObstacle &, const WorldTriangle &, float) = 0;
};
struct HostSegment { SpaceID space; std::array<float, 3> start, end; };
extern std::vector<HostSegment> hostSegments;
struct ClientSpace {
    SpaceID id_ = 0;
    std::vector<uint32> triangles;
    SpaceID id() const { return id_; }
    float collide(const Vector3 & s, const Vector3 & e, CollisionCallback & cc) {
        hostSegments.push_back(HostSegment{id_, {{s.x, s.y, s.z}}, {{e.x, e.y, e.z}}});
        // f642b0's ClosestCcbNative asks terrain for its highest LOD.
        assert(cc.useHighestAvailableLod());
        CollisionObstacle obstacle;
        for (uint32 flags : triangles) cc(obstacle, WorldTriangle{flags}, 0.25f);
        return -1.f;
    }
};
struct ClientSpacePtr {
    ClientSpace * p = nullptr;
    ClientSpacePtr(ClientSpace * s = nullptr) : p(s) {}
    ClientSpace * operator->() const { return p; }
    explicit operator bool() const { return p != nullptr; }
    bool operator!() const { return p == nullptr; }
};
struct SpaceManager {
    std::map<SpaceID, ClientSpace *> spaces;
    static SpaceManager & instance() { static SpaceManager m; return m; }
    ClientSpace * space(SpaceID id) { auto f = spaces.find(id); return f == spaces.end() ? nullptr : f->second; }
};
namespace DeprecatedSpaceHelpers {
extern ClientSpacePtr camera;
inline ClientSpacePtr cameraSpace() { return camera; }
}
struct HostWaterQuery { std::array<float, 3> start, end; };
extern std::vector<HostWaterQuery> hostWaterQueries;
struct Water {
    std::function<float(const Vector3 &, const Vector3 &)> query;
    float stockGroundRayDistance(const Vector3 & s, const Vector3 & e) const {
        hostWaterQueries.push_back(HostWaterQuery{{{s.x, s.y, s.z}}, {{e.x, e.y, e.z}}});
        return query(s, e);
    }
};
struct Waters : std::vector<Water *> { static Waters & instance() { static Waters w; return w; } };
}
