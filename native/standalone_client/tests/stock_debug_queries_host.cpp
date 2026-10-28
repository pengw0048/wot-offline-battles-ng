// Real CPython 2.7 runs the engine's PY_AUTO macros, the public Script::getData
// (float) and argCountError bodies, the production statPing/getFPS bindings and
// statistics task, and DebugApp's frame-rate sampling and accessor taken from
// the patched pinned debug_app.cpp/hpp. ConnectionControl, the server
// connection, MainLoopTasks and Vector4 conversion are fixtures.
#include <Python.h>
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <deque>
#include <string>
#include <strings.h>
#include <vector>

#define BW_BEGIN_NAMESPACE namespace BW {
#define BW_END_NAMESPACE }
#define BW_NAMESPACE BW::
#define BW_STATIC_ASSERT(expression, message) static_assert(expression, #message)
#define _stricmp strcasecmp

namespace BW {
typedef long long int64;
typedef unsigned long long uint64;
typedef unsigned int uint32;
typedef unsigned int uint;
typedef std::string string;

struct Vector4 { float x, y, z, w; };
struct Vector3 {float x,y,z;Vector3(float a,float b,float c):x(a),y(b),z(c){}};

struct HostMethodLink { std::string module; PyMethodDef def; };
std::deque<HostMethodLink> & hostMethodLinks() { static std::deque<HostMethodLink> links; return links; }
struct PyModuleMethodLink
{
	PyModuleMethodLink(const char * module, const char * name, PyCFunction function, const char * doc = NULL)
	{
		hostMethodLinks().push_back(HostMethodLink{module, PyMethodDef{name, function, METH_VARARGS, doc}});
	}
};

namespace Script {
template <class T> struct IsValidArgType { static const bool isValid = true; };
PyObject * getData(const float data);
int setData(PyObject*,bool&,const char* = "");
inline PyObject* getData(const Vector3& data){return Py_BuildValue("(fff)",data.x,data.y,data.z);}
// Fixture for the engine's PyVectorCopy<Vector4>: the components in order.
inline PyObject * getData(const Vector4 & data)
{
	return Py_BuildValue("(dddd)", double(data.x), double(data.y), double(data.z), double(data.w));
}
PyObject * argCountError(const char * fn, int optas, int allas, ...);
@RET_DATA@
}

@SCRIPT_BODIES@

// MainLoopTasks::root().add: production registers its statistics task here.
class MainLoopTask
{
public:
	virtual ~MainLoopTask() {}
	virtual void tick( float, float ) {}
};
struct HostTask { MainLoopTask * task; std::string name; std::string order; };
std::vector<HostTask> & hostTasks() { static std::vector<HostTask> tasks; return tasks; }
struct HostTaskRoot
{
	void add( MainLoopTask * task, const char * name, const char * order, const char * )
	{
		hostTasks().push_back(HostTask{task, name, order ? order : ""});
	}
};
struct MainLoopTasks { static HostTaskRoot & root() { static HostTaskRoot root; return root; } };

struct ServerConnection
{
	float latency_ = 0.f;
	float latency() const { return latency_; }
};
struct BWServerConnection
{
	bool online_ = false;
	ServerConnection server_;
	bool isOnline() const { return online_; }
	ServerConnection * pServerConnection() const { return const_cast<ServerConnection *>(&server_); }
};
struct ConnectionControl
{
	static ConnectionControl * s_pInstance;
	static ConnectionControl * pInstance() { return s_pInstance; }
	BWServerConnection * pServerConnection_ = NULL;
	BWServerConnection * pServerConnection() const { return pServerConnection_; }
};
ConnectionControl * ConnectionControl::s_pInstance = NULL;

class DebugApp
{
public:
	static DebugApp instance;
@DEBUG_ACCESSOR@

	void tick( float /* dGameTime */, float dRenderTime )
	{
@DEBUG_SAMPLING@
	}

	float dRenderTime_ = 0.f;
	float fps_ = 0.f;
	float fpsAverage_ = 0.f;
	float maxFps_ = 0.f;
	float minFps_ = 0.f;
	float fpsCache_[50] = {};
	float dTimeCache_[50] = {};
	uint32 fpsIndex_ = 0;
	float timeSinceFPSUpdate_ = 1.f;
};
DebugApp DebugApp::instance;
}

@PYAUTO_MACROS@

#include "stock_debug_queries.cpp"
namespace BW {
@ENABLE_BATTLE_BINDING@
}


static BW::ConnectionControl s_control;
static BW::BWServerConnection s_connection;

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

static PyObject * hostTasks(PyObject *, PyObject *)
{
	PyObject * result = PyList_New(0);
	for (const BW::HostTask & task : BW::hostTasks())
	{
		PyObject * item = Py_BuildValue("(ss)", task.name.c_str(), task.order.c_str());
		PyList_Append(result, item);
		Py_DECREF(item);
	}
	return result;
}

// One active frame of the main loop: every registered task ticks.
static PyObject * hostTickStatistics(PyObject *, PyObject * args)
{
	float elapsed=0.f;
	if (!PyArg_ParseTuple(args,"|f",&elapsed)) return NULL;
	for (const BW::HostTask & task : BW::hostTasks()) task.task->tick(0.f, elapsed);
	Py_RETURN_NONE;
}

static PyObject * hostTickDebugApp(PyObject *, PyObject * args)
{
	float dRenderTime;
	if (!PyArg_ParseTuple(args, "f", &dRenderTime)) return NULL;
	BW::DebugApp::instance.tick(0.f, dRenderTime);
	const BW::DebugApp & app = BW::DebugApp::instance;
	return Py_BuildValue("(dddd)", double(app.fps_), double(app.fpsAverage_), double(app.minFps_),
		double(app.maxFps_));
}

static PyObject * hostResetDebugApp(PyObject *, PyObject *)
{
	BW::DebugApp::instance = BW::DebugApp();
	Py_RETURN_NONE;
}

// control: ConnectionControl exists; server: it has a BWServerConnection.
static PyObject * hostSetConnection(PyObject *, PyObject * args)
{
	int control, server, online;
	float latency;
	if (!PyArg_ParseTuple(args, "iiif", &control, &server, &online, &latency)) return NULL;
	s_connection.online_ = online != 0;
	s_connection.server_.latency_ = latency;
	s_control.pServerConnection_ = server ? &s_connection : NULL;
	BW::ConnectionControl::s_pInstance = control ? &s_control : NULL;
	Py_RETURN_NONE;
}

static PyObject * hostResetPing(PyObject *, PyObject *)
{
	wot::resetStockStatPing();
	Py_RETURN_NONE;
}

static PyObject * hostSetLoadingTimer(PyObject *, PyObject * args)
{
	int enabled;
	if (!PyArg_ParseTuple(args, "i", &enabled)) return NULL;
	wot::setStockLoadingTimer(enabled != 0);
	Py_RETURN_NONE;
}

static PyMethodDef hostMethods[] = {
	{"initScripts", hostInitScripts, METH_NOARGS, NULL},
	{"tasks", hostTasks, METH_NOARGS, NULL},
	{"tickStatistics", hostTickStatistics, METH_VARARGS, NULL},
	{"tickDebugApp", hostTickDebugApp, METH_VARARGS, NULL},
	{"resetDebugApp", hostResetDebugApp, METH_NOARGS, NULL},
	{"setConnection", hostSetConnection, METH_VARARGS, NULL},
	{"resetPing", hostResetPing, METH_NOARGS, NULL},
	{"setLoadingTimer", hostSetLoadingTimer, METH_VARARGS, NULL},
	{NULL, NULL, 0, NULL},
};

int main(int argc, char ** argv)
{
	if (argc < 2) return 2;
	Py_Initialize();
	PySys_SetArgv(argc - 1, argv + 1);
	Py_InitModule("_debug_host", hostMethods);
	FILE * script = std::fopen(argv[1], "r");
	if (!script) return 2;
	const int failed = PyRun_SimpleFileEx(script, argv[1], 1);
	Py_Finalize();
	return failed ? 1 : 0;
}
