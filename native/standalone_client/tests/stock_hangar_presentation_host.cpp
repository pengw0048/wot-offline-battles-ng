// Real CPython 2.7 runs the engine's PY_AUTO macros, the public Script::setData
// and argCountError bodies and the production stock_hangar_presentation.cpp.
// The GUI, texture, effect, model and Influx owners are recording fixtures.
#include <Python.h>
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <map>
#include <sstream>
#include <string>
#include <strings.h>
#include <vector>

#define BW_BEGIN_NAMESPACE namespace BW {
#define BW_END_NAMESPACE }
#define BW_GUARD
#define BW_STATIC_ASSERT(expression, message) static_assert(expression, #message)
#define _stricmp strcasecmp
#define WOT_STOCK_HANGAR_PRESENTATION_PORTABLE 1

typedef unsigned int uint;
typedef unsigned int uint32;
typedef int int32;
typedef long long int64;
typedef long HRESULT;
#define FAILED(hr) (((HRESULT)(hr)) < 0)

std::vector<std::string> hostCalls;
void record(const std::string & call) { hostCalls.push_back(call); }
#define ERROR_MSG(...) do { char text[256]; std::snprintf(text, sizeof(text), __VA_ARGS__); \
    record(std::string("ERROR:") + text); } while (0)

namespace BW {
typedef std::string string;
template <class T> using vector = std::vector<T>;

class PyObjectPtr
{
public:
    enum StealReference { STEAL_REFERENCE };
    PyObjectPtr() : object_(NULL) {}
    PyObjectPtr(PyObject * object, StealReference) : object_(object) {}
    PyObjectPtr & operator=(const PyObjectPtr & other)
    {
        Py_XINCREF(other.object_);
        Py_XDECREF(object_);
        object_ = other.object_;
        return *this;
    }
    ~PyObjectPtr() { Py_XDECREF(object_); }
    PyObject * get() const { return object_; }
private:
    PyObject * object_;
};

struct HostLink { const char * module; const char * name; PyCFunction function; };
std::vector<HostLink> & hostLinks() { static std::vector<HostLink> links; return links; }
struct PyModuleMethodLink
{
    PyModuleMethodLink(const char * module, const char * name, PyCFunction function, const char *)
    {
        hostLinks().push_back(HostLink{module, name, function});
    }
};

// A non-owning fixture pointer with the engine smart-pointer surface used here.
template <class T> class Ptr
{
public:
    Ptr(T * value = NULL) : value_(value) {}
    T * operator->() const { return value_; }
    T & operator*() const { return *value_; }
    T * get() const { return value_; }
    T * getObject() const { return value_; }
    explicit operator bool() const { return value_ != NULL; }
private:
    T * value_;
};

class StringRef
{
public:
    StringRef(const std::string & text) : text_(text) {}
    const std::string & str() const { return text_; }
private:
    std::string text_;
};

struct Vector2 { Vector2(float a, float b) : x(a), y(b) {} float x, y; };

// Public lib/input/input.hpp MouseEvent fields.
class MouseEvent
{
public:
    MouseEvent() : dx_(-1), dy_(-1), dz_(-1), x_(-1.f), y_(-1.f) {}
    void fill(long dx, long dy, long dz, const Vector2 & position)
    {
        dx_ = dx; dy_ = dy; dz_ = dz; x_ = position.x; y_ = position.y;
    }
    long dx_, dy_, dz_;
    float x_, y_;
};

class SimpleGUI
{
public:
    static SimpleGUI *& slot() { static SimpleGUI * value = NULL; return value; }
    static SimpleGUI * pInstance() { return slot(); }
    virtual ~SimpleGUI() {}
    virtual bool handleMouseEvent(const MouseEvent & event)
    {
        std::ostringstream text;
        text << "SimpleGUI.handleMouseEvent:" << event.dx_ << ',' << event.dy_ << ',' << event.dz_
             << ',' << event.x_ << ',' << event.y_;
        record(text.str());
        return false;
    }
};

void setStockTPSDrawEnabled(bool enabled) { record(std::string("tPS.drawEnabled:") + (enabled ? "1" : "0")); }

struct ID3DXEffect
{
    std::string bound;
    HRESULT SetTexture(const char * name, void * texture)
    {
        bound = texture ? static_cast<const char *>(texture) : "";
        record(std::string("effect.SetTexture:") + name + ":" + (texture ? bound : "NULL"));
        return 0;
    }
};

namespace Moo {
class BaseTexture
{
public:
    explicit BaseTexture(const std::string & name) : name_(name) {}
    void * pTexture() { return const_cast<char *>(name_.c_str()); }
private:
    std::string name_;
};
typedef Ptr<BaseTexture> BaseTexturePtr;

class TextureManager
{
public:
    static TextureManager * instance() { static TextureManager manager; return &manager; }
    std::map<std::string, BaseTexture *> textures;
    BaseTexturePtr get(const StringRef & name, bool animation, bool mustExist, bool load, const char * allocator)
    {
        std::ostringstream text;
        text << "TextureManager.get:" << name.str() << ':' << animation << mustExist << load << ':' << allocator;
        record(text.str());
        // A missing resource yields an empty reference, as the public manager does.
        if (name.str().find("missing") != std::string::npos) return BaseTexturePtr();
        BaseTexture *& texture = textures[name.str()];
        if (!texture) texture = new BaseTexture(name.str());
        return BaseTexturePtr(texture);
    }
};

class DeviceCallback
{
public:
    virtual ~DeviceCallback() {}
    virtual void createManagedObjects() {}
    virtual void deleteManagedObjects() {}
};

class ManagedEffect
{
public:
    ID3DXEffect * pEffect() { return &effect_; }
private:
    ID3DXEffect effect_;
};

class EffectProperty
{
public:
    EffectProperty(const std::string & owner) : owner_(owner) {}
    bool be(const StringRef & value)
    {
        record("property.be:" + owner_ + ":" + value.str());
        return true;
    }
private:
    std::string owner_;
};
typedef Ptr<EffectProperty> EffectPropertyPtr;

class EffectMaterial
{
public:
    explicit EffectMaterial(const std::string & name) : name_(name) {}
    const Ptr<ManagedEffect> pEffect() { return Ptr<ManagedEffect>(&effect_); }
    EffectPropertyPtr getProperty(const StringRef & name)
    {
        std::map<std::string, EffectProperty *>::iterator found = properties_.find(name.str());
        return EffectPropertyPtr(found == properties_.end() ? NULL : found->second);
    }
    void addProperty(const std::string & name) { properties_[name] = new EffectProperty(name_ + "." + name); }
    const std::string & name() const { return name_; }
private:
    std::string name_;
    ManagedEffect effect_;
    std::map<std::string, EffectProperty *> properties_;
};
typedef Ptr<EffectMaterial> EffectMaterialPtr;

bool effectLoads = true;
bool createEffect(EffectMaterialPtr & material, const char * resource)
{
    record(std::string("createEffect:") + resource);
    material = effectLoads ? EffectMaterialPtr(new EffectMaterial(resource)) : EffectMaterialPtr();
    return effectLoads;
}

class ComplexEffectMaterial
{
public:
    explicit ComplexEffectMaterial(EffectMaterial * base) : base_(base) {}
    EffectMaterialPtr baseMaterial() const { return EffectMaterialPtr(base_); }
private:
    EffectMaterial * base_;
};

namespace Visual {
struct PrimitiveGroup { Ptr<ComplexEffectMaterial> material_; };
}

struct BackBufferDesc { uint Width, Height; };
class FullscreenQuad
{
public:
    bool drawChecked(EffectMaterial & material, uint width, uint height)
    {
        std::ostringstream text;
        text << "fsQuad.drawChecked:" << material.name() << ':' << width << 'x' << height << ':'
             << material.pEffect()->pEffect()->bound;
        record(text.str());
        return true;
    }
};
class RenderContext
{
public:
    const BackBufferDesc & backBufferDesc() const { static BackBufferDesc desc = {1280, 720}; return desc; }
    FullscreenQuad & fsQuad() { static FullscreenQuad quad; return quad; }
};
RenderContext & rc() { static RenderContext context; return context; }
}

// lib/model: gatherMaterials("*") over each model of the parent chain.
class Model
{
public:
    Model(const std::string & name, Model * parent) : name_(name), parent_(parent) {}
    Model * parent() { return parent_; }
    int gatherMaterials(const std::string & identifier, std::vector<Moo::Visual::PrimitiveGroup *> & groups)
    {
        record("Model.gatherMaterials:" + name_ + ":" + identifier);
        groups = groups_;
        return int(groups.size());
    }
    std::vector<Moo::Visual::PrimitiveGroup *> groups_;
private:
    std::string name_;
    Model * parent_;
};
typedef Ptr<Model> ModelPtr;

class SuperModel
{
public:
    std::vector<Model *> models;
    int nModels() const { return int(models.size()); }
    ModelPtr topModel(int index) { return ModelPtr(models[index]); }
};

struct PyModel : PyObject
{
    SuperModel * superModel;
    SuperModel * pSuperModel() const { return superModel; }
};
typedef Ptr<PyModel> PyModelPtr;
PyTypeObject pyModelType = { PyVarObject_HEAD_INIT(&PyType_Type, 0) "PyModel", sizeof(PyModel) };

namespace Script {
template <class T> struct IsValidArgType { static const bool isValid = true; };
int setData(PyObject * pObject, bool & rBool, const char * varName = "");
int setData(PyObject * pObject, float & rFloat, const char * varName = "");
int setData(PyObject * pObject, double & rDouble, const char * varName = "");
int setData(PyObject * pObject, BW::string & rString, const char * varName = "");
PyObject * getData(const bool data);
PyObject * argCountError(const char * fn, int optas, int allas, ...);
// PY_SCRIPT_CONVERTERS for a ScriptObjectPtr: None is NULL, otherwise the exact type.
int setData(PyObject * pObject, PyModelPtr & rModel, const char * varName = "")
{
    if (pObject == Py_None) { rModel = PyModelPtr(); return 0; }
    if (Py_TYPE(pObject) != &pyModelType)
    {
        PyErr_Format(PyExc_TypeError, "%s must be set to a PyModel", varName);
        return -1;
    }
    rModel = PyModelPtr(static_cast<PyModel *>(pObject));
    return 0;
}
template <class T> PyObject * getRetData(const T & data) { return getData(data); }
}

@SCRIPT_BODIES@
}

@PYAUTO_MACROS@

#include "stock_hangar_presentation.cpp"

namespace {
PyObject * calls(PyObject *, PyObject *)
{
    PyObject * result = PyList_New(0);
    for (size_t i = 0; i < hostCalls.size(); ++i)
    {
        PyObject * item = PyString_FromString(hostCalls[i].c_str());
        PyList_Append(result, item);
        Py_DECREF(item);
    }
    hostCalls.clear();
    return result;
}

PyObject * setGUI(PyObject *, PyObject * args)
{
    int present;
    if (!PyArg_ParseTuple(args, "i", &present)) return NULL;
    static BW::SimpleGUI gui;
    BW::SimpleGUI::slot() = present ? &gui : NULL;
    Py_RETURN_NONE;
}

PyObject * setEffectLoads(PyObject *, PyObject * args)
{
    int loads;
    if (!PyArg_ParseTuple(args, "i", &loads)) return NULL;
    BW::Moo::effectLoads = loads != 0;
    Py_RETURN_NONE;
}

PyObject * initialise(PyObject *, PyObject *) { return PyBool_FromLong(BW::initialiseStockHangarPresentation()); }
PyObject * finalise(PyObject *, PyObject *) { BW::finaliseStockHangarPresentation(); Py_RETURN_NONE; }
PyObject * drawBackground(PyObject *, PyObject *) { return PyBool_FromLong(BW::drawStockGUIBackground()); }

// makeModel(levels): a PyModel whose first top model has a parent chain of
// `levels` models, each with a diffuseMap material, another material and an
// empty group. levels == 0 yields a PyModel without top models.
PyObject * makeModel(PyObject *, PyObject * args)
{
    int levels;
    if (!PyArg_ParseTuple(args, "i", &levels)) return NULL;
    BW::SuperModel * superModel = new BW::SuperModel;
    BW::Model * parent = NULL;
    for (int level = levels; level > 0; --level)
    {
        std::ostringstream name;
        name << "model" << level;
        BW::Model * model = new BW::Model(name.str(), parent);
        BW::Moo::EffectMaterial * shadow = new BW::Moo::EffectMaterial(name.str() + "/shadow");
        shadow->addProperty("diffuseMap");
        shadow->addProperty("alphaReference");
        BW::Moo::EffectMaterial * other = new BW::Moo::EffectMaterial(name.str() + "/other");
        other->addProperty("normalMap");
        BW::Moo::Visual::PrimitiveGroup * groups = new BW::Moo::Visual::PrimitiveGroup[3];
        groups[0].material_ = BW::Ptr<BW::Moo::ComplexEffectMaterial>(new BW::Moo::ComplexEffectMaterial(shadow));
        groups[1].material_ = BW::Ptr<BW::Moo::ComplexEffectMaterial>(new BW::Moo::ComplexEffectMaterial(other));
        model->groups_.push_back(&groups[0]);
        model->groups_.push_back(&groups[1]);
        model->groups_.push_back(&groups[2]);
        parent = model;
    }
    if (parent) superModel->models.push_back(parent);
    BW::PyModel * object = PyObject_New(BW::PyModel, &BW::pyModelType);
    object->superModel = superModel;
    return object;
}

PyMethodDef hostMethods[] = {
    {"calls", calls, METH_NOARGS, NULL},
    {"setGUI", setGUI, METH_VARARGS, NULL},
    {"setEffectLoads", setEffectLoads, METH_VARARGS, NULL},
    {"initialise", initialise, METH_NOARGS, NULL},
    {"finalise", finalise, METH_NOARGS, NULL},
    {"drawBackground", drawBackground, METH_NOARGS, NULL},
    {"makeModel", makeModel, METH_VARARGS, NULL},
    {NULL, NULL, 0, NULL}};
}

int main(int argc, char ** argv)
{
    assert(argc >= 2);
    Py_Initialize();
    BW::pyModelType.tp_flags = Py_TPFLAGS_DEFAULT;
    assert(PyType_Ready(&BW::pyModelType) == 0);
    static std::map<std::string, std::vector<PyMethodDef> > modules;
    for (const BW::HostLink & link : BW::hostLinks())
        modules[link.module].push_back(PyMethodDef{link.name, link.function, METH_VARARGS, NULL});
    for (std::map<std::string, std::vector<PyMethodDef> >::iterator it = modules.begin(); it != modules.end(); ++it)
    {
        it->second.push_back(PyMethodDef{NULL, NULL, 0, NULL});
        Py_InitModule(it->first.c_str(), it->second.data());
    }
    Py_InitModule("_presentation", hostMethods);
    PySys_SetArgv(argc - 1, argv + 1);
    FILE * script = std::fopen(argv[1], "r");
    assert(script);
    const int failed = PyRun_SimpleFileEx(script, argv[1], 1);
    Py_Finalize();
    return failed ? 1 : 0;
}
