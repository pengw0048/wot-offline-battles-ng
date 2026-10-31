#pragma once
// Table-driven binary battle wire codec; tables come from wire/gen_cpp.py.
#include <Python.h>
#include <cstddef>
#include <cstdint>
#include <string>

namespace stock_native_client_state { struct Value; }

namespace stock_wire {

const std::size_t frame_header_bytes = 8;

// Python objects equal to the former json.loads result (unicode text, int/long, float).
PyObject* decode(const unsigned char* data, std::size_t size);
// Encode one message dict; sets a Python ValueError and returns false on schema mismatch.
bool encode(PyObject* message, std::string& out);
// Tagged binary tree for open values (replay records); same contract as decode/encode.
PyObject* decode_any(const unsigned char* data, std::size_t size);
bool encode_any(PyObject* value, std::string& out);
// Total frame size from the 8-byte header, or 0 when the header is incomplete.
std::size_t frame_size(const unsigned char* data, std::size_t size);
// Decode one frame into owned native values without the GIL; false with a reason on failure.
bool decode_value(const unsigned char* data, std::size_t size, stock_native_client_state::Value& out,
                  std::string& error);
// Encode one owned message (with its "type") as a frame; same contract as encode, false with a reason.
bool encode_value(const stock_native_client_state::Value& message, std::string& out, std::string& error);
// Message id for a schema message name (0 when absent), and the id in a frame header.
uint16_t message_id(const char* name);
uint16_t frame_message(const unsigned char* data, std::size_t size);
PyMethodDef* methods();

}
