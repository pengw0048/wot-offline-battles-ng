// Real CPython 2.7 runs the engine's PY_AUTO macros, the public Script::setData
// and argCountError bodies and the production stock_hangar_presentation.cpp.
// The GUI, texture, effect, model and Influx owners are recording fixtures.
#include <Python.h>
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstddef>
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
typedef unsigned short WORD;
#define INLINE inline
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
    StringRef(const char * text) : text_(text) {}
    const std::string & str() const { return text_; }
private:
    std::string text_;
};

struct Vector2 { Vector2(float a, float b) : x(a), y(b) {} float x, y; };
struct Vector4
{
    Vector4(float a = 0.f, float b = 0.f, float c = 0.f, float d = 0.f) : x(a), y(b), z(c), w(d) {}
    float & operator[](unsigned index) { return (&x)[index]; }
    float x, y, z, w;
};
typedef Vector4 D3DXVECTOR4;
std::string text(const Vector4 & v)
{
    std::ostringstream out;
    out << v.x << ',' << v.y << ',' << v.z << ',' << v.w;
    return out.str();
}
#define SUCCEEDED(hr) (((HRESULT)(hr)) >= 0)
enum { D3DFMT_A8R8G8B8 = 21, D3DCLEAR_TARGET = 1 };
namespace DX {
struct Surface
{
    const char * name;
    unsigned refs = 0;
    void AddRef() { ++refs; }
    void Release() { assert(refs); --refs; }
};
}
template <class T> class ComObjectWrap
{
public:
    ~ComObjectWrap() { if (value_) value_->Release(); }
    T ** operator&() { assert(!value_); return &value_; }
    T * pComObject() const { return value_; }
    explicit operator bool() const { return value_ != NULL; }
private:
    T * value_ = NULL;
};

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

// One ARIE effect instance: the shared definition of a ManagedEffect, or one
// material's private binding (patch_stock_effect_owners SELECT_BINDING).
struct ID3DXEffect
{
    std::string owner;
    std::map<std::string, std::string> values;
    HRESULT set(const char * kind, const char * name, const std::string & value)
    {
        if (value.empty()) values.erase(name);
        else values[name] = value;
        record(std::string("effect.") + kind + ":" + owner + ":" + name + ":" + (value.empty() ? "NULL" : value));
        return 0;
    }
    std::string summary() const
    {
        std::string result;
        for (std::map<std::string, std::string>::const_iterator it = values.begin(); it != values.end(); ++it)
            result += (result.empty() ? "" : ";") + it->first + "=" + it->second;
        return result;
    }
    HRESULT SetTexture(const char * name, void * texture)
    {
        return set("SetTexture", name, texture ? static_cast<const char *>(texture) : "");
    }
    HRESULT SetVector(const char * name, const Vector4 * value) { return set("SetVector", name, text(*value)); }
    HRESULT SetInt(const char * name, int value) { return set("SetInt", name, std::to_string(value)); }
    HRESULT SetBool(const char * name, int value) { return set("SetBool", name, std::to_string(value)); }
    HRESULT SetVectorArray(const char * name, const Vector4 * values, unsigned count)
    {
        std::string list;
        for (unsigned i = 0; i < count; ++i) list += (i ? "|" : "") + text(values[i]);
        return set("SetVectorArray", name, list);
    }
};
// Every fixture effect is an ARIE stock effect (stock_effect_adapter.hpp).
namespace wot { inline bool is_stock_effect(ID3DXEffect * effect) { return effect != NULL; } }

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

// pEffect() is the selected binding between select and end, else the definition.
class ManagedEffect
{
public:
    explicit ManagedEffect(const std::string & name) : active_(NULL) { definition_.owner = name; }
    ID3DXEffect * pEffect() { return active_ ? active_ : &definition_; }
    ID3DXEffect definition_;
    ID3DXEffect * active_;
};
bool selectFails = false;

class EffectProperty
{
public:
    EffectProperty(const std::string & owner) : owner_(owner) {}
    bool be(const StringRef & value)
    {
        record("property.be:" + owner_ + ":" + value.str());
        return true;
    }
    bool be(const Vector4 & value) { record("property.be:" + owner_ + ":" + text(value)); return true; }
    bool be(const int & value) { record("property.be:" + owner_ + ":" + std::to_string(value)); return true; }
    bool be(const bool & value) { record("property.be:" + owner_ + ":" + std::to_string(value)); return true; }
private:
    std::string owner_;
};
typedef Ptr<EffectProperty> EffectPropertyPtr;

class EffectMaterial
{
public:
    // Each Properties lazily creates and keeps its own private binding.
    class Properties
    {
    public:
        Properties() : binding_(NULL) {}
        bool select(ManagedEffect & effect)
        {
            if (selectFails) return false;
            if (!binding_)
            {
                binding_ = new ID3DXEffect;
                binding_->owner = effect.definition_.owner + "#binding";
            }
            effect.active_ = binding_;
            return true;
        }
        ID3DXEffect * existingStockBinding() const { return binding_; }
    private:
        ID3DXEffect * binding_;
    };
    explicit EffectMaterial(const std::string & name) : name_(name), effect_(name) {}
    const Ptr<ManagedEffect> pEffect() { return Ptr<ManagedEffect>(&effect_); }
    Properties & properties() { return properties_; }
    // begin() runs setProperties, which selects the binding; end() clears it.
    ID3DXEffect * begin() { return properties_.select(effect_) ? effect_.pEffect() : NULL; }
    void end() { effect_.active_ = NULL; }
    EffectPropertyPtr getProperty(const StringRef & name)
    {
        std::map<std::string, EffectProperty *>::iterator found = properties_map_.find(name.str());
        return EffectPropertyPtr(found == properties_map_.end() ? NULL : found->second);
    }
    void addProperty(const std::string & name) { properties_map_[name] = new EffectProperty(name_ + "." + name); }
    const std::string & name() const { return name_; }
private:
    std::string name_;
    ManagedEffect effect_;
    Properties properties_;
    std::map<std::string, EffectProperty *> properties_map_;
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
        ID3DXEffect * drawn = material.begin();
        if (!drawn) return false;
        std::ostringstream text;
        text << "fsQuad.drawChecked:" << material.name() << ':' << width << 'x' << height << ':' << drawn->summary();
        record(text.str());
        material.end();
        return true;
    }
};
struct D3DGAMMARAMP { WORD red[256], green[256], blue[256]; };
enum { D3DSGR_CALIBRATE = 1 };
struct Device
{
    DX::Surface scene{"scene"}, stale{"captured"};
    DX::Surface * depth = &scene;
    bool depthReadFails = false, depthBindFails = false;
    HRESULT GetDepthStencilSurface(DX::Surface ** surface)
    {
        record(std::string("device.GetDepthStencilSurface:") + depth->name);
        if (depthReadFails) return -1;
        *surface = depth;
        depth->AddRef();
        return 0;
    }
    HRESULT SetDepthStencilSurface(DX::Surface * surface)
    {
        record(std::string("device.SetDepthStencilSurface:") + surface->name);
        if (depthBindFails) return -1;
        depth = surface;
        return 0;
    }
    D3DGAMMARAMP ramp;
    HRESULT SetGammaRamp(unsigned swapChain, unsigned flags, const D3DGAMMARAMP * value)
    {
        ramp = *value;
        std::ostringstream text;
        text << "device.SetGammaRamp:" << swapChain << ':' << flags << ':' << value->red[64] << ','
             << value->green[128] << ',' << value->blue[255];
        record(text.str());
        return 0;
    }
    HRESULT Clear(unsigned, const void *, unsigned flags, unsigned colour, float z, unsigned stencil)
    {
        std::ostringstream text;
        text << "device.Clear:" << flags << ':' << colour << ':' << z << ':' << stencil;
        record(text.str());
        return 0;
    }
};
// Public lib/moo RenderContext gamma owner: the patched render_context.ipp
// accessors, setGammaCorrection and the present/reset insertions run unchanged.
class RenderContext
{
public:
    RenderContext() : device_(NULL), gammaCorrection_(1.f) {}
    const BackBufferDesc & backBufferDesc() const { static BackBufferDesc desc = {1280, 720}; return desc; }
    FullscreenQuad & fsQuad() { static FullscreenQuad quad; return quad; }
    Device * device() { static Device value; return &value; }
    float gammaCorrection() const;
    void gammaCorrection(float gammaCorrection);
    void setGammaCorrection();
    void present();
    void reset();
    Device * device_;  // NULL before createDevice.
private:
    float gammaCorrection_;
    bool gammaCorrectionDirty_ = true;
};
using std::max;
using std::min;
@RC_GAMMA_ACCESSORS@
@RC_SET_GAMMA@
// RenderContext::present before device_->Present, and changeModePriv after Reset.
void RenderContext::present()
{
@RC_PRESENT_GAMMA@}
void RenderContext::reset()
{
@RC_RESET_GAMMA@}
RenderContext & rc() { static RenderContext context; return context; }

class RenderTarget
{
public:
    explicit RenderTarget(const std::string & name) : name_(name) {}
    // SDK reuseZ captures its depth at allocation. Deliberately model the
    // Windows failure: the active scene has since switched to another surface.
    bool push()
    {
        previous_ = rc().device()->depth;
        rc().device()->depth = &rc().device()->stale;
        record("target.push:" + name_);
        return true;
    }
    void pop()
    {
        rc().device()->depth = previous_;
        record("target.pop:" + name_);
    }
    void * pTexture() { return const_cast<char *>(name_.c_str()); }
private:
    std::string name_;
    DX::Surface * previous_ = NULL;
};
typedef Ptr<RenderTarget> RenderTargetPtr;
bool targetCreates = true;
bool createRenderTarget(RenderTargetPtr & target, uint width, uint height, int format, const std::string & name,
                        bool mainZ, bool discardZ)
{
    std::ostringstream text;
    text << "createRenderTarget:" << name << ':' << width << 'x' << height << ':' << format << ':' << mainZ << discardZ;
    record(text.str());
    target = targetCreates ? RenderTargetPtr(new RenderTarget(name)) : RenderTargetPtr();
    return targetCreates;
}

enum ERenderingPassType { RENDERING_PASS_COLOR = 0 };
class DrawContext
{
public:
    enum ChannelMask { ALL_CHANNELS_MASK = 7 };
    class OverrideBlock
    {
    public:
        OverrideBlock(const StringRef & prefix, bool)
        {
            record("OverrideBlock:" + prefix.str());
            for (int index = 0; index < 2; ++index)
            {
                EffectMaterial * material = effectLoads ?
                    new EffectMaterial(prefix.str() + (index ? "_skinned.fx" : ".fx")) : NULL;
                materials_[index] = material ? new ComplexEffectMaterial(material) : NULL;
            }
        }
        ComplexEffectMaterial * stockMaterial(bool skinned) const { return materials_[skinned]; }
    private:
        ComplexEffectMaterial * materials_[2];
    };
    explicit DrawContext(ERenderingPassType) : override_(NULL) {}
    void begin(unsigned mask) { record("context.begin:" + std::to_string(mask)); }
    void end(unsigned mask) { record("context.end:" + std::to_string(mask)); }
    void flush(unsigned mask) { record("context.flush:" + std::to_string(mask)); }
    void pushImmediateMode() { record("context.pushImmediateMode"); }
    void popImmediateMode() { record("context.popImmediateMode"); }
    void pushOverrideBlock(OverrideBlock * block) { override_ = block; record("context.pushOverrideBlock"); }
    void popOverrideBlock(OverrideBlock * block) { assert(override_ == block); override_ = NULL; record("context.popOverrideBlock"); }
    OverrideBlock * override_;
};
}

// lib/space IEntityEmbodiment and client Entity/PyEntity.
struct PyAttachment : PyObject {};
struct ScriptObject { PyObject * value; PyObject * get() const { return value; } };
struct App
{
    enum { COLOUR_DRAW_CONTEXT };
    Moo::DrawContext colour{Moo::RENDERING_PASS_COLOR};
    static App & instance() { static App value; return value; }
    Moo::DrawContext & drawContext(int) { return colour; }
};
class IEntityEmbodiment
{
public:
    explicit IEntityEmbodiment(const std::string & name) : name_(name) {}
    ScriptObject scriptObject() const { return {const_cast<PyAttachment *>(&attachment_)}; }
    // An immediate-mode render op through the rigid override material.
    void draw(Moo::DrawContext & context)
    {
        if (!context.override_) { record("embodiment.draw:" + name_ + ":plain"); return; }
        assert(Moo::rc().device()->depth == &Moo::rc().device()->scene);
        const bool skinned = name_.find("Skinned") != std::string::npos;
        Moo::EffectMaterial * material = context.override_->stockMaterial(skinned)->baseMaterial().get();
        ID3DXEffect * drawn = material->begin();
        record("embodiment.draw:" + name_ + ":" + (drawn ? drawn->summary() : "unselected"));
        material->end();
    }
private:
    std::string name_;
    PyAttachment attachment_{};
};
typedef Ptr<IEntityEmbodiment> IEntityEmbodimentPtr;
typedef std::vector<IEntityEmbodimentPtr> EntityEmbodiments;

class Entity
{
public:
    Entity() : primary_(NULL), inWorld_(true), destroyed_(false) {}
    IEntityEmbodiment * pPrimaryEmbodiment() const { return primary_; }
    EntityEmbodiments & auxiliaryEmbodiments() { return auxiliary_; }
    bool isInWorld() const { return inWorld_; }
    bool isDestroyed() const { return destroyed_; }
    IEntityEmbodiment * primary_;
    EntityEmbodiments auxiliary_;
    bool inWorld_, destroyed_;
};
typedef Ptr<Entity> EntityPtr;
struct PyEntity : PyObject
{
    PyObject * dict;  // Script attributes such as ClientSelectableObject's own state.
    Entity * entity;
    EntityPtr pEntity() const { return EntityPtr(entity); }
};
typedef Ptr<PyEntity> PyEntityPtr;
PyTypeObject pyEntityType = { PyVarObject_HEAD_INIT(&PyType_Type, 0) "PyEntity", sizeof(PyEntity) };

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
    PyObject * dict;  // position and yaw written by __onFakeShadowLoaded.
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
PyObject * getData(const float data);
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
int setData(PyObject * pObject, PyEntityPtr & rEntity, const char * varName = "")
{
    if (pObject == Py_None) { rEntity = PyEntityPtr(); return 0; }
    if (Py_TYPE(pObject) != &pyEntityType)
    {
        PyErr_Format(PyExc_TypeError, "%s must be set to an Entity", varName);
        return -1;
    }
    rEntity = PyEntityPtr(static_cast<PyEntity *>(pObject));
    return 0;
}
// Public INT_ACCESSOR( uint, int64 ) over the int64 conversion.
int setData(PyObject * pObject, uint & rInt, const char * varName = "")
{
    long long value = PyInt_Check(pObject) ? PyInt_AsLong(pObject) : -1;
    if (!PyInt_Check(pObject))
    {
        PyErr_Format(PyExc_TypeError, "%s must be set to an int", varName);
        return -1;
    }
    rInt = uint(value);
    if (rInt != value)
    {
        PyErr_SetString(PyExc_TypeError, "Integer is out of range");
        return -1;
    }
    return 0;
}
// setDataSequence of Vector4: a sequence of four-number sequences.
int setData(PyObject * pObject, std::vector<Vector4> & rColors, const char * varName = "")
{
    if (!PySequence_Check(pObject))
    {
        PyErr_Format(PyExc_TypeError, "%s must be a sequence", varName);
        return -1;
    }
    std::vector<Vector4> result;
    for (Py_ssize_t i = 0; i < PySequence_Size(pObject); ++i)
    {
        PyObject * item = PySequence_GetItem(pObject, i);
        Vector4 value;
        const int ok = PyArg_ParseTuple(item, "ffff", &value.x, &value.y, &value.z, &value.w);
        Py_DECREF(item);
        if (!ok) return -1;
        result.push_back(value);
    }
    rColors = result;
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
    object->dict = NULL;
    object->superModel = superModel;
    return object;
}

PyObject * setTargetCreates(PyObject *, PyObject * args)
{
    int creates;
    if (!PyArg_ParseTuple(args, "i", &creates)) return NULL;
    BW::Moo::targetCreates = creates != 0;
    Py_RETURN_NONE;
}

PyObject * setDepthFails(PyObject *, PyObject * args)
{
    int read, bind;
    if (!PyArg_ParseTuple(args, "ii", &read, &bind)) return NULL;
    BW::Moo::rc().device()->depthReadFails = read != 0;
    BW::Moo::rc().device()->depthBindFails = bind != 0;
    Py_RETURN_NONE;
}

PyObject * setSelectFails(PyObject *, PyObject * args)
{
    int fails;
    if (!PyArg_ParseTuple(args, "i", &fails)) return NULL;
    BW::Moo::selectFails = fails != 0;
    Py_RETURN_NONE;
}

// A lost device: unmanaged resources are released, then recreated.
PyObject * resetDevice(PyObject *, PyObject *)
{
    if (BW::edgeDetector)
    {
        BW::edgeDetector->deleteUnmanagedObjects();
        BW::edgeDetector->createUnmanagedObjects();
    }
    Py_RETURN_NONE;
}

// createDevice/changeModePriv: attach the device, then run setGammaCorrection
// as the patched creation (line 1324) and reset paths do.
PyObject * applyDeviceGamma(PyObject *, PyObject *)
{
    BW::Moo::rc().device_ = BW::Moo::rc().device();
    BW::Moo::rc().setGammaCorrection();
    Py_RETURN_NONE;
}

PyObject * presentFrame(PyObject *, PyObject *)
{
    BW::Moo::rc().present();
    Py_RETURN_NONE;
}

PyObject * resetDeviceGamma(PyObject *, PyObject *)
{
    BW::Moo::rc().reset();
    Py_RETURN_NONE;
}

PyObject * detachDevice(PyObject *, PyObject *)
{
    BW::Moo::rc().device_ = NULL;
    Py_RETURN_NONE;
}

// The device ramp, and the #1513 00b2cd50 ramp for the same gamma: index
// times the float 1/255, powf to 1/gamma, times 65535, truncated.
PyObject * gammaRamps(PyObject *, PyObject * args)
{
    float gamma;
    if (!PyArg_ParseTuple(args, "f", &gamma)) return NULL;
    const BW::Moo::D3DGAMMARAMP & ramp = BW::Moo::rc().device()->ramp;
    PyObject * actual = PyList_New(0);
    PyObject * original = PyList_New(0);
    const float reciprocal = 1.f / gamma;
    for (int i = 0; i < 256; ++i)
    {
        if (ramp.red[i] != ramp.green[i] || ramp.red[i] != ramp.blue[i]) return PyErr_Format(PyExc_AssertionError, "channels");
        PyObject * value = PyInt_FromLong(ramp.red[i]);
        PyList_Append(actual, value);
        Py_DECREF(value);
        const float scaled = std::pow(float(i) * 0.003921568859368563f, reciprocal) * 65535.f;
        value = PyInt_FromLong(WORD(int(scaled)));
        PyList_Append(original, value);
        Py_DECREF(value);
    }
    PyObject * result = Py_BuildValue("(OO)", actual, original);
    Py_DECREF(actual);
    Py_DECREF(original);
    return result;
}

PyObject * drawEdges(PyObject *, PyObject *)
{
    const bool prepared = BW::prepareStockEdgeDetection();
    const bool drawn = BW::drawStockEdgeDetection();
    assert(BW::Moo::rc().device()->depth == &BW::Moo::rc().device()->scene);
    assert(BW::Moo::rc().device()->scene.refs == 0);
    return PyBool_FromLong(drawn && prepared);
}
PyObject * deferEntity(PyObject *, PyObject * args)
{
    PyObject * object;int main = 1;
    if (!PyArg_ParseTuple(args, "O!|i", &BW::pyEntityType, &object, &main)) return NULL;
    BW::Entity & entity = *static_cast<BW::PyEntity *>(object)->entity;
    BW::Moo::DrawContext other(BW::Moo::RENDERING_PASS_COLOR);
    BW::Moo::DrawContext & context = main ? BW::App::instance().drawContext(0) : other;
    const auto members = BW::EdgeDetector::embodiments(entity);
    PyObject * result = PyTuple_New(members.size());
    for (size_t i = 0; i < members.size(); ++i)
        PyTuple_SET_ITEM(result, i, PyBool_FromLong(BW::stockDeferEdgeAttachment(
            static_cast<const BW::PyAttachment *>(members[i]->scriptObject().get()), context)));
    return result;
}

// makeEntity(primary, auxiliaries...): a PyEntity with named embodiments.
PyObject * makeEntity(PyObject *, PyObject * args)
{
    BW::Entity * entity = new BW::Entity;
    for (Py_ssize_t i = 0; i < PyTuple_Size(args); ++i)
    {
        const char * name = PyString_AsString(PyTuple_GetItem(args, i));
        if (!name) return NULL;
        BW::IEntityEmbodiment * embodiment = new BW::IEntityEmbodiment(name);
        if (i == 0) entity->primary_ = embodiment;
        else entity->auxiliary_.push_back(BW::IEntityEmbodimentPtr(embodiment));
    }
    BW::PyEntity * object = PyObject_New(BW::PyEntity, &BW::pyEntityType);
    object->dict = NULL;
    object->entity = entity;
    return object;
}

// setEntity(entity, inWorld, destroyed[, newPrimary]).
PyObject * setEntity(PyObject *, PyObject * args)
{
    PyObject * object;
    int inWorld, destroyed;
    const char * primary = NULL;
    if (!PyArg_ParseTuple(args, "O!ii|s", &BW::pyEntityType, &object, &inWorld, &destroyed, &primary))
        return NULL;
    BW::Entity * entity = static_cast<BW::PyEntity *>(object)->entity;
    entity->inWorld_ = inWorld != 0;
    entity->destroyed_ = destroyed != 0;
    if (primary) entity->primary_ = new BW::IEntityEmbodiment(primary);
    Py_RETURN_NONE;
}

PyMethodDef hostMethods[] = {
    {"setTargetCreates", setTargetCreates, METH_VARARGS, NULL},
    {"drawEdges", drawEdges, METH_NOARGS, NULL},
    {"deferEntity", deferEntity, METH_VARARGS, NULL},
    {"setSelectFails", setSelectFails, METH_VARARGS, NULL},
    {"setDepthFails", setDepthFails, METH_VARARGS, NULL},
    {"resetDevice", resetDevice, METH_NOARGS, NULL},
    {"applyDeviceGamma", applyDeviceGamma, METH_NOARGS, NULL},
    {"detachDevice", detachDevice, METH_NOARGS, NULL},
    {"presentFrame", presentFrame, METH_NOARGS, NULL},
    {"resetDeviceGamma", resetDeviceGamma, METH_NOARGS, NULL},
    {"gammaRamps", gammaRamps, METH_VARARGS, NULL},
    {"makeEntity", makeEntity, METH_VARARGS, NULL},
    {"setEntity", setEntity, METH_VARARGS, NULL},
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
    static BW::PyModel modelProbe;
    BW::pyModelType.tp_dictoffset = reinterpret_cast<char *>(&modelProbe.dict) -
        reinterpret_cast<char *>(static_cast<PyObject *>(&modelProbe));
    BW::pyModelType.tp_getattro = PyObject_GenericGetAttr;
    BW::pyModelType.tp_setattro = PyObject_GenericSetAttr;
    BW::pyEntityType.tp_flags = Py_TPFLAGS_DEFAULT;
    static BW::PyEntity probe;
    BW::pyEntityType.tp_dictoffset = reinterpret_cast<char *>(&probe.dict) -
        reinterpret_cast<char *>(static_cast<PyObject *>(&probe));
    BW::pyEntityType.tp_getattro = PyObject_GenericGetAttr;
    BW::pyEntityType.tp_setattro = PyObject_GenericSetAttr;
    assert(PyType_Ready(&BW::pyEntityType) == 0);
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
