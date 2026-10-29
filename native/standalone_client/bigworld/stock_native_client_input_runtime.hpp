#pragma once
#include <Python.h>
#include <optional>
namespace stock_native_client_input {
// Final checkpoint immediately before every admitted movement or fire intent.
// Native time and current shared driver/gun owners determine the complete DTO.
bool sendCurrent(PyObject* battle,std::optional<bool> siege={});
bool sendCurrentAt(PyObject* battle,double now,std::optional<bool> siege={});
// Preserve committed world-contact barriers before any later pose input.
bool flushBarrierInputsAt(PyObject* battle,double now);
// Admit at most one landing after its current-pose input has been queued.
bool flushLandingAt(PyObject* battle,double now);
PyObject* sendCurrentPython(PyObject*,PyObject*);
}
