// Real CPython 2.7 executes the production Svarog wrapper and WaterSensor
// sources plus exact #1513 consumer bytecode. Engine owners are fixtures.
#include "stock_vehicle_water_sensor_host.hpp"

#include "stock_vehicle_components.cpp"
#include "stock_vehicle_water_sensor.cpp"

#include <iostream>
#include <memory>

namespace BW {
std::vector<std::string> hostErrors;
std::vector<std::array<float, 3>> hostTransforms;
std::vector<HostSegment> hostSegments;
std::vector<HostWaterQuery> hostWaterQueries;
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
    // PyList_Append retained every name; drop the creation references.
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

// A compound node fixture: only the MatrixProvider contract is exercised.
class HostNode : public MatrixProvider {
    Py_Header(HostNode, MatrixProvider)
public:
    HostNode() : MatrixProvider(false, &s_type_) { for (int i = 0; i < 4; ++i) value.m[i][i] = 1.f; }
    void matrix(Matrix & m) const override { ++reads; m = value; }
    Matrix value;
    mutable int reads = 0;
};
PY_TYPEOBJECT(HostNode)
PY_BEGIN_METHODS(HostNode)
PY_END_METHODS()
PY_BEGIN_ATTRIBUTES(HostNode)
PY_END_ATTRIBUTES()

// The actual production float-link owner/make/copy/sample code is compiled
// from its source slice; only the native filter-field producer is a fixture.
#include "stock_float_link_owner.inc"
static int nativeFloatReads = 0;

class HostBoolLink : public PyObjectPlus {
    Py_Header(HostBoolLink, PyObjectPlus)
public:
    explicit HostBoolLink(std::function<bool()> r) : PyObjectPlus(&s_type_), reader(r) {}
    PY_METHOD_DECLARE(pyCall)
    std::function<bool()> reader;
};
PyObject * HostBoolLink::pyCall(PyObject *) { return PyBool_FromLong(reader()); }
PY_TYPEOBJECT_SPECIALISE_CALL(HostBoolLink, &HostBoolLink::_pyCall)
PY_TYPEOBJECT(HostBoolLink)
PY_BEGIN_METHODS(HostBoolLink)
PY_END_METHODS()
PY_BEGIN_ATTRIBUTES(HostBoolLink)
PY_END_ATTRIBUTES()

class HostLodLink : public PyObjectPlus {
    Py_Header(HostLodLink, PyObjectPlus)
public:
    HostLodLink() : PyObjectPlus(&s_type_) {}
    uint32 priority = 0; float distance = 0.f; bool valid = true;
};
PY_TYPEOBJECT(HostLodLink)
PY_BEGIN_METHODS(HostLodLink)
PY_END_METHODS()
PY_BEGIN_ATTRIBUTES(HostLodLink)
PY_END_ATTRIBUTES()

bool sampleStockLodLink(PyObject * link, uint32 & priority, float & distance) {
    if (!HostLodLink::Check(link)) return false;
    HostLodLink * lod = static_cast<HostLodLink *>(link);
    priority = lod->priority; distance = lod->distance; return lod->valid;
}
}

using namespace BW;

namespace {
std::vector<std::unique_ptr<Water>> waters;
std::map<SpaceID, std::unique_ptr<ClientSpace>> spaces;

PyObject * vec(const std::array<float, 3> & v) { return Py_BuildValue("(fff)", v[0], v[1], v[2]); }
PyObject * drain(std::vector<std::string> & v) {
    PyObject * list = PyList_New(0);
    for (auto & s : v) { PyObject * t = PyString_FromString(s.c_str()); PyList_Append(list, t); Py_DECREF(t); }
    v.clear(); return list;
}

PyObject * createFloatLink(PyObject *, PyObject * args) {
    PyObject * owner; const char * name = "";
    if (!PyArg_ParseTuple(args, "O|s:createFloatLink", &owner, &name)) return NULL;
    PyObjectPtr pin=owner;
    const std::string field=name;
    return makeStockFloatLink([pin,field](float &value) {
        ++nativeFloatReads;
        PyObject * result=PyObject_GetAttrString(pin.get(),field.c_str());
        if (!result) return false;
        const bool valid=Script::setData(result,value,field.c_str())==0;
        Py_DECREF(result);return valid;
    });
}
// The proposed stock_vehicle_visuals branch: native bool properties only.
PyObject * createBoolLink(PyObject *, PyObject * args) {
    PyObject * owner; const char * name = "";
    if (!PyArg_ParseTuple(args, "O|s:createBoolLink", &owner, &name)) return NULL;
    std::function<bool()> reader;
    if (stockWaterSensorBoolLink(owner, name, reader)) return new HostBoolLink(reader);
    Py_RETURN_NONE;
}
PyObject * node(PyObject *, PyObject *) { return new HostNode(); }
PyObject * setNode(PyObject *, PyObject * args) {
    PyObject * object, * values;
    if (!PyArg_ParseTuple(args, "OO", &object, &values) || !HostNode::Check(object)) return NULL;
    for (int i = 0; i < 16; ++i) {
        PyObject * item = PySequence_GetItem(values, i); if (!item) return NULL;
        static_cast<HostNode *>(object)->value.m[i / 4][i % 4] = float(PyFloat_AsDouble(item)); Py_DECREF(item);
    }
    Py_RETURN_NONE;
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
PyObject * setWaters(PyObject *, PyObject * args) {
    PyObject * values; if (!PyArg_ParseTuple(args, "O", &values)) return NULL;
    Waters::instance().clear(); waters.clear();
    for (Py_ssize_t i = 0; i < PySequence_Size(values); ++i) {
        PyObject * item = PySequence_GetItem(values, i);
        const float height = float(PyFloat_AsDouble(item)); Py_DECREF(item);
        waters.emplace_back(new Water());
        waters.back()->query = [height](const Vector3 &, const Vector3 &) { return height; };
        Waters::instance().push_back(waters.back().get());
    }
    Py_RETURN_NONE;
}
PyObject * setSpace(PyObject *, PyObject * args) {
    PyObject * camera, * triangles; int registered;
    if (!PyArg_ParseTuple(args, "OiO", &camera, &registered, &triangles)) return NULL;
    SpaceManager::instance().spaces.clear(); DeprecatedSpaceHelpers::camera = ClientSpacePtr();
    if (camera == Py_None) Py_RETURN_NONE;
    const SpaceID id = SpaceID(PyInt_AsLong(camera));
    std::unique_ptr<ClientSpace> & space = spaces[id];
    if (!space) space.reset(new ClientSpace());
    space->id_ = id; space->triangles.clear();
    for (Py_ssize_t i = 0; i < PySequence_Size(triangles); ++i) {
        PyObject * item = PySequence_GetItem(triangles, i);
        space->triangles.push_back(uint32(PyInt_AsLong(item))); Py_DECREF(item);
    }
    DeprecatedSpaceHelpers::camera = ClientSpacePtr(space.get());
    if (registered) SpaceManager::instance().spaces[id] = space.get();
    Py_RETURN_NONE;
}
PyObject * tick(PyObject *, PyObject * args) {
    float dt; unsigned frame;
    if (!PyArg_ParseTuple(args, "fI", &dt, &frame)) return NULL;
    Moo::rc().frame = frame; tickStockWaterSensors(dt);
    if (PyErr_Occurred()) return NULL;
    Py_RETURN_NONE;
}
PyObject * waterQueries(PyObject *, PyObject *) {
    PyObject * list = PyList_New(0);
    for (auto & q : hostWaterQueries) {
        PyObject * t = Py_BuildValue("(NN)", vec(q.start), vec(q.end)); PyList_Append(list, t); Py_DECREF(t);
    }
    hostWaterQueries.clear(); return list;
}
PyObject * segments(PyObject *, PyObject *) {
    PyObject * list = PyList_New(0);
    for (auto & s : hostSegments) {
        PyObject * t = Py_BuildValue("(INN)", s.space, vec(s.start), vec(s.end)); PyList_Append(list, t); Py_DECREF(t);
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
    if (!PyWaterSensor::Check(object)) { PyErr_SetString(PyExc_TypeError, "WaterSensor required"); return nullptr; }
    auto state = std::dynamic_pointer_cast<SensorState>(static_cast<PyWaterSensor *>(object)->component());
    if (!state) PyErr_SetString(PyExc_ValueError, "WaterSensor has been destroyed");
    return state;
}
// Runs one production 00794cb0-equivalent update outside the scheduler.
PyObject * update(PyObject *, PyObject * args) {
    PyObject * object; if (!PyArg_ParseTuple(args, "O", &object)) return NULL;
    auto state = sensorState(object); if (!state) return NULL;
    state->activate(); state->update(state->token);
    if (PyErr_Occurred()) return NULL;
    return PyBool_FromLong(state->active);
}
PyObject * history(PyObject *, PyObject * args) {
    PyObject * object; if (!PyArg_ParseTuple(args, "O", &object)) return NULL;
    auto state = sensorState(object); if (!state) return NULL;
    std::uint32_t bits; std::memcpy(&bits, &state->data.waterHeight, 4);
    return Py_BuildValue("(iiiI)", state->data.splash, state->data.inWater, state->data.underWater, bits);
}
PyObject * setHistory(PyObject *, PyObject * args) {
    PyObject * object; int splash, inWater, underWater; unsigned bits;
    if (!PyArg_ParseTuple(args, "OiiiI", &object, &splash, &inWater, &underWater, &bits)) return NULL;
    auto state = sensorState(object); if (!state) return NULL;
    state->data.splash = uint8(splash); state->data.inWater = uint8(inWater); state->data.underWater = uint8(underWater);
    std::memcpy(&state->data.waterHeight, &bits, 4);
    Py_RETURN_NONE;
}
PyObject * linkReads(PyObject *, PyObject * args) {
    PyObject * object; if (!PyArg_ParseTuple(args, "O", &object) || !isStockFloatLink(object)) return NULL;
    return PyInt_FromLong(nativeFloatReads);
}
PyObject * scheduled(PyObject *, PyObject *) { return PyInt_FromSize_t(schedule.size()); }

PyMethodDef dataLinks[] = {
    {"createFloatLink", createFloatLink, METH_VARARGS, NULL},
    {"createBoolLink", createBoolLink, METH_VARARGS, NULL},
    {NULL, NULL, 0, NULL}};
PyMethodDef hostMethods[] = {
    {"node", node, METH_NOARGS, NULL}, {"setNode", setNode, METH_VARARGS, NULL},
    {"nodeReads", nodeReads, METH_VARARGS, NULL},
    {"lodLink", lodLink, METH_NOARGS, NULL}, {"setLod", setLod, METH_VARARGS, NULL},
    {"setWaters", setWaters, METH_VARARGS, NULL}, {"setSpace", setSpace, METH_VARARGS, NULL},
    {"tick", tick, METH_VARARGS, NULL}, {"waterQueries", waterQueries, METH_NOARGS, NULL},
    {"segments", segments, METH_NOARGS, NULL}, {"transforms", transforms, METH_NOARGS, NULL},
    {"errors", errors, METH_NOARGS, NULL}, {"scheduled", scheduled, METH_NOARGS, NULL},
    {"update", update, METH_VARARGS, NULL}, {"history", history, METH_VARARGS, NULL},
    {"setHistory", setHistory, METH_VARARGS, NULL}, {"linkReads", linkReads, METH_VARARGS, NULL},
    {NULL, NULL, 0, NULL}};
}

int main(int argc, char ** argv) {
    assert(argc == 2);
    Py_NoSiteFlag = 1;
    Py_Initialize();
    assert(PyType_Ready(&PyObjectPlus::s_type_) == 0);
    for (PyTypeObject * type : hostTypes()) assert(PyType_Ready(type) == 0);
    // The client always has these modules; a missing type is an AttributeError.
    std::map<std::string, std::vector<PyMethodDef>> modules{{"Vehicular", {}}, {"Svarog", {}}};
    for (const HostFactory & f : hostFactories())
        modules[f.module].push_back(PyMethodDef{f.name, (PyCFunction)f.function, METH_VARARGS | METH_KEYWORDS, NULL});
    for (auto & module : modules) {
        module.second.push_back(PyMethodDef{NULL, NULL, 0, NULL});
        assert(Py_InitModule(module.first.c_str(), module.second.data()));
    }
    assert(Py_InitModule("DataLinks", dataLinks) && Py_InitModule("Host", hostMethods));
    FILE * script = std::fopen(argv[1], "r");
    assert(script);
    const int failed = PyRun_SimpleFileEx(script, argv[1], 1);
    Py_Finalize();
    if (failed) return 1;
    std::cout << "Real CPython 2.7 WaterSensor wrapper and consumer contract passed" << std::endl;
    return 0;
}
