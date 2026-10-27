// Real CPython 2.7 runs the public Script::setData(float/bool) and
// argCountError bodies with the production collider converters and the
// battle cameras' setDynamicColliders/addVolumeGroup wrappers. Matrix
// providers, Vector2 and geometry links are fixtures with the engine texts.
#include <Python.h>
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

class PyObjectPtr
{
public:
	enum StealReference { STEAL_REFERENCE };
	PyObjectPtr() : object_(NULL) {}
	PyObjectPtr(PyObject * object, StealReference) : object_(object) {}
	PyObjectPtr(const PyObjectPtr & other) : object_(other.object_) { Py_XINCREF(object_); }
	PyObjectPtr & operator=(const PyObjectPtr & other)
	{
		Py_XINCREF(other.object_);
		Py_XDECREF(object_);
		object_ = other.object_;
		return *this;
	}
	~PyObjectPtr() { Py_XDECREF(object_); }
	PyObject * get() const { return object_; }
	explicit operator bool() const { return object_ != NULL; }
private:
	PyObject * object_;
};
typedef PyObjectPtr MatrixProviderPtr;

struct Vector2 { float x, y; Vector2(float a = 0.f, float b = 0.f) : x(a), y(b) {} };
struct Vector3 { float x, y, z; Vector3(float a = 0.f, float b = 0.f, float c = 0.f) : x(a), y(b), z(c) {} };

namespace Script {
template <class T> struct IsValidArgType { static const bool isValid = true; };
int setData(PyObject * pObject, bool & rBool, const char * varName = "");
int setData(PyObject * pObject, float & rFloat, const char * varName = "");
int setData(PyObject * pObject, double & rDouble, const char * varName = "");
int setData(PyObject * pObject, Vector2 & rVector, const char * varName = "");
int setData(PyObject * pObject, MatrixProviderPtr & rProvider, const char * varName = "");
PyObject * argCountError(const char * fn, int optas, int allas, ...);
}

@SCRIPT_BODIES@

PyObject * hostProviderType = NULL;
PyObject * hostLinkType = NULL;
PyObject * hostVector2Type = NULL;

// Engine Vector2 conversion: a Vector2 or a tuple of 2 floats.
int Script::setData(PyObject * pObject, Vector2 & rVector, const char * varName)
{
	if (hostVector2Type && PyObject_IsInstance(pObject, hostVector2Type) == 1)
	{
		PyObjectPtr x(PyObject_GetAttrString(pObject, "x"), PyObjectPtr::STEAL_REFERENCE);
		PyObjectPtr y(PyObject_GetAttrString(pObject, "y"), PyObjectPtr::STEAL_REFERENCE);
		if (x && y)
		{
			rVector = Vector2(float(PyFloat_AsDouble(x.get())), float(PyFloat_AsDouble(y.get())));
			return 0;
		}
	}
	PyErr_Clear();
	if (PyTuple_Check(pObject) && PyArg_ParseTuple(pObject, "ff", &rVector.x, &rVector.y))
		return 0;
	PyErr_Clear();
	PyErr_Format(PyExc_TypeError, "%s must be set to a Vector2 or a tuple of 2 floats", varName);
	return -1;
}

// Engine MatrixProviderPtr conversion (#1513 0069ef50).
int Script::setData(PyObject * pObject, MatrixProviderPtr & rProvider, const char * varName)
{
	if (pObject == Py_None) { rProvider = MatrixProviderPtr(); return 0; }
	if (hostProviderType && PyObject_IsInstance(pObject, hostProviderType) == 1)
	{
		Py_INCREF(pObject);
		rProvider = MatrixProviderPtr(pObject, MatrixProviderPtr::STEAL_REFERENCE);
		return 0;
	}
	PyErr_Format(PyExc_TypeError, "%s must be set to a MatrixProvider or None", varName);
	return -1;
}

class StockVehicleStickerGeometry
{
public:
	explicit StockVehicleStickerGeometry(PyObject * link) : link(link) {}
	PyObject * link;
};
std::shared_ptr<StockVehicleStickerGeometry> stockVehicleStickerGeometry(PyObject * link)
{
	if (!hostLinkType || PyObject_IsInstance(link, hostLinkType) != 1)
		return std::shared_ptr<StockVehicleStickerGeometry>();
	return std::make_shared<StockVehicleStickerGeometry>(link);
}
}

#include "stock_camera_collider.hpp"

namespace BW {
@CONVERTERS@

// Host collider: records what the production wrappers install.
std::vector<StockColliderVolumeGroup> hostGroups;
std::vector<StockCameraDynamicCollider> hostColliders;
int hostReplacements = 0;
void StockAdvancedCameraCollider::addVolumeGroup(const StockColliderVolumeGroup & group)
{
	hostGroups.push_back(group);
}
void StockCameraCollider::dynamicColliders(BW::vector<StockCameraDynamicCollider> & colliders)
{
	++hostReplacements;
	hostColliders.swap(colliders);
}
StockCameraCollider hostCollider(true);

@WRAPPERS@
}

static PyObject * hostSetDynamicColliders(PyObject *, PyObject * args)
{
	return BW::setDynamicCollidersFrom(BW::hostCollider, args);
}

static PyObject * hostAddVolumeGroup(PyObject *, PyObject * args)
{
	return BW::addVolumeGroupFrom(BW::hostCollider, args);
}

static PyObject * hostSetTypes(PyObject *, PyObject * args)
{
	if (!PyArg_ParseTuple(args, "OOO", &BW::hostProviderType, &BW::hostLinkType, &BW::hostVector2Type))
		return NULL;
	Py_INCREF(BW::hostProviderType); Py_INCREF(BW::hostLinkType); Py_INCREF(BW::hostVector2Type);
	Py_RETURN_NONE;
}

static PyObject * hostState(PyObject *, PyObject *)
{
	PyObject * groups = PyList_New(0);
	for (const BW::StockColliderVolumeGroup & g : BW::hostGroups)
	{
		PyObject * item = Py_BuildValue("(ffffffffi)", g.minVolume, g.lowSpeedLimit,
			g.vehicleVisibilityLimit, g.approachSpeed.x, g.approachSpeed.y, g.cameraSpeedFactor,
			g.criticalDistanceSq, 0.f, int(g.canSkip));
		PyList_Append(groups, item);
		Py_DECREF(item);
	}
	PyObject * colliders = PyList_New(0);
	for (const BW::StockCameraDynamicCollider & c : BW::hostColliders)
	{
		PyObject * item = Py_BuildValue("(OOO)", c.node ? c.node.get() : Py_None,
			c.bounds ? c.bounds.get() : Py_None, c.geometry ? c.geometry->link : Py_None);
		PyList_Append(colliders, item);
		Py_DECREF(item);
	}
	PyObject * result = Py_BuildValue("{s:N,s:N,s:i}", "groups", groups, "colliders", colliders,
		"replacements", BW::hostReplacements);
	return result;
}

static PyMethodDef hostMethods[] = {
	{"setDynamicColliders", hostSetDynamicColliders, METH_VARARGS, NULL},
	{"addVolumeGroup", hostAddVolumeGroup, METH_VARARGS, NULL},
	{"setTypes", hostSetTypes, METH_VARARGS, NULL},
	{"state", hostState, METH_NOARGS, NULL},
	{NULL, NULL, 0, NULL},
};

int main(int argc, char ** argv)
{
	if (argc < 2) return 2;
	Py_Initialize();
	PySys_SetArgv(argc - 1, argv + 1);
	Py_InitModule("_camera_binding_host", hostMethods);
	FILE * script = std::fopen(argv[1], "r");
	if (!script) return 2;
	const int failed = PyRun_SimpleFileEx(script, argv[1], 1);
	BW::hostColliders.clear();
	Py_Finalize();
	return failed ? 1 : 0;
}
