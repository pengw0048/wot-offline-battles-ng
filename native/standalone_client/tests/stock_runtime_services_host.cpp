// Real CPython 2.7 runs the engine's PY_AUTO macros, the public Script::setData,
// getData and argCountError bodies, the production BigWorld runtime-service
// bindings and StockFramePacing, and the patched App frame-pacing methods,
// configuration and frame-sleep lines extracted from the pinned app.cpp.
// App's clock, Moo::rc(), URL::Manager and PyModuleMethodLink are fixtures.
#include <Python.h>
#include <algorithm>
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <deque>
#include <map>
#include <string>
#include <strings.h>
#include <vector>

#define BW_BEGIN_NAMESPACE namespace BW {
#define BW_END_NAMESPACE }
#define BW_STATIC_ASSERT(expression, message) static_assert(expression, #message)
#define _stricmp strcasecmp

namespace BW {
typedef long long int64;
typedef unsigned long long uint64;
typedef unsigned int uint32;
typedef unsigned int uint;
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

// PyModuleMethodLink: the public constructors select METH_VARARGS, or
// METH_VARARGS | METH_KEYWORDS for PyCFunctionWithKeywords.
struct HostMethodLink { std::string module; PyMethodDef def; };
std::deque<HostMethodLink> & hostMethodLinks() { static std::deque<HostMethodLink> links; return links; }
struct PyModuleMethodLink
{
	PyModuleMethodLink(const char * module, const char * name, PyCFunction function, const char * doc = NULL)
	{
		hostMethodLinks().push_back(HostMethodLink{module, PyMethodDef{name, function, METH_VARARGS, doc}});
	}
	PyModuleMethodLink(const char * module, const char * name, PyCFunctionWithKeywords function,
		const char * doc = NULL)
	{
		hostMethodLinks().push_back(HostMethodLink{module,
			PyMethodDef{name, (PyCFunction)function, METH_VARARGS | METH_KEYWORDS, doc}});
	}
};

namespace Script {
template <class T> struct IsValidArgType { static const bool isValid = true; };
int setData(PyObject * pObject, bool & rBool, const char * varName = "");
int setData(PyObject * pObject, int64 & rInt, const char * varName = "");
int setData(PyObject * pObject, float & rFloat, const char * varName = "");
int setData(PyObject * pObject, double & rDouble, const char * varName = "");
int setData(PyObject * pObject, BW::string & rString, const char * varName = "");
PyObject * getData(const uint data);
PyObject * getData(const float data);
PyObject * argCountError(const char * fn, int optas, int allas, ...);
@RET_DATA@
}

@SCRIPT_BODIES@

uint64 hostTicks = 0;
double hostFrequency = 1000.0;
}

@PYAUTO_MACROS@

#include "stock_runtime_services.hpp"

namespace BW {
#define frameTimerValue() BW::hostTicks
#define frameTimerFreq() BW::hostFrequency
#define FRAME_TIMER_TYPE uint64
using std::max;

struct HostConfig
{
	std::map<std::string, int> values;
	int readInt(const char * key, int fallback) const
	{
		std::map<std::string, int>::const_iterator found = values.find(key);
		return found == values.end() ? fallback : found->second;
	}
};

class App
{
public:
	static App & instance() { return *pInstance_; }
	static App * pInstance_;

	double getRenderTimeNow() const;
	void setSpecialFPSMode( float fps );
	void disableSpecialFPSMode();
	void setReducedFpsMode( bool enabled );

	void hostInit(const HostConfig * configSection)
	{
		appStartRenderTime_ = frameTimerValue();
		framePacing_ = wot::StockFramePacing();
@APP_CONFIGURATION@
	}
	int hostSleep(int sleepTime) const
	{
@APP_SLEEP@
		return sleepTime;
	}

	uint64 appStartRenderTime_ = 0;
	uint64 minFrameTime_ = 0;
	wot::StockFramePacing framePacing_;
	int sleepTime_ = 0;
};
App * App::pInstance_ = NULL;

@APP_METHODS@

namespace Moo {
struct HostRenderContext
{
	uint32 currentFrame_ = 0;
	int reuseListClears = 0;
	uint32 frameTimestamp() const { return currentFrame_; }
	void nextFrame() { ++currentFrame_; }
	void clearTextureReuseList() { ++reuseListClears; }
};
HostRenderContext & rc() { static HostRenderContext context; return context; }
}

namespace URL {
struct Manager
{
	static Manager * instance() { return s_managerInstance_; }
	static Manager * s_managerInstance_;
};
Manager * Manager::s_managerInstance_ = NULL;
}
}

// The statistics owner's ping state is exercised in the debug-query host.
namespace wot {
int hostPingResets = 0;
bool hostLoadingTimer = false;
void resetStockStatPing() { ++hostPingResets; }
void enableStockBattleStatistics(bool enabled) { if (enabled) ++hostPingResets; }
void setStockLoadingTimer( bool enabled ) { hostLoadingTimer = enabled; }
}

#include "stock_runtime_services.cpp"

static PyObject * hostInitScripts(PyObject *, PyObject *)
{
	for (BW::HostMethodLink & link : BW::hostMethodLinks())
	{
		PyObject * module = PyImport_AddModule(link.module.c_str());
		PyObject * function = module ? PyCFunction_NewEx(&link.def, NULL, NULL) : NULL;
		int failed = function == NULL || PyObject_SetAttrString(module, link.def.ml_name, function) != 0;
		Py_XDECREF(function);
		if (failed) return NULL;
	}
	return PyInt_FromSize_t(BW::hostMethodLinks().size());
}

static PyObject * hostLinks(PyObject *, PyObject *)
{
	PyObject * result = PyList_New(0);
	for (const BW::HostMethodLink & link : BW::hostMethodLinks())
	{
		PyObject * item = Py_BuildValue("(ssi)", link.module.c_str(), link.def.ml_name, link.def.ml_flags);
		PyList_Append(result, item);
		Py_DECREF(item);
	}
	return result;
}

static PyObject * hostConfigure(PyObject *, PyObject * args)
{
	PyObject * values;
	double frequency;
	if (!PyArg_ParseTuple(args, "O!d", &PyDict_Type, &values, &frequency)) return NULL;
	BW::HostConfig config;
	PyObject * key, * value;
	Py_ssize_t position = 0;
	while (PyDict_Next(values, &position, &key, &value))
		config.values[PyString_AsString(key)] = int(PyInt_AsLong(value));
	BW::hostFrequency = frequency;
	BW::App::instance().hostInit(&config);
	Py_RETURN_NONE;
}

static PyObject * hostSetTicks(PyObject *, PyObject * args)
{
	unsigned long long ticks;
	if (!PyArg_ParseTuple(args, "K", &ticks)) return NULL;
	BW::hostTicks = ticks;
	Py_RETURN_NONE;
}

static PyObject * hostNextFrame(PyObject *, PyObject * args)
{
	unsigned int frame = 0;
	if (!PyArg_ParseTuple(args, "|I", &frame)) return NULL;
	if (PyTuple_Size(args)) BW::Moo::rc().currentFrame_ = frame;
	else BW::Moo::rc().nextFrame();
	Py_RETURN_NONE;
}

static PyObject * hostSetUrlManager(PyObject *, PyObject * args)
{
	static BW::URL::Manager manager;
	int present;
	if (!PyArg_ParseTuple(args, "i", &present)) return NULL;
	BW::URL::Manager::s_managerInstance_ = present ? &manager : NULL;
	Py_RETURN_NONE;
}

static PyObject * hostSleep(PyObject *, PyObject * args)
{
	int sleepTime;
	if (!PyArg_ParseTuple(args, "i", &sleepTime)) return NULL;
	return PyInt_FromLong(BW::App::instance().hostSleep(sleepTime));
}

static PyObject * hostState(PyObject *, PyObject *)
{
	const BW::App & app = BW::App::instance();
	return Py_BuildValue("{s:K,s:O,s:O,s:I,s:i,s:i,s:O}",
		"minFrameTime", (unsigned long long)app.minFrameTime_,
		"special", app.framePacing_.special() ? Py_True : Py_False,
		"reduced", app.framePacing_.reduced() ? Py_True : Py_False,
		"frame", BW::Moo::rc().currentFrame_,
		"reuseListClears", BW::Moo::rc().reuseListClears,
		"pingResets", wot::hostPingResets,
		"loadingTimer", wot::hostLoadingTimer ? Py_True : Py_False);
}

static PyMethodDef hostMethods[] = {
	{"initScripts", hostInitScripts, METH_NOARGS, NULL},
	{"links", hostLinks, METH_NOARGS, NULL},
	{"configure", hostConfigure, METH_VARARGS, NULL},
	{"setTicks", hostSetTicks, METH_VARARGS, NULL},
	{"nextFrame", hostNextFrame, METH_VARARGS, NULL},
	{"setUrlManager", hostSetUrlManager, METH_VARARGS, NULL},
	{"sleep", hostSleep, METH_VARARGS, NULL},
	{"state", hostState, METH_NOARGS, NULL},
	{NULL, NULL, 0, NULL},
};

int main(int argc, char ** argv)
{
	if (argc < 2) return 2;
	static BW::App app;
	BW::App::pInstance_ = &app;
	Py_Initialize();
	PySys_SetArgv(argc - 1, argv + 1);
	Py_InitModule("_runtime_host", hostMethods);
	FILE * script = std::fopen(argv[1], "r");
	if (!script) return 2;
	const int failed = PyRun_SimpleFileEx(script, argv[1], 1);
	Py_Finalize();
	return failed ? 1 : 0;
}
