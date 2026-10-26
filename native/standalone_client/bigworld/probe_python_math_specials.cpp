// Actual embedded CPython math/cmath calls on bit-exact inputs; no oracle here.
#include <Python.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdint.h>

static double word(const char *text)
{
    const uint64_t bits = std::strtoull(text, NULL, 16);
    double value;
    std::memcpy(&value, &bits, 8);
    return value;
}

static unsigned long long bits(double value)
{
    uint64_t result;
    std::memcpy(&result, &value, 8);
    return (unsigned long long)result;
}

// Builds the exact Python argument tuple described by one case line.
static PyObject *arguments(const char *kind, char **tokens, int count)
{
    if (!std::strcmp(kind, "f1") && count == 1)
        return Py_BuildValue("(N)", PyFloat_FromDouble(word(tokens[0])));
    if (!std::strcmp(kind, "f2") && count == 2)
        return Py_BuildValue("(NN)", PyFloat_FromDouble(word(tokens[0])),
                             PyFloat_FromDouble(word(tokens[1])));
    if (!std::strcmp(kind, "c1") && count == 2)
        return Py_BuildValue("(N)", PyComplex_FromDoubles(word(tokens[0]), word(tokens[1])));
    if (!std::strcmp(kind, "fi") && count == 2)
        return Py_BuildValue("(NN)", PyFloat_FromDouble(word(tokens[0])),
                             PyLong_FromString(tokens[1], NULL, 10));
    if (!std::strcmp(kind, "fs") && count == 2) {
        // Parser path: float(text) must agree with the bit-built argument.
        PyObject *text = PyString_FromString(tokens[1]);
        PyObject *value = text ? PyFloat_FromString(text, NULL) : NULL;
        Py_XDECREF(text);
        if (!value || bits(PyFloat_AsDouble(value)) != bits(word(tokens[0]))) {
            Py_XDECREF(value);
            return NULL;
        }
        return Py_BuildValue("(N)", value);
    }
    return NULL;
}

static void outcome(PyObject *result, FILE *out)
{
    if (!result) {
        PyObject *type, *value, *trace;
        PyErr_Fetch(&type, &value, &trace);
        PyErr_NormalizeException(&type, &value, &trace);
        PyObject *text = value ? PyObject_Str(value) : NULL;
        std::fprintf(out, "E:%s:%s\n", ((PyTypeObject *)type)->tp_name,
                     text ? PyString_AsString(text) : "");
        Py_XDECREF(text); Py_XDECREF(type); Py_XDECREF(value); Py_XDECREF(trace);
    } else if (PyFloat_CheckExact(result)) {
        std::fprintf(out, "F:%016llx\n", bits(PyFloat_AS_DOUBLE(result)));
    } else if (PyComplex_CheckExact(result)) {
        Py_complex z = PyComplex_AsCComplex(result);
        std::fprintf(out, "C:%016llx,%016llx\n", bits(z.real), bits(z.imag));
    } else if (PyTuple_CheckExact(result) && PyTuple_GET_SIZE(result) == 2) {
        std::fprintf(out, "T:%016llx,%016llx\n",
                     bits(PyFloat_AsDouble(PyTuple_GET_ITEM(result, 0))),
                     bits(PyFloat_AsDouble(PyTuple_GET_ITEM(result, 1))));
    } else {
        std::fprintf(out, "X:unexpected result type\n");
    }
}

int main(int argc, char **argv)
{
    if (argc != 4) return 2;
    FILE *in = std::fopen(argv[2], "r");
    FILE *out = std::fopen(argv[3], "w");
    if (!in || !out) return 3;
    Py_NoSiteFlag = 1; Py_DontWriteBytecodeFlag = 1;
    Py_SetPythonHome(argv[1]); Py_Initialize();
    if (std::strncmp(Py_GetVersion(), "2.7.7", 5)) return 4;
    char line[512];
    unsigned int executed = 0;
    while (std::fgets(line, sizeof(line), in)) {
        char *tokens[8]; int count = 0;
        for (char *t = std::strtok(line, " \r\n"); t && count < 8; t = std::strtok(NULL, " \r\n"))
            tokens[count++] = t;
        if (count < 5) return 5;
        PyObject *module = PyImport_ImportModule(tokens[1]);
        PyObject *function = module ? PyObject_GetAttrString(module, tokens[2]) : NULL;
        PyObject *args = arguments(tokens[3], tokens + 4, count - 4);
        if (!function || !args) {
            std::fprintf(stderr, "Case %s could not be constructed\n", tokens[0]);
            return 6;
        }
        std::fprintf(out, "%s ", tokens[0]);
        PyObject *result = PyObject_CallObject(function, args);
        outcome(result, out);
        Py_XDECREF(result); Py_DECREF(args); Py_DECREF(function); Py_DECREF(module);
        ++executed;
    }
    Py_Finalize();
    std::fclose(out);
    std::printf("Actual embedded 2.7.7 math/cmath probe executed %u cases\n", executed);
    return 0;
}
