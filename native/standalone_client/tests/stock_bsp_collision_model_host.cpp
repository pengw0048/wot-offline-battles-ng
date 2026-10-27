// Real CPython 2.7 runs the production WGBspCollisionModel binding over the
// host physics2 transcription and a Python reader of exact #1513 resources.
#include <cstdarg>
#include "stock_bsp_collision_model_host.hpp"
#include "stock_bsp_collision_model.cpp"

#include <cassert>
#include <iostream>

std::vector<HostCall> hostCalls;
HostResults hostResults;

namespace BW {
std::vector<HostFactory> & hostFactories() { static std::vector<HostFactory> v; return v; }
std::vector<PyTypeObject *> & hostTypes() { static std::vector<PyTypeObject *> v; return v; }
PyObject * hostMemberNames(PyObject * self, bool methods) {
    PyObject * names = PyList_New(0);
    for (PyTypeObject * type = Py_TYPE(self); type && type != &PyBaseObject_Type; type = type->tp_base)
        if (methods)
            for (PyMethodDef * m = type->tp_methods; m && m->ml_name; ++m) {
                PyObject * n = PyString_FromString(m->ml_name); PyList_Append(names, n); Py_DECREF(n);
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

PyMethodDef * PyModel::s_getMethodDefs() { static PyMethodDef defs[] = {{NULL, NULL, 0, NULL}}; return defs; }
PyGetSetDef * PyModel::s_getAttributeDefs() {
    static PyGetSetDef defs[] = {{(char *)"matrix", PyModel::getMatrix, PyModel::setMatrix, NULL, NULL},
                                 {(char *)"sources", PyModel::getSources, NULL, NULL, NULL},
                                 {NULL, NULL, NULL, NULL, NULL}};
    return defs;
}
PyTypeObject PyModel::s_type_ = BW::hostType<PyModel>("BigWorld.Model", &PyObjectPlus::s_type_);

PyObject * & hostModelLoader() { static PyObject * loader = NULL; return loader; }
std::map<std::string, Model *> & hostModels() { static std::map<std::string, Model *> models; return models; }

ModelPtr Model::get(const std::string & name) {
    auto found = hostModels().find(name);
    if (found != hostModels().end()) return ModelPtr(found->second);
    PyObject * data = PyObject_CallFunction(hostModelLoader(), (char *)"s", name.c_str());
    if (!data) { PyErr_Print(); std::abort(); }
    if (data == Py_None) { Py_DECREF(data); return ModelPtr(); }
    PyObject * low, * high, * bsp, * flags;
    float a[3], b[3];
    if (!PyArg_ParseTuple(data, "OOOO", &low, &high, &bsp, &flags)) std::abort();
    for (int i = 0; i < 3; ++i) {
        a[i] = float(PyFloat_AsDouble(PySequence_GetItem(low, i)));
        b[i] = float(PyFloat_AsDouble(PySequence_GetItem(high, i)));
    }
    BSPTree * tree = NULL;
    if (bsp != Py_None) {
        tree = BSPTree::load(std::string(PyString_AsString(bsp), size_t(PyString_Size(bsp))));
        std::vector<WorldTriangle::Flags> map;
        for (Py_ssize_t i = 0; i < PyList_Size(flags); ++i)
            map.push_back(WorldTriangle::Flags(PyInt_AsLong(PyList_GET_ITEM(flags, i))));
        tree->remapFlags(map);
    }
    Py_DECREF(data);
    Model * model = new Model(name, BoundingBox(Vector3(a[0], a[1], a[2]), Vector3(b[0], b[1], b[2])), tree);
    hostModels()[name] = model;
    return ModelPtr(model);
}

PyObject * hostMathVector3(const Vector3 & v) {
    PyObject * math = PyImport_ImportModule("Math");
    if (!math) return NULL;
    PyObject * result = PyObject_CallMethod(math, (char *)"Vector3", (char *)"fff", double(v.x), double(v.y),
                                            double(v.z));
    Py_DECREF(math);
    return result;
}
}

namespace {
PyObject * setLoader(PyObject *, PyObject * args) {
    PyObject * loader;
    if (!PyArg_ParseTuple(args, "O", &loader)) return NULL;
    Py_XDECREF(hostModelLoader());
    Py_INCREF(loader);
    hostModelLoader() = loader;
    Py_RETURN_NONE;
}
PyObject * liveModels(PyObject *, PyObject *) {
    PyObject * result = PyDict_New();
    for (const auto & entry : hostModels()) {
        PyObject * refs = PyInt_FromLong(entry.second->refs_);
        PyDict_SetItemString(result, entry.first.c_str(), refs);
        Py_DECREF(refs);
    }
    return result;
}
// The central seam other native owners use; reports the shared tree identity.
PyObject * nativeOwner(PyObject *, PyObject * args) {
    PyObject * object;
    if (!PyArg_ParseTuple(args, "O", &object)) return NULL;
    const BSPTree * tree = BW::stockBspCollisionModelTree(object);
    Model * model = BW::stockBspCollisionModelModel(object);
    return Py_BuildValue("(KKs)", (unsigned long long)std::uintptr_t(tree),
                         (unsigned long long)std::uintptr_t(model), model ? model->resourceID().c_str() : "");
}
PyObject * treeTriangles(PyObject *, PyObject * args) {
    PyObject * object;
    if (!PyArg_ParseTuple(args, "O", &object)) return NULL;
    const BSPTree * tree = BW::stockBspCollisionModelTree(object);
    if (!tree) Py_RETURN_NONE;
    PyObject * result = PyList_New(0);
    for (const WorldTriangle & t : tree->triangles()) {
        PyObject * item = Py_BuildValue("((fff)(fff)(fff)i)", t.v0().x, t.v0().y, t.v0().z, t.v1().x, t.v1().y,
                                        t.v1().z, t.v2().x, t.v2().y, t.v2().z, int(t.flags()));
        PyList_Append(result, item);
        Py_DECREF(item);
    }
    return result;
}
PyMethodDef hostMethods[] = {
    {"setLoader", setLoader, METH_VARARGS, NULL}, {"liveModels", liveModels, METH_NOARGS, NULL},
    {"nativeOwner", nativeOwner, METH_VARARGS, NULL}, {"treeTriangles", treeTriangles, METH_VARARGS, NULL},
    {NULL, NULL, 0, NULL}};
}

int main(int argc, char ** argv) {
    assert(argc >= 2);
    Py_NoSiteFlag = 1;
    Py_Initialize();
    PySys_SetArgv(argc - 1, argv + 1);
    assert(PyType_Ready(&BW::PyObjectPlus::s_type_) == 0);
    BW::PyModel::s_type_.tp_dictoffset = BW::PyModel::dictOffset();
    assert(PyType_Ready(&BW::PyModel::s_type_) == 0);
    for (PyTypeObject * type : BW::hostTypes()) assert(PyType_Ready(type) == 0);
    PyObject * bigworld = PyImport_AddModule("BigWorld");
    Py_INCREF(&BW::PyModel::s_type_);
    PyModule_AddObject(bigworld, "Model", reinterpret_cast<PyObject *>(&BW::PyModel::s_type_));
    std::vector<std::string> names;
    for (const BW::HostFactory & f : BW::hostFactories()) names.push_back(std::string(f.module) + "." + f.name);
    for (size_t i = 0; i < names.size(); ++i) {
        const BW::HostFactory & f = BW::hostFactories()[i];
        PyObject * module = PyImport_AddModule(f.module);
        f.type->tp_name = names[i].c_str();
        Py_INCREF(f.type);
        PyModule_AddObject(module, f.name, reinterpret_cast<PyObject *>(f.type));
    }
    assert(Py_InitModule("Host", hostMethods));
    FILE * script = std::fopen(argv[1], "r");
    assert(script);
    const int failed = PyRun_SimpleFileEx(script, argv[1], 1);
    Py_XDECREF(hostModelLoader());
    hostModelLoader() = NULL;
    const bool leaked = !BW::hostModels().empty();
    Py_Finalize();
    if (failed) return 1;
    if (leaked) { std::cerr << "Models outlived their WGBspCollisionModel owners" << std::endl; return 2; }
    std::cout << "Real CPython 2.7 WGBspCollisionModel binding contract passed" << std::endl;
    return 0;
}
