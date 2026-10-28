// Embedded CPython 2.7 for the exact #1513 FlockManager/FlockExotic PYC. The
// BigWorld.WGFlockManager here is a test binding over the production
// stock_flock_field.hpp; it mirrors stock_flock_manager.cpp's method table,
// onTriggerCallback, start/stop, camera space fence and WG-queue tick. Matrix
// providers expose a mutable 'translation' (x, y, z) read at sweep time.
#include <Python.h>
#include "stock_flock_field.hpp"
#include <algorithm>
#include <cstdio>
#include <vector>

namespace {
struct Manager
{
    PyObject_HEAD
    wot::StockFlockField * field;
    wot::StockFlockProviders<PyObject *> * providers;
    PyObject * callback;
};

PyObject * g_bounds = NULL;   // camera space bounds tuple, or NULL
unsigned g_spaceChanges = 0;  // DeprecatedSpaceHelpers::cameraSpaceChanges()
double g_now = 0.0;
unsigned g_frame = 0;
std::vector<Manager *> g_running;

int arity(PyObject * args, const char * name, int count)
{
    if (PyTuple_Size(args) == count) return 0;
    PyErr_Format(PyExc_TypeError, "%s() takes exactly %d argument(s)", name, count);
    return -1;
}

int vector2(PyObject * object, wot::StockFlockPoint & point)
{
    PyObject * x = PyObject_GetAttrString(object, "x");
    PyObject * y = x ? PyObject_GetAttrString(object, "y") : NULL;
    if (y)
    {
        point.x = float(PyFloat_AsDouble(x));
        point.z = float(PyFloat_AsDouble(y));
    }
    Py_XDECREF(x);
    Py_XDECREF(y);
    if (!y || PyErr_Occurred())
    {
        PyErr_SetString(PyExc_TypeError, "Vector2 expected");
        return -1;
    }
    return 0;
}

void forget(Manager * manager)
{
    g_running.erase(std::remove(g_running.begin(), g_running.end(), manager), g_running.end());
}

void stopManager(Manager * self)
{
    self->field->stop();
    for (size_t i = 0; i < self->providers->entries().size(); ++i)
        Py_XDECREF(self->providers->entries()[i].provider);
    self->providers->clear();
    forget(self);
}

bool fence(Manager * self)
{
    if (!self->field->stale(g_spaceChanges)) return false;
    stopManager(self);
    return true;
}

PyObject * vehicleEnter(Manager * self, PyObject * args)
{
    if (arity(args, "vehicleEnter", 1)) return NULL;
    fence(self);
    PyObject * provider = PyTuple_GET_ITEM(args, 0);
    provider = provider == Py_None ? NULL : provider;
    Py_XINCREF(provider);
    self->providers->enter(provider);
    Py_RETURN_NONE;
}

PyObject * vehicleLeave(Manager * self, PyObject * args)
{
    if (arity(args, "vehicleLeave", 1)) return NULL;
    fence(self);
    PyObject * provider = PyTuple_GET_ITEM(args, 0);
    provider = provider == Py_None ? NULL : provider;
    PyObject * erased = NULL;
    if (self->providers->leave(provider, &erased)) Py_XDECREF(erased);
    Py_RETURN_NONE;
}

PyObject * start(Manager * self, PyObject * args)
{
    if (arity(args, "start", 1)) return NULL;
    const double interval = PyFloat_AsDouble(PyTuple_GET_ITEM(args, 0));
    if (PyErr_Occurred()) return NULL;
    fence(self);
    if (self->field->running() || !g_bounds) Py_RETURN_NONE;
    wot::StockFlockBounds box;
    if (!PyArg_ParseTuple(g_bounds, "ffffff", &box.minX, &box.minY, &box.minZ,
            &box.maxX, &box.maxY, &box.maxZ))
        return NULL;
    self->field->start(float(interval), &box, g_spaceChanges, g_now, g_frame);
    if (std::find(g_running.begin(), g_running.end(), self) == g_running.end())
        g_running.push_back(self);
    Py_RETURN_NONE;
}

PyObject * stop(Manager * self, PyObject * args)
{
    if (arity(args, "stop", 0)) return NULL;
    stopManager(self);
    Py_RETURN_NONE;
}

PyObject * addFlock(Manager * self, PyObject * args)
{
    if (arity(args, "addFlock", 5)) return NULL;
    wot::StockFlockPoint position, explosion;
    if (vector2(PyTuple_GET_ITEM(args, 0), position) ||
            vector2(PyTuple_GET_ITEM(args, 3), explosion))
        return NULL;
    const long id = PyInt_AsLong(PyTuple_GET_ITEM(args, 1));
    const double radius = PyFloat_AsDouble(PyTuple_GET_ITEM(args, 2));
    const double respawn = PyFloat_AsDouble(PyTuple_GET_ITEM(args, 4));
    if (PyErr_Occurred()) return NULL;
    fence(self);
    self->field->addFlock(position, int(id), float(radius), explosion, float(respawn));
    Py_RETURN_NONE;
}

PyObject * addActivationPoint(Manager * self, PyObject * args)
{
    if (arity(args, "addActivationPoint", 1)) return NULL;
    wot::StockFlockPoint point;
    if (vector2(PyTuple_GET_ITEM(args, 0), point)) return NULL;
    fence(self);
    self->field->addActivationPoint(point);
    Py_RETURN_NONE;
}

PyObject * getCallback(Manager * self, void *)
{
    PyObject * value = self->callback ? self->callback : Py_None;
    Py_INCREF(value);
    return value;
}

int setCallback(Manager * self, PyObject * value, void *)
{
    PyObject * stored = value == Py_None ? NULL : value;
    Py_XINCREF(stored);
    Py_XDECREF(self->callback);
    self->callback = stored;
    return 0;
}

struct Trigger
{
    Manager * owner;
    void operator()(int id)
    {
        PyObject * callback = owner->callback;
        if (!callback) return;
        Py_INCREF(callback);
        PyObject * result = PyObject_CallFunction(callback, const_cast<char *>("(i)"), id);
        if (!result) PyErr_Print();
        Py_XDECREF(result);
        Py_DECREF(callback);
        fence(owner);
    }
};

void sweep(Manager * self, float cameraX, float cameraZ)
{
    self->field->advance();
    const unsigned generation = self->field->generation();
    const wot::StockFlockField::Window window = self->field->window({ cameraX, cameraZ });
    Trigger trigger = { self };
    typedef wot::StockFlockProviders<PyObject *>::Entry Entry;
    std::vector<Entry> providers = self->providers->entries();
    for (size_t i = 0; i < providers.size(); ++i) Py_XINCREF(providers[i].provider);
    bool alive = true;
    for (size_t i = 0; alive && i < providers.size(); ++i)
    {
        if (!self->providers->registered(providers[i].serial) || !providers[i].provider) continue;
        PyObject * t = PyObject_GetAttrString(providers[i].provider, "translation");
        double x = 0.0, y = 0.0, z = 0.0;
        if (!t || !PyArg_ParseTuple(t, "ddd", &x, &y, &z)) { PyErr_Print(); Py_XDECREF(t); continue; }
        Py_DECREF(t);
        alive = self->field->test({ float(x), float(z) }, false, window, generation, trigger);
    }
    for (size_t i = 0; i < providers.size(); ++i) Py_XDECREF(providers[i].provider);
    if (!alive) return;
    const std::vector<wot::StockFlockPoint> points = self->field->takeActivationPoints();
    for (size_t i = 0; i < points.size(); ++i)
        if (!self->field->test(points[i], true, window, generation, trigger)) return;
}

void dealloc(Manager * self)
{
    stopManager(self);
    Py_XDECREF(self->callback);
    delete self->field;
    delete self->providers;
    PyObject_Del(self);
}

PyMethodDef managerMethods[] = {
    { "vehicleEnter", (PyCFunction)vehicleEnter, METH_VARARGS, NULL },
    { "vehicleLeave", (PyCFunction)vehicleLeave, METH_VARARGS, NULL },
    { "start", (PyCFunction)start, METH_VARARGS, NULL },
    { "stop", (PyCFunction)stop, METH_VARARGS, NULL },
    { "addFlock", (PyCFunction)addFlock, METH_VARARGS, NULL },
    { "addActivationPoint", (PyCFunction)addActivationPoint, METH_VARARGS, NULL },
    { NULL, NULL, 0, NULL }
};

PyGetSetDef managerAttributes[] = {
    { const_cast<char *>("onTriggerCallback"), (getter)getCallback, (setter)setCallback, NULL, NULL },
    { NULL, NULL, NULL, NULL, NULL }
};

PyObject * create(PyTypeObject * type, PyObject *, PyObject *);

PyTypeObject managerType = { PyObject_HEAD_INIT(NULL) 0, "WGFlockManager", sizeof(Manager) };

PyObject * create(PyTypeObject *, PyObject *, PyObject *)
{
    // #1513 008a2440 ignores its arguments.
    Manager * self = PyObject_New(Manager, &managerType);
    self->field = new wot::StockFlockField();
    self->providers = new wot::StockFlockProviders<PyObject *>();
    self->callback = NULL;
    return (PyObject *)self;
}

PyObject * tick(PyObject *, PyObject * args)
{
    float cameraX, cameraZ;
    if (!PyArg_ParseTuple(args, "dIff", &g_now, &g_frame, &cameraX, &cameraZ)) return NULL;
    std::vector<Manager *> running = g_running;
    for (size_t i = 0; i < running.size(); ++i) Py_INCREF(running[i]);
    long swept = 0;
    for (size_t i = 0; i < running.size(); ++i)
    {
        Manager * manager = running[i];
        if (fence(manager) || !manager->field->due(g_now, g_frame)) continue;
        const unsigned generation = manager->field->generation();
        sweep(manager, cameraX, cameraZ);
        if (manager->field->generation() == generation) manager->field->reschedule(g_now, g_frame);
        ++swept;
    }
    for (size_t i = 0; i < running.size(); ++i) Py_DECREF(running[i]);
    return PyInt_FromLong(swept);
}

PyObject * setCameraSpace(PyObject *, PyObject * args)
{
    PyObject * bounds;
    if (!PyArg_ParseTuple(args, "O", &bounds)) return NULL;
    // CameraApp::tick assigns every frame; only a different object counts.
    PyObject * next = bounds == Py_None ? NULL : bounds;
    if (next != g_bounds) ++g_spaceChanges;
    Py_XINCREF(next);
    Py_XDECREF(g_bounds);
    g_bounds = next;
    Py_RETURN_NONE;
}

PyMethodDef moduleMethods[] = {
    { "_tick", tick, METH_VARARGS, NULL },
    { "_setCameraSpace", setCameraSpace, METH_VARARGS, NULL },
    { NULL, NULL, 0, NULL }
};
}

int main(int argc, char ** argv)
{
    if (argc != 2) return 2;
    Py_NoSiteFlag = 1;
    Py_DontWriteBytecodeFlag = 1;
    Py_Initialize();
    managerType.tp_flags = Py_TPFLAGS_DEFAULT;
    managerType.tp_dealloc = (destructor)dealloc;
    managerType.tp_methods = managerMethods;
    managerType.tp_getset = managerAttributes;
    managerType.tp_new = create;
    if (PyType_Ready(&managerType) < 0) return 3;
    PyObject * module = Py_InitModule("BigWorld", moduleMethods);
    Py_INCREF(&managerType);
    PyModule_AddObject(module, "WGFlockManager", (PyObject *)&managerType);
    FILE * driver = std::fopen(argv[1], "r");
    if (!driver) return 4;
    const int result = PyRun_SimpleFileEx(driver, argv[1], 1);
    Py_Finalize();
    return result ? 1 : 0;
}
