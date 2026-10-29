#pragma once
#include "stock_native_client_weapon.hpp"
#include <Python.h>
namespace stock_native_client_weapon {
Gun* gun(PyObject*);
PyObject* create(PyObject*,PyObject*,PyObject*);
PyMethodDef* methods();
bool initialize(PyObject*);
}
