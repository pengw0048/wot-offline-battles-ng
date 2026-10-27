// Real CPython 2.7 host for the production battle animation and Svarog
// sources. Compound nodes, audio and D3DX are recorded fixtures.
#pragma once
#include <Python.h>
#include <array>
#include <cassert>
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
#define bw_snprintf std::snprintf

namespace BW {
using string = std::string;
using uint16 = std::uint16_t;
using uint32 = std::uint32_t;
using uint64 = std::uint64_t;

extern std::vector<std::string> hostErrors;
#define ERROR_MSG(...) do { char text[256]; std::snprintf(text, sizeof(text), __VA_ARGS__); \
    BW::hostErrors.push_back(text); } while (0)

struct Vector3 { float x, y, z; Vector3(float a = 0, float b = 0, float c = 0) : x(a), y(b), z(c) {} };
struct Matrix {
    float m[4][4] = {};
    static const Matrix identity;
};

class PyObjectPtr {
public:
    enum Steal { STEAL_REFERENCE };
    PyObjectPtr(PyObject * p = nullptr) : p_(p) { Py_XINCREF(p_); }
    PyObjectPtr(PyObject * p, Steal) : p_(p) {}
    PyObjectPtr(const PyObjectPtr & o) : p_(o.p_) { Py_XINCREF(p_); }
    ~PyObjectPtr() { Py_XDECREF(p_); }
    PyObjectPtr & operator=(PyObject * p) { PyObject * old = p_; Py_XINCREF(p); p_ = p; Py_XDECREF(old); return *this; }
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
    // Engine PyObjectPlus objects accept weak references (WeakPyPtr owners).
    PyObject * weakreflist_ = nullptr;
    static Py_ssize_t weakOffset() {
        PyObjectPlus * probe = reinterpret_cast<PyObjectPlus *>(alignof(PyObjectPlus) * 64);
        return reinterpret_cast<char *>(&probe->weakreflist_) - reinterpret_cast<char *>(static_cast<PyObject *>(probe));
    }
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
    T * operator->() const { return p_; }
    T * get() const { return p_; }
    explicit operator bool() const { return p_ != nullptr; }
    bool operator!() const { return p_ == nullptr; }
private:
    T * p_;
};

// Borrowed identity that becomes NULL when the Python object dies.
template <class T> class WeakPyPtr {
public:
    WeakPyPtr(T * p = nullptr) { if (p) ref_ = PyObjectPtr(PyWeakref_NewRef(p, NULL), PyObjectPtr::STEAL_REFERENCE); }
    const PyObject * getPyObj() const {
        if (!ref_) return nullptr;
        PyObject * o = PyWeakref_GetObject(ref_.get());
        return o == Py_None ? nullptr : o;
    }
private:
    PyObjectPtr ref_;
};

struct HostFactory { const char * module; const char * name; PyCFunctionWithKeywords function; };
std::vector<HostFactory> & hostFactories();
std::vector<PyTypeObject *> & hostTypes();
PyObject * hostMemberNames(PyObject * self, bool methods);

namespace PyTypeObjectUtil { template <class T> ternaryfunc callFunction() { return 0; } }

template <class T> PyTypeObject hostType(const char * name, PyTypeObject * base) {
    PyTypeObject type = { PyVarObject_HEAD_INIT(&PyType_Type, 0) };
    type.tp_name = name;
    type.tp_basicsize = sizeof(T);
    type.tp_dealloc = [](PyObject * o) {
        if (static_cast<PyObjectPlus *>(o)->weakreflist_) PyObject_ClearWeakRefs(o);
        delete static_cast<T *>(static_cast<PyObjectPlus *>(o)); };
    type.tp_getattro = PyObject_GenericGetAttr;
    type.tp_setattro = PyObject_GenericSetAttr;
    type.tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE;
    type.tp_methods = T::s_getMethodDefs();
    type.tp_getset = T::s_getAttributeDefs();
    type.tp_base = base;
    type.tp_call = PyTypeObjectUtil::callFunction<T>();
    type.tp_weaklistoffset = PyObjectPlus::weakOffset();
    return type;
}
struct HostTypeRegistration { explicit HostTypeRegistration(PyTypeObject * t) { hostTypes().push_back(t); } };
struct HostFactoryRegistration {
    HostFactoryRegistration(const char * m, const char * n, PyCFunctionWithKeywords f) {
        hostFactories().push_back(HostFactory{m, n, f}); }
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
            NULL, NULL, NULL});
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

// Auto-ticked providers join the host ProviderStore like the engine's.
class MatrixProvider : public PyObjectPlus {
    Py_Header(MatrixProvider, PyObjectPlus)
public:
    MatrixProvider(bool autoTick, PyTypeObject * type);
    virtual ~MatrixProvider();
    virtual void tick(float) {}
    virtual void matrix(Matrix & m) const = 0;
private:
    bool autoTick_;
};
using MatrixProviderPtr = SmartPointer<MatrixProvider>;
void tickHostProviderStore(float dTime);

namespace Script {
struct FiniTimeJob { virtual ~FiniTimeJob() {} virtual void fini() = 0; };
inline int setData(PyObject * o, float & value, const char * name = "") {
    if (PyFloat_Check(o)) value = float(PyFloat_AsDouble(o));
    else if (PyInt_Check(o)) value = float(PyInt_AsLong(o));
    else { PyErr_Format(PyExc_TypeError, "%s must be set to a float", name); return -1; }
    return 0;
}
inline int setData(PyObject * o, uint16 & value, const char * name = "") {
    if (!PyInt_Check(o)) { PyErr_Format(PyExc_TypeError, "%s must be set to an int", name); return -1; }
    const long v = PyInt_AsLong(o);
    if (v < 0 || v > 0xffff) { PyErr_SetString(PyExc_ValueError, "Integer is out of range"); return -1; }
    value = uint16(v); return 0;
}
inline int setData(PyObject * o, BW::string & value, const char * name = "") {
    if (!PyString_Check(o)) { PyErr_Format(PyExc_TypeError, "%s must be set to a string", name); return -1; }
    value = PyString_AsString(o); return 0;
}
inline int setData(PyObject * o, Vector3 & value, const char * name = "") {
    float v[3];
    if (!PySequence_Check(o) || PySequence_Size(o) != 3) {
        PyErr_Clear(); PyErr_Format(PyExc_TypeError, "%s must be set to a Vector3", name); return -1; }
    for (int i = 0; i < 3; ++i) {
        PyObjectPtr item(PySequence_GetItem(o, i), PyObjectPtr::STEAL_REFERENCE);
        if (setData(item.get(), v[i], name)) return -1;
    }
    value = Vector3(v[0], v[1], v[2]); return 0;
}
inline int setData(PyObject * o, MatrixProviderPtr & value, const char * name = "") {
    if (o == Py_None) { value = MatrixProviderPtr(); return 0; }
    if (!MatrixProvider::Check(o)) { PyErr_Format(PyExc_TypeError, "%s must be set to a MatrixProvider", name); return -1; }
    value = MatrixProviderPtr(static_cast<MatrixProvider *>(static_cast<PyObjectPlus *>(o))); return 0;
}
// A MatrixProvider or a 16-float sequence, like the engine's Matrix converter.
inline int setData(PyObject * o, Matrix & value, const char * name = "") {
    if (MatrixProvider::Check(o)) { static_cast<MatrixProvider *>(static_cast<PyObjectPlus *>(o))->matrix(value); return 0; }
    if (!PySequence_Check(o) || PySequence_Size(o) != 16) {
        PyErr_Clear(); PyErr_Format(PyExc_TypeError, "%s must be set to a Matrix", name); return -1; }
    for (int i = 0; i < 16; ++i) {
        PyObjectPtr item(PySequence_GetItem(o, i), PyObjectPtr::STEAL_REFERENCE);
        if (setData(item.get(), value.m[i / 4][i % 4], name)) return -1;
    }
    return 0;
}
inline PyObject * getData(float v) { return PyFloat_FromDouble(v); }
inline PyObject * getData(uint16 v) { return PyInt_FromLong(v); }
inline PyObject * getData(bool v) { return PyBool_FromLong(v); }
inline PyObject * getData(const Matrix & v) {
    PyObject * t = PyTuple_New(16);
    for (int i = 0; i < 16; ++i) PyTuple_SET_ITEM(t, i, PyFloat_FromDouble(v.m[i / 4][i % 4]));
    return t;
}
inline PyObject * getData(const PyObjectPtr & v) { PyObject * o = v.get() ? v.get() : Py_None; Py_INCREF(o); return o; }
inline bool call(PyObject * fn, PyObject * args, const char *) {
    if (!fn) { PyErr_Clear(); Py_XDECREF(args); return false; }
    PyObject * r = PyObject_CallObject(fn, args); Py_DECREF(fn); Py_XDECREF(args);
    if (!r) { PyErr_Print(); return false; }
    Py_DECREF(r); return true;
}
}

// Recorded audio owner; definitions live in the host translation unit.
namespace StockAudioDetail { struct Source { std::string name; bool alive; }; struct Voice {}; }
extern std::vector<std::string> hostAudio;
struct ResourceRefs;
struct BoundingBox;
}
