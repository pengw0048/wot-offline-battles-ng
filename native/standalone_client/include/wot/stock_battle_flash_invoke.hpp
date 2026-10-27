#pragma once

// Requires Python.h (CPython 2.7) before inclusion.
#include <cstddef>
#include <vector>

namespace wot
{
namespace stock_battle_flash
{
// #1513 00aaef10: exact str, unicode, int, long, float and bool are converted;
// NULL, None and every other type (tuples, lists, dicts, subclasses) are null.
// UTF-8 copies of unicode arguments stay referenced in keepAlive until the call
// returns; the original leaks them.
template <class Adapter>
void toGFx(PyObject* object, typename Adapter::Value& out, std::vector<PyObject*>& keepAlive)
{
    if (object == NULL)
    {
        Adapter::setNull(out);
        return;
    }
    PyTypeObject* type = Py_TYPE(object);
    if (type == &PyUnicode_Type)
    {
        PyObject* utf8 = PyUnicode_AsUTF8String(object);
        if (utf8 == NULL)
        {
            Adapter::setNull(out);
            return;
        }
        keepAlive.push_back(utf8);
        Adapter::setString(out, PyString_AsString(utf8));
    }
    else if (type == &PyString_Type)
        Adapter::setString(out, PyString_AsString(object));
    else if (type == &PyInt_Type)
        Adapter::setNumber(out, static_cast<double>(PyInt_AsLong(object)));
    else if (type == &PyLong_Type)
        Adapter::setNumber(out, PyLong_AsDouble(object));
    else if (type == &PyFloat_Type)
        Adapter::setNumber(out, PyFloat_AsDouble(object));
    else if (type == &PyBool_Type)
        Adapter::setBoolean(out, object == Py_True);
    else
        Adapter::setNull(out);
}

// #1513 00aaf450 inside 013ed040: the CPython result of a NULL return without
// an error is SystemError("error return without exception set").
template <class Adapter>
PyObject* resultToPython(const typename Adapter::Value& value)
{
    PyObject* result = Adapter::toPython(value);
    if (result == NULL && !PyErr_Occurred())
        PyErr_SetString(PyExc_SystemError, "error return without exception set");
    return result;
}

struct KeepAlive
{
    std::vector<PyObject*> objects;
    ~KeepAlive()
    {
        for (std::size_t i = 0; i < objects.size(); ++i)
            Py_DECREF(objects[i]);
    }
};

// #1513 013ed040 "invokePy2As". signature is a method name, or a tuple
// (name, a1, a2, ...); a pair whose second item is exactly a list expands it.
template <class Adapter>
PyObject* invokePy2As(typename Adapter::Value& target, PyObject* signature)
{
    typedef typename Adapter::Value Value;
    Py_ssize_t count = -1;
    const char* name = NULL;
    if (PyTuple_Check(signature))
    {
        count = PyTuple_Size(signature);
        if (count == 0)
        {
            PyErr_SetString(PyExc_Exception, "invokePy2As expected at least 1 argument.");
            return NULL;
        }
        PyObject* first = PyTuple_GetItem(signature, 0);
        if (Py_TYPE(first) != &PyString_Type)
        {
            PyErr_SetString(PyExc_Exception, "invokePy2As - The first argument must be a String.");
            return NULL;
        }
        name = PyString_AsString(first);
    }
    else
    {
        // A non-string signature leaves PyString_AsString's TypeError; the
        // original then invokes a NULL name.
        name = PyString_AsString(signature);
        if (name == NULL)
            return NULL;
    }

    KeepAlive keepAlive;
    std::vector<Value> arguments;
    PyObject* list = count == 2 ? PyTuple_GetItem(signature, 1) : NULL;
    if (list != NULL && Py_TYPE(list) == &PyList_Type)
    {
        const Py_ssize_t size = PyList_Size(list);
        if (size < 1)
        {
            PyErr_SetString(PyExc_Exception,
                "invokePy2As - Failed to invoke method. Bad argument count.");
            return NULL;
        }
        arguments.resize(static_cast<std::size_t>(size));
        for (Py_ssize_t i = 0; i < size; ++i)
            toGFx<Adapter>(PyList_GetItem(list, i), arguments[static_cast<std::size_t>(i)],
                keepAlive.objects);
    }
    else if (count > 1)
    {
        arguments.resize(static_cast<std::size_t>(count - 1));
        for (Py_ssize_t i = 1; i < count; ++i)
            toGFx<Adapter>(PyTuple_GetItem(signature, i), arguments[static_cast<std::size_t>(i - 1)],
                keepAlive.objects);
    }
    // A failed long/unicode conversion is raised before the call rather than
    // left pending behind a successful result.
    if (PyErr_Occurred())
        return NULL;

    Value result;
    if (!Adapter::invoke(target, name, arguments.empty() ? NULL : &arguments[0],
            arguments.size(), result))
    {
        PyErr_SetString(PyExc_Exception, "invokePy2As - Failed to invoke method.");
        return NULL;
    }
    return resultToPython<Adapter>(result);
}
}
}
