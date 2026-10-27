// Host stand-ins for the engine, GFx and pyscript surfaces used by the
// production stock_battle_*.cpp files. The AS3 side is an in-memory display
// tree; Python conversions model the public 32-bit Script::setData/getData.
#pragma once

#include <Python.h>

#include <cstring>
#include <cwchar>
#include <map>
#include <memory>
#include <string>
#include <strings.h>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#define BW_BEGIN_NAMESPACE namespace BW {
#define BW_END_NAMESPACE }

typedef unsigned uint;
typedef std::size_t UPInt;

namespace host
{
// One AS3 display object or plain object.
struct Node
{
    std::string name;
    int parent = 0;
    bool display = true;
    std::vector<int> children;
    std::map<std::string, int> members;
    unsigned flags = 0;
    double x = 0, y = 0, rotation = 0, xScale = 100, yScale = 100, alpha = 100;
    bool visible = true;
    float projection[16] = {};
    float view[12] = {};
    float matrix3D[12] = {};
    unsigned displayCalls = 0;
};

struct Stage
{
    std::vector<Node> nodes = std::vector<Node>(1);
    std::map<std::string, int> paths;
    std::map<std::string, std::vector<std::string> > symbols;
    std::vector<std::string> log;
    std::vector<std::string> failing;
    std::string result;

    int create(const std::string& name, int parent)
    {
        Node node;
        node.name = name;
        node.parent = parent;
        nodes.push_back(node);
        const int id = static_cast<int>(nodes.size()) - 1;
        if (parent)
        {
            nodes[parent].children.push_back(id);
            nodes[parent].members[name] = id;
        }
        return id;
    }

    int find(const std::string& name) const
    {
        for (std::size_t i = 1; i < nodes.size(); ++i)
            if (nodes[i].name == name)
                return static_cast<int>(i);
        return 0;
    }

    void detach(int id)
    {
        if (!nodes[id].parent)
            return;
        std::vector<int>& children = nodes[nodes[id].parent].children;
        for (std::size_t i = 0; i < children.size(); ++i)
            if (children[i] == id)
            {
                children.erase(children.begin() + static_cast<std::ptrdiff_t>(i));
                break;
            }
        nodes[id].parent = 0;
    }
};

inline Stage& stage()
{
    static Stage value;
    return value;
}
}

namespace Scaleform
{
namespace Render
{
struct Matrix3F
{
    float M[3][4];
    void SetIdentity()
    {
        std::memset(M, 0, sizeof(M));
        M[0][0] = M[1][1] = M[2][2] = 1.f;
    }
};

struct Matrix4F
{
    float M[4][4];
};
}

namespace GFx
{
class Value
{
public:
    enum ValueType
    {
        VT_Undefined = 0, VT_Null = 1, VT_Boolean = 2, VT_Int = 3, VT_UInt = 4, VT_Number = 5,
        VT_String = 6, VT_StringW = 7, VT_Object = 8, VT_Array = 9, VT_DisplayObject = 10,
        VT_Closure = 11
    };

    class DisplayInfo
    {
    public:
        enum Flags
        {
            V_x = 0x01, V_y = 0x02, V_rotation = 0x04, V_xscale = 0x08, V_yscale = 0x10,
            V_alpha = 0x20, V_visible = 0x40, V_projMatrix3D = 0x1000, V_viewMatrix3D = 0x2000
        };
        DisplayInfo() : VarsSet(0) {}
        DisplayInfo(double x, double y) : VarsSet(V_x | V_y), X(x), Y(y) {}
        void SetX(double x) { VarsSet |= V_x; X = x; }
        void SetY(double y) { VarsSet |= V_y; Y = y; }
        void SetRotation(double value) { VarsSet |= V_rotation; Rotation = value; }
        void SetXScale(double value) { VarsSet |= V_xscale; XScale = value; }
        void SetYScale(double value) { VarsSet |= V_yscale; YScale = value; }
        void SetAlpha(double value) { VarsSet |= V_alpha; Alpha = value; }
        void SetVisible(bool value) { VarsSet |= V_visible; Visible = value; }
        void SetProjectionMatrix3D(const Render::Matrix4F* matrix)
        {
            VarsSet |= V_projMatrix3D;
            Projection = *matrix;
        }
        void SetViewMatrix3D(const Render::Matrix3F* matrix)
        {
            VarsSet |= V_viewMatrix3D;
            View = *matrix;
        }
        unsigned VarsSet;
        double X = 0, Y = 0, Rotation = 0, XScale = 0, YScale = 0, Alpha = 0;
        bool Visible = false;
        Render::Matrix4F Projection = {};
        Render::Matrix3F View = {};
    };

    Value() : type_(VT_Undefined) {}
    static Value node(int id, ValueType type)
    {
        Value value;
        value.type_ = type;
        value.node_ = id;
        return value;
    }

    ValueType GetType() const { return type_; }
    bool IsUndefined() const { return type_ == VT_Undefined; }
    bool IsObject() const
    {
        return type_ == VT_Object || type_ == VT_Array || type_ == VT_DisplayObject;
    }
    bool IsDisplayObject() const { return type_ == VT_DisplayObject; }
    bool GetBool() const { return bool_; }
    double GetNumber() const { return number_; }
    const char* GetString() const { return string_; }
    const wchar_t* GetStringW() const { return wide_; }
    int Node() const { return node_; }

    void SetUndefined() { *this = Value(); }
    void SetNull() { *this = Value(); type_ = VT_Null; }
    void SetBoolean(bool value) { *this = Value(); type_ = VT_Boolean; bool_ = value; }
    void SetNumber(double value) { *this = Value(); type_ = VT_Number; number_ = value; }
    void SetString(const char* value) { *this = Value(); type_ = VT_String; string_ = value; }
    void SetInt(int value) { *this = Value(); type_ = VT_Int; number_ = value; }

    bool GetMember(const char* name, Value* out) const
    {
        host::Stage& s = host::stage();
        if (!IsObject())
            return false;
        if (!std::strcmp(name, "parent"))
        {
            const int parent = s.nodes[node_].parent;
            *out = parent ? node(parent, VT_DisplayObject) : Value();
            if (!parent)
                out->type_ = VT_Null;
            return true;
        }
        std::map<std::string, int>::const_iterator found = s.nodes[node_].members.find(name);
        if (found == s.nodes[node_].members.end())
            return false;
        *out = node(found->second, s.nodes[found->second].display ? VT_DisplayObject : VT_Object);
        return true;
    }

    bool AttachMovie(Value* out, const char* symbol, const char* instanceName, int depth)
    {
        host::Stage& s = host::stage();
        s.log.push_back(std::string("attach ") + symbol + " " + instanceName + " " +
            std::to_string(depth));
        if (!IsDisplayObject() || !s.symbols.count(symbol))
            return false;
        host::Node created;
        created.name = instanceName;
        created.parent = node_;
        s.nodes.push_back(created);
        const int id = static_cast<int>(s.nodes.size()) - 1;
        std::vector<int>& children = s.nodes[node_].children;
        const std::size_t index = depth < 0 || static_cast<std::size_t>(depth) > children.size() ?
            children.size() : static_cast<std::size_t>(depth);
        children.insert(children.begin() + static_cast<std::ptrdiff_t>(index), id);
        const std::vector<std::string> names = s.symbols[symbol];
        for (std::size_t i = 0; i < names.size(); ++i)
            s.create(names[i], id);
        *out = node(id, VT_DisplayObject);
        return true;
    }

    // removeChild, swapChildren and addChild act on the tree; any other
    // method is logged with its arguments.
    bool Invoke(const char* name, Value* result, const Value* args, UPInt count)
    {
        host::Stage& s = host::stage();
        if (!IsObject())
            return false;
        std::string line = std::string("call ") + s.nodes[node_].name + "." + name + "(";
        for (UPInt i = 0; i < count; ++i)
            line += (i ? "," : "") + args[i].describe();
        line += ")";
        s.log.push_back(line);
        for (std::size_t i = 0; i < s.failing.size(); ++i)
            if (s.failing[i] == name)
                return false;
        if (!std::strcmp(name, "removeChild") && count == 1)
            s.detach(args[0].node_);
        else if (!std::strcmp(name, "addChild") && count == 1)
        {
            s.detach(args[0].node_);
            s.nodes[args[0].node_].parent = node_;
            s.nodes[node_].children.push_back(args[0].node_);
        }
        else if (!std::strcmp(name, "swapChildren") && count == 2)
        {
            std::vector<int>& children = s.nodes[node_].children;
            std::size_t a = 0, b = 0;
            for (std::size_t i = 0; i < children.size(); ++i)
            {
                if (children[i] == args[0].node_) a = i;
                if (children[i] == args[1].node_) b = i;
            }
            std::swap(children[a], children[b]);
        }
        if (result)
        {
            if (s.result == "int")
                result->SetInt(7);
            else if (s.result == "number")
                result->SetNumber(2.5);
            else if (s.result == "string")
                result->SetString("done");
            else if (s.result == "bool")
                result->SetBoolean(true);
            else if (s.result == "object")
                *result = node(node_, VT_DisplayObject);
            else
                result->SetUndefined();
        }
        return true;
    }

    bool SetDisplayInfo(const DisplayInfo& info)
    {
        host::Node& n = host::stage().nodes[node_];
        n.flags = info.VarsSet;
        if (info.VarsSet & DisplayInfo::V_x) n.x = info.X;
        if (info.VarsSet & DisplayInfo::V_y) n.y = info.Y;
        if (info.VarsSet & DisplayInfo::V_rotation) n.rotation = info.Rotation;
        if (info.VarsSet & DisplayInfo::V_xscale) n.xScale = info.XScale;
        if (info.VarsSet & DisplayInfo::V_yscale) n.yScale = info.YScale;
        if (info.VarsSet & DisplayInfo::V_alpha) n.alpha = info.Alpha;
        if (info.VarsSet & DisplayInfo::V_visible) n.visible = info.Visible;
        if (info.VarsSet & DisplayInfo::V_projMatrix3D)
            std::memcpy(n.projection, info.Projection.M, sizeof(n.projection));
        if (info.VarsSet & DisplayInfo::V_viewMatrix3D)
            std::memcpy(n.view, info.View.M, sizeof(n.view));
        ++n.displayCalls;
        return true;
    }

    bool SetMatrix3D(const Render::Matrix3F& matrix)
    {
        std::memcpy(host::stage().nodes[node_].matrix3D, matrix.M, sizeof(matrix.M));
        return true;
    }

    std::string describe() const
    {
        switch (type_)
        {
        case VT_Null: return "null";
        case VT_Boolean: return bool_ ? "true" : "false";
        case VT_Number: return std::to_string(number_);
        case VT_String: return std::string("'") + string_ + "'";
        case VT_DisplayObject: return host::stage().nodes[node_].name;
        default: return "undefined";
        }
    }

private:
    ValueType type_;
    int node_ = 0;
    bool bool_ = false;
    double number_ = 0;
    const char* string_ = "";
    const wchar_t* wide_ = L"";
};

class Movie
{
public:
    bool GetVariable(Value* out, const char* path) const
    {
        std::map<std::string, int>::const_iterator found = host::stage().paths.find(path);
        if (found == host::stage().paths.end())
            return false;
        *out = found->second ? Value::node(found->second, Value::VT_DisplayObject) : Value();
        if (!found->second)
            out->SetNull();
        return true;
    }
};
}
}
using namespace Scaleform;

namespace BW
{
typedef std::string string;
typedef PyTypeObject PyTypePlus;

struct Matrix
{
    float m[4][4];
};

struct Vector2
{
    float x = 0, y = 0;
    Vector2() {}
    Vector2(float a, float b) : x(a), y(b) {}
};

struct Vector4
{
    float x = 0, y = 0, z = 0, w = 0;
    Vector4() {}
    Vector4(float a, float b, float c, float d) : x(a), y(b), z(c), w(d) {}
};

class PyObjectPlus : public PyObject
{
public:
    explicit PyObjectPlus(PyTypeObject* type) { (void)PyObject_INIT(static_cast<PyObject*>(this), type); }
    virtual ~PyObjectPlus() {}
    static void dealloc(PyObject* self) { delete static_cast<PyObjectPlus*>(self); }
};

template <class T>
class SmartPointer
{
public:
    SmartPointer(T* object = NULL) : object_(object) { Py_XINCREF(asPy()); }
    // OPTARG( MatrixProviderPtr, NULL, ... ) passes the NULL macro.
    template <class I, class = typename std::enable_if<std::is_integral<I>::value>::type>
    SmartPointer(I) : object_(NULL) {}
    SmartPointer(const SmartPointer& other) : object_(other.object_) { Py_XINCREF(asPy()); }
    ~SmartPointer() { Py_XDECREF(asPy()); }
    SmartPointer& operator=(const SmartPointer& other)
    {
        SmartPointer copy(other);
        std::swap(object_, copy.object_);
        return *this;
    }
    T* get() const { return object_; }
    T* operator->() const { return object_; }
    explicit operator bool() const { return object_ != NULL; }
    bool operator==(const SmartPointer& other) const { return object_ == other.object_; }

private:
    PyObject* asPy() const { return object_ ? static_cast<PyObject*>(object_) : NULL; }
    T* object_;
};
typedef SmartPointer<PyObject> PyObjectPtr;

class MatrixProvider : public PyObjectPlus
{
public:
    static PyTypeObject s_type_;
    MatrixProvider() : PyObjectPlus(&s_type_) { std::memset(world.m, 0, sizeof(world.m)); }
    void matrix(Matrix& out) const { out = world; }
    Matrix world;
};
typedef SmartPointer<MatrixProvider> MatrixProviderPtr;

class PyTextureProvider : public PyObjectPlus
{
public:
    static PyTypeObject s_type_;
};
typedef SmartPointer<PyTextureProvider> PyTextureProviderPtr;

namespace Script
{
// 32-bit public Script::setData/getData models.
inline int setData(PyObject* object, uint& value, const char* name)
{
    if (PyInt_Check(object))
    {
        const long number = PyInt_AsLong(object);
        value = static_cast<uint>(number);
        if (number >= 0 && number <= 0x7fffffffL)
            return 0;
    }
    if (PyFloat_Check(object))
    {
        value = static_cast<uint>(static_cast<int>(PyFloat_AsDouble(object)));
        return 0;
    }
    if (PyLong_Check(object))
    {
        const unsigned long number = PyLong_AsUnsignedLong(object);
        if (!PyErr_Occurred() && number <= 0xffffffffUL)
        {
            value = static_cast<uint>(number);
            return 0;
        }
        PyErr_Clear();
    }
    PyErr_Format(PyExc_TypeError, "%s must be set to an uint", name);
    return -1;
}

inline int setData(PyObject* object, bool& value, const char* name)
{
    if (PyInt_Check(object))
    {
        value = PyInt_AsLong(object) != 0;
        return 0;
    }
    if (PyString_Check(object))
    {
        if (!strcasecmp(PyString_AsString(object), "true")) { value = true; return 0; }
        if (!strcasecmp(PyString_AsString(object), "false")) { value = false; return 0; }
    }
    PyErr_Format(PyExc_TypeError, "%s must be set to a bool", name);
    return -1;
}

inline int setData(PyObject* object, BW::string& value, const char* name)
{
    if (PyString_Check(object))
    {
        value = PyString_AsString(object);
        return 0;
    }
    PyErr_Format(PyExc_TypeError, "%s must be set to a string", name);
    return -1;
}

inline int setData(PyObject* object, PyObjectPtr& value, const char*)
{
    value = PyObjectPtr(object);
    return 0;
}

template <class T>
int setPointer(PyObject* object, SmartPointer<T>& value, const char* name)
{
    if (object == Py_None)
    {
        value = SmartPointer<T>();
        return 0;
    }
    if (PyObject_TypeCheck(object, &T::s_type_))
    {
        value = SmartPointer<T>(static_cast<T*>(object));
        return 0;
    }
    PyErr_Format(PyExc_TypeError, "%s must be set to a %s", name, T::s_type_.tp_name);
    return -1;
}

inline int setData(PyObject* object, MatrixProviderPtr& value, const char* name)
{
    return setPointer(object, value, name);
}

inline int setData(PyObject* object, PyTextureProviderPtr& value, const char* name)
{
    return setPointer(object, value, name);
}

inline int setFloats(PyObject* object, float* values, Py_ssize_t count, const char* name)
{
    if (!PyTuple_Check(object) || PyTuple_Size(object) != count)
    {
        PyErr_Format(PyExc_TypeError, "%s must be set to a Vector%d", name, int(count));
        return -1;
    }
    for (Py_ssize_t i = 0; i < count; ++i)
    {
        values[i] = static_cast<float>(PyFloat_AsDouble(PyTuple_GetItem(object, i)));
        if (PyErr_Occurred())
            return -1;
    }
    return 0;
}

inline int setData(PyObject* object, Vector2& value, const char* name)
{
    return setFloats(object, &value.x, 2, name);
}

inline int setData(PyObject* object, Vector4& value, const char* name)
{
    return setFloats(object, &value.x, 4, name);
}

inline PyObject* getData(uint value)
{
    return static_cast<int>(value) < 0 ? PyLong_FromUnsignedLong(value) : PyInt_FromLong(value);
}

inline PyObject* getData(bool value) { return PyBool_FromLong(value); }

inline PyObject* getData(const Vector2& value)
{
    return Py_BuildValue("(ff)", value.x, value.y);
}

inline PyObject* getData(const Vector4& value)
{
    return Py_BuildValue("(ffff)", value.x, value.y, value.z, value.w);
}
}

namespace Moo
{
struct Camera
{
    float nearPlane_ = 1.f, farPlane_ = 1000.f;
    float nearPlane() const { return nearPlane_; }
    float farPlane() const { return farPlane_; }
};

struct RenderContext
{
    Matrix view_ = {}, viewProjection_ = {};
    Camera camera_;
    float zoomFactor_ = 1.f, halfScreenWidth_ = 960.f, halfScreenHeight_ = 540.f;
    const Matrix& view() const { return view_; }
    const Matrix& viewProjection() const { return viewProjection_; }
    const Camera& camera() const { return camera_; }
    float zoomFactor() const { return zoomFactor_; }
    float halfScreenWidth() const { return halfScreenWidth_; }
    float halfScreenHeight() const { return halfScreenHeight_; }
};

inline RenderContext& rc()
{
    static RenderContext value;
    return value;
}
}

namespace ScaleformBW
{
class PyMovieView : public PyObjectPlus
{
public:
    static PyTypeObject s_type_;
    PyMovieView() : PyObjectPlus(&s_type_) {}
    static bool Check(PyObject* object) { return object && PyObject_TypeCheck(object, &s_type_); }
    GFx::Movie* pMovieView() const { return const_cast<GFx::Movie*>(&movie_); }

private:
    GFx::Movie movie_;
};

class FlashGUIComponent : public PyObjectPlus
{
public:
    static PyTypeObject s_type_;
    FlashGUIComponent(PyMovieView* movie, PyTypePlus* type) :
        PyObjectPlus(type), inputKeyMode_(0), weakReferences(NULL), pMovieView_(movie)
    {
    }
    ~FlashGUIComponent()
    {
        if (weakReferences)
            PyObject_ClearWeakRefs(static_cast<PyObject*>(this));
    }
    virtual void update(float, float, float) {}
    virtual void drawSelf(bool, bool) {}
    // Offset of the weak-reference list from the PyObject header.
    static Py_ssize_t weakOffset()
    {
        alignas(FlashGUIComponent) static char storage[sizeof(FlashGUIComponent)];
        FlashGUIComponent* object = reinterpret_cast<FlashGUIComponent*>(storage);
        return reinterpret_cast<char*>(&object->weakReferences) -
            reinterpret_cast<char*>(static_cast<PyObject*>(object));
    }
    uint inputKeyMode_;
    PyObject* weakReferences;

protected:
    SmartPointer<PyMovieView> pMovieView_;
};
}
}

namespace host
{
struct End {};
template <class T, class R> struct Arg { R rest; };
template <class T, class R> struct OptArg { T value; R rest; };

template <class T, class R> Arg<T, R> arg(R rest) { Arg<T, R> a = {rest}; return a; }
template <class T, class R, class D> OptArg<T, R> optarg(const D& value, R rest)
{
    OptArg<T, R> a = {T(value), rest};
    return a;
}

inline std::tuple<> values(End) { return std::tuple<>(); }
template <class T, class R> auto values(const Arg<T, R>& a)
{
    return std::tuple_cat(std::tuple<T>(T()), values(a.rest));
}
template <class T, class R> auto values(const OptArg<T, R>& a)
{
    return std::tuple_cat(std::tuple<T>(a.value), values(a.rest));
}
inline int required(End) { return 0; }
template <class T, class R> int required(const Arg<T, R>& a) { return 1 + required(a.rest); }
template <class T, class R> int required(const OptArg<T, R>&) { return 0; }

template <std::size_t I, class Tuple>
typename std::enable_if<I == std::tuple_size<Tuple>::value, bool>::type
parse(PyObject*, Py_ssize_t, Tuple&) { return true; }

template <std::size_t I, class Tuple>
typename std::enable_if<I < std::tuple_size<Tuple>::value, bool>::type
parse(PyObject* args, Py_ssize_t count, Tuple& values)
{
    if (static_cast<Py_ssize_t>(I) < count)
    {
        const std::string name = "() argument " + std::to_string(I + 1);
        if (BW::Script::setData(PyTuple_GetItem(args, static_cast<Py_ssize_t>(I)),
                std::get<I>(values), name.c_str()) != 0)
            return false;
    }
    return parse<I + 1>(args, count, values);
}

struct RETOWN {};
struct RETOK {};
struct RETVOID {};
struct RETDATA {};

template <class C, class F, class Tuple, std::size_t... I>
auto apply(C* self, F function, Tuple& values, std::index_sequence<I...>)
{
    return (self->*function)(std::get<I>(values)...);
}

template <class R> struct Result;
template <> struct Result<RETOWN>
{
    template <class C, class F, class T> static PyObject* call(C* s, F f, T& v)
    {
        return apply(s, f, v, std::make_index_sequence<std::tuple_size<T>::value>());
    }
};
template <> struct Result<RETOK>
{
    template <class C, class F, class T> static PyObject* call(C* s, F f, T& v)
    {
        if (!apply(s, f, v, std::make_index_sequence<std::tuple_size<T>::value>()))
            return NULL;
        Py_RETURN_NONE;
    }
};
template <> struct Result<RETVOID>
{
    template <class C, class F, class T> static PyObject* call(C* s, F f, T& v)
    {
        apply(s, f, v, std::make_index_sequence<std::tuple_size<T>::value>());
        Py_RETURN_NONE;
    }
};
template <> struct Result<RETDATA>
{
    template <class C, class F, class T> static PyObject* call(C* s, F f, T& v)
    {
        return BW::Script::getData(apply(s, f, v,
            std::make_index_sequence<std::tuple_size<T>::value>()));
    }
};

template <class R, class C, class F, class Spec>
PyObject* call(C* self, F function, PyObject* args, const char* name, const Spec& spec)
{
    auto parsed = values(spec);
    const Py_ssize_t count = PyTuple_Size(args);
    if (count < required(spec) ||
        count > static_cast<Py_ssize_t>(std::tuple_size<decltype(parsed)>::value))
    {
        PyErr_Format(PyExc_TypeError, "%s() takes %d to %d arguments (%d given)", name,
            required(spec), int(std::tuple_size<decltype(parsed)>::value), int(count));
        return NULL;
    }
    if (!parse<0>(args, count, parsed))
        return NULL;
    return Result<R>::call(self, function, parsed);
}

struct Factory
{
    const char* name;
    PyObject* (*create)(PyObject*);
    PyTypeObject* type;
    PyMethodDef* methods;
    PyGetSetDef* attributes;
};

inline std::vector<Factory>& factories()
{
    static std::vector<Factory> value;
    return value;
}

struct FactoryRegistration
{
    FactoryRegistration(const Factory& factory) { factories().push_back(factory); }
};
}

#define END host::End()
#define ARG(TYPE, REST) host::arg<TYPE>(REST)
#define OPTARG(TYPE, VALUE, REST) host::optarg<TYPE>(VALUE, REST)

#define Py_Header(CLASS, SUPER)                                                                    \
public:                                                                                            \
    static PyTypeObject s_type_;                                                                   \
    typedef SUPER Super;                                                                           \
    typedef CLASS This;                                                                            \
                                                                                                   \
private:

#define PY_AUTO_METHOD_DECLARE(RET, NAME, ARGS)                                                    \
    static PyObject* _py_##NAME(PyObject* self, PyObject* args)                                    \
    {                                                                                              \
        return host::call<host::RET>(static_cast<This*>(self), &This::NAME, args, #NAME, ARGS);    \
    }

#define PY_RW_ACCESSOR_ATTRIBUTE_DECLARE(TYPE, FUNCTION, NAME)                                     \
    static PyObject* _pyget_##NAME(PyObject* self, void*)                                          \
    {                                                                                              \
        return BW::Script::getData(static_cast<This*>(self)->FUNCTION());                          \
    }                                                                                              \
    static int _pyset_##NAME(PyObject* self, PyObject* value, void*)                               \
    {                                                                                              \
        TYPE converted = TYPE();                                                                   \
        if (!value || BW::Script::setData(value, converted, #NAME) != 0)                           \
            return -1;                                                                             \
        static_cast<This*>(self)->FUNCTION(converted);                                             \
        return 0;                                                                                  \
    }

#define PY_FACTORY_DECLARE() static PyObject* pyNew(PyObject* args);

#define PY_TYPEOBJECT(CLASS)                                                                       \
    PyTypeObject CLASS::s_type_ = {PyVarObject_HEAD_INIT(NULL, 0) #CLASS, sizeof(CLASS), 0,        \
        PyObjectPlus::dealloc};

#define PY_BEGIN_METHODS(CLASS)                                                                    \
    typedef CLASS HostPyClass;                                                                     \
    PyMethodDef s_hostMethods_##CLASS[] = {
#define PY_METHOD(NAME) {#NAME, (PyCFunction)HostPyClass::_py_##NAME, METH_VARARGS, NULL},
#define PY_END_METHODS() {NULL, NULL, 0, NULL}};

#define PY_BEGIN_ATTRIBUTES(CLASS) PyGetSetDef s_hostAttributes_##CLASS[] = {
#define PY_ATTRIBUTE(NAME)                                                                         \
    {(char*)#NAME, HostPyClass::_pyget_##NAME, HostPyClass::_pyset_##NAME, NULL, NULL},
#define PY_END_ATTRIBUTES() {NULL, NULL, NULL, NULL, NULL}};

#define PY_FACTORY_NAMED(CLASS, NAME, MODULE)                                                      \
    static host::FactoryRegistration s_hostFactory_##CLASS(                                        \
        host::Factory{NAME, &CLASS::pyNew, &CLASS::s_type_, s_hostMethods_##CLASS,                 \
            s_hostAttributes_##CLASS});
