// Real CPython 2.7 executes the production Svarog wrapper and TerrainMatKindSensor
// sources plus exact #1513 consumer bytecode. Engine owners are fixtures.
#include "stock_vehicle_terrain_sensor_host.hpp"

#include "stock_vehicle_components.cpp"
#include "stock_vehicle_terrain_sensor.cpp"

#include <iostream>
#include <memory>

namespace BW {
std::vector<std::string> hostErrors;
std::vector<std::array<float, 3>> hostTransforms;
std::vector<HostSegment> hostSegments;
ClientSpacePtr DeprecatedSpaceHelpers::camera;
std::vector<HostFactory> & hostFactories() { static std::vector<HostFactory> v; return v; }
std::vector<PyTypeObject *> & hostTypes() { static std::vector<PyTypeObject *> v; return v; }

PyObject * hostMemberNames(PyObject * self, bool methods) {
    PyObject * names = PyList_New(0);
    for (PyTypeObject * type = Py_TYPE(self); type; type = type->tp_base) {
        if (methods) {
            for (PyMethodDef * m = type->tp_methods; m && m->ml_name; ++m)
                PyList_Append(names, PyString_FromString(m->ml_name));
        } else {
            for (PyGetSetDef * g = type->tp_getset; g && g->name; ++g)
                if (std::strcmp(g->name, "__members__") && std::strcmp(g->name, "__methods__"))
                    PyList_Append(names, PyString_FromString(g->name));
        }
    }
    for (Py_ssize_t i = 0; i < PyList_GET_SIZE(names); ++i) Py_DECREF(PyList_GET_ITEM(names, i));
    return names;
}

PyTypeObject PyObjectPlus::s_type_ = [] {
    PyTypeObject type = { PyVarObject_HEAD_INIT(&PyType_Type, 0) };
    type.tp_name = "PyObjectPlus";
    type.tp_basicsize = sizeof(PyObjectPlus);
    type.tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE;
    return type;
}();
PY_TYPEOBJECT(MatrixProvider)
PY_BEGIN_METHODS(MatrixProvider)
PY_END_METHODS()
PY_BEGIN_ATTRIBUTES(MatrixProvider)
PY_END_ATTRIBUTES()

// Math.Vector3 needs only its PyVector identity and `+` for model_assembler.
PyObject * vectorAdd(PyObject * a, PyObject * b) {
    Vector3 left, right;
    if (Script::setData(a, left) || Script::setData(b, right)) {
        PyErr_Clear(); Py_INCREF(Py_NotImplemented); return Py_NotImplemented;
    }
    return new HostVector3(Vector3(left.x + right.x, left.y + right.y, left.z + right.z));
}
PyNumberMethods vectorNumber = [] { PyNumberMethods n = {}; n.nb_add = vectorAdd; return n; }();
namespace PyTypeObjectUtil { template <> PyNumberMethods * asNumber<HostVector3>() { return &vectorNumber; } }
PY_TYPEOBJECT(HostVector3)
PY_BEGIN_METHODS(HostVector3)
PY_END_METHODS()
PY_BEGIN_ATTRIBUTES(HostVector3)
PY_END_ATTRIBUTES()

// A compound node fixture: only the MatrixProvider contract is exercised.
class HostNode : public MatrixProvider {
    Py_Header(HostNode, MatrixProvider)
public:
    HostNode() : MatrixProvider(false, &s_type_) { for (int i = 0; i < 4; ++i) value.m[i][i] = 1.f; }
    void matrix(Matrix & m) const override {
        ++reads; m = value;
        // A reentrant provider may run Python, which can destroy its consumer.
        if (onRead) { PyObjectPtr pin = onRead; PyObject * r = PyObject_CallObject(pin.get(), NULL);
            if (!r) PyErr_Print(); Py_XDECREF(r); }
    }
    Matrix value;
    mutable int reads = 0;
    PyObjectPtr onRead;
};
PY_TYPEOBJECT(HostNode)
PY_BEGIN_METHODS(HostNode)
PY_END_METHODS()
PY_BEGIN_ATTRIBUTES(HostNode)
PY_END_ATTRIBUTES()

class HostLodLink : public PyObjectPlus {
    Py_Header(HostLodLink, PyObjectPlus)
public:
    HostLodLink() : PyObjectPlus(&s_type_) {}
    uint32 priority = 0; float distance = 0.f; bool valid = true; int reads = 0;
};
PY_TYPEOBJECT(HostLodLink)
PY_BEGIN_METHODS(HostLodLink)
PY_END_METHODS()
PY_BEGIN_ATTRIBUTES(HostLodLink)
PY_END_ATTRIBUTES()

bool sampleStockLodLink(PyObject * link, uint32 & priority, float & distance) {
    if (!HostLodLink::Check(link)) return false;
    HostLodLink * lod = static_cast<HostLodLink *>(link);
    ++lod->reads; priority = lod->priority; distance = lod->distance; return lod->valid;
}
}

using namespace BW;

namespace {
std::map<SpaceID, std::unique_ptr<ClientSpace>> spaces;

PyObject * vec(const std::array<float, 3> & v) { return Py_BuildValue("(fff)", v[0], v[1], v[2]); }
PyObject * drain(std::vector<std::string> & v) {
    PyObject * list = PyList_New(0);
    for (auto & s : v) { PyObject * t = PyString_FromString(s.c_str()); PyList_Append(list, t); Py_DECREF(t); }
    v.clear(); return list;
}
float item(PyObject * sequence, Py_ssize_t index) {
    PyObject * value = PySequence_GetItem(sequence, index);
    const float result = value ? float(PyFloat_AsDouble(value)) : 0.f; Py_XDECREF(value); return result;
}

PyObject * vector3(PyObject *, PyObject * args) {
    float x, y, z;
    if (!PyArg_ParseTuple(args, "fff", &x, &y, &z)) return NULL;
    return new HostVector3(Vector3(x, y, z));
}
PyObject * node(PyObject *, PyObject *) { return new HostNode(); }
PyObject * setNode(PyObject *, PyObject * args) {
    PyObject * object, * values, * callback = NULL;
    if (!PyArg_ParseTuple(args, "OO|O", &object, &values, &callback) || !HostNode::Check(object)) return NULL;
    HostNode * target = static_cast<HostNode *>(object);
    for (int i = 0; i < 16; ++i) target->value.m[i / 4][i % 4] = item(values, i);
    target->onRead = callback == Py_None ? NULL : callback;
    Py_RETURN_NONE;
}
PyObject * nodeValue(PyObject *, PyObject * args) {
    PyObject * object; if (!PyArg_ParseTuple(args, "O", &object) || !HostNode::Check(object)) return NULL;
    PyObject * t = PyTuple_New(16);
    for (int i = 0; i < 16; ++i)
        PyTuple_SET_ITEM(t, i, PyFloat_FromDouble(static_cast<HostNode *>(object)->value.m[i / 4][i % 4]));
    return t;
}
PyObject * nodeReads(PyObject *, PyObject * args) {
    PyObject * object; if (!PyArg_ParseTuple(args, "O", &object) || !HostNode::Check(object)) return NULL;
    return PyInt_FromLong(static_cast<HostNode *>(object)->reads);
}
PyObject * lodLink(PyObject *, PyObject *) { return new HostLodLink(); }
PyObject * setLod(PyObject *, PyObject * args) {
    PyObject * object; unsigned priority; float distance; int valid;
    if (!PyArg_ParseTuple(args, "OIfi", &object, &priority, &distance, &valid) || !HostLodLink::Check(object)) return NULL;
    HostLodLink * lod = static_cast<HostLodLink *>(object);
    lod->priority = priority; lod->distance = distance; lod->valid = valid != 0; Py_RETURN_NONE;
}
// setSpace(camera ID or None, registered, [[(flags, distance), ...] per segment])
PyObject * setSpace(PyObject *, PyObject * args) {
    PyObject * camera, * script; int registered;
    if (!PyArg_ParseTuple(args, "OiO", &camera, &registered, &script)) return NULL;
    SpaceManager::instance().spaces.clear(); DeprecatedSpaceHelpers::camera = ClientSpacePtr();
    if (camera == Py_None) Py_RETURN_NONE;
    const SpaceID id = SpaceID(PyInt_AsLong(camera));
    std::unique_ptr<ClientSpace> & space = spaces[id];
    if (!space) space.reset(new ClientSpace());
    space->id_ = id; space->script.clear(); space->cursor = 0;
    for (Py_ssize_t i = 0; i < PySequence_Size(script); ++i) {
        PyObject * segment = PySequence_GetItem(script, i);
        std::vector<HostTriangle> triangles;
        for (Py_ssize_t j = 0; j < PySequence_Size(segment); ++j) {
            PyObject * triangle = PySequence_GetItem(segment, j);
            PyObject * flags = PySequence_GetItem(triangle, 0);
            triangles.push_back(HostTriangle{uint32(PyInt_AsLong(flags)), item(triangle, 1)});
            Py_DECREF(flags); Py_DECREF(triangle);
        }
        space->script.push_back(triangles); Py_DECREF(segment);
    }
    DeprecatedSpaceHelpers::camera = ClientSpacePtr(space.get());
    if (registered) SpaceManager::instance().spaces[id] = space.get();
    Py_RETURN_NONE;
}
PyObject * tick(PyObject *, PyObject * args) {
    float dt; unsigned frame;
    if (!PyArg_ParseTuple(args, "fI", &dt, &frame)) return NULL;
    Moo::rc().frame = frame; tickStockTerrainMatKindSensors(dt);
    if (PyErr_Occurred()) return NULL;
    Py_RETURN_NONE;
}
PyObject * segments(PyObject *, PyObject *) {
    PyObject * list = PyList_New(0);
    for (auto & s : hostSegments) {
        PyObject * results = PyList_New(0);
        for (int r : s.results) { PyObject * v = PyInt_FromLong(r); PyList_Append(results, v); Py_DECREF(v); }
        PyObject * t = Py_BuildValue("(INNN)", s.space, vec(s.start), vec(s.end), results);
        PyList_Append(list, t); Py_DECREF(t);
    }
    hostSegments.clear(); return list;
}
PyObject * transforms(PyObject *, PyObject *) {
    PyObject * list = PyList_New(0);
    for (auto & v : hostTransforms) { PyObject * t = vec(v); PyList_Append(list, t); Py_DECREF(t); }
    hostTransforms.clear(); return list;
}
PyObject * errors(PyObject *, PyObject *) { return drain(hostErrors); }
std::shared_ptr<SensorState> sensorState(PyObject * object) {
    if (!PyTerrainMatKindSensor::Check(object)) { PyErr_SetString(PyExc_TypeError, "TerrainMatKindSensor required"); return nullptr; }
    auto state = std::dynamic_pointer_cast<SensorState>(static_cast<PyTerrainMatKindSensor *>(object)->component());
    if (!state) PyErr_SetString(PyExc_ValueError, "TerrainMatKindSensor has been destroyed");
    return state;
}
// Runs one production 00796410-equivalent update outside the scheduler.
PyObject * update(PyObject *, PyObject * args) {
    PyObject * object; if (!PyArg_ParseTuple(args, "O", &object)) return NULL;
    auto state = sensorState(object); if (!state) return NULL;
    state->activate(); state->update(state->token);
    if (PyErr_Occurred()) return NULL;
    return PyBool_FromLong(state->active);
}
// Exposes the raw vector, including the 0xffffffff pre-fill used by the oracle.
PyObject * kinds(PyObject *, PyObject * args) {
    PyObject * object; if (!PyArg_ParseTuple(args, "O", &object)) return NULL;
    auto state = sensorState(object); if (!state) return NULL;
    PyObject * list = PyList_New(0);
    for (uint32 k : state->matKinds) { PyObject * v = PyLong_FromUnsignedLong(k); PyList_Append(list, v); Py_DECREF(v); }
    return list;
}
PyObject * setKinds(PyObject *, PyObject * args) {
    PyObject * object, * values; if (!PyArg_ParseTuple(args, "OO", &object, &values)) return NULL;
    auto state = sensorState(object); if (!state) return NULL;
    for (Py_ssize_t i = 0; i < PySequence_Size(values) && size_t(i) < state->matKinds.size(); ++i) {
        PyObject * v = PySequence_GetItem(values, i);
        state->matKinds[i] = uint32(PyLong_AsUnsignedLongMask(v)); Py_DECREF(v);
    }
    Py_RETURN_NONE;
}
PyObject * lodReads(PyObject *, PyObject * args) {
    PyObject * object; if (!PyArg_ParseTuple(args, "O", &object) || !HostLodLink::Check(object)) return NULL;
    return PyInt_FromLong(static_cast<HostLodLink *>(object)->reads);
}
PyObject * scheduled(PyObject *, PyObject *) { return PyInt_FromSize_t(schedule.size()); }
PyObject * fini(PyObject *, PyObject *) {
    static_cast<Script::FiniTimeJob &>(terrainSensorScriptShutdown).fini(); Py_RETURN_NONE;
}

PyMethodDef mathMethods[] = {{"Vector3", vector3, METH_VARARGS, NULL}, {NULL, NULL, 0, NULL}};
PyMethodDef hostMethods[] = {
    {"node", node, METH_NOARGS, NULL}, {"setNode", setNode, METH_VARARGS, NULL},
    {"nodeValue", nodeValue, METH_VARARGS, NULL}, {"nodeReads", nodeReads, METH_VARARGS, NULL},
    {"lodLink", lodLink, METH_NOARGS, NULL}, {"setLod", setLod, METH_VARARGS, NULL},
    {"lodReads", lodReads, METH_VARARGS, NULL},
    {"setSpace", setSpace, METH_VARARGS, NULL}, {"tick", tick, METH_VARARGS, NULL},
    {"segments", segments, METH_NOARGS, NULL}, {"transforms", transforms, METH_NOARGS, NULL},
    {"errors", errors, METH_NOARGS, NULL}, {"scheduled", scheduled, METH_NOARGS, NULL},
    {"update", update, METH_VARARGS, NULL}, {"kinds", kinds, METH_VARARGS, NULL},
    {"setKinds", setKinds, METH_VARARGS, NULL}, {"fini", fini, METH_NOARGS, NULL},
    {NULL, NULL, 0, NULL}};
}

int main(int argc, char ** argv) {
    assert(argc == 2);
    Py_NoSiteFlag = 1;
    Py_Initialize();
    assert(PyType_Ready(&PyObjectPlus::s_type_) == 0);
    for (PyTypeObject * type : hostTypes()) assert(PyType_Ready(type) == 0);
    std::map<std::string, std::vector<PyMethodDef>> modules{{"Vehicular", {}}, {"Svarog", {}}};
    for (const HostFactory & f : hostFactories())
        modules[f.module].push_back(PyMethodDef{f.name, (PyCFunction)f.function, METH_VARARGS | METH_KEYWORDS, NULL});
    for (auto & module : modules) {
        module.second.push_back(PyMethodDef{NULL, NULL, 0, NULL});
        assert(Py_InitModule(module.first.c_str(), module.second.data()));
    }
    assert(Py_InitModule("Math", mathMethods) && Py_InitModule("Host", hostMethods));
    FILE * script = std::fopen(argv[1], "r");
    assert(script);
    const int failed = PyRun_SimpleFileEx(script, argv[1], 1);
    Py_Finalize();
    if (failed) return 1;
    std::cout << "Real CPython 2.7 TerrainMatKindSensor wrapper and consumer contract passed" << std::endl;
    return 0;
}
