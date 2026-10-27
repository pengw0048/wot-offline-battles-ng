// Real CPython 2.7 runs the engine's PY_AUTO macros, PY_AUTO_METHOD_DECLARE,
// the public Script::setData(bool/int64/float/double/BW::string) bodies and
// Script::argCountError, and the production BigWorld.Screener binding.
// PyObjectPlus, its type macros and PyModuleResultLink are fixtures.
#include <Python.h>
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <strings.h>
#include <vector>

#define BW_BEGIN_NAMESPACE namespace BW {
#define BW_END_NAMESPACE }
#define BW_STATIC_ASSERT(expression, message) static_assert(expression, #message)
#define _stricmp strcasecmp

typedef long long int64;
typedef unsigned long long uint64;

namespace BW {
typedef std::string string;

class PyObjectPtr
{
public:
	enum StealReference { STEAL_REFERENCE };
	PyObjectPtr() : object_(NULL) {}
	PyObjectPtr(PyObject * object, StealReference) : object_(object) {}
	PyObjectPtr & operator=(const PyObjectPtr & other)
	{
		Py_XINCREF(other.object_);
		Py_XDECREF(object_);
		object_ = other.object_;
		return *this;
	}
	~PyObjectPtr() { Py_XDECREF(object_); }
	PyObject * get() const { return object_; }
private:
	PyObject * object_;
};

// PyModuleResultLink (#1513 0068dca0): the expression's result becomes MODULE.NAME at script init.
struct HostResultLink { const char * module; const char * name; PyObject * (*function)(); };
std::vector<HostResultLink> & hostResultLinks() { static std::vector<HostResultLink> links; return links; }
struct PyModuleResultLink
{
	PyModuleResultLink(const char * module, const char * name, PyObject * (*function)())
	{
		hostResultLinks().push_back(HostResultLink{module, name, function});
	}
};
struct PyModuleMethodLink
{
	PyModuleMethodLink(const char *, const char *, PyCFunction, const char *) {}
};

namespace Script {
template <class T> struct IsValidArgType { static const bool isValid = true; };
int setData(PyObject * pObject, bool & rBool, const char * varName = "");
int setData(PyObject * pObject, int64 & rInt, const char * varName = "");
int setData(PyObject * pObject, float & rFloat, const char * varName = "");
int setData(PyObject * pObject, double & rDouble, const char * varName = "");
int setData(PyObject * pObject, BW::string & rString, const char * varName = "");
PyObject * argCountError(const char * fn, int optas, int allas, ...);
}

@SCRIPT_BODIES@

class PyObjectPlus : public PyObject
{
public:
	explicit PyObjectPlus(PyTypeObject * type) { PyObject_Init(this, type); }
	virtual ~PyObjectPlus() {}
	static void * operator new(std::size_t size) { return PyObject_Malloc(size); }
	static void operator delete(void * p) { PyObject_Free(p); }
};

int hostDeallocations = 0;
template <class T> PyTypeObject hostType(const char * name)
{
	PyTypeObject type = { PyVarObject_HEAD_INIT(&PyType_Type, 0) };
	type.tp_name = name;
	type.tp_basicsize = sizeof(T);
	type.tp_dealloc = [](PyObject * o) { ++hostDeallocations; delete static_cast<T *>(static_cast<PyObjectPlus *>(o)); };
	type.tp_getattro = PyObject_GenericGetAttr;
	type.tp_flags = Py_TPFLAGS_DEFAULT;
	type.tp_methods = T::s_getMethodDefs();
	return type;
}
}

@PYAUTO_MACROS@

@PY_AUTO_METHOD_DECLARE@

#define Py_Header(CLASS, SUPER) \
	public: \
		typedef CLASS This; typedef SUPER Super; \
		static PyTypeObject s_type_; \
		static PyMethodDef * s_getMethodDefs(); \
		static This * getSelf(PyObject * o) { return o ? static_cast<This *>(static_cast<PyObjectPlus *>(o)) : NULL; } \
	private:
#define PY_TYPEOBJECT(CLASS) PyTypeObject CLASS::s_type_ = BW::hostType<CLASS>(#CLASS);
#define PY_BEGIN_METHODS(CLASS) \
	PyMethodDef * CLASS::s_getMethodDefs() { typedef CLASS This; static std::vector<PyMethodDef> defs; if (defs.empty()) {
// Public PY_METHOD and #1513 01a5c200: METH_VARARGS | METH_KEYWORDS.
#define PY_METHOD(NAME) defs.push_back(PyMethodDef{#NAME, (PyCFunction)&This::_py_##NAME, METH_VARARGS | METH_KEYWORDS, NULL});
#define PY_END_METHODS() defs.push_back(PyMethodDef{NULL, NULL, 0, NULL}); } return defs.data(); }
#define PY_BEGIN_ATTRIBUTES(CLASS)
#define PY_END_ATTRIBUTES()

#include "stock_screener.cpp"

static PyObject * hostArgCountError(PyObject *, PyObject * args)
{
	const char * fn; int optas, allas; const char * types[3] = {"", "", ""};
	if (!PyArg_ParseTuple(args, "sii|sss", &fn, &optas, &allas, &types[0], &types[1], &types[2]))
		return NULL;
	return BW::Script::argCountError(fn, optas, allas, types[0], types[1], types[2]);
}

static PyObject * hostState(PyObject *, PyObject *)
{
	return Py_BuildValue("{s:i,s:i}", "deallocations", BW::hostDeallocations,
		"basicsize", int(BW::PyWatermarkersWrapper::s_type_.tp_basicsize));
}

// Runs every recorded module result link, as Script::init does.
static PyObject * hostInitScripts(PyObject *, PyObject *)
{
	for (const BW::HostResultLink & link : BW::hostResultLinks())
	{
		PyObject * object = link.function();
		if (object == NULL) return NULL;
		PyObject * module = PyImport_AddModule(link.module);
		int failed = module == NULL || PyObject_SetAttrString(module, link.name, object) != 0;
		Py_DECREF(object);
		if (failed) return NULL;
	}
	return PyInt_FromSize_t(BW::hostResultLinks().size());
}

static PyMethodDef hostMethods[] = {
	{"argCountError", hostArgCountError, METH_VARARGS, NULL},
	{"state", hostState, METH_NOARGS, NULL},
	{"initScripts", hostInitScripts, METH_NOARGS, NULL},
	{NULL, NULL, 0, NULL},
};

int main(int argc, char ** argv)
{
	if (argc < 2) return 2;
	Py_Initialize();
	PySys_SetArgv(argc - 1, argv + 1);
	Py_InitModule("_screener_host", hostMethods);
	FILE * script = std::fopen(argv[1], "r");
	if (!script) return 2;
	const int failed = PyRun_SimpleFileEx(script, argv[1], 1);
	Py_Finalize();
	return failed ? 1 : 0;
}
