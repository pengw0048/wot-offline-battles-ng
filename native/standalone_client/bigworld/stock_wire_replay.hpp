#pragma once
// Native offline replay writer (background thread, gzip) and reader.
#include <Python.h>

#include <memory>
#include <string>

namespace stock_wire_replay {
class Writer;
// The writer behind a replay_writer capsule, shared so the receive thread may append to it.
std::shared_ptr<Writer> writer(PyObject* capsule);
// Append one received wire frame at a replay time; false once the writer stopped.
bool record_frame(Writer& writer, const std::string& frame, double t);
PyMethodDef* methods();
}
