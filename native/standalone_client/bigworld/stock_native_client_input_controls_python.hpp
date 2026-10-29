#pragma once
#include <Python.h>
namespace stock_native_client_input {
PyObject* control(PyObject*,PyObject*);
PyObject* alignControl(PyObject*,PyObject*);
PyObject* assignControl(PyObject*,PyObject*);
void mirrorControls(PyObject* battle);
void echoGun(PyObject* battle,double turret,double pitch);
// #1513 getAutorotation returns this handler-owned private boolean directly.
// The stock control-mode/settings edges remain its sole producer.
bool autorotationEnabled(PyObject* battle);
}
