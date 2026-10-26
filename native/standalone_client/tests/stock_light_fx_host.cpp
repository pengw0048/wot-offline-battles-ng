// Real CPython 2.7 runs the production BigWorld.WGLightFx binding against a
// recording LightFX.dll fixture. It proves binding and control flow only.
#include "stock_bigworld_ipc_host.hpp"

#include <cassert>
#include <iostream>
#include <map>
#include <set>

typedef void * HMODULE;
typedef int (*FARPROC)();
typedef unsigned int UINT;
struct Vector4 { float x, y, z, w; };

std::vector<HostCall> hostCalls;
HostResults hostResults;

namespace {
bool libraryPresent = false;
std::set<std::string> missingExports;
std::map<std::string, unsigned> results;
unsigned reportedCount = 0;
std::string reportedDescription;

unsigned record(const char * name, std::vector<std::uintptr_t> values) {
    hostCalls.push_back({name, values, ""});
    return results[name];
}
unsigned colorValue(const void * color) {
    const unsigned char * bytes = static_cast<const unsigned char *>(color);
    return bytes[0] | bytes[1] << 8 | bytes[2] << 16 | unsigned(bytes[3]) << 24;
}
unsigned lfxInitialize() { return record("LFX_Initialize", {}); }
unsigned lfxRelease() { return record("LFX_Release", {}); }
unsigned lfxReset() { return record("LFX_Reset", {}); }
unsigned lfxUpdate() { return record("LFX_Update", {}); }
unsigned lfxGetNumDevices(unsigned * count) {
    *count = reportedCount;
    return record("LFX_GetNumDevices", {});
}
unsigned lfxGetNumLights(unsigned device, unsigned * count) {
    *count = reportedCount;
    return record("LFX_GetNumLights", {device});
}
unsigned lfxGetLightDescription(unsigned device, unsigned light, char * text, unsigned size) {
    std::snprintf(text, size, "%s", reportedDescription.c_str());
    return record("LFX_GetLightDescription", {device, light, size});
}
unsigned lfxSetLightActionColor(unsigned device, unsigned light, unsigned action, const void * color) {
    return record("LFX_SetLightActionColor", {device, light, action, colorValue(color)});
}
unsigned lfxActionColorEx(unsigned mask, unsigned action, unsigned primary, unsigned secondary) {
    return record("LFX_ActionColorEx", {mask, action, primary, secondary});
}
}

inline HMODULE LoadLibraryW(const wchar_t * name) {
    std::string text;
    for (const wchar_t * c = name; *c; ++c) text.push_back(char(*c));
    hostCalls.push_back({"LoadLibraryW", {}, text});
    return libraryPresent ? reinterpret_cast<HMODULE>(0x1000) : NULL;
}
inline FARPROC GetProcAddress(HMODULE module, const char * name) {
    assert(module == reinterpret_cast<HMODULE>(0x1000));
    static const std::map<std::string, void *> exports = {
        {"LFX_Initialize", (void *)&lfxInitialize}, {"LFX_Release", (void *)&lfxRelease},
        {"LFX_Reset", (void *)&lfxReset}, {"LFX_Update", (void *)&lfxUpdate},
        {"LFX_GetNumDevices", (void *)&lfxGetNumDevices}, {"LFX_GetNumLights", (void *)&lfxGetNumLights},
        {"LFX_GetLightDescription", (void *)&lfxGetLightDescription},
        {"LFX_SetLightActionColor", (void *)&lfxSetLightActionColor},
        {"LFX_ActionColorEx", (void *)&lfxActionColorEx}};
    auto found = exports.find(name);
    assert(found != exports.end());
    return missingExports.count(name) ? NULL : reinterpret_cast<FARPROC>(found->second);
}
inline int FreeLibrary(HMODULE module) {
    hostCalls.push_back({"FreeLibrary", {std::uintptr_t(module)}, ""});
    return 1;
}

namespace BW {
namespace Script {
// Public BigWorld 14.4.1 script.cpp conversions used by the #1513 wrappers.
inline PyObject * getData(bool value) { return PyBool_FromLong(value); }
inline PyObject * getData(unsigned value) {
    unsigned long asULong = value;
    return long(asULong) < 0 ? PyLong_FromUnsignedLong(asULong) : PyInt_FromLong(long(asULong));
}
inline PyObject * getData(const BW::string & value) {
    return PyString_FromStringAndSize(value.data(), Py_ssize_t(value.size()));
}
inline int setData(PyObject * object, unsigned & value, const char * name) {
    if (PyInt_Check(object)) {
        long longValue = PyInt_AsLong(object);
        value = unsigned(longValue);
        if (longValue >= 0 && long(value) == longValue) return 0;
    }
    if (PyFloat_Check(object)) { value = unsigned(int(PyFloat_AsDouble(object))); return 0; }
    if (PyLong_Check(object)) {
        unsigned long asUnsignedLong = PyLong_AsUnsignedLong(object);
        value = unsigned(asUnsignedLong);
        if (!PyErr_Occurred() && value == asUnsignedLong) return 0;
        PyErr_Clear();
    }
    PyErr_Format(PyExc_TypeError, "%s must be set to an uint", name);
    return -1;
}
inline int setData(PyObject * object, Vector4 & value, const char * name) {
    if (PyArg_ParseTuple(object, "ffff", &value.x, &value.y, &value.z, &value.w)) return 0;
    PyErr_Format(PyExc_TypeError, "%s must be set to a Vector4 or tuple of 4 floats", name);
    return -1;
}
}
}

#include "stock_light_fx.cpp"

namespace BW {
std::vector<HostFactory> & hostFactories() { static std::vector<HostFactory> v; return v; }
std::vector<PyTypeObject *> & hostTypes() { static std::vector<PyTypeObject *> v; return v; }
PyObject * hostMemberNames(PyObject * self, bool methods) {
    PyObject * names = PyList_New(0);
    for (PyTypeObject * type = Py_TYPE(self); type && type != &PyBaseObject_Type; type = type->tp_base) {
        for (PyMethodDef * m = methods ? type->tp_methods : NULL; m && m->ml_name; ++m) {
            PyObject * n = PyString_FromString(m->ml_name); PyList_Append(names, n); Py_DECREF(n);
        }
    }
    return names;
}
PyTypeObject PyObjectPlus::s_type_ = [] {
    PyTypeObject type = { PyVarObject_HEAD_INIT(&PyType_Type, 0) };
    type.tp_name = "PyObjectPlus";
    type.tp_basicsize = sizeof(PyObjectPlus);
    type.tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE;
    return type;
}();
}

namespace {
PyObject * configure(PyObject *, PyObject * args) {
    int present; PyObject * missing; PyObject * values; unsigned count; const char * description;
    if (!PyArg_ParseTuple(args, "iOOIs", &present, &missing, &values, &count, &description)) return NULL;
    libraryPresent = present != 0;
    missingExports.clear();
    for (Py_ssize_t i = 0; i < PySequence_Size(missing); ++i) {
        PyObject * item = PySequence_GetItem(missing, i);
        missingExports.insert(PyString_AsString(item)); Py_DECREF(item);
    }
    results.clear();
    PyObject * key; PyObject * value; Py_ssize_t position = 0;
    while (PyDict_Next(values, &position, &key, &value))
        results[PyString_AsString(key)] = unsigned(PyInt_AsLong(value));
    reportedCount = count; reportedDescription = description;
    Py_RETURN_NONE;
}
PyObject * calls(PyObject *, PyObject *) {
    PyObject * list = PyList_New(0);
    for (const HostCall & call : hostCalls) {
        PyObject * values = PyTuple_New(Py_ssize_t(call.values.size()));
        for (size_t i = 0; i < call.values.size(); ++i)
            PyTuple_SET_ITEM(values, Py_ssize_t(i), PyLong_FromUnsignedLongLong(call.values[i]));
        PyObject * item = call.text.empty() ? Py_BuildValue("(sN)", call.name.c_str(), values) :
            Py_BuildValue("(sNs)", call.name.c_str(), values, call.text.c_str());
        PyList_Append(list, item); Py_DECREF(item);
    }
    hostCalls.clear();
    return list;
}
PyMethodDef hostMethods[] = {
    {"configure", configure, METH_VARARGS, NULL}, {"calls", calls, METH_NOARGS, NULL},
    {NULL, NULL, 0, NULL}};
}

int main(int argc, char ** argv) {
    assert(argc == 2);
    Py_NoSiteFlag = 1;
    Py_Initialize();
    assert(PyType_Ready(&BW::PyObjectPlus::s_type_) == 0);
    for (PyTypeObject * type : BW::hostTypes()) assert(PyType_Ready(type) == 0);
    std::vector<std::string> names;
    for (const BW::HostFactory & f : BW::hostFactories()) names.push_back(std::string(f.module) + "." + f.name);
    for (size_t i = 0; i < names.size(); ++i) {
        const BW::HostFactory & f = BW::hostFactories()[i];
        PyObject * module = PyImport_AddModule(f.module);
        assert(module);
        f.type->tp_name = names[i].c_str();
        Py_INCREF(f.type);
        PyModule_AddObject(module, f.name, reinterpret_cast<PyObject *>(f.type));
    }
    assert(Py_InitModule("Host", hostMethods));
    FILE * script = std::fopen(argv[1], "r");
    assert(script);
    const int failed = PyRun_SimpleFileEx(script, argv[1], 1);
    Py_Finalize();
    if (failed) return 1;
    std::cout << "Real CPython 2.7 BigWorld.WGLightFx binding contract passed" << std::endl;
    return 0;
}
