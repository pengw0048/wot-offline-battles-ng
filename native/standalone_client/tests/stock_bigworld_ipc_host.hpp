// Real CPython 2.7 host for the production BigWorldIPC.Mutex binding.
// Win32 is a recording fixture here: it proves calls and control flow only.
#pragma once
#include <Python.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#define BW_BEGIN_NAMESPACE namespace BW {
#define BW_END_NAMESPACE }

// ---- recorded Win32 subset used by stock_bigworld_ipc.cpp ----
typedef void * HANDLE;
typedef int BOOL;
typedef std::uint32_t DWORD;
typedef char * LPSTR;
typedef const char * LPCSTR;
typedef void * LPVOID;
struct SECURITY_ATTRIBUTES;
#define FALSE 0
#define WAIT_OBJECT_0 0u
#define WAIT_ABANDONED 0x80u
#define WAIT_FAILED 0xffffffffu
#define FORMAT_MESSAGE_FROM_SYSTEM 0x1000u
struct HostCall { std::string name; std::vector<std::uintptr_t> values; std::string text; };
extern std::vector<HostCall> hostCalls;
struct HostResults {
    std::uintptr_t created = 0x100;
    DWORD wait = 0, lastError = 0;
    BOOL released = 1, closed = 1;
    bool formatted = true;
};
extern HostResults hostResults;
inline HANDLE CreateMutexA(SECURITY_ATTRIBUTES * security, BOOL owner, LPCSTR name) {
    hostCalls.push_back({"CreateMutexA", {std::uintptr_t(security), std::uintptr_t(owner)}, name ? name : "<NULL>"});
    return reinterpret_cast<HANDLE>(hostResults.created);
}
inline DWORD WaitForSingleObject(HANDLE handle, DWORD milliseconds) {
    hostCalls.push_back({"WaitForSingleObject", {std::uintptr_t(handle), milliseconds}, ""});
    return hostResults.wait;
}
inline BOOL ReleaseMutex(HANDLE handle) {
    hostCalls.push_back({"ReleaseMutex", {std::uintptr_t(handle)}, ""});
    return hostResults.released;
}
inline BOOL CloseHandle(HANDLE handle) {
    hostCalls.push_back({"CloseHandle", {std::uintptr_t(handle)}, ""});
    return hostResults.closed;
}
inline DWORD GetLastError() {
    hostCalls.push_back({"GetLastError", {}, ""});
    return hostResults.lastError;
}
inline DWORD FormatMessageA(DWORD flags, LPVOID, DWORD error, DWORD language, LPSTR buffer, DWORD size, void *) {
    hostCalls.push_back({"FormatMessageA", {flags, error, language, size}, ""});
    if (!hostResults.formatted) return 0;
    std::snprintf(buffer, size, "system message %u\r\n", unsigned(error));
    return DWORD(std::strlen(buffer));
}

namespace BW {
using string = std::string;
struct HostFactory { const char * module; const char * name; PyTypeObject * type; };
std::vector<HostFactory> & hostFactories();
std::vector<PyTypeObject *> & hostTypes();
PyObject * hostMemberNames(PyObject * self, bool methods);

class PyObjectPlus : public PyObject {
public:
    explicit PyObjectPlus(PyTypeObject * type) { PyObject_Init(this, type); }
    virtual ~PyObjectPlus() {}
    static void * operator new(std::size_t size) { return PyObject_Malloc(size); }
    static void operator delete(void * p) { PyObject_Free(p); }
    static PyTypeObject s_type_;
};

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
    type.tp_new = (newfunc)T::_pyNew;
    return type;
}
struct HostTypeRegistration { explicit HostTypeRegistration(PyTypeObject * t) { hostTypes().push_back(t); } };
struct HostFactoryRegistration {
    HostFactoryRegistration(const char * m, const char * n, PyTypeObject * t) {
        hostFactories().push_back(HostFactory{m, n, t});
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
#define PY_FACTORY_DECLARE() \
    static PyObject * pyNew(PyObject * args); \
    static PyObject * _pyNew(PyObject *, PyObject * args, PyObject *) { return This::pyNew(args); }
#define PY_TYPEOBJECT(CLASS) \
    PyTypeObject CLASS::s_type_ = BW::hostType<CLASS>(#CLASS, &Super::s_type_); \
    static BW::HostTypeRegistration CLASS##_hostType(&CLASS::s_type_);
#define PY_BEGIN_METHODS(CLASS) \
    PyMethodDef * CLASS::s_getMethodDefs() { static std::vector<PyMethodDef> defs; if (defs.empty()) {
#define PY_METHOD(NAME) defs.push_back(PyMethodDef{#NAME, (PyCFunction)This::_py_##NAME, METH_VARARGS | METH_KEYWORDS, NULL});
#define PY_END_METHODS() defs.push_back(PyMethodDef{NULL, NULL, 0, NULL}); } return defs.data(); }
#define PY_BEGIN_ATTRIBUTES(CLASS) \
    PyGetSetDef * CLASS::s_getAttributeDefs() { static std::vector<PyGetSetDef> defs; if (defs.empty()) { \
        defs.push_back(PyGetSetDef{(char *)"__members__", [](PyObject * s, void *) { return BW::hostMemberNames(s, false); }, NULL, NULL, NULL}); \
        defs.push_back(PyGetSetDef{(char *)"__methods__", [](PyObject * s, void *) { return BW::hostMemberNames(s, true); }, NULL, NULL, NULL});
#define PY_END_ATTRIBUTES() defs.push_back(PyGetSetDef{NULL, NULL, NULL, NULL, NULL}); } return defs.data(); }
// PyFactoryMethodLink (#1513 006a8ae0, public py_factory_method_link.cpp):
// the type itself becomes MODULE.NAME and tp_name is qualified.
#define PY_FACTORY_NAMED(CLASS, NAME, MODULE) \
    static BW::HostFactoryRegistration CLASS##_hostFactory(#MODULE, NAME, &CLASS::s_type_);

namespace Script {
// Public BigWorld 14.4.1 script.cpp semantics, matched by #1513 0068fe20.
inline int setData(PyObject * o, BW::string & value, const char * name) {
    PyObject * utf8 = NULL;
    if (PyUnicode_Check(o)) {
        utf8 = PyUnicode_AsUTF8String(o);
        if (!utf8) return -1;
        o = utf8;
    }
    if (!PyString_Check(o)) {
        PyErr_Format(PyExc_TypeError, "%s must be set to a string.", name);
        Py_XDECREF(utf8);
        return -1;
    }
    char * data; Py_ssize_t size;
    PyString_AsStringAndSize(o, &data, &size);
    value.assign(data, size);
    Py_XDECREF(utf8);
    return 0;
}
// Fixture only: the engine's shared float conversion is not under test here.
inline int setData(PyObject * o, float & value, const char * name) {
    if (PyFloat_Check(o)) value = float(PyFloat_AsDouble(o));
    else if (PyInt_Check(o)) value = float(PyInt_AsLong(o));
    else if (PyLong_Check(o)) value = float(PyLong_AsDouble(o));
    else { PyErr_Format(PyExc_TypeError, "%s must be set to a float.", name); return -1; }
    return PyErr_Occurred() ? -1 : 0;
}
}
}
