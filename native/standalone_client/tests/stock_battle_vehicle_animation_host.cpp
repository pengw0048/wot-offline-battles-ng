// Real CPython 2.7 executes the production Svarog wrapper and battle
// animation sources plus exact #1513 producer bytecode. Engine owners
// (compound, audio, ProviderStore, D3DX) are recorded fixtures.
#include "stock_battle_vehicle_animation_host.hpp"

#include "stock_vehicle_components.cpp"
#include "stock_vehicle_visuals.hpp"
namespace BW {
#include "stock_float_link_owner.inc"
}
#include "stock_battle_vehicle_animation.cpp"
#include "stock_battle_vehicle_animation_reference.hpp"

#include <algorithm>
#include <iostream>
#include <memory>

namespace BW {
std::vector<std::string> hostErrors;
std::vector<std::string> hostAudio;
const Matrix Matrix::identity = [] { Matrix m; for (int i = 0; i < 4; ++i) m.m[i][i] = 1.f; return m; }();
std::vector<HostFactory> & hostFactories() { static std::vector<HostFactory> v; return v; }
std::vector<PyTypeObject *> & hostTypes() { static std::vector<PyTypeObject *> v; return v; }

PyObject * hostMemberNames(PyObject * self, bool) {
    PyObject * names = PyList_New(0);
    for (PyTypeObject * type = Py_TYPE(self); type; type = type->tp_base)
        for (PyGetSetDef * g = type->tp_getset; g && g->name; ++g)
            if (std::strcmp(g->name, "__members__")) {
                PyObject * name = PyString_FromString(g->name);
                PyList_Append(names, name);
                Py_DECREF(name);
            }
    return names;
}

PyTypeObject PyObjectPlus::s_type_ = [] {
    PyTypeObject type = { PyVarObject_HEAD_INIT(&PyType_Type, 0) };
    type.tp_name = "PyObjectPlus";
    type.tp_basicsize = sizeof(PyObjectPlus);
    type.tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE;
    return type;
}();

std::vector<MatrixProvider *> providerStore;
MatrixProvider::MatrixProvider(bool autoTick, PyTypeObject * type) : PyObjectPlus(type), autoTick_(autoTick) {
    if (autoTick_) providerStore.push_back(this);
}
MatrixProvider::~MatrixProvider() {
    if (autoTick_) providerStore.erase(std::find(providerStore.begin(), providerStore.end(), this));
}
void tickHostProviderStore(float dTime) {
    const std::vector<MatrixProvider *> snapshot = providerStore;
    for (MatrixProvider * provider : snapshot) provider->tick(dTime);
}
PY_TYPEOBJECT(MatrixProvider)
PY_BEGIN_METHODS(MatrixProvider)
PY_END_METHODS()
PY_BEGIN_ATTRIBUTES(MatrixProvider)
PY_END_ATTRIBUTES()

const wot::StockAnimationMath & stockBattleAnimationMath() { return referenceAnimationMath(); }

StockAudioSource createStockAudioSource(const BW::string & name, bool) {
    hostAudio.push_back("create " + name);
    return StockAudioSource(new StockAudioDetail::Source{name, true});
}
bool playStockAudioSource(const StockAudioSource & s, const BW::string & event) {
    if (!s) return false;
    hostAudio.push_back("play " + s->name + " " + event);
    return true;
}
void stopStockAudioSource(const StockAudioSource & s) { if (s) hostAudio.push_back("stop " + s->name); }
bool setStockAudioSourceMatrix(const StockAudioSource & s, MatrixProviderPtr p) {
    if (!s) return false;
    hostAudio.push_back("matrix " + s->name + (p ? " bound" : " none"));
    return true;
}

// A settable matrix provider (Math.Matrix stand-in).
class HostMatrix : public MatrixProvider {
    Py_Header(HostMatrix, MatrixProvider)
public:
    HostMatrix() : MatrixProvider(false, &s_type_), value(Matrix::identity) {}
    void matrix(Matrix & m) const override { m = value; }
    PyObject * pyGet_value() { return Script::getData(value); }
    int pySet_value(PyObject * o) { return Script::setData(o, value, "value"); }
    Matrix value;
    static PyObject * New(PyObject *, PyObject * args) {
        HostMatrix * result = new HostMatrix();
        PyObject * initial = NULL;
        if (!PyArg_ParseTuple(args, "|O", &initial)) { Py_DECREF(result); return NULL; }
        if (initial && Script::setData(initial, result->value, "Matrix")) { Py_DECREF(result); return NULL; }
        return result;
    }
};
PY_TYPEOBJECT(HostMatrix)
PY_BEGIN_METHODS(HostMatrix)
PY_END_METHODS()
PY_BEGIN_ATTRIBUTES(HostMatrix)
    PY_ATTRIBUTE(value)
PY_END_ATTRIBUTES()

// Compound fixture: node locals, provider sources and the read/write seams.
class HostCompound;
class HostCompoundNode : public MatrixProvider {
    Py_Header(HostCompoundNode, MatrixProvider)
public:
    HostCompoundNode(HostCompound * owner, size_t index);
    void matrix(Matrix & m) const override;
    PyObject * pyGet_localMatrix();
    PY_RO_ATTRIBUTE_SET(localMatrix)
    PyObjectPtr owner;
    size_t index;
};

class HostCompound : public PyObjectPlus {
    Py_Header(HostCompound, PyObjectPlus)
public:
    HostCompound() : PyObjectPlus(&s_type_) {}
    struct Node { std::string name; Matrix local; MatrixProviderPtr source; int writes = 0; };
    std::vector<Node> nodes;
    PyObjectPtr matrixProvider;
    bool active = true;
    int find(const std::string & name) const {
        if (name.empty()) return 0;
        for (size_t i = 0; i < nodes.size(); ++i) if (nodes[i].name == name) return int(i);
        return -1;
    }
    PY_METHOD_DECLARE(py_node)
    PY_METHOD_DECLARE(py_evaluate)
    PY_METHOD_DECLARE(py_local)
    PY_METHOD_DECLARE(py_writes)
    PY_METHOD_DECLARE(py_reset)
    PyObject * pyGet_matrix() { return Script::getData(matrixProvider); }
    int pySet_matrix(PyObject * o) { matrixProvider = o; return 0; }
    static PyObject * New(PyObject *, PyObject * args) {
        PyObject * list;
        if (!PyArg_ParseTuple(args, "O", &list)) return NULL;
        HostCompound * result = new HostCompound();
        for (Py_ssize_t i = 0; i < PySequence_Size(list); ++i) {
            PyObjectPtr item(PySequence_GetItem(list, i), PyObjectPtr::STEAL_REFERENCE);
            Node node;
            PyObject * name; PyObject * local;
            if (!PyArg_ParseTuple(item.get(), "OO", &name, &local) || Script::setData(name, node.name, "name") ||
                Script::setData(local, node.local, "local")) { Py_DECREF(result); return NULL; }
            result->nodes.push_back(node);
        }
        HostMatrix * root = new HostMatrix();
        result->matrixProvider = PyObjectPtr(root, PyObjectPtr::STEAL_REFERENCE);
        return result;
    }
};
HostCompoundNode::HostCompoundNode(HostCompound * o, size_t i) : MatrixProvider(false, &s_type_), owner(o), index(i) {}
void HostCompoundNode::matrix(Matrix & m) const {
    m = static_cast<HostCompound *>(static_cast<PyObjectPlus *>(owner.get()))->nodes[index].local;
}
PyObject * HostCompoundNode::pyGet_localMatrix() {
    HostMatrix * result = new HostMatrix();
    this->matrix(result->value);
    return result;
}
PY_TYPEOBJECT(HostCompoundNode)
PY_BEGIN_METHODS(HostCompoundNode)
PY_END_METHODS()
PY_BEGIN_ATTRIBUTES(HostCompoundNode)
    PY_ATTRIBUTE(localMatrix)
PY_END_ATTRIBUTES()

PyObject * HostCompound::py_node(PyObject * args) {
    const char * name; PyObject * source = NULL;
    if (!PyArg_ParseTuple(args, "s|O", &name, &source)) return NULL;
    const int index = this->find(name);
    if (index < 0) Py_RETURN_NONE;
    if (source && Script::setData(source, nodes[index].source, "source")) return NULL;
    return new HostCompoundNode(this, size_t(index));
}
// Compound evaluation writes each provider into its node local (008e3f00).
PyObject * HostCompound::py_evaluate(PyObject *) {
    for (Node & node : nodes) if (node.source) node.source->matrix(node.local);
    Py_RETURN_NONE;
}
PyObject * HostCompound::py_local(PyObject * args) {
    const char * name;
    if (!PyArg_ParseTuple(args, "s", &name) || this->find(name) < 0) return NULL;
    return Script::getData(nodes[this->find(name)].local);
}
PyObject * HostCompound::py_writes(PyObject * args) {
    const char * name;
    if (!PyArg_ParseTuple(args, "s", &name) || this->find(name) < 0) return NULL;
    return PyInt_FromLong(nodes[this->find(name)].writes);
}
PyObject * HostCompound::py_reset(PyObject *) { active = false; Py_RETURN_NONE; }
PY_TYPEOBJECT(HostCompound)
PY_BEGIN_METHODS(HostCompound)
    PY_METHOD(node)
    PY_METHOD(evaluate)
    PY_METHOD(local)
    PY_METHOD(writes)
    PY_METHOD(reset)
PY_END_METHODS()
PY_BEGIN_ATTRIBUTES(HostCompound)
    PY_ATTRIBUTE(matrix)
PY_END_ATTRIBUTES()

bool stockVehicleCompoundNodeLocal(PyObject * compound, const BW::string & name, Matrix & local) {
    if (!HostCompound::Check(compound)) return false;
    HostCompound * c = static_cast<HostCompound *>(static_cast<PyObjectPlus *>(compound));
    const int index = c->find(name);
    if (!c->active || index < 0) return false;
    local = c->nodes[index].local;
    return true;
}
bool writeStockVehicleCompoundNodeLocal(PyObject * compound, const BW::string & name, const Matrix & local) {
    if (!HostCompound::Check(compound)) return false;
    HostCompound * c = static_cast<HostCompound *>(static_cast<PyObjectPlus *>(compound));
    const int index = c->find(name);
    if (!c->active || index < 0) return false;
    c->nodes[index].local = local;
    ++c->nodes[index].writes;
    return true;
}

PyObject * floatLink(PyObject *, PyObject * args) {
    PyObject * owner; const char * attribute;
    if (!PyArg_ParseTuple(args, "Os", &owner, &attribute)) return NULL;
    PyObjectPtr pin(owner);
    std::string name(attribute);
    return makeStockFloatLink([pin, name](float & value) {
        PyObjectPtr result(PyObject_GetAttrString(pin.get(), name.c_str()), PyObjectPtr::STEAL_REFERENCE);
        if (!result) { PyErr_Clear(); return false; }
        value = float(PyFloat_AsDouble(result.get()));
        return true;
    });
}
PyObject * drain(std::vector<std::string> & values) {
    PyObject * list = PyList_New(0);
    for (const std::string & v : values) { PyObject * s = PyString_FromString(v.c_str()); PyList_Append(list, s); Py_DECREF(s); }
    values.clear();
    return list;
}
PyObject * hostErrorsList(PyObject *, PyObject *) { return drain(hostErrors); }
PyObject * hostAudioList(PyObject *, PyObject *) { return drain(hostAudio); }
PyObject * tickProviders(PyObject *, PyObject * args) {
    float dt; if (!PyArg_ParseTuple(args, "f", &dt)) return NULL;
    tickHostProviderStore(dt); Py_RETURN_NONE;
}
PyObject * tickLinked(PyObject *, PyObject *) { tickStockLinkedNodesPitchAnimators(); Py_RETURN_NONE; }
PyObject * tickSounds(PyObject *, PyObject * args) {
    float dt; if (!PyArg_ParseTuple(args, "f", &dt)) return NULL;
    tickStockSuspensionSounds(dt); Py_RETURN_NONE;
}
PyObject * providerCount(PyObject *, PyObject *) { return PyInt_FromLong(long(providerStore.size())); }
PyObject * outputOf(PyObject *, PyObject * args) {
    PyObject * provider;
    if (!PyArg_ParseTuple(args, "O", &provider) || !MatrixProvider::Check(provider)) return NULL;
    Matrix m; static_cast<MatrixProvider *>(static_cast<PyObjectPlus *>(provider))->matrix(m);
    return Script::getData(m);
}
PyMethodDef hostMethods[] = {
    {"Matrix", (PyCFunction)HostMatrix::New, METH_VARARGS, NULL},
    {"Compound", (PyCFunction)HostCompound::New, METH_VARARGS, NULL},
    {"floatLink", floatLink, METH_VARARGS, NULL},
    {"errors", hostErrorsList, METH_NOARGS, NULL},
    {"audio", hostAudioList, METH_NOARGS, NULL},
    {"tickProviders", tickProviders, METH_VARARGS, NULL},
    {"tickLinked", tickLinked, METH_NOARGS, NULL},
    {"tickSounds", tickSounds, METH_VARARGS, NULL},
    {"providerCount", providerCount, METH_NOARGS, NULL},
    {"output", outputOf, METH_VARARGS, NULL},
    {NULL, NULL, 0, NULL}};
}

int main(int argc, char ** argv) {
    using namespace BW;
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
    assert(Py_InitModule("Host", hostMethods));
    FILE * script = std::fopen(argv[1], "r");
    assert(script);
    const int failed = PyRun_SimpleFileEx(script, argv[1], 1);
    Py_Finalize();
    if (failed) return 1;
    std::cout << "Real CPython 2.7 battle animation producers and lifecycle passed" << std::endl;
    return 0;
}
