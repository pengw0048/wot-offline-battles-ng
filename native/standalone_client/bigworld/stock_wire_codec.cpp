#include "stock_wire_codec.hpp"
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <vector>

namespace stock_wire_schema {
enum Kind : unsigned char {
    U8, U16, U32, U64, I8, I16, I32, I64, F32, F64, BOOL, STR, LIST, MAP, TUPLE, ENUM, ANY, REF,
    ONEOF, VARIANT
};
struct Type { unsigned char kind; uint32_t a, b, c; const char* key; };
struct EnumValue { unsigned char kind; long long integer; const char* text; };
struct FieldDesc { const char* name; uint32_t type; unsigned char flags; };
struct RecordDesc { const char* name; uint32_t first, count, flagged; };
struct VariantEntry { const char* tag; uint32_t record; };
struct MessageDesc { const char* name; unsigned char layout; uint32_t a, b; };
#include "stock_wire_schema.inc"
}

namespace stock_wire {
namespace {
using namespace stock_wire_schema;
struct Failure : std::runtime_error { using std::runtime_error::runtime_error; };
enum AnyTag : unsigned char { A_NULL, A_FALSE, A_TRUE, A_INT, A_FLOAT, A_STR, A_LIST, A_MAP, A_UINT };

struct Ref {
    PyObject* p;
    explicit Ref(PyObject* value = nullptr) : p(value) {}
    ~Ref() { Py_XDECREF(p); }
    Ref(const Ref&) = delete;
    Ref& operator=(const Ref&) = delete;
    PyObject* release() { PyObject* value = p; p = nullptr; return value; }
};

PyObject* checked(PyObject* value) { if (!value) throw Failure("python allocation failed"); return value; }

// ---- decoding -------------------------------------------------------------------
struct Reader {
    const unsigned char* data; std::size_t size, at;
    void need(std::size_t n) { if (size - at < n) throw Failure("truncated frame"); }
    template<class T> T read() { need(sizeof(T)); T value; std::memcpy(&value, data + at, sizeof(T)); at += sizeof(T); return value; }
};

PyObject* integer(long long value) {
    if (value >= LONG_MIN && value <= LONG_MAX) return checked(PyInt_FromLong(static_cast<long>(value)));
    return checked(PyLong_FromLongLong(value));
}
PyObject* text(const char* data, std::size_t size) { return checked(PyUnicode_DecodeUTF8(data, static_cast<Py_ssize_t>(size), "strict")); }
PyObject* decodeValue(Reader& r, uint32_t type);

PyObject* decodeAny(Reader& r) {
    switch (r.read<unsigned char>()) {
    case A_NULL: Py_RETURN_NONE;
    case A_FALSE: Py_RETURN_FALSE;
    case A_TRUE: Py_RETURN_TRUE;
    case A_INT: return integer(r.read<long long>());
    case A_UINT: return checked(PyLong_FromUnsignedLongLong(r.read<unsigned long long>()));
    case A_FLOAT: return checked(PyFloat_FromDouble(r.read<double>()));
    case A_STR: { uint32_t n = r.read<uint32_t>(); r.need(n); PyObject* s = text(reinterpret_cast<const char*>(r.data + r.at), n); r.at += n; return s; }
    case A_LIST: {
        uint32_t n = r.read<uint32_t>(); Ref list(checked(PyList_New(n)));
        for (uint32_t i = 0; i < n; ++i) PyList_SET_ITEM(list.p, i, decodeAny(r));
        return list.release();
    }
    case A_MAP: {
        uint32_t n = r.read<uint32_t>(); Ref dict(checked(PyDict_New()));
        for (uint32_t i = 0; i < n; ++i) {
            uint32_t k = r.read<uint32_t>(); r.need(k);
            Ref key(text(reinterpret_cast<const char*>(r.data + r.at), k)); r.at += k;
            Ref value(decodeAny(r));
            if (PyDict_SetItem(dict.p, key.p, value.p) < 0) throw Failure("dict insert failed");
        }
        return dict.release();
    }
    }
    throw Failure("bad any tag");
}

void decodeRecord(Reader& r, uint32_t index, PyObject* dict) {
    const RecordDesc& rec = records[index];
    std::size_t bytes = (2 * rec.flagged + 7) / 8;
    r.need(bytes);
    const unsigned char* bits = r.data + r.at;
    r.at += bytes;
    uint32_t flag = 0;
    for (uint32_t f = 0; f < rec.count; ++f) {
        const FieldDesc& field = fields[rec.first + f];
        if (field.flags) {
            uint32_t bit = 2 * flag;
            bool present = (bits[bit / 8] >> (bit % 8)) & 1, null = (bits[(bit + 1) / 8] >> ((bit + 1) % 8)) & 1;
            ++flag;
            if (!present) continue;
            if (null) { if (PyDict_SetItemString(dict, field.name, Py_None) < 0) throw Failure("dict insert failed"); continue; }
        }
        Ref value(decodeValue(r, field.type));
        Ref key(text(field.name, std::strlen(field.name)));
        if (PyDict_SetItem(dict, key.p, value.p) < 0) throw Failure("dict insert failed");
    }
}

PyObject* decodeValue(Reader& r, uint32_t index) {
    const Type& t = types[index];
    switch (t.kind) {
    case U8: return integer(r.read<uint8_t>());
    case U16: return integer(r.read<uint16_t>());
    case U32: return integer(r.read<uint32_t>());
    case U64: return checked(PyLong_FromUnsignedLongLong(r.read<unsigned long long>()));
    case I8: return integer(r.read<int8_t>());
    case I16: return integer(r.read<int16_t>());
    case I32: return integer(r.read<int32_t>());
    case I64: return integer(r.read<long long>());
    case F32: return checked(PyFloat_FromDouble(r.read<float>()));
    case F64: return checked(PyFloat_FromDouble(r.read<double>()));
    case BOOL: if (r.read<unsigned char>()) Py_RETURN_TRUE; Py_RETURN_FALSE;
    case STR: { uint16_t n = r.read<uint16_t>(); r.need(n); PyObject* s = text(reinterpret_cast<const char*>(r.data + r.at), n); r.at += n; return s; }
    case LIST: case TUPLE: {
        uint32_t n = t.kind == LIST ? r.read<uint16_t>() : t.b;
        Ref list(checked(PyList_New(n)));
        for (uint32_t i = 0; i < n; ++i) PyList_SET_ITEM(list.p, i, decodeValue(r, t.kind == LIST ? t.a : children[t.a + i]));
        return list.release();
    }
    case MAP: {
        uint16_t n = r.read<uint16_t>(); Ref dict(checked(PyDict_New()));
        for (uint16_t i = 0; i < n; ++i) {
            uint16_t k = r.read<uint16_t>(); r.need(k);
            Ref key(text(reinterpret_cast<const char*>(r.data + r.at), k)); r.at += k;
            Ref value(decodeValue(r, t.a));
            if (PyDict_SetItem(dict.p, key.p, value.p) < 0) throw Failure("dict insert failed");
        }
        return dict.release();
    }
    case ENUM: {
        unsigned char i = r.read<unsigned char>();
        if (i >= t.b) throw Failure("enum index out of range");
        const EnumValue& v = enums[t.a + i];
        switch (v.kind) {
        case 0: Py_RETURN_NONE;
        case 1: Py_RETURN_FALSE;
        case 2: Py_RETURN_TRUE;
        case 3: return integer(v.integer);
        default: return text(v.text, std::strlen(v.text));
        }
    }
    case ANY: return decodeAny(r);
    case REF: { Ref dict(checked(PyDict_New())); decodeRecord(r, t.a, dict.p); return dict.release(); }
    case ONEOF: {
        unsigned char i = r.read<unsigned char>();
        if (i >= t.b) throw Failure("alternative out of range");
        return decodeValue(r, children[t.a + i]);
    }
    case VARIANT: {
        unsigned char i = r.read<unsigned char>();
        if (i >= t.b) throw Failure("variant out of range");
        const VariantEntry& entry = variants[t.a + i];
        Ref dict(checked(PyDict_New()));
        Ref tag(text(entry.tag, std::strlen(entry.tag)));
        if (PyDict_SetItemString(dict.p, t.key, tag.p) < 0) throw Failure("dict insert failed");
        decodeRecord(r, entry.record, dict.p);
        return dict.release();
    }
    }
    throw Failure("bad schema type");
}

// ---- encoding -------------------------------------------------------------------
struct Writer {
    std::string& out;
    template<class T> void put(T value) { out.append(reinterpret_cast<const char*>(&value), sizeof(T)); }
};

long long exactInteger(PyObject* value) {
    if (PyBool_Check(value) || !(PyInt_Check(value) || PyLong_Check(value))) throw Failure("expected int");
    int overflow = 0;
    long long result = PyLong_Check(value) ? PyLong_AsLongLongAndOverflow(value, &overflow) : PyInt_AS_LONG(value);
    if (overflow || (result == -1 && PyErr_Occurred())) { PyErr_Clear(); throw Failure("int out of i64 range"); }
    return result;
}
template<class T> void putInteger(Writer& w, PyObject* value) {
    long long v = exactInteger(value);
    if (v < static_cast<long long>(std::numeric_limits<T>::min()) ||
        (v > 0 && static_cast<unsigned long long>(v) > static_cast<unsigned long long>(std::numeric_limits<T>::max())))
        throw Failure("int out of range");
    w.put(static_cast<T>(v));
}
std::string utf8(PyObject* value) {
    if (PyUnicode_Check(value)) {
        Ref bytes(PyUnicode_AsUTF8String(value));
        if (!bytes.p) { PyErr_Clear(); throw Failure("bad unicode"); }
        return std::string(PyString_AS_STRING(bytes.p), PyString_GET_SIZE(bytes.p));
    }
    if (PyString_Check(value)) return std::string(PyString_AS_STRING(value), PyString_GET_SIZE(value));
    throw Failure("expected text");
}
bool sameEnum(PyObject* value, const EnumValue& e) {
    switch (e.kind) {
    case 0: return value == Py_None;
    case 1: return value == Py_False;
    case 2: return value == Py_True;
    case 3: return !PyBool_Check(value) && (PyInt_Check(value) || PyLong_Check(value)) && exactInteger(value) == e.integer;
    default: return (PyUnicode_Check(value) || PyString_Check(value)) && utf8(value) == e.text;
    }
}
void encodeValue(Writer& w, uint32_t type, PyObject* value);

void encodeAny(Writer& w, PyObject* value) {
    if (value == Py_None) { w.put<unsigned char>(A_NULL); return; }
    if (value == Py_True) { w.put<unsigned char>(A_TRUE); return; }
    if (value == Py_False) { w.put<unsigned char>(A_FALSE); return; }
    if (PyInt_Check(value) || PyLong_Check(value)) {
        int overflow = 0;
        long long v = PyLong_Check(value) ? PyLong_AsLongLongAndOverflow(value, &overflow) : PyInt_AS_LONG(value);
        if (overflow > 0) {
            unsigned long long u = PyLong_AsUnsignedLongLong(value);
            if (PyErr_Occurred()) { PyErr_Clear(); throw Failure("int exceeds u64"); }
            w.put<unsigned char>(A_UINT); w.put(u); return;
        }
        if (overflow < 0) throw Failure("int below i64");
        w.put<unsigned char>(A_INT); w.put(v); return;
    }
    if (PyFloat_Check(value)) { w.put<unsigned char>(A_FLOAT); w.put(PyFloat_AS_DOUBLE(value)); return; }
    if (PyUnicode_Check(value) || PyString_Check(value)) {
        std::string s = utf8(value); w.put<unsigned char>(A_STR); w.put(static_cast<uint32_t>(s.size())); w.out += s; return;
    }
    if (PyList_Check(value) || PyTuple_Check(value)) {
        Ref seq(PySequence_Fast(value, "sequence")); Py_ssize_t n = PySequence_Fast_GET_SIZE(seq.p);
        w.put<unsigned char>(A_LIST); w.put(static_cast<uint32_t>(n));
        for (Py_ssize_t i = 0; i < n; ++i) encodeAny(w, PySequence_Fast_GET_ITEM(seq.p, i));
        return;
    }
    if (PyDict_Check(value)) {
        w.put<unsigned char>(A_MAP); w.put(static_cast<uint32_t>(PyDict_Size(value)));
        PyObject *key, *item; Py_ssize_t pos = 0;
        while (PyDict_Next(value, &pos, &key, &item)) {
            std::string k = utf8(key); w.put(static_cast<uint32_t>(k.size())); w.out += k; encodeAny(w, item);
        }
        return;
    }
    throw Failure("value is not a wire value");
}

void encodeRecord(Writer& w, uint32_t index, PyObject* dict, const char* skip) {
    if (!PyDict_Check(dict)) throw Failure(std::string("expected object for ") + records[index].name);
    const RecordDesc& rec = records[index];
    Py_ssize_t known = 0;
    std::vector<unsigned char> bits((2 * rec.flagged + 7) / 8, 0); uint32_t flag = 0;
    for (uint32_t f = 0; f < rec.count; ++f) {
        const FieldDesc& field = fields[rec.first + f];
        PyObject* item = PyDict_GetItemString(dict, field.name);
        if (item) ++known;
        if (field.flags) {
            if (item) {
                bits[(2 * flag) / 8] |= static_cast<unsigned char>(1u << ((2 * flag) % 8));
                if (item == Py_None) {
                    if (!(field.flags & 2)) throw Failure(std::string(field.name) + " is not nullable");
                    bits[(2 * flag + 1) / 8] |= static_cast<unsigned char>(1u << ((2 * flag + 1) % 8));
                }
            } else if (!(field.flags & 1)) throw Failure(std::string(field.name) + " is required");
            ++flag;
        } else if (!item) throw Failure(std::string(field.name) + " is required");
    }
    if (skip && PyDict_GetItemString(dict, skip)) ++known;
    if (known != PyDict_Size(dict)) throw Failure(std::string(rec.name) + " has unknown keys");
    if (!bits.empty()) w.out.append(reinterpret_cast<const char*>(&bits[0]), bits.size());
    for (uint32_t f = 0; f < rec.count; ++f) {
        const FieldDesc& field = fields[rec.first + f];
        PyObject* item = PyDict_GetItemString(dict, field.name);
        if (!item || (item == Py_None && (field.flags & 2))) continue;
        encodeValue(w, field.type, item);
    }
}

void encodeValue(Writer& w, uint32_t index, PyObject* value) {
    const Type& t = types[index];
    switch (t.kind) {
    case U8: putInteger<uint8_t>(w, value); return;
    case U16: putInteger<uint16_t>(w, value); return;
    case U32: putInteger<uint32_t>(w, value); return;
    case U64: {
        if (PyBool_Check(value) || !(PyInt_Check(value) || PyLong_Check(value))) throw Failure("expected int");
        unsigned long long u = PyLong_Check(value) ? PyLong_AsUnsignedLongLong(value) : static_cast<unsigned long long>(PyInt_AS_LONG(value));
        if (PyErr_Occurred() || (PyInt_Check(value) && PyInt_AS_LONG(value) < 0)) { PyErr_Clear(); throw Failure("u64 out of range"); }
        w.put(u); return;
    }
    case I8: putInteger<int8_t>(w, value); return;
    case I16: putInteger<int16_t>(w, value); return;
    case I32: putInteger<int32_t>(w, value); return;
    case I64: w.put(exactInteger(value)); return;
    case F32: case F64: {
        if (!PyFloat_Check(value)) throw Failure("expected float");
        double v = PyFloat_AS_DOUBLE(value);
        if (t.kind == F32) { float f = static_cast<float>(v); if (f != v) throw Failure("float not exact in f32"); w.put(f); }
        else w.put(v);
        return;
    }
    case BOOL: if (!PyBool_Check(value)) throw Failure("expected bool"); w.put<unsigned char>(value == Py_True); return;
    case STR: {
        std::string s = utf8(value);
        if (s.size() > 65535) throw Failure("string too long");
        w.put(static_cast<uint16_t>(s.size())); w.out += s; return;
    }
    case LIST: case TUPLE: {
        if (!PyList_Check(value) && !PyTuple_Check(value)) throw Failure("expected list");
        Ref seq(PySequence_Fast(value, "sequence")); Py_ssize_t n = PySequence_Fast_GET_SIZE(seq.p);
        if (t.kind == LIST) { if (static_cast<uint32_t>(n) > t.b) throw Failure("list too long"); w.put(static_cast<uint16_t>(n)); }
        else if (static_cast<uint32_t>(n) != t.b) throw Failure("tuple length mismatch");
        for (Py_ssize_t i = 0; i < n; ++i) encodeValue(w, t.kind == LIST ? t.a : children[t.a + i], PySequence_Fast_GET_ITEM(seq.p, i));
        return;
    }
    case MAP: {
        if (!PyDict_Check(value) || static_cast<uint32_t>(PyDict_Size(value)) > t.b) throw Failure("expected map");
        w.put(static_cast<uint16_t>(PyDict_Size(value)));
        PyObject *key, *item; Py_ssize_t pos = 0;
        while (PyDict_Next(value, &pos, &key, &item)) {
            std::string k = utf8(key); w.put(static_cast<uint16_t>(k.size())); w.out += k; encodeValue(w, t.a, item);
        }
        return;
    }
    case ENUM:
        for (uint32_t i = 0; i < t.b; ++i) if (sameEnum(value, enums[t.a + i])) { w.put(static_cast<unsigned char>(i)); return; }
        throw Failure("value not in enum");
    case ANY: encodeAny(w, value); return;
    case REF: encodeRecord(w, t.a, value, nullptr); return;
    case ONEOF: {
        std::string message;
        for (uint32_t i = 0; i < t.b; ++i) {
            std::string trial; Writer tw{trial};
            try { encodeValue(tw, children[t.a + i], value); }
            catch (const Failure& error) { message += error.what(); message += " | "; continue; }
            w.put(static_cast<unsigned char>(i)); w.out += trial; return;
        }
        throw Failure("no alternative matched: " + message);
    }
    case VARIANT: {
        PyObject* tag = PyDict_Check(value) ? PyDict_GetItemString(value, t.key) : nullptr;
        if (!tag) throw Failure("variant tag missing");
        std::string name = utf8(tag);
        for (uint32_t i = 0; i < t.b; ++i) if (name == variants[t.a + i].tag) {
            w.put(static_cast<unsigned char>(i)); encodeRecord(w, variants[t.a + i].record, value, t.key); return;
        }
        throw Failure("unknown variant " + name);
    }
    }
    throw Failure("bad schema type");
}

bool buffer(PyObject* args, const unsigned char*& data, std::size_t& size) {
    PyObject* object; const void* raw; Py_ssize_t length;
    if (!PyArg_ParseTuple(args, "O", &object) || PyObject_AsReadBuffer(object, &raw, &length) < 0) return false;
    data = static_cast<const unsigned char*>(raw); size = static_cast<std::size_t>(length);
    return true;
}
PyObject* decodePython(PyObject*, PyObject* args) {
    const unsigned char* data; std::size_t size;
    if (!buffer(args, data, size)) return nullptr;
    return decode(data, size);
}
PyObject* encodePython(PyObject*, PyObject* message) {
    std::string out;
    if (!encode(message, out)) return nullptr;
    return PyString_FromStringAndSize(out.data(), static_cast<Py_ssize_t>(out.size()));
}
PyObject* frameSizePython(PyObject*, PyObject* args) {
    const unsigned char* data; std::size_t size;
    if (!buffer(args, data, size)) return nullptr;
    return PyInt_FromSize_t(frame_size(data, size));
}
}

std::size_t frame_size(const unsigned char* data, std::size_t size) {
    if (size < frame_header_bytes) return 0;
    uint32_t length; std::memcpy(&length, data, sizeof(length));
    return frame_header_bytes + length;
}

PyObject* decode(const unsigned char* data, std::size_t size) {
    try {
        Reader r{data, size, 0};
        uint32_t length = r.read<uint32_t>(); uint16_t id = r.read<uint16_t>(); r.read<uint16_t>();
        if (length + frame_header_bytes != size) throw Failure("frame length mismatch");
        if (id == 0 || id > message_count) throw Failure("unknown message id");
        const MessageDesc& m = messages[id - 1];
        Ref result;
        if (m.layout == 0) { result.p = decodeAny(r); if (!PyDict_Check(result.p)) throw Failure("control body is not a dict"); }
        else {
            result.p = checked(PyDict_New());
            uint32_t record = m.a;
            if (m.layout == 2) { unsigned char i = r.read<unsigned char>(); if (i >= m.b) throw Failure("layout out of range"); record = types[children[m.a + i]].a; }
            decodeRecord(r, record, result.p);
        }
        Ref type(text(m.name, std::strlen(m.name)));
        if (PyDict_SetItemString(result.p, "type", type.p) < 0) throw Failure("dict insert failed");
        if (r.at != size) throw Failure("trailing bytes");
        return result.release();
    } catch (const Failure& error) {
        if (!PyErr_Occurred()) PyErr_SetString(PyExc_ValueError, error.what());
        return nullptr;
    }
}

bool encode(PyObject* message, std::string& out) {
    try {
        if (!PyDict_Check(message)) throw Failure("message is not a dict");
        PyObject* type = PyDict_GetItemString(message, "type");
        if (!type) throw Failure("message has no type");
        std::string name = utf8(type);
        uint32_t id = 0;
        for (uint32_t i = 0; i < message_count; ++i) if (name == messages[i].name) { id = i + 1; break; }
        if (!id) throw Failure("message type " + name + " is not in the schema");
        const MessageDesc& m = messages[id - 1];
        out.assign(frame_header_bytes, '\0');
        Writer w{out};
        if (m.layout == 0) {
            Ref body(checked(PyDict_Copy(message)));
            PyDict_DelItemString(body.p, "type");
            encodeAny(w, body.p);
        } else if (m.layout == 1) encodeRecord(w, m.a, message, "type");
        else {
            std::string reasons;
            bool done = false;
            for (uint32_t i = 0; i < m.b && !done; ++i) {
                std::string trial(1, static_cast<char>(i)); Writer tw{trial};
                try { encodeRecord(tw, types[children[m.a + i]].a, message, "type"); }
                catch (const Failure& error) { reasons += error.what(); reasons += " | "; continue; }
                out += trial; done = true;
            }
            if (!done) throw Failure("no " + name + " layout matched: " + reasons);
        }
        uint32_t length = static_cast<uint32_t>(out.size() - frame_header_bytes);
        uint16_t mid = static_cast<uint16_t>(id), flags = 0;
        std::memcpy(&out[0], &length, 4); std::memcpy(&out[4], &mid, 2); std::memcpy(&out[6], &flags, 2);
        return true;
    } catch (const Failure& error) {
        if (!PyErr_Occurred()) PyErr_SetString(PyExc_ValueError, error.what());
        return false;
    }
}

PyMethodDef* methods() {
    static PyMethodDef table[] = {
        {"decode", decodePython, METH_VARARGS, "Decode one binary battle frame."},
        {"encode", encodePython, METH_O, "Encode one battle message dict as a frame."},
        {"frame_size", frameSizePython, METH_VARARGS, "Frame size from its header, 0 if incomplete."},
        {nullptr, nullptr, 0, nullptr}};
    return table;
}

}
