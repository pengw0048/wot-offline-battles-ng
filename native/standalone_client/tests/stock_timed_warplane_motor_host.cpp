// Real CPython 2.7 executes the production BigWorld.PyTimedWarplaneMotor
// binding and the exact FlockExotic bytecode. Motor and PyModel follow the
// public lib/duplo ownership (addMotor/delMotor/motors, tick rev order);
// App time, Math.Vector3 and D3DX are fixtures.
#include "stock_battle_vehicle_animation_host.hpp"
#include "stock_timed_warplane_motor_data.hpp"

#include <algorithm>
#include <iostream>

namespace BW {
std::vector<std::string> hostErrors;
std::vector<std::string> hostAudio;
const Matrix Matrix::identity = [] { Matrix m; for (int i = 0; i < 4; ++i) m.m[i][i] = 1.f; return m; }();
std::vector<HostFactory> & hostFactories() { static std::vector<HostFactory> v; return v; }
std::vector<PyTypeObject *> & hostTypes() { static std::vector<PyTypeObject *> v; return v; }
PyObject * hostMemberNames(PyObject *, bool) { return PyList_New(0); }
PyTypeObject PyObjectPlus::s_type_ = [] {
    PyTypeObject type = { PyVarObject_HEAD_INIT(&PyType_Type, 0) };
    type.tp_name = "PyObjectPlus";
    type.tp_basicsize = sizeof(PyObjectPlus);
    type.tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE;
    return type;
}();

namespace Script {
// Math.Vector3 is the consumer's arithmetic class, as the client exposes it.
inline PyObject * getData(const Vector3 & v) {
    PyObjectPtr math(PyImport_ImportModule("Math"), PyObjectPtr::STEAL_REFERENCE);
    if (!math) return NULL;
    return PyObject_CallMethod(math.get(), const_cast<char *>("Vector3"), const_cast<char *>("fff"), v.x, v.y, v.z);
}
}

double hostTime = 0.0;
struct App {
    static App & instance() { static App app; return app; }
    double getGameTimeFrameStart() const { return hostTime; }
};

class PyModel;
// Public lib/duplo/motor.hpp interface.
class Motor : public PyObjectPlus {
    Py_Header(Motor, PyObjectPlus)
public:
    explicit Motor(PyTypeObject * type) : PyObjectPlus(type), pOwner_(NULL) {}
    virtual void attach(PyModel * owner) { pOwner_ = owner; }
    virtual void detach() { pOwner_ = NULL; }
    virtual void rev(float dTime) = 0;
    PyModel * pOwner() { return pOwner_; }
    PyObject * pyGet_owner();
    PY_RO_ATTRIBUTE_SET(owner)
protected:
    PyModel * pOwner_;
};
PY_TYPEOBJECT(Motor)
PY_BEGIN_METHODS(Motor)
PY_END_METHODS()
PY_BEGIN_ATTRIBUTES(Motor)
    PY_ATTRIBUTE(owner)
PY_END_ATTRIBUTES()

// Public PyModel motor ownership: addMotor, delMotor, motors and tick.
class PyModel : public PyObjectPlus {
    Py_Header(PyModel, PyObjectPlus)
public:
    PyModel() : PyObjectPlus(&s_type_), world(Matrix::identity) {}
    ~PyModel() { this->clear(); }
    void worldTransform(const Matrix & value) { world = value; ++transforms; }
    void clear() {
        std::vector<Motor *> old;
        old.swap(motors);
        for (Motor * motor : old) { motor->detach(); Py_DECREF(motor); }
    }
    bool add(PyObject * object) {
        if (!Motor::Check(object)) { PyErr_SetString(PyExc_TypeError, "Model.addMotor() expects a Motor"); return false; }
        Motor * motor = static_cast<Motor *>(static_cast<PyObjectPlus *>(object));
        if (motor->pOwner() != NULL) {
            PyErr_SetString(PyExc_ValueError, "Model.addMotor() cannot add a Motor is already attached to a Model");
            return false;
        }
        Py_INCREF(motor); motor->attach(this); motors.push_back(motor);
        return true;
    }
    PY_METHOD_DECLARE(py_addMotor)
    PY_METHOD_DECLARE(py_delMotor)
    PY_METHOD_DECLARE(py_tick)
    PY_METHOD_DECLARE(py_world)
    PyObject * pyGet_motors() {
        PyObject * tuple = PyTuple_New(motors.size());
        for (std::size_t i = 0; i < motors.size(); ++i) { Py_INCREF(motors[i]); PyTuple_SET_ITEM(tuple, i, motors[i]); }
        return tuple;
    }
    int pySet_motors(PyObject * value) {
        if (!PySequence_Check(value)) { PyErr_SetString(PyExc_TypeError, "motors must be a sequence"); return -1; }
        this->clear();
        for (Py_ssize_t i = 0; i < PySequence_Size(value); ++i) {
            PyObjectPtr item(PySequence_GetItem(value, i), PyObjectPtr::STEAL_REFERENCE);
            if (!this->add(item.get())) return -1;
        }
        return 0;
    }
    // PyModel.position writes the world translation; visible is a flag.
    PyObject * pyGet_position() { return Script::getData(Vector3(world.m[3][0], world.m[3][1], world.m[3][2])); }
    int pySet_position(PyObject * value) {
        Vector3 v; if (Script::setData(value, v, "position")) return -1;
        world.m[3][0] = v.x; world.m[3][1] = v.y; world.m[3][2] = v.z; return 0;
    }
    PyObject * pyGet_visible() { return PyBool_FromLong(visible); }
    int pySet_visible(PyObject * value) { visible = PyObject_IsTrue(value) == 1; return 0; }
    // model.action(name) returns an ActionQueuer-like callable; calls are recorded.
    PY_METHOD_DECLARE(py_action)
    PY_METHOD_DECLARE(py_actions)
    Matrix world;
    int transforms = 0;
    bool visible = true;
    std::vector<Motor *> motors;
    PyObjectPtr played;
};
PyObject * Motor::pyGet_owner() {
    PyObject * result = pOwner_ ? static_cast<PyObject *>(pOwner_) : Py_None;
    Py_INCREF(result);
    return result;
}
PyObject * PyModel::py_addMotor(PyObject * args) {
    PyObject * object;
    if (!PyArg_ParseTuple(args, "O", &object) || !this->add(object)) return NULL;
    Py_RETURN_NONE;
}
PyObject * PyModel::py_delMotor(PyObject * args) {
    PyObject * object;
    if (!PyArg_ParseTuple(args, "O", &object)) return NULL;
    for (std::size_t i = 0; i < motors.size(); ++i)
        if (motors[i] == object) {
            Motor * motor = motors[i];
            motors.erase(motors.begin() + i);
            motor->detach(); Py_DECREF(motor);
            Py_RETURN_NONE;
        }
    PyErr_SetString(PyExc_ValueError, "Model.delMotor(): Motor not attached");
    return NULL;
}
// pymodel.cpp:2328: motors rev by index, so a callback may change the list.
PyObject * PyModel::py_tick(PyObject * args) {
    float dTime;
    if (!PyArg_ParseTuple(args, "f", &dTime)) return NULL;
    PyObjectPtr pin(this);
    for (std::size_t i = 0; i < motors.size(); ++i) motors[i]->rev(dTime);
    Py_RETURN_NONE;
}
PyObject * PyModel::py_world(PyObject *) { return Script::getData(world); }
static PyObject * queueAction(PyObject * name, PyObject *) {
    PyObject * list = PyTuple_GET_ITEM(name, 1);
    PyList_Append(list, PyTuple_GET_ITEM(name, 0));
    Py_RETURN_NONE;
}
static PyMethodDef queueDef = {"ActionQueuer", (PyCFunction)queueAction, METH_NOARGS, NULL};
PyObject * PyModel::py_action(PyObject * args) {
    PyObject * name;
    if (!PyArg_ParseTuple(args, "S", &name)) return NULL;
    if (!played) played = PyObjectPtr(PyList_New(0), PyObjectPtr::STEAL_REFERENCE);
    PyObjectPtr bound(Py_BuildValue("(OO)", name, played.get()), PyObjectPtr::STEAL_REFERENCE);
    return PyCFunction_New(&queueDef, bound.get());
}
PyObject * PyModel::py_actions(PyObject *) {
    PyObject * result = played ? played.get() : Py_None;
    Py_INCREF(result); return result;
}
PY_TYPEOBJECT(PyModel)
PY_BEGIN_METHODS(PyModel)
    PY_METHOD(addMotor)
    PY_METHOD(delMotor)
    PY_METHOD(tick)
    PY_METHOD(world)
    PY_METHOD(action)
    PY_METHOD(actions)
PY_END_METHODS()
PY_BEGIN_ATTRIBUTES(PyModel)
    PY_ATTRIBUTE(motors)
    PY_ATTRIBUTE(position)
    PY_ATTRIBUTE(visible)
PY_END_ATTRIBUTES()
}

#include "stock_timed_warplane_motor.cpp"
#include "stock_timed_warplane_motor_reference.hpp"

namespace BW {
const wot::StockWarplaneMath & stockTimedWarplaneMath() { return referenceWarplaneMath(); }

PyObject * newModel(PyObject *, PyObject *) { return new PyModel(); }
PyObject * setTime(PyObject *, PyObject * args) {
    if (!PyArg_ParseTuple(args, "d", &hostTime)) return NULL;
    Py_RETURN_NONE;
}
// BigWorld.time (006529a0) returns float(App::getGameTimeFrameStart()).
PyObject * clientTimeNow(PyObject *, PyObject *) { return PyFloat_FromDouble(float(hostTime)); }
PyMethodDef hostMethods[] = {
    {"time", clientTimeNow, METH_NOARGS, NULL},
    {"Model", newModel, METH_NOARGS, NULL},
    {"setTime", setTime, METH_VARARGS, NULL},
    {NULL, NULL, 0, NULL}};
}

int main(int argc, char ** argv) {
    using namespace BW;
    assert(argc == 2);
    Py_NoSiteFlag = 1;
    Py_Initialize();
    assert(PyType_Ready(&PyObjectPlus::s_type_) == 0);
    for (PyTypeObject * type : hostTypes()) assert(PyType_Ready(type) == 0);
    std::map<std::string, std::vector<PyMethodDef>> modules{{"BigWorld", {}}};
    for (const HostFactory & f : hostFactories())
        modules[f.module].push_back(PyMethodDef{f.name, (PyCFunction)f.function, METH_VARARGS | METH_KEYWORDS, NULL});
    for (auto & module : modules) {
        module.second.push_back(PyMethodDef{NULL, NULL, 0, NULL});
        assert(Py_InitModule(module.first.c_str(), module.second.data()));
    }
    assert(Py_InitModule("Host", hostMethods));
    assert(Py_InitModule("Math", NULL));
    FILE * script = std::fopen(argv[1], "r");
    assert(script);
    const int failed = PyRun_SimpleFileEx(script, argv[1], 1);
    Py_Finalize();
    if (failed) return 1;
    std::cout << "Real CPython 2.7 PyTimedWarplaneMotor and FlockExotic contract passed" << std::endl;
    return 0;
}
