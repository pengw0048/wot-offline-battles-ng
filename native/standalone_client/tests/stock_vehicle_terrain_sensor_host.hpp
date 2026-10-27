// Real CPython 2.7 host for the production Svarog/TerrainMatKindSensor sources.
// Space collision, D3DX and LOD links are recorded fixtures, not engine owners.
#pragma once
#include <Python.h>
#include <array>
#include <cassert>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#define BW_BEGIN_NAMESPACE namespace BW {
#define BW_END_NAMESPACE }

namespace BW {
using string = std::string;
using uint8 = std::uint8_t;
using uint32 = std::uint32_t;
using uint64 = std::uint64_t;
using SpaceID = std::uint32_t;

extern std::vector<std::string> hostErrors;
#define ERROR_MSG(...) do { char text[256]; std::snprintf(text, sizeof(text), __VA_ARGS__); \
    BW::hostErrors.push_back(text); } while (0)

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
    PyObjectPtr(PyObject * p = nullptr) : p_(p) { Py_XINCREF(p_); }
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
    PyObject * pyRepr() { return PyString_FromFormat("<%s object at %p>", typeName(), (void *)this); }
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
template <class T> PySequenceMethods * asSequence() { return 0; }
template <class T> PyNumberMethods * asNumber() { return 0; }
}

template <class T> PyTypeObject hostType(const char * name, PyTypeObject * base) {
    PyTypeObject type = { PyVarObject_HEAD_INIT(&PyType_Type, 0) };
    type.tp_name = name;
    type.tp_basicsize = sizeof(T);
    type.tp_dealloc = [](PyObject * o) { delete static_cast<T *>(static_cast<PyObjectPlus *>(o)); };
    type.tp_repr = &T::_tp_repr;
    type.tp_getattro = PyObject_GenericGetAttr;
    type.tp_setattro = PyObject_GenericSetAttr;
    type.tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE | Py_TPFLAGS_CHECKTYPES;
    type.tp_methods = T::s_getMethodDefs();
    type.tp_getset = T::s_getAttributeDefs();
    type.tp_base = base;
    type.tp_call = PyTypeObjectUtil::callFunction<T>();
    type.tp_as_sequence = PyTypeObjectUtil::asSequence<T>();
    type.tp_as_number = PyTypeObjectUtil::asNumber<T>();
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
        static PyObject * _tp_repr(PyObject * o) { return getSelf(o)->pyRepr(); } \
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
// Public BigWorld 14.4.1 pyobject_plus.hpp sequence method shapes.
#define PY_SIZE_INQUIRY_METHOD(NAME) \
    Py_ssize_t NAME(); static Py_ssize_t _##NAME(PyObject * self) { return getSelf(self)->NAME(); }
#define PY_BINARY_FUNC_METHOD(NAME) \
    PyObject * NAME(PyObject * a); static PyObject * _##NAME(PyObject * self, PyObject * a) { return getSelf(self)->NAME(a); }
#define PY_INTARG_FUNC_METHOD(NAME) \
    PyObject * NAME(Py_ssize_t a); static PyObject * _##NAME(PyObject * self, Py_ssize_t a) { return getSelf(self)->NAME(a); }
#define PY_INTINTARG_FUNC_METHOD(NAME) \
    PyObject * NAME(Py_ssize_t a, Py_ssize_t b); \
    static PyObject * _##NAME(PyObject * self, Py_ssize_t a, Py_ssize_t b) { return getSelf(self)->NAME(a, b); }
#define PY_INTOBJARG_PROC_METHOD(NAME) \
    int NAME(Py_ssize_t a, PyObject * b); \
    static int _##NAME(PyObject * self, Py_ssize_t a, PyObject * b) { return getSelf(self)->NAME(a, b); }
#define PY_INTINTOBJARG_PROC_METHOD(NAME) \
    int NAME(Py_ssize_t a, Py_ssize_t b, PyObject * c); \
    static int _##NAME(PyObject * self, Py_ssize_t a, Py_ssize_t b, PyObject * c) { return getSelf(self)->NAME(a, b, c); }
#define PY_OBJOBJ_PROC_METHOD(NAME) \
    int NAME(PyObject * a); static int _##NAME(PyObject * self, PyObject * a) { return getSelf(self)->NAME(a); }
#define PY_GENERAL_TYPEOBJECT_WITH_BASE_WITH_NAME(CLASS, BASE, NAME) \
    PyTypeObject CLASS::s_type_ = BW::hostType<CLASS>(NAME, BASE); \
    static BW::HostTypeRegistration CLASS##_hostType(&CLASS::s_type_);
#define PY_TYPEOBJECT(CLASS) PY_GENERAL_TYPEOBJECT_WITH_BASE_WITH_NAME(CLASS, &Super::s_type_, #CLASS)
#define PY_TYPEOBJECT_SPECIALISE_CALL(CLASS, CALL) \
    namespace PyTypeObjectUtil { template <> ternaryfunc callFunction<CLASS>() { return CALL; } }
#define PY_TYPEOBJECT_SPECIALISE_SEQ(CLASS, SEQ) \
    namespace PyTypeObjectUtil { template <> PySequenceMethods * asSequence<CLASS>() { return SEQ; } }
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

class MatrixProvider : public PyObjectPlus {
    Py_Header(MatrixProvider, PyObjectPlus)
public:
    MatrixProvider(bool, PyTypeObject * type) : PyObjectPlus(type) {}
    virtual void matrix(Matrix & m) const = 0;
};
using MatrixProviderPtr = SmartPointer<MatrixProvider>;

// PyVector<Vector3>, registered as Math.Vector3 (0x01a0bac8) in the client.
class HostVector3 : public PyObjectPlus {
    Py_Header(HostVector3, PyObjectPlus)
public:
    explicit HostVector3(const Vector3 & v) : PyObjectPlus(&s_type_), value(v) {}
    PyObject * pyRepr() { return PyString_FromFormat("(%s, %s, %s)", text(value.x).c_str(), text(value.y).c_str(), text(value.z).c_str()); }
    static std::string text(float v) { char b[32]; std::snprintf(b, sizeof(b), "%g", v); return b; }
    Vector3 value;
};

namespace Script {
struct FiniTimeJob { virtual ~FiniTimeJob() {} virtual void fini() = 0; };
// Public BigWorld 14.4.1 script.cpp conversions that the client's #1513
// readers (0068fba0, 0068fad0, 00690240) reproduce.
inline int setData(PyObject * o, double & value, const char * name = "") {
    if (PyFloat_Check(o)) { value = PyFloat_AsDouble(o); return 0; }
    if (PyInt_Check(o)) { value = PyInt_AsLong(o); return 0; }
    if (PyLong_Check(o)) { value = PyLong_AsUnsignedLong(o); if (!PyErr_Occurred()) return 0; }
    PyErr_Format(PyExc_TypeError, "%s must be set to a float", name); return -1;
}
inline int setData(PyObject * o, float & value, const char * name = "") {
    double d; const int r = setData(o, d, name); if (r == 0) value = float(d); return r;
}
inline int setData(PyObject * o, uint32 & value, const char * name = "") {
    if (PyInt_Check(o)) {
        long v = PyInt_AsLong(o); value = uint32(v);
        if (v >= 0 && static_cast<long>(value) == v) return 0;
    }
    if (PyFloat_Check(o)) { value = uint32(int(PyFloat_AsDouble(o))); return 0; }
    if (PyLong_Check(o)) {
        unsigned long u = PyLong_AsUnsignedLong(o); value = uint32(u);
        if (!PyErr_Occurred() && value == u) return 0;
        PyErr_Clear();
        long s = PyLong_AsLong(o); value = uint32(s);
        if (!PyErr_Occurred() && s >= 0 && s == static_cast<long>(value)) return 0;
    }
    PyErr_Format(PyExc_TypeError, "%s must be set to an uint", name); return -1;
}
inline int setData(PyObject * o, Vector3 & value, const char * name = "") {
    if (HostVector3::Check(o)) { value = static_cast<HostVector3 *>(o)->value; return 0; }
    PyErr_Clear();
    if (PyArg_ParseTuple(o, "fff", &value.x, &value.y, &value.z)) return 0;
    PyErr_Format(PyExc_TypeError, "%s must be set to a Vector3 or a tuple of 3 floats", name); return -1;
}
inline int setData(PyObject * o, BW::string & value, const char * name = "") {
    if (!PyString_Check(o)) { PyErr_Format(PyExc_TypeError, "%s must be set to a string", name); return -1; }
    value = PyString_AsString(o); return 0;
}
inline PyObject * getData(bool v) { return PyBool_FromLong(v); }
inline PyObject * getData(int v) { return PyInt_FromLong(v); }
inline PyObject * getData(uint32 v) {
    unsigned long u = v; return long(u) < 0 ? PyLong_FromUnsignedLong(u) : PyInt_FromLong(long(u));
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
struct WorldTriangle {
    uint32 flags;
    uint32 collisionFlags() const { return flags & 0xff; }
    uint32 materialKind() const { return (flags >> 8) & 0xff; }
};
struct CollisionObstacle {};
const int COLLIDE_BEFORE = 1;
const int COLLIDE_ALL = 3;
class CollisionCallback {
public:
    virtual ~CollisionCallback() {}
    virtual bool useHighestAvailableLod() { return false; }
    virtual int operator()(const CollisionObstacle &, const WorldTriangle &, float) = 0;
};
struct HostTriangle { uint32 flags; float distance; };
struct HostSegment { SpaceID space; std::array<float, 3> start, end; std::vector<int> results; };
extern std::vector<HostSegment> hostSegments;
struct ClientSpace {
    SpaceID id_ = 0;
    // Called once per segment; returns the triangles the traversal offers.
    std::vector<std::vector<HostTriangle>> script;
    size_t cursor = 0;
    SpaceID id() const { return id_; }
    float collide(const Vector3 & s, const Vector3 & e, CollisionCallback & cc) {
        HostSegment segment{id_, {{s.x, s.y, s.z}}, {{e.x, e.y, e.z}}, {}};
        // f642b0's ClosestCcbNative asks terrain for its highest LOD.
        assert(cc.useHighestAvailableLod());
        if (!script.empty()) {
            CollisionObstacle obstacle;
            for (const HostTriangle & t : script[cursor++ % script.size()])
                segment.results.push_back(cc(obstacle, WorldTriangle{t.flags}, t.distance));
        }
        hostSegments.push_back(segment);
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

// Stand-in for stock_vehicle_visuals' native PyDataLink<LodState>.
bool sampleStockLodLink(PyObject * link, uint32 & priority, float & distance);
}
