#pragma once
#include <Python.h>
namespace stock_native_client_transport {
PyObject* configure(PyObject*,PyObject*);
PyObject* decode(PyObject*,PyObject*);
PyObject* materialize(PyObject*,PyObject*);
PyObject* canonicalRows(PyObject*,PyObject*);
PyObject* canonicalRow(PyObject*,PyObject*);
PyObject* decodeEquipment(PyObject*,PyObject*);
PyMethodDef* methods();
void initialize(PyObject*);
}
