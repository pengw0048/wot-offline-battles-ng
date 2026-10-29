#pragma once
#include <Python.h>
namespace stock_native_client_input {
// The queue retains the existing transport thread and generation. Only its
// synchronization wake-up is a Python boundary; validation and freezing are not.
PyObject* configure(PyObject*,PyObject*);
PyObject* sendInput(PyObject*,PyObject*,PyObject*);
PyObject* send(PyObject*,PyObject*);
PyObject* freeze(PyObject*,PyObject*);
PyMethodDef* methods();
bool enqueue(PyObject*client,PyObject*message);
}
