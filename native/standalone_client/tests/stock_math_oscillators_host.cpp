// Real CPython 2.7 runs the engine's PY_AUTO macros, its factory, method and
// accessor-attribute macros, the public Script::setData(float) and
// Script::argCountError bodies, and the production Math oscillator bindings.
// PyObjectPlus, Vector3 and its Script conversion are fixtures; Vector3
// conversion keeps the engine's "Vector3 or a tuple of 3 floats" contract.
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
template <class T> using vector = std::vector<T>;

class PyObjectPtr
{
public:
	enum StealReference { STEAL_REFERENCE };
	PyObjectPtr() : object_(NULL) {}
	PyObjectPtr(PyObject * object, StealReference) : object_(object) {}
	~PyObjectPtr() { Py_XDECREF(object_); }
	PyObject * get() const { return object_; }
private:
	PyObject * object_;
};

struct Vector3
{
	float x, y, z;
	Vector3() : x(0.f), y(0.f), z(0.f) {}
	Vector3(float a, float b, float c) : x(a), y(b), z(c) {}
};

// PyFactoryMethodLink: Script::init publishes each type as MODULE.NAME.
struct HostFactoryLink { const char * module; const char * name; PyTypeObject * type; };
std::vector<HostFactoryLink> & hostFactoryLinks() { static std::vector<HostFactoryLink> links; return links; }
struct PyFactoryMethodLink
{
	PyFactoryMethodLink(const char * module, const char * name, PyTypeObject * type)
	{
		hostFactoryLinks().push_back(HostFactoryLink{module, name, type});
	}
};

namespace Script {
template <class T> struct IsValidArgType { static const bool isValid = true; };
int setData(PyObject * pObject, float & rFloat, const char * varName = "");
int setData(PyObject * pObject, double & rDouble, const char * varName = "");
int setData(PyObject * pObject, Vector3 & rVector, const char * varName = "");
PyObject * getData(const Vector3 & data);
PyObject * argCountError(const char * fn, int optas, int allas, ...);
}

@SCRIPT_BODIES@

int hostSetDataCalls = 0;
PyObject * hostVectorType = NULL;

// Engine Script::setData(Vector3): a Vector3 or a tuple of 3 floats.
int Script::setData(PyObject * pObject, Vector3 & rVector, const char * varName)
{
	++hostSetDataCalls;
	if (hostVectorType && PyObject_IsInstance(pObject, hostVectorType) == 1)
	{
		PyObject * tuple = PyObject_CallMethod(pObject, (char *)"tuple", NULL);
		if (tuple && PyArg_ParseTuple(tuple, "fff", &rVector.x, &rVector.y, &rVector.z))
		{
			Py_DECREF(tuple);
			return 0;
		}
		Py_XDECREF(tuple);
	}
	PyErr_Clear();
	if (PyArg_ParseTuple(pObject, "fff", &rVector.x, &rVector.y, &rVector.z))
		return 0;
	PyErr_Format(PyExc_TypeError, "%s must be set to a Vector3 or a tuple of 3 floats", varName);
	return -1;
}

// Engine Script::getData(Vector3) returns a new Vector3 copy.
PyObject * Script::getData(const Vector3 & data)
{
	return PyObject_CallFunction(hostVectorType, (char *)"fff", data.x, data.y, data.z);
}

class PyObjectPlus : public PyObject
{
public:
	explicit PyObjectPlus(PyTypeObject * type) { PyObject_Init(this, type); }
	virtual ~PyObjectPlus() {}
	static void * operator new(std::size_t size) { return PyObject_Malloc(size); }
	static void operator delete(void * p) { PyObject_Free(p); }
	static PyTypeObject s_type_;
};
PyTypeObject PyObjectPlus::s_type_ = { PyVarObject_HEAD_INIT(&PyType_Type, 0) "PyObjectPlus" };

int hostDeallocations = 0;
template <class T> PyTypeObject hostType(const char * name, PyTypeObject * base)
{
	PyTypeObject type = { PyVarObject_HEAD_INIT(&PyType_Type, 0) };
	type.tp_name = name;
	type.tp_basicsize = sizeof(T);
	type.tp_dealloc = [](PyObject * o) { ++hostDeallocations; delete static_cast<T *>(static_cast<PyObjectPlus *>(o)); };
	type.tp_getattro = PyObject_GenericGetAttr;
	type.tp_setattro = PyObject_GenericSetAttr;
	type.tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE;
	type.tp_methods = T::s_getMethodDefs();
	type.tp_getset = T::s_getAttributeDefs();
	type.tp_base = base;
	type.tp_new = (newfunc)T::_pyNew;
	return type;
}
}

@PYAUTO_MACROS@

@PYOBJECT_PLUS_MACROS@

#define Py_Header(CLASS, SUPER) \
	public: \
		typedef SUPER Super; \
		static PyTypeObject s_type_; \
		static bool Check(PyObject * o) { return o && PyObject_TypeCheck(o, &s_type_); } \
		static PyMethodDef * s_getMethodDefs(); \
		static PyGetSetDef * s_getAttributeDefs(); \
		static CLASS * getSelf(PyObject * o) { return o ? static_cast<CLASS *>(static_cast<PyObjectPlus *>(o)) : NULL; } \
	private: \
		typedef CLASS This;
#define PY_TYPEOBJECT(CLASS) PyTypeObject CLASS::s_type_ = BW::hostType<CLASS>(#CLASS, &CLASS::Super::s_type_);
// The engine also adds __members__ and __methods__; consumers read neither.
#define PY_BEGIN_ATTRIBUTES(THIS_CLASS) \
	PyGetSetDef * THIS_CLASS::s_getAttributeDefs() \
	{ \
		static BW::vector<PyGetSetDef> s_attributes; \
		if (!s_attributes.empty()) return &s_attributes[0];

#include "stock_math_oscillators.cpp"

static PyObject * hostArgCountError(PyObject *, PyObject * args)
{
	const char * fn; int optas, allas; const char * t[5] = {"", "", "", "", ""};
	if (!PyArg_ParseTuple(args, "sii|sssss", &fn, &optas, &allas, &t[0], &t[1], &t[2], &t[3], &t[4]))
		return NULL;
	return BW::Script::argCountError(fn, optas, allas, t[0], t[1], t[2], t[3], t[4]);
}

static PyObject * hostState(PyObject *, PyObject *)
{
	return Py_BuildValue("{s:i,s:i}", "deallocations", BW::hostDeallocations,
		"setDataVector3", BW::hostSetDataCalls);
}

static PyObject * hostSetVectorType(PyObject *, PyObject * args)
{
	PyObject * type;
	if (!PyArg_ParseTuple(args, "O", &type)) return NULL;
	Py_XDECREF(BW::hostVectorType);
	Py_INCREF(type);
	BW::hostVectorType = type;
	Py_RETURN_NONE;
}

// Runs every recorded factory link, as Script::init does.
static PyObject * hostInitScripts(PyObject *, PyObject *)
{
	if (PyType_Ready(&BW::PyObjectPlus::s_type_) < 0) return NULL;
	for (const BW::HostFactoryLink & link : BW::hostFactoryLinks())
	{
		if (PyType_Ready(link.type) < 0) return NULL;
		PyObject * module = PyImport_AddModule(link.module);
		if (module == NULL || PyObject_SetAttrString(module, link.name, (PyObject *)link.type) != 0)
			return NULL;
	}
	return PyInt_FromSize_t(BW::hostFactoryLinks().size());
}

static PyMethodDef hostMethods[] = {
	{"argCountError", hostArgCountError, METH_VARARGS, NULL},
	{"state", hostState, METH_NOARGS, NULL},
	{"setVectorType", hostSetVectorType, METH_VARARGS, NULL},
	{"initScripts", hostInitScripts, METH_NOARGS, NULL},
	{NULL, NULL, 0, NULL},
};

int main(int argc, char ** argv)
{
	if (argc < 2) return 2;
	Py_Initialize();
	PySys_SetArgv(argc - 1, argv + 1);
	Py_InitModule("_oscillator_host", hostMethods);
	FILE * script = std::fopen(argv[1], "r");
	if (!script) return 2;
	const int failed = PyRun_SimpleFileEx(script, argv[1], 1);
	Py_Finalize();
	return failed ? 1 : 0;
}
