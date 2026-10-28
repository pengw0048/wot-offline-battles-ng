// Real CPython 2.7 runs the engine's PY_AUTO macros with the exact method
// declarations patch_stock_avatar_filter_vectors.py installs, the public
// Script::setData(int/double) and argCountError bodies, and the production
// channels and PyAvatarFilter methods. PyObjectPlus, the attached native
// filter and Vector3 conversion are fixtures.
#include <Python.h>
#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <memory>
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

struct Vector3
{
	float x, y, z;
	Vector3() : x(0.f), y(0.f), z(0.f) {}
	Vector3(float a, float b, float c) : x(a), y(b), z(c) {}
};

namespace Script {
template <class T> struct IsValidArgType { static const bool isValid = true; };
int setData(PyObject * pObject, int & rInt, const char * varName = "");
int setData(PyObject * pObject, double & rDouble, const char * varName = "");
int setData(PyObject * pObject, Vector3 & rVector, const char * varName = "");
PyObject * getData(const Vector3 & data);
// The engine's RETDATA conversion.
template <class T> PyObject * getRetData(const T & data) { return getData(data); }
PyObject * argCountError(const char * fn, int optas, int allas, ...);
}

@SCRIPT_BODIES@

std::vector<std::string> hostLog;
PyObject * hostVectorType = NULL;

// Engine Script::setData(Vector3): a Vector3 or a tuple of 3 floats. A
// Vector3 fixture converts through Python, so conversion can re-enter.
int Script::setData(PyObject * pObject, Vector3 & rVector, const char * varName)
{
	hostLog.push_back(std::string("convert ") + varName);
	if (hostVectorType && PyObject_IsInstance(pObject, hostVectorType) == 1)
	{
		PyObject * tuple = PyObject_CallMethod(pObject, (char *)"tuple", NULL);
		if (tuple && PyArg_ParseTuple(tuple, "fff", &rVector.x, &rVector.y, &rVector.z))
		{
			Py_DECREF(tuple);
			return 0;
		}
		Py_XDECREF(tuple);
		if (PyErr_Occurred()) return -1;
	}
	PyErr_Clear();
	if (PyTuple_Check(pObject) && PyArg_ParseTuple(pObject, "fff", &rVector.x, &rVector.y, &rVector.z))
		return 0;
	PyErr_Clear();
	PyErr_Format(PyExc_TypeError, "%s must be set to a Vector3 or a tuple of 3 floats", varName);
	return -1;
}

PyObject * Script::getData(const Vector3 & data)
{
	return PyObject_CallFunction(hostVectorType, (char *)"fff", data.x, data.y, data.z);
}

#define ERROR_MSG(...) do { char text[256]; std::snprintf(text, sizeof(text), __VA_ARGS__); \
	hostLog.push_back(std::string("error ") + text); } while (0)

namespace host
{
bool connected = true;
double lastMessage = 0.0, server = 0.0;
}

// @CHANNELS@

bool stockAvatarVectorLastMessageTime(double & time)
{
	if (!host::connected) return false;
	time = host::lastMessage;
	return true;
}

bool stockAvatarVectorServerTime(double & time)
{
	if (!host::connected) return false;
	time = host::server;
	return true;
}

// The attached native filter owns the channels, as AvatarFilter+0x2f8 does.
struct AvatarFilter
{
	int generation;
	StockAvatarVectorChannels channels;
	StockAvatarVectorChannels & stockVectorChannels() { return channels; }
};

class PyObjectPlus : public PyObject
{
public:
	explicit PyObjectPlus(PyTypeObject * type) { PyObject_Init(this, type); }
	virtual ~PyObjectPlus() {}
	static void * operator new(std::size_t size) { return PyObject_Malloc(size); }
	static void operator delete(void * p) { PyObject_Free(p); }
};
}

@PYAUTO_MACROS@

@PYOBJECT_PLUS_MACROS@

namespace BW {
int hostGenerations = 0;

class PyAvatarFilter : public PyObjectPlus
{
public:
	typedef PyAvatarFilter This;
	static PyTypeObject s_type_;
	static PyAvatarFilter * getSelf(PyObject * o) { return static_cast<PyAvatarFilter *>(static_cast<PyObjectPlus *>(o)); }
	PyAvatarFilter() : PyObjectPlus(&s_type_) {}
	AvatarFilter * pAttachedFilter() { return attached_.get(); }
	void attach() { attached_.reset(new AvatarFilter); attached_->generation = ++hostGenerations; }
	void detach() { attached_.reset(); }
	std::unique_ptr<AvatarFilter> attached_;

// @DECLARATIONS@
};

// @METHODS@
}

static PyMethodDef filterMethods[] = {
	{"syncVector3", (PyCFunction)&BW::PyAvatarFilter::_py_syncVector3, METH_VARARGS | METH_KEYWORDS, NULL},
	{"getVector3", (PyCFunction)&BW::PyAvatarFilter::_py_getVector3, METH_VARARGS | METH_KEYWORDS, NULL},
	{"resetVector3", (PyCFunction)&BW::PyAvatarFilter::_py_resetVector3, METH_VARARGS | METH_KEYWORDS, NULL},
	{"setInterpolationType", (PyCFunction)&BW::PyAvatarFilter::_py_setInterpolationType,
		METH_VARARGS | METH_KEYWORDS, NULL},
	{NULL, NULL, 0, NULL},
};

PyTypeObject BW::PyAvatarFilter::s_type_ = { PyVarObject_HEAD_INIT(&PyType_Type, 0) "AvatarFilter" };

static BW::PyAvatarFilter * filterArg(PyObject * args)
{
	PyObject * o;
	if (!PyArg_ParseTuple(args, "O", &o) || !PyObject_TypeCheck(o, &BW::PyAvatarFilter::s_type_)) return NULL;
	return BW::PyAvatarFilter::getSelf(o);
}

static PyObject * hostNew(PyObject *, PyObject *) { return new BW::PyAvatarFilter(); }
static PyObject * hostAttach(PyObject *, PyObject * args)
{
	BW::PyAvatarFilter * f = filterArg(args);
	if (!f) return NULL;
	f->attach();
	return PyInt_FromLong(f->attached_->generation);
}
static PyObject * hostDetach(PyObject *, PyObject * args)
{
	BW::PyAvatarFilter * f = filterArg(args);
	if (!f) return NULL;
	f->detach();
	Py_RETURN_NONE;
}
static PyObject * hostGeneration(PyObject *, PyObject * args)
{
	BW::PyAvatarFilter * f = filterArg(args);
	if (!f) return NULL;
	return PyInt_FromLong(f->attached_ ? f->attached_->generation : 0);
}
static PyObject * hostSamples(PyObject *, PyObject * args)
{
	PyObject * o; int id;
	if (!PyArg_ParseTuple(args, "Oi", &o, &id)) return NULL;
	BW::PyAvatarFilter * f = BW::PyAvatarFilter::getSelf(o);
	const BW::StockAvatarVectorChannel * c = f->attached_ ? f->attached_->channels.channel(id) : NULL;
	if (!c) return PyInt_FromLong(-1);
	return Py_BuildValue("(ii)", int(c->samples().size()), c->interpolationType());
}
static PyObject * hostClock(PyObject *, PyObject * args)
{
	int connected;
	if (!PyArg_ParseTuple(args, "idd", &connected, &BW::host::lastMessage, &BW::host::server)) return NULL;
	BW::host::connected = connected != 0;
	Py_RETURN_NONE;
}
static PyObject * hostLogTake(PyObject *, PyObject *)
{
	PyObject * list = PyList_New(0);
	for (const std::string & line : BW::hostLog)
	{
		PyObject * item = PyString_FromString(line.c_str());
		PyList_Append(list, item);
		Py_DECREF(item);
	}
	BW::hostLog.clear();
	return list;
}
static PyObject * hostSetVectorType(PyObject *, PyObject * args)
{
	if (!PyArg_ParseTuple(args, "O", &BW::hostVectorType)) return NULL;
	Py_INCREF(BW::hostVectorType);
	Py_RETURN_NONE;
}
static PyObject * hostArgCountError(PyObject *, PyObject * args)
{
	const char * fn; int optas, allas; const char * t[3] = {"", "", ""};
	if (!PyArg_ParseTuple(args, "sii|sss", &fn, &optas, &allas, &t[0], &t[1], &t[2])) return NULL;
	return BW::Script::argCountError(fn, optas, allas, t[0], t[1], t[2]);
}

static PyMethodDef hostMethods[] = {
	{"new", hostNew, METH_NOARGS, NULL},
	{"attach", hostAttach, METH_VARARGS, NULL},
	{"detach", hostDetach, METH_VARARGS, NULL},
	{"generation", hostGeneration, METH_VARARGS, NULL},
	{"samples", hostSamples, METH_VARARGS, NULL},
	{"clock", hostClock, METH_VARARGS, NULL},
	{"log", hostLogTake, METH_NOARGS, NULL},
	{"setVectorType", hostSetVectorType, METH_VARARGS, NULL},
	{"argCountError", hostArgCountError, METH_VARARGS, NULL},
	{NULL, NULL, 0, NULL},
};

int main(int argc, char ** argv)
{
	if (argc < 2) return 2;
	Py_Initialize();
	PySys_SetArgv(argc - 1, argv + 1);
	BW::PyAvatarFilter::s_type_.tp_basicsize = sizeof(BW::PyAvatarFilter);
	BW::PyAvatarFilter::s_type_.tp_dealloc = [](PyObject * o) { delete BW::PyAvatarFilter::getSelf(o); };
	BW::PyAvatarFilter::s_type_.tp_flags = Py_TPFLAGS_DEFAULT;
	BW::PyAvatarFilter::s_type_.tp_methods = filterMethods;
	if (PyType_Ready(&BW::PyAvatarFilter::s_type_) < 0) return 2;
	Py_InitModule("_avatar_vector_host", hostMethods);
	FILE * script = std::fopen(argv[1], "r");
	if (!script) return 2;
	const int failed = PyRun_SimpleFileEx(script, argv[1], 1);
	Py_Finalize();
	return failed ? 1 : 0;
}
