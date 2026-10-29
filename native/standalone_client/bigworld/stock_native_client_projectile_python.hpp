#pragma once
#include <Python.h>
namespace stock_native_client_projectile {
PyObject* create(PyObject*,PyObject*);
PyObject* ingress(PyObject*,PyObject*);
PyObject* event(PyObject*,PyObject*);
PyObject* trigger(PyObject*,PyObject*);
PyObject* advance(PyObject*,PyObject*);
PyObject* close(PyObject*,PyObject*);
PyObject* metrics(PyObject*,PyObject*);
PyMethodDef* methods();
}
