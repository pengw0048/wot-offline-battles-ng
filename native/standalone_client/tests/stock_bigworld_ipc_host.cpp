// Real CPython 2.7 runs the production BigWorldIPC.Mutex binding with a
// recording Win32 fixture. Kernel mutex semantics need the Win32 probe.
#include "stock_bigworld_ipc_host.hpp"

// The trace hooks below read and seed the private handle; nothing else does.
#define private public
#include "stock_bigworld_ipc.cpp"
#undef private

#include <cassert>
#include <iostream>
#include <map>
#include <string>

std::vector<HostCall> hostCalls;
HostResults hostResults;

namespace BW {
std::vector<HostFactory> & hostFactories() { static std::vector<HostFactory> v; return v; }
std::vector<PyTypeObject *> & hostTypes() { static std::vector<PyTypeObject *> v; return v; }
PyObject * hostMemberNames(PyObject * self, bool methods) {
    PyObject * names = PyList_New(0);
    for (PyTypeObject * type = Py_TYPE(self); type && type != &PyBaseObject_Type; type = type->tp_base) {
        if (methods) {
            for (PyMethodDef * m = type->tp_methods; m && m->ml_name; ++m) {
                PyObject * n = PyString_FromString(m->ml_name); PyList_Append(names, n); Py_DECREF(n);
            }
        } else {
            for (PyGetSetDef * g = type->tp_getset; g && g->name; ++g)
                if (std::strcmp(g->name, "__members__") && std::strcmp(g->name, "__methods__")) {
                    PyObject * n = PyString_FromString(g->name); PyList_Append(names, n); Py_DECREF(n);
                }
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
PyObject * setResults(PyObject *, PyObject * args) {
    unsigned long created; unsigned wait, lastError; int released, closed, formatted;
    if (!PyArg_ParseTuple(args, "kIIiii", &created, &wait, &lastError, &released, &closed, &formatted)) return NULL;
    hostResults.created = created; hostResults.wait = wait; hostResults.lastError = lastError;
    hostResults.released = released; hostResults.closed = closed; hostResults.formatted = formatted != 0;
    Py_RETURN_NONE;
}
PyObject * calls(PyObject *, PyObject *) {
    PyObject * list = PyList_New(0);
    for (const HostCall & call : hostCalls) {
        PyObject * values = PyTuple_New(Py_ssize_t(call.values.size()));
        for (size_t i = 0; i < call.values.size(); ++i)
            PyTuple_SET_ITEM(values, Py_ssize_t(i), PyLong_FromUnsignedLongLong(call.values[i]));
        PyObject * item = call.name != "CreateMutexA" ? Py_BuildValue("(sN)", call.name.c_str(), values) :
            Py_BuildValue("(sNs)", call.name.c_str(), values, call.text.c_str());
        PyList_Append(list, item); Py_DECREF(item);
    }
    hostCalls.clear();
    return list;
}
PyObject * milliseconds(PyObject *, PyObject * args) {
    float seconds; if (!PyArg_ParseTuple(args, "f", &seconds)) return NULL;
    return PyLong_FromUnsignedLong(wot::StockIpcMutex::timeoutMilliseconds(seconds));
}
PyObject * handle(PyObject *, PyObject * args) {
    PyObject * object; if (!PyArg_ParseTuple(args, "O", &object) || !BW::PyMutex::Check(object)) return NULL;
    return PyLong_FromUnsignedLongLong(std::uintptr_t(static_cast<BW::PyMutex *>(object)->mutex_.handle_));
}
PyObject * setHandle(PyObject *, PyObject * args) {
    PyObject * object; unsigned long value;
    if (!PyArg_ParseTuple(args, "Ok", &object, &value) || !BW::PyMutex::Check(object)) return NULL;
    static_cast<BW::PyMutex *>(object)->mutex_.handle_ = reinterpret_cast<void *>(std::uintptr_t(value));
    Py_RETURN_NONE;
}
PyMethodDef hostMethods[] = {
    {"handle", handle, METH_VARARGS, NULL}, {"setHandle", setHandle, METH_VARARGS, NULL},
    {"setResults", setResults, METH_VARARGS, NULL}, {"calls", calls, METH_NOARGS, NULL},
    {"milliseconds", milliseconds, METH_VARARGS, NULL}, {NULL, NULL, 0, NULL}};
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
    std::cout << "Real CPython 2.7 BigWorldIPC.Mutex binding contract passed" << std::endl;
    return 0;
}
