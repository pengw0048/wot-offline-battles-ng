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
namespace BW { template<class T> using vector = std::vector<T>; }
static unsigned invoked = 0;
namespace GFx {
struct Value {
    enum Kind { Undefined, Null, Boolean, Number, String, Array, Object };
    enum ValueType { VT_Object, VT_Closure };
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
    bool SetMember(const char * key, const Value & v) {
        if (!key) return false;
        (*members)[key] = v;
        return true;
    }
    ValueType GetType() const { return VT_Object; }
    bool Invoke(const char *, Value * result, const Value *, uint) {
        ++invoked; result->SetNull(); return true;
    }
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
    bool Invoke(const char *, Value * result, const Value *, uint) {
        ++invoked; result->SetNull(); return true;
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
    PyMovieView * pyMovieView_ = nullptr;
    std::shared_ptr<PyGFxValue> parent_;
    std::string name_ = "as_setSettings";
    using Children_t = std::map<std::string, std::shared_ptr<PyGFxValue>>;
    Children_t children_;
    bool needResyncSelf_ = false;
    static GFx::Value convertValueToGFx(PyObject *, PyMovieView *);
    static PyObject * convertValueToPython(GFx::Value &, PyMovieView *) { Py_RETURN_NONE; }
    PyObject * pyCall(PyObject *);
};
PyTypeObject PyGFxValue::s_type_ = { PyObject_HEAD_INIT(NULL) 0 };
@CONVERTER@
@PYCALL@

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
static PyObject * invoke(PyObject *, PyObject * args) {
    PyMovieView movie;
    PyGFxValue value;
    value.pyMovieView_ = &movie;
    value.parent_.reset(new PyGFxValue);
    return value.pyCall(args);
}
static PyObject * invokeCount(PyObject *, PyObject *) { return PyInt_FromLong(invoked); }
static PyMethodDef methods[] = {
    { "convert", convert, METH_O, "Execute the SDK conversion body." },
    { "invoke", invoke, METH_VARARGS, "Execute the SDK ActionScript call owner." },
    { "invokeCount", invokeCount, METH_NOARGS, "Read the ActionScript invocation receipt." },
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
