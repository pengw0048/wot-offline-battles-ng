// Real CPython 2.7 runs the engine's PY_AUTO macros, PY_AUTO_METHOD_DECLARE,
// the public Script::setData(bool/int/uint/float) bodies, Script::argCountError
// and the production BigWorld.PyBallisticsSimulator/PySalvo binding over the
// production presentation core. PyObjectPlus, Motor, PyModel, ScriptObject,
// Vector3/Matrix, App time and the camera are fixtures.
#include <Python.h>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <strings.h>
#include <vector>

#include "wot/stock_ballistics.hpp"

#define BW_BEGIN_NAMESPACE namespace BW {
#define BW_END_NAMESPACE }
#define BW_STATIC_ASSERT(expression, message) static_assert(expression, #message)
#define _stricmp strcasecmp

typedef int int32;
typedef unsigned int uint;
typedef long long int64;

namespace BW {
template <class T> using vector = std::vector<T>;
typedef std::string string;
typedef int32 SpaceID;

struct Vector3
{
	float x, y, z;
	Vector3() : x(0), y(0), z(0) {}
	Vector3(float a, float b, float c) : x(a), y(b), z(c) {}
};
struct Matrix { float m[4][4]; };

// Script::init's module links.
struct HostLink { const char * module; const char * name; PyCFunction function; };
std::vector<HostLink> & hostLinks() { static std::vector<HostLink> links; return links; }
struct PyModuleMethodLink
{
	PyModuleMethodLink(const char * module, const char * name, PyCFunction function, const char *)
	{
		hostLinks().push_back(HostLink{module, name, function});
	}
};

namespace Script {
template <class T> struct IsValidArgType { static const bool isValid = true; };
int setData(PyObject * pObject, bool & rBool, const char * varName = "");
int setData(PyObject * pObject, int & rInt, const char * varName = "");
int setData(PyObject * pObject, uint & rUint, const char * varName = "");
int setData(PyObject * pObject, float & rFloat, const char * varName = "");
int setData(PyObject * pObject, double & rDouble, const char * varName = "");
PyObject * argCountError(const char * fn, int optas, int allas, ...);

// Fixture: PyVector<Vector3> is any object with float x, y and z.
int setData(PyObject * pObject, Vector3 & value, const char * varName = "")
{
	PyObject * parts[3] = {PyObject_GetAttrString(pObject, "x"), PyObject_GetAttrString(pObject, "y"),
		PyObject_GetAttrString(pObject, "z")};
	bool ok = parts[0] && parts[1] && parts[2];
	float result[3] = {0, 0, 0};
	for (int i = 0; i < 3 && ok; ++i)
	{
		result[i] = float(PyFloat_AsDouble(parts[i]));
		ok = !PyErr_Occurred();
	}
	for (PyObject * part : parts) Py_XDECREF(part);
	if (!ok)
	{
		PyErr_Clear();
		PyErr_Format(PyExc_TypeError, "%s must be set to a Vector3", varName);
		return -1;
	}
	value = Vector3(result[0], result[1], result[2]);
	return 0;
}

PyObject * getData(const Vector3 & value)
{
	PyObject * math = PyImport_ImportModule("Math");
	if (!math) return NULL;
	PyObject * result = PyObject_CallMethod(math, (char *)"Vector3", (char *)"fff",
		double(value.x), double(value.y), double(value.z));
	Py_DECREF(math);
	return result;
}
PyObject * getData(int value) { return PyInt_FromLong(value); }

int hostPrintedErrors = 0;
void printError() { ++hostPrintedErrors; PyErr_Print(); }
void clearError() { PyErr_Clear(); }
}

@SCRIPT_BODIES@

class ScriptArgs;
class ScriptErrorPrint
{
public:
	void handleError() const { Script::printError(); Script::clearError(); }
};

class ScriptObject
{
public:
	ScriptObject() : object_(NULL) {}
	ScriptObject(PyObject * object, bool newReference) : object_(object)
	{
		if (!newReference) Py_XINCREF(object_);
	}
	ScriptObject(const ScriptObject & other) : object_(other.object_) { Py_XINCREF(object_); }
	ScriptObject & operator=(const ScriptObject & other)
	{
		Py_XINCREF(other.object_);
		Py_XDECREF(object_);
		object_ = other.object_;
		return *this;
	}
	~ScriptObject() { Py_XDECREF(object_); }
	PyObject * get() const { return object_; }
	bool exists() const { return object_ != NULL; }
	bool isCallable() const { return object_ && PyCallable_Check(object_); }
	inline ScriptObject callFunction(const ScriptArgs & args, const ScriptErrorPrint & handler) const;
protected:
	PyObject * object_;
};

class ScriptArgs : public ScriptObject
{
public:
	explicit ScriptArgs(PyObject * tuple) : ScriptObject(tuple, true) {}
	template <class A> static ScriptArgs create(const A & a)
	{
		return ScriptArgs(Py_BuildValue("(N)", Script::getData(a)));
	}
	template <class A, class B> static ScriptArgs create(const A & a, const B & b)
	{
		return ScriptArgs(Py_BuildValue("(NN)", Script::getData(a), Script::getData(b)));
	}
	template <class A, class B, class C> static ScriptArgs create(const A & a, const B & b, const C & c)
	{
		return ScriptArgs(Py_BuildValue("(NNN)", Script::getData(a), Script::getData(b), Script::getData(c)));
	}
};

ScriptObject ScriptObject::callFunction(const ScriptArgs & args, const ScriptErrorPrint & handler) const
{
	PyObject * result = PyObject_CallObject(object_, args.get());
	if (!result) handler.handleError();
	return ScriptObject(result, true);
}

namespace Script {
int setData(PyObject * pObject, ScriptObject & value, const char * = "")
{
	value = ScriptObject(pObject, false);
	return 0;
}
}

class PyObjectPlus : public PyObject
{
public:
	typedef PyObjectPlus This;
	static PyTypeObject s_type_;
	explicit PyObjectPlus(PyTypeObject * type) { PyObject_Init(this, type); }
	virtual ~PyObjectPlus() {}
	static void * operator new(std::size_t size) { return PyObject_Malloc(size); }
	static void operator delete(void * p) { PyObject_Free(p); }
};

int hostDeallocations = 0;
std::vector<PyTypeObject *> & hostTypes() { static std::vector<PyTypeObject *> types; return types; }
template <class T> PyTypeObject hostType(const char * name, PyTypeObject * base, PyMethodDef * methods)
{
	PyTypeObject type = { PyVarObject_HEAD_INIT(&PyType_Type, 0) };
	type.tp_name = name;
	type.tp_basicsize = sizeof(T);
	type.tp_dealloc = [](PyObject * o) { ++hostDeallocations; delete static_cast<T *>(static_cast<PyObjectPlus *>(o)); };
	type.tp_getattro = PyObject_GenericGetAttr;
	type.tp_setattro = PyObject_GenericSetAttr;
	type.tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE;
	type.tp_methods = methods;
	type.tp_base = base;
	return type;
}
PyTypeObject PyObjectPlus::s_type_ = hostType<PyObjectPlus>("PyObjectPlus", NULL, NULL);
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
#define PY_GENERAL_TYPEOBJECT_WITH_BASE_WITH_NAME(CLASS, BASE, NAME) \
	PyTypeObject CLASS::s_type_ = BW::hostType<CLASS>(NAME, BASE, CLASS::s_getMethodDefs()); \
	static int s_register_##CLASS = (BW::hostTypes().push_back(&CLASS::s_type_), 0);
#define PY_TYPEOBJECT(CLASS) PY_GENERAL_TYPEOBJECT_WITH_BASE_WITH_NAME(CLASS, &Super::s_type_, #CLASS)
#define PY_BEGIN_METHODS(CLASS) \
	PyMethodDef * CLASS::s_getMethodDefs() { typedef CLASS This; static std::vector<PyMethodDef> defs; if (defs.empty()) {
// Public PY_METHOD and #1513 01a168c0: METH_VARARGS | METH_KEYWORDS.
#define PY_METHOD(NAME) defs.push_back(PyMethodDef{#NAME, (PyCFunction)&This::_py_##NAME, METH_VARARGS | METH_KEYWORDS, NULL});
#define PY_END_METHODS() defs.push_back(PyMethodDef{NULL, NULL, 0, NULL}); } return defs.data(); }
#define PY_BEGIN_ATTRIBUTES(CLASS)
#define PY_END_ATTRIBUTES()

namespace BW {
class PyModel;

// Public lib/duplo/motor.hpp interface.
class Motor : public PyObjectPlus
{
	Py_Header(Motor, PyObjectPlus)
public:
	Motor(PyTypeObject * type) : PyObjectPlus(type), pOwner_(NULL) {}
	virtual void attach(PyModel * owner) { pOwner_ = owner; }
	virtual void detach() { pOwner_ = NULL; }
	virtual void rev(float dTime) = 0;
	PyModel * pOwner() { return pOwner_; }
protected:
	PyModel * pOwner_;
};
PY_TYPEOBJECT(Motor)
PY_BEGIN_METHODS(Motor)
PY_END_METHODS()

// Public PyModel motor ownership: addMotor/delMotor and tick's rev order.
class PyModel : public PyObjectPlus
{
	Py_Header(PyModel, PyObjectPlus)
public:
	PyModel() : PyObjectPlus(&s_type_), visible_(true), visibleAttachments_(false), transforms_(0)
	{
		std::memset(&world_, 0, sizeof(world_));
	}
	~PyModel() { for (Motor * motor : motors_) { motor->detach(); Py_DECREF(motor); } }
	void visible(bool value) { visible_ = value; }
	void worldTransform(const Matrix & world) { world_ = world; ++transforms_; }

	static PyObject * _py_addMotor(PyObject * self, PyObject * args, PyObject *)
	{
		PyModel * model = getSelf(self);
		PyObject * object;
		if (!PyArg_ParseTuple(args, "O", &object) || !PyObject_TypeCheck(object, &Motor::s_type_))
		{
			PyErr_SetString(PyExc_TypeError, "Model.addMotor() expects a Motor");
			return NULL;
		}
		Motor * motor = static_cast<Motor *>(static_cast<PyObjectPlus *>(object));
		if (motor->pOwner() != NULL)
		{
			PyErr_SetString(PyExc_ValueError, "Model.addMotor() cannot add a Motor is already attached to a Model");
			return NULL;
		}
		Py_INCREF(motor);
		motor->attach(model);
		model->motors_.push_back(motor);
		Py_RETURN_NONE;
	}
	static PyObject * _py_delMotor(PyObject * self, PyObject * args, PyObject *)
	{
		PyModel * model = getSelf(self);
		PyObject * object;
		if (!PyArg_ParseTuple(args, "O", &object) || !PyObject_TypeCheck(object, &Motor::s_type_))
		{
			PyErr_SetString(PyExc_TypeError, "Model.delMotor() expects a Motor");
			return NULL;
		}
		for (std::size_t i = 0; i < model->motors_.size(); ++i)
			if (model->motors_[i] == object)
			{
				Motor * motor = model->motors_[i];
				model->motors_.erase(model->motors_.begin() + i);
				motor->detach();
				Py_DECREF(motor);
				Py_RETURN_NONE;
			}
		PyErr_SetString(PyExc_ValueError, "Model.delMotor(): Motor not attached");
		return NULL;
	}
	// Test probe: one PyModel::tick motor pass, then the model state.
	static PyObject * _py_rev(PyObject * self, PyObject *, PyObject *)
	{
		PyModel * model = getSelf(self);
		for (std::size_t i = 0; i < model->motors_.size(); ++i) model->motors_[i]->rev(0.016f);
		const float (&m)[4][4] = model->world_.m;
		return Py_BuildValue("{s:i,s:i,s:i,s:(fff),s:(fff),s:f}", "visible", int(model->visible_),
			"transforms", model->transforms_, "motors", int(model->motors_.size()),
			"position", m[3][0], m[3][1], m[3][2], "forward", m[2][0], m[2][1], m[2][2],
			"scale", std::sqrt(m[2][0] * m[2][0] + m[2][1] * m[2][1] + m[2][2] * m[2][2]));
	}
	bool visible_;
	bool visibleAttachments_;
	int transforms_;
	Matrix world_;
	std::vector<Motor *> motors_;
};
PY_TYPEOBJECT(PyModel)
PY_BEGIN_METHODS(PyModel)
	PY_METHOD(addMotor)
	PY_METHOD(delMotor)
	PY_METHOD(rev)
PY_END_METHODS()

// The PyModel visible/visibleAttachments accessors used by ProjectileMover.add.
static PyObject * getVisible(PyObject * self, void * which)
{
	PyModel * model = PyModel::getSelf(self);
	return PyBool_FromLong(which ? model->visibleAttachments_ : model->visible_);
}
static int setVisible(PyObject * self, PyObject * value, void * which)
{
	bool flag;
	if (value == NULL || Script::setData(value, flag, which ? "visibleAttachments" : "visible") != 0) return -1;
	(which ? PyModel::getSelf(self)->visibleAttachments_ : PyModel::getSelf(self)->visible_) = flag;
	return 0;
}
static PyGetSetDef modelAccessors[] = {
	{(char *)"visible", getVisible, setVisible, NULL, NULL},
	{(char *)"visibleAttachments", getVisible, setVisible, NULL, (void *)1},
	{NULL, NULL, NULL, NULL, NULL},
};
static int s_modelAccessors = (PyModel::s_type_.tp_getset = modelAccessors, 0);

double hostTime = 0.0;
Vector3 hostCamera(0, 0, 0);
struct App
{
	static App & instance() { static App app; return app; }
	double getGameTimeFrameStart() const { return hostTime; }
};
namespace Moo {
struct HostMatrix { Vector3 applyToOrigin() const { return hostCamera; } };
struct HostRenderContext { HostMatrix invView() const { return HostMatrix(); } };
HostRenderContext & rc() { static HostRenderContext context; return context; }
}
}

namespace BW {
// The BQ scene owner is exercised with BP's core in test_stock_ballistics_scene;
// this host checks the Python contract over a scene without geometry.
int hostSceneQueries = 0;
struct HostScene : public wot::stock_ballistics::Scene
{
	bool collideSegment(std::int32_t, const wot::stock_ballistics::Vec3 &, const wot::stock_ballistics::Vec3 &,
		std::uint16_t, const wot::stock_ballistics::SceneFilter &, wot::stock_ballistics::SceneHit &) override
	{
		++hostSceneQueries;
		return false;
	}
	float collideWater(const wot::stock_ballistics::Vec3 &, const wot::stock_ballistics::Vec3 &) override
	{
		return -1.f;
	}
};
wot::stock_ballistics::Scene & stockBallisticsScene() { static HostScene scene; return scene; }
}

#include "stock_ballistics_simulator.cpp"

static PyObject * hostSetTime(PyObject *, PyObject * args)
{
	if (!PyArg_ParseTuple(args, "d", &BW::hostTime)) return NULL;
	Py_RETURN_NONE;
}

static PyObject * hostSetCamera(PyObject *, PyObject * args)
{
	if (!PyArg_ParseTuple(args, "fff", &BW::hostCamera.x, &BW::hostCamera.y, &BW::hostCamera.z)) return NULL;
	Py_RETURN_NONE;
}

static PyObject * hostTick(PyObject *, PyObject *)
{
	BW::tickStockBallistics();
	if (PyErr_Occurred()) return NULL;
	Py_RETURN_NONE;
}

static PyObject * hostModel(PyObject *, PyObject *)
{
	return new BW::PyModel();
}

static PyObject * hostState(PyObject *, PyObject *)
{
	const wot::stock_ballistics::Parameters & p = wot::stock_ballistics::parameters();
	return Py_BuildValue("{s:i,s:i,s:i,s:(ffff),s:i,s:i,s:s,s:s,s:s}",
		"deallocations", BW::hostDeallocations, "printed", BW::Script::hostPrintedErrors, "sceneQueries", BW::hostSceneQueries,
		"fixed", p.hidingTime, p.timeAfterDeath, p.autoScaleDistance, p.serverTickLength,
		"spaceID", int(p.spaceID), "autoScaleOff", int(p.autoScaleOff),
		"simulatorType", BW::PyBallisticsSimulatorObject::s_type_.tp_name,
		"motorType", BW::PyProjectileMotor::s_type_.tp_name,
		"salvoType", BW::PySalvoObject::s_type_.tp_name);
}

// Readies the types and installs every module link into its module.
static PyObject * hostInitScripts(PyObject *, PyObject *)
{
	for (PyTypeObject * type : BW::hostTypes())
		if (PyType_Ready(type) < 0) return NULL;
	for (const BW::HostLink & link : BW::hostLinks())
	{
		PyObject * module = PyImport_AddModule(link.module);
		PyMethodDef * def = new PyMethodDef{link.name, link.function, METH_VARARGS, NULL};
		PyObject * function = PyCFunction_NewEx(def, NULL, NULL);
		const int failed = module == NULL || function == NULL ||
			PyObject_SetAttrString(module, link.name, function) != 0;
		Py_XDECREF(function);
		if (failed) return NULL;
	}
	return PyInt_FromSize_t(BW::hostLinks().size());
}

static PyMethodDef hostMethods[] = {
	{"setTime", hostSetTime, METH_VARARGS, NULL},
	{"setCamera", hostSetCamera, METH_VARARGS, NULL},
	{"tick", hostTick, METH_NOARGS, NULL},
	{"model", hostModel, METH_NOARGS, NULL},
	{"state", hostState, METH_NOARGS, NULL},
	{"initScripts", hostInitScripts, METH_NOARGS, NULL},
	{NULL, NULL, 0, NULL},
};

int main(int argc, char ** argv)
{
	if (argc < 2) return 2;
	Py_Initialize();
	PySys_SetArgv(argc - 1, argv + 1);
	Py_InitModule("_ballistics_host", hostMethods);
	FILE * script = std::fopen(argv[1], "r");
	if (!script) return 2;
	const int failed = PyRun_SimpleFileEx(script, argv[1], 1);
	Py_Finalize();
	return failed ? 1 : 0;
}
