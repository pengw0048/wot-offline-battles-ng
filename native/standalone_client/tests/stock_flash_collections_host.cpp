// The real SDK conversion body runs against CPython 2.7; GFx allocation is a receipt boundary.
#include <Python.h>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <cstdio>
#include <cassert>
#include <climits>
#include <cstring>
#include <cwchar>
#define BW_GUARD
#define MF_ASSERT assert
using uint = unsigned int;
namespace GFx {
struct Value {
    enum Kind { Undefined, Null, Boolean, Number, String, Array, Object };
    Kind kind = Undefined;
    double number = 0;
    std::string text;
    std::shared_ptr<std::vector<Value> > elements;
    std::shared_ptr<std::map<std::string, Value> > members;
    void SetUndefined() { kind = Undefined; }
    void SetNull() { kind = Null; }
    void SetString(const char * s) { kind = String; text = s; }
    void SetInt(long v) { kind = Number; number = v; }
    void SetNumber(double v) { kind = Number; number = v; }
    void SetBoolean(bool v) { kind = Boolean; number = v; }
    void SetElement(uint i, const Value & v) {
        if (elements->size() <= i) elements->resize(i + 1);
        (*elements)[i] = v;
    }
    void SetMember(const char * key, const Value & v) { (*members)[key] = v; }
};
struct Movie {
    void CreateArray(Value * value) {
        value->kind = Value::Array;
        value->elements.reset(new std::vector<Value>());
    }
    void CreateObject(Value * value) {
        value->kind = Value::Object;
        value->members.reset(new std::map<std::string, Value>());
    }
};
}
struct PyMovieView {
    GFx::Movie movie;
    GFx::Movie * pMovieView() { return &movie; }
};
struct PyGFxValue {
    static PyTypeObject s_type_;
    GFx::Value gfxValue_;
    static GFx::Value convertValueToGFx(PyObject *, PyMovieView *);
};
PyTypeObject PyGFxValue::s_type_ = { PyObject_HEAD_INIT(NULL) 0 };
@CONVERTER@

static PyObject * receipt(const GFx::Value & value) {
    if (value.kind == GFx::Value::Undefined) return Py_BuildValue("(s)", "undefined");
    if (value.kind == GFx::Value::Null) return Py_BuildValue("(s)", "null");
    if (value.kind == GFx::Value::String) return Py_BuildValue("(ss)", "string", value.text.c_str());
    if (value.kind == GFx::Value::Boolean) return Py_BuildValue("(si)", "bool", int(value.number));
    if (value.kind == GFx::Value::Number) return Py_BuildValue("(sd)", "number", value.number);
    PyObject * data;
    if (value.kind == GFx::Value::Array) {
        data = PyList_New(value.elements->size());
        for (size_t i = 0; i < value.elements->size(); ++i)
            PyList_SET_ITEM(data, i, receipt((*value.elements)[i]));
    } else {
        data = PyDict_New();
        for (const auto & item : *value.members) {
            PyObject * child = receipt(item.second);
            PyDict_SetItemString(data, item.first.c_str(), child);
            Py_DECREF(child);
        }
    }
    PyObject * result = Py_BuildValue("(sO)", value.kind == GFx::Value::Array ? "array" : "object", data);
    Py_DECREF(data);
    return result;
}
static PyObject * convert(PyObject *, PyObject * object) {
    PyMovieView movie;
    GFx::Value value = PyGFxValue::convertValueToGFx(object, &movie);
    if (PyErr_Occurred()) return NULL;
    return receipt(value);
}
static PyMethodDef methods[] = {
    { "convert", convert, METH_O, "Execute the SDK conversion body." },
    { NULL, NULL, 0, NULL }
};
int main(int argc, char ** argv) {
    Py_Initialize();
    PySys_SetArgv(argc - 1, argv + 1);
    Py_InitModule("_flash_collections", methods);
    FILE * script = fopen(argv[1], "r");
    if (!script) return 2;
    int result = PyRun_SimpleFile(script, argv[1]);
    fclose(script);
    Py_Finalize();
    return result == 0 ? 0 : 1;
}
