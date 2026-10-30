#pragma once
// Native offline replay writer (background thread, gzip) and reader.
#include <Python.h>

namespace stock_wire_replay {
PyMethodDef* methods();
}
