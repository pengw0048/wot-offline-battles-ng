#pragma once
// Table-driven binary battle wire codec; tables come from wire/gen_cpp.py.
#include <Python.h>
#include <cstddef>
#include <cstdint>
#include <string>

namespace stock_wire {

const std::size_t frame_header_bytes = 8;

// Python objects equal to the former json.loads result (unicode text, int/long, float).
PyObject* decode(const unsigned char* data, std::size_t size);
// Encode one message dict; sets a Python ValueError and returns false on schema mismatch.
bool encode(PyObject* message, std::string& out);
// Total frame size from the 8-byte header, or 0 when the header is incomplete.
std::size_t frame_size(const unsigned char* data, std::size_t size);
PyMethodDef* methods();

}
