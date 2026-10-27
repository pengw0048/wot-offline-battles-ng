// CPython 2.7 module hosting the production battle canvas/minimap bindings.
#include "stock_battle_host_engine.hpp"


namespace BW
{
PyTypeObject MatrixProvider::s_type_ = {PyVarObject_HEAD_INIT(NULL, 0) "Matrix",
    sizeof(MatrixProvider), 0, PyObjectPlus::dealloc};
PyTypeObject PyTextureProvider::s_type_ = {PyVarObject_HEAD_INIT(NULL, 0) "PyTextureProvider",
    sizeof(PyTextureProvider), 0, PyObjectPlus::dealloc};
namespace ScaleformBW
{
PyTypeObject PyMovieView::s_type_ = {PyVarObject_HEAD_INIT(NULL, 0) "PyMovieView",
    sizeof(PyMovieView), 0, PyObjectPlus::dealloc};
PyTypeObject FlashGUIComponent::s_type_ = {PyVarObject_HEAD_INIT(NULL, 0) "FlashGUIComponent",
    sizeof(FlashGUIComponent), 0, PyObjectPlus::dealloc};
}
}

namespace
{
using BW::ScaleformBW::FlashGUIComponent;
using BW::ScaleformBW::PyMovieView;

PyObject* inputKeyModeGet(PyObject* self, void*)
{
    return PyInt_FromLong(static_cast<FlashGUIComponent*>(self)->inputKeyMode_);
}

int inputKeyModeSet(PyObject* self, PyObject* value, void*)
{
    uint mode = 0;
    if (!value || BW::Script::setData(value, mode, "wg_inputKeyMode") != 0)
        return -1;
    static_cast<FlashGUIComponent*>(self)->inputKeyMode_ = mode;
    return 0;
}

PyGetSetDef flashAttributes[] = {
    {(char*)"wg_inputKeyMode", inputKeyModeGet, inputKeyModeSet, NULL, NULL},
    {NULL, NULL, NULL, NULL, NULL}};

PyObject* noBackground(PyObject*, void*) { return PyFloat_FromDouble(1.0); }
int setBackground(PyObject*, PyObject* value, void*) { return value ? 0 : -1; }
PyGetSetDef movieAttributes[] = {
    {(char*)"backgroundAlpha", noBackground, setBackground, NULL, NULL},
    {NULL, NULL, NULL, NULL, NULL}};

PyObject* reset(PyObject*, PyObject*)
{
    host::stage() = host::Stage();
    Py_RETURN_NONE;
}

PyObject* node(PyObject*, PyObject* args)
{
    const char* name;
    const char* parent = "";
    int display = 1;
    if (!PyArg_ParseTuple(args, "s|si", &name, &parent, &display))
        return NULL;
    const int id = host::stage().create(name, *parent ? host::stage().find(parent) : 0);
    host::stage().nodes[id].display = display != 0;
    return PyInt_FromLong(id);
}

PyObject* symbol(PyObject*, PyObject* args)
{
    const char* name;
    PyObject* children;
    if (!PyArg_ParseTuple(args, "sO!", &name, &PyList_Type, &children))
        return NULL;
    std::vector<std::string>& names = host::stage().symbols[name];
    for (Py_ssize_t i = 0; i < PyList_Size(children); ++i)
        names.push_back(PyString_AsString(PyList_GetItem(children, i)));
    Py_RETURN_NONE;
}

PyObject* path(PyObject*, PyObject* args)
{
    const char* variable;
    const char* name;
    if (!PyArg_ParseTuple(args, "ss", &variable, &name))
        return NULL;
    host::stage().paths[variable] = *name ? host::stage().find(name) : 0;
    Py_RETURN_NONE;
}

PyObject* movie(PyObject*, PyObject*)
{
    return new PyMovieView();
}

PyObject* matrix(PyObject*, PyObject* args)
{
    PyObject* values;
    if (!PyArg_ParseTuple(args, "O!", &PyTuple_Type, &values) || PyTuple_Size(values) != 16)
        return NULL;
    BW::MatrixProvider* provider = new BW::MatrixProvider();
    for (int i = 0; i < 16; ++i)
        provider->world.m[i / 4][i % 4] = static_cast<float>(PyFloat_AsDouble(PyTuple_GetItem(values, i)));
    return provider;
}

bool readMatrix(PyObject* values, BW::Matrix& out)
{
    if (!PyTuple_Check(values) || PyTuple_Size(values) != 16)
        return false;
    for (int i = 0; i < 16; ++i)
        out.m[i / 4][i % 4] = static_cast<float>(PyFloat_AsDouble(PyTuple_GetItem(values, i)));
    return true;
}

PyObject* camera(PyObject*, PyObject* args)
{
    PyObject* view;
    PyObject* viewProjection;
    BW::Moo::RenderContext& rc = BW::Moo::rc();
    if (!PyArg_ParseTuple(args, "OOfffff", &view, &viewProjection, &rc.camera_.nearPlane_,
            &rc.camera_.farPlane_, &rc.zoomFactor_, &rc.halfScreenWidth_, &rc.halfScreenHeight_) ||
        !readMatrix(view, rc.view_) || !readMatrix(viewProjection, rc.viewProjection_))
        return NULL;
    Py_RETURN_NONE;
}

PyObject* tick(PyObject*, PyObject* args)
{
    PyObject* component;
    if (!PyArg_ParseTuple(args, "O!", &FlashGUIComponent::s_type_, &component))
        return NULL;
    static_cast<FlashGUIComponent*>(component)->update(0.016f, 1.f, 1.f);
    static_cast<FlashGUIComponent*>(component)->drawSelf(true, false);
    Py_RETURN_NONE;
}

PyObject* log(PyObject*, PyObject*)
{
    PyObject* result = PyList_New(0);
    for (std::size_t i = 0; i < host::stage().log.size(); ++i)
    {
        PyObject* line = PyString_FromString(host::stage().log[i].c_str());
        PyList_Append(result, line);
        Py_DECREF(line);
    }
    host::stage().log.clear();
    return result;
}

PyObject* children(PyObject*, PyObject* args)
{
    const char* name;
    if (!PyArg_ParseTuple(args, "s", &name))
        return NULL;
    const host::Node& parent = host::stage().nodes[host::stage().find(name)];
    PyObject* result = PyList_New(0);
    for (std::size_t i = 0; i < parent.children.size(); ++i)
    {
        PyObject* child = PyString_FromString(host::stage().nodes[parent.children[i]].name.c_str());
        PyList_Append(result, child);
        Py_DECREF(child);
    }
    return result;
}

PyObject* display(PyObject*, PyObject* args)
{
    const char* name;
    if (!PyArg_ParseTuple(args, "s", &name))
        return NULL;
    const int id = host::stage().find(name);
    if (!id)
        Py_RETURN_NONE;
    const host::Node& n = host::stage().nodes[id];
    PyObject* projection = PyTuple_New(16);
    for (int i = 0; i < 16; ++i)
        PyTuple_SetItem(projection, i, PyFloat_FromDouble(n.projection[i]));
    PyObject* matrix3D = PyTuple_New(12);
    for (int i = 0; i < 12; ++i)
        PyTuple_SetItem(matrix3D, i, PyFloat_FromDouble(n.matrix3D[i]));
    return Py_BuildValue("{s:I,s:d,s:d,s:d,s:d,s:d,s:d,s:O,s:I,s:N,s:N,s:i}", "flags", n.flags,
        "x", n.x, "y", n.y, "rotation", n.rotation, "xScale", n.xScale, "yScale", n.yScale,
        "alpha", n.alpha, "visible", n.visible ? Py_True : Py_False, "calls", n.displayCalls,
        "projection", projection, "matrix3D", matrix3D, "parent", n.parent);
}

PyObject* configure(PyObject*, PyObject* args)
{
    const char* result;
    PyObject* failing;
    if (!PyArg_ParseTuple(args, "sO!", &result, &PyList_Type, &failing))
        return NULL;
    host::stage().result = result;
    host::stage().failing.clear();
    for (Py_ssize_t i = 0; i < PyList_Size(failing); ++i)
        host::stage().failing.push_back(PyString_AsString(PyList_GetItem(failing, i)));
    Py_RETURN_NONE;
}

PyObject* factoryCall(PyObject* self, PyObject* args)
{
    const host::Factory* factory =
        static_cast<const host::Factory*>(PyCapsule_GetPointer(self, "factory"));
    return factory->create(args);
}

PyMethodDef probeMethods[] = {
    {"reset", reset, METH_NOARGS, NULL},
    {"node", node, METH_VARARGS, NULL},
    {"symbol", symbol, METH_VARARGS, NULL},
    {"path", path, METH_VARARGS, NULL},
    {"movie", movie, METH_NOARGS, NULL},
    {"Matrix", matrix, METH_VARARGS, NULL},
    {"camera", camera, METH_VARARGS, NULL},
    {"tick", tick, METH_VARARGS, NULL},
    {"log", log, METH_NOARGS, NULL},
    {"children", children, METH_VARARGS, NULL},
    {"display", display, METH_VARARGS, NULL},
    {"configure", configure, METH_VARARGS, NULL},
    {NULL, NULL, 0, NULL}};

PyMethodDef factoryMethod = {"factory", factoryCall, METH_VARARGS, NULL};

bool ready(PyTypeObject& type, PyTypeObject* base, PyMethodDef* methods, PyGetSetDef* attributes)
{
    type.tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE;
    type.tp_getattro = PyObject_GenericGetAttr;
    type.tp_setattro = PyObject_GenericSetAttr;
    type.tp_base = base;
    type.tp_methods = methods;
    type.tp_getset = attributes;
    return PyType_Ready(&type) == 0;
}
}

extern "C" void initbattle_gui_probe()
{
    using namespace BW;
    if (!ready(MatrixProvider::s_type_, NULL, NULL, NULL) ||
        !ready(PyTextureProvider::s_type_, NULL, NULL, NULL) ||
        !ready(ScaleformBW::PyMovieView::s_type_, NULL, NULL, movieAttributes))
        return;
    // The engine's components are PyObjectPlusWithWeakReference.
    FlashGUIComponent::s_type_.tp_weaklistoffset = FlashGUIComponent::weakOffset();
    if (!ready(FlashGUIComponent::s_type_, NULL, NULL, flashAttributes))
        return;
    PyObject* module = Py_InitModule("battle_gui_probe", probeMethods);
    PyObject* gui = Py_InitModule("GUI", NULL);
    if (!module || !gui)
        return;
    for (std::size_t i = 0; i < host::factories().size(); ++i)
    {
        host::Factory& factory = host::factories()[i];
        if (!ready(*factory.type, &FlashGUIComponent::s_type_, factory.methods, factory.attributes))
            return;
        PyObject* capsule = PyCapsule_New(&factory, "factory", NULL);
        PyObject* function = PyCFunction_NewEx(&factoryMethod, capsule, NULL);
        PyModule_AddObject(gui, factory.name, function);
        Py_DECREF(capsule);
        Py_INCREF(factory.type);
        PyModule_AddObject(module, factory.type->tp_name, reinterpret_cast<PyObject*>(factory.type));
    }
}
