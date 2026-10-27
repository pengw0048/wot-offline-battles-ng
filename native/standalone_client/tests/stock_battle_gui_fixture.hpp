// Recording engine fixture for the battle-GUI bindings. Real CPython 2.7 runs
// the engine's PY_AUTO, attribute and Script::setData(float/bool) bodies and
// argCountError (substituted from the pinned public engine). PyObjectPlus,
// the type/factory macros, the GFx display tree, SimpleGUIComponent,
// FlashGUIComponent, PyMovieView, MatrixProvider and the render context are
// fixtures that record what the production code asks of them.
#pragma once
#include <Python.h>
#include <cassert>
#include <cmath>
#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <strings.h>
#include <vector>

#define BW_BEGIN_NAMESPACE namespace BW {
#define BW_END_NAMESPACE }
#define BW_STATIC_ASSERT(expression, message) static_assert(expression, #message)
#define _stricmp strcasecmp
#define SCALEFORM_SUPPORT 1

typedef long long int64;
typedef unsigned long long uint64;
typedef unsigned int uint32;
typedef unsigned int uint;
typedef int int32;

namespace BW {
typedef std::string string;
template <class T> using vector = std::vector<T>;

class PyObjectPtr
{
public:
	enum StealReference { STEAL_REFERENCE };
	PyObjectPtr() : object_(NULL) {}
	PyObjectPtr(PyObject * object) : object_(object) { Py_XINCREF(object_); }
	PyObjectPtr(PyObject * object, StealReference) : object_(object) {}
	PyObjectPtr(const PyObjectPtr & other) : object_(other.object_) { Py_XINCREF(object_); }
	PyObjectPtr & operator=(const PyObjectPtr & other)
	{
		Py_XINCREF(other.object_);
		Py_XDECREF(object_);
		object_ = other.object_;
		return *this;
	}
	~PyObjectPtr() { Py_XDECREF(object_); }
	PyObject * get() const { return object_; }
	PyObject * getObject() const { return object_; }
private:
	PyObject * object_;
};

struct Vector2
{
	float x, y;
	Vector2() : x(0.f), y(0.f) {}
	Vector2(float a, float b) : x(a), y(b) {}
	float & operator[](int i) { return i ? y : x; }
};
struct Vector3
{
	float x, y, z;
	Vector3() : x(0.f), y(0.f), z(0.f) {}
	Vector3(float a, float b, float c) : x(a), y(b), z(c) {}
};
struct Matrix
{
	float m[4][4];
	Matrix() { setIdentity(); }
	void setZero() { std::memset(m, 0, sizeof(m)); }
	void setIdentity() { setZero(); m[0][0] = m[1][1] = m[2][2] = m[3][3] = 1.f; }
	float & operator()(int r, int c) { return m[r][c]; }
	float operator()(int r, int c) const { return m[r][c]; }
};

// --- PyObjectPlus and its type/factory macros (fixtures) ----------------------
typedef PyTypeObject PyTypePlus;
int hostDeallocations = 0;
struct HostFactory { const char * module; const char * name; PyObject * (*function)(PyObject *, PyObject *, PyObject *); };
std::vector<HostFactory> & hostFactories() { static std::vector<HostFactory> f; return f; }
struct PyFactoryMethodLink
{
	PyFactoryMethodLink(const char * module, const char * name, PyTypeObject * type,
		PyObject * (*function)(PyObject *, PyObject *, PyObject *))
	{
		hostFactories().push_back(HostFactory{module, name, function});
		(void)type;
	}
};

class PyObjectPlus : public PyObject
{
public:
	explicit PyObjectPlus(PyTypeObject * type) : dict_(NULL) { PyObject_Init(this, type); }
	virtual ~PyObjectPlus() { Py_XDECREF(dict_); }
	static void * operator new(std::size_t size) { return PyObject_Malloc(size); }
	static void operator delete(void * p) { PyObject_Free(p); }
	const char * typeName() const { return ob_type->tp_name; }
	void incRef() { Py_INCREF(this); }
	void decRef() { Py_DECREF(this); }
	PyObject * dict_;
	static PyTypeObject s_type_;
	static PyMethodDef * s_getMethodDefs() { static PyMethodDef none[] = {{NULL, NULL, 0, NULL}}; return none; }
	static PyGetSetDef * s_getGetSets() { static PyGetSetDef none[] = {{NULL, NULL, NULL, NULL, NULL}}; return none; }
};

template <class T> Py_ssize_t hostDictOffset()
{
	alignas(T) static char storage[sizeof(T)];
	T * object = reinterpret_cast<T *>(storage);
	return reinterpret_cast<char *>(&object->dict_) -
		reinterpret_cast<char *>(static_cast<PyObject *>(object));
}

template <class T> PyTypeObject hostType(const char * name, PyTypeObject * base)
{
	PyTypeObject type = { PyVarObject_HEAD_INIT(&PyType_Type, 0) };
	type.tp_name = name;
	type.tp_basicsize = sizeof(T);
	type.tp_dealloc = [](PyObject * o) { ++hostDeallocations; delete static_cast<T *>(static_cast<PyObjectPlus *>(o)); };
	type.tp_getattro = PyObject_GenericGetAttr;
	type.tp_setattro = PyObject_GenericSetAttr;
	type.tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE;
	type.tp_methods = T::s_getMethodDefs();
	type.tp_getset = T::s_getGetSets();
	type.tp_base = base;
	type.tp_dictoffset = hostDictOffset<T>();
	return type;
}
PyTypeObject PyObjectPlus::s_type_ = hostType<PyObjectPlus>("PyObjectPlus", NULL);

template <class T> class SmartPointer
{
public:
	SmartPointer(T * p = NULL) : p_(p) { if (p_) p_->incRef(); }
	SmartPointer(const SmartPointer & o) : p_(o.p_) { if (p_) p_->incRef(); }
	~SmartPointer() { if (p_) p_->decRef(); }
	SmartPointer & operator=(const SmartPointer & o)
	{
		if (o.p_) o.p_->incRef();
		if (p_) p_->decRef();
		p_ = o.p_;
		return *this;
	}
	SmartPointer & operator=(T * p) { return *this = SmartPointer(p); }
	T * get() const { return p_; }
	T * getObject() const { return p_; }
	T * operator->() const { return p_; }
	T & operator*() const { return *p_; }
	explicit operator bool() const { return p_ != NULL; }
	bool operator!() const { return p_ == NULL; }
private:
	T * p_;
};

namespace Script {
int setData(PyObject * pObject, bool & rBool, const char * varName = "");
int setData(PyObject * pObject, float & rFloat, const char * varName = "");
int setData(PyObject * pObject, double & rDouble, const char * varName = "");
PyObject * argCountError(const char * fn, int optas, int allas, ...);
inline PyObject * getData(float data) { return PyFloat_FromDouble(data); }
inline PyObject * getData(bool data) { return PyBool_FromLong(data); }
inline PyObject * getData(const Vector2 & v) { return Py_BuildValue("(ff)", v.x, v.y); }
template <class T> PyObject * getReadOnlyData(const T & d) { return getData(d); }
// Fixture forms of the public Vector2/Vector3/SmartPointer<PyObject> converters.
inline int setData(PyObject * pObject, Vector2 & v, const char * varName = "")
{
	float a, b;
	if (PyArg_ParseTuple(pObject, "ff", &a, &b)) { v.x = a; v.y = b; return 0; }
	PyErr_Format(PyExc_TypeError, "%s must be set to a Vector2 or tuple of 2 floats", varName);
	return -1;
}
inline int setData(PyObject * pObject, Vector3 & v, const char * varName = "")
{
	if (PyArg_ParseTuple(pObject, "fff", &v.x, &v.y, &v.z)) return 0;
	PyErr_Format(PyExc_TypeError, "%s must be set to a Vector3 or a tuple of 3 floats", varName);
	return -1;
}
inline int setData(PyObject * pObject, PyObjectPtr & r, const char * = "")
{
	r = PyObjectPtr(pObject != Py_None ? pObject : NULL);
	return 0;
}
}

@SCRIPT_BODIES@

@PYAUTO_MACROS@

@PY_AUTO_METHOD_DECLARE@

#define PY_ATTR_SCOPE
@PY_ATTRIBUTE_DECLARE_MACROS@

#define Py_Header(CLASS, SUPER) \
	public: \
		typedef CLASS This; typedef SUPER Super; \
		static PyTypeObject s_type_; \
		static PyMethodDef * s_getMethodDefs(); \
		static PyGetSetDef * s_getGetSets(); \
		static bool Check(PyObject * o) { return o && PyObject_TypeCheck(o, &s_type_); } \
		static This * getSelf(PyObject * o) { return o ? static_cast<This *>(static_cast<PyObjectPlus *>(o)) : NULL; } \
	private:
#define PY_TYPEOBJECT(CLASS) PyTypeObject CLASS::s_type_ = BW::hostType<CLASS>(#CLASS, &CLASS::Super::s_type_);
#define PY_BEGIN_METHODS(CLASS) \
	PyMethodDef * CLASS::s_getMethodDefs() { typedef CLASS This; (void)static_cast<This *>(nullptr); static std::vector<PyMethodDef> defs; if (defs.empty()) {
#define PY_METHOD(NAME) defs.push_back(PyMethodDef{#NAME, (PyCFunction)&This::_py_##NAME, METH_VARARGS | METH_KEYWORDS, NULL});
#define PY_END_METHODS() defs.push_back(PyMethodDef{NULL, NULL, 0, NULL}); } return defs.data(); }
#define PY_BEGIN_ATTRIBUTES(CLASS) \
	PyGetSetDef * CLASS::s_getGetSets() { typedef CLASS This; (void)static_cast<This *>(nullptr); static std::vector<PyGetSetDef> defs; if (defs.empty()) {
#define PY_ATTRIBUTE(NAME) defs.push_back(PyGetSetDef{(char *)#NAME, \
	[](PyObject * o, void *) -> PyObject * { return This::getSelf(o)->pyGet_##NAME(); }, \
	[](PyObject * o, PyObject * v, void *) -> int { \
		if (v == NULL) { PyErr_SetString(PyExc_TypeError, "cannot delete " #NAME); return -1; } \
		return This::getSelf(o)->pySet_##NAME(v); }, NULL, NULL});
#define PY_END_ATTRIBUTES() defs.push_back(PyGetSetDef{NULL, NULL, NULL, NULL, NULL}); } return defs.data(); }
#define PY_FACTORY_DECLARE() \
	static PyObject * pyNew(PyObject * pArgs); \
	static PyObject * _pyNew(PyObject *, PyObject * args, PyObject *) { return This::pyNew(args); } \
	static PyFactoryMethodLink s_link_pyNew;
#define PY_FACTORY_NAMED(CLASS, NAME, MODULE) \
	PyFactoryMethodLink CLASS::s_link_pyNew(#MODULE, NAME, &CLASS::s_type_, &CLASS::_pyNew);
#define PY_FACTORY(CLASS, MODULE) PY_FACTORY_NAMED(CLASS, #CLASS, MODULE)

// --- MatrixProvider / Math.MatrixAnimation -----------------------------------------
class MatrixProvider : public PyObjectPlus
{
public:
	MatrixProvider(bool, PyTypeObject * type) : PyObjectPlus(type) {}
	virtual void matrix(Matrix & m) const = 0;
	static PyTypeObject s_type_;
	static PyMethodDef * s_getMethodDefs() { return PyObjectPlus::s_getMethodDefs(); }
	static PyGetSetDef * s_getGetSets() { return PyObjectPlus::s_getGetSets(); }
	static bool Check(PyObject * o) { return o && PyObject_TypeCheck(o, &s_type_); }
};
struct HostAbstractProvider : MatrixProvider
{
	HostAbstractProvider() : MatrixProvider(false, &s_type_) {}
	void matrix(Matrix &) const override {}
};
PyTypeObject MatrixProvider::s_type_ = hostType<HostAbstractProvider>("MatrixProvider", &PyObjectPlus::s_type_);
typedef SmartPointer<MatrixProvider> MatrixProviderPtr;

namespace Script {
inline int setData(PyObject * pObject, MatrixProviderPtr & r, const char * varName = "")
{
	if (pObject == Py_None) { r = NULL; return 0; }
	if (!MatrixProvider::Check(pObject))
	{
		PyErr_Format(PyExc_TypeError, "%s must be set to a MatrixProvider", varName);
		return -1;
	}
	r = static_cast<MatrixProvider *>(static_cast<PyObjectPlus *>(pObject));
	return 0;
}
inline PyObject * getData(const MatrixProviderPtr & p)
{
	PyObject * o = p ? static_cast<PyObject *>(p.get()) : Py_None;
	Py_INCREF(o);
	return o;
}
}

// A recorded matrix provider: rows set from Python, sample count recorded.
class HostMatrix : public MatrixProvider
{
public:
	HostMatrix() : MatrixProvider(false, &s_type_), samples(0) {}
	void matrix(Matrix & m) const override { ++samples; m = value; }
	Matrix value;
	mutable int samples;
	static PyTypeObject s_type_;
	static PyMethodDef * s_getMethodDefs() { return PyObjectPlus::s_getMethodDefs(); }
	static PyGetSetDef * s_getGetSets() { return PyObjectPlus::s_getGetSets(); }
};
PyTypeObject HostMatrix::s_type_ = hostType<HostMatrix>("Math.HostMatrix", &MatrixProvider::s_type_);

// --- the render context --------------------------------------------------------------
namespace Moo {
class Camera
{
public:
	float nearPlane() const { return nearPlane_; }
	float farPlane() const { return farPlane_; }
	float nearPlane_ = 0.5f, farPlane_ = 1500.f;
};
class RenderContext
{
public:
	const Matrix & view() const { return view_; }
	const Matrix & invView() const { return invView_; }
	const Matrix & viewProjection() const { return viewProjection_; }
	float halfScreenWidth() const { return halfWidth_; }
	float halfScreenHeight() const { return halfHeight_; }
	const Camera & camera() const { return camera_; }
	Matrix view_, invView_, viewProjection_;
	float halfWidth_ = 960.f, halfHeight_ = 540.f;
	Camera camera_;
};
inline RenderContext & rc() { static RenderContext context; return context; }
}
}

// --- a recording GFx subset -------------------------------------------------------------
namespace Scaleform {
namespace Render {
struct Matrix3F { float M[3][4]; void SetIdentity() { std::memset(M, 0, sizeof(M)); M[0][0] = M[1][1] = M[2][2] = 1.f; } };
struct Matrix4F { float M[4][4]; Matrix4F() { std::memset(M, 0, sizeof(M)); } };
}
template <class C> class Ptr
{
public:
	Ptr() : p_(NULL) {}
	Ptr(C & adopted) : p_(&adopted) {}
	Ptr(const Ptr & o) : p_(o.p_) { if (p_) p_->AddRef(); }
	~Ptr() { if (p_) p_->Release(); }
	Ptr & operator=(C & adopted) { if (p_) p_->Release(); p_ = &adopted; return *this; }
	Ptr & operator=(const Ptr & o) { if (o.p_) o.p_->AddRef(); if (p_) p_->Release(); p_ = o.p_; return *this; }
	C * GetPtr() const { return p_; }
	C * operator->() const { return p_; }
	explicit operator bool() const { return p_ != NULL; }
private:
	C * p_;
};
namespace GFx {
class Movie;
class Value;
int hostLiveHandlers = 0;
class FunctionHandler
{
public:
	struct Params { Value * pRetVal; Movie * pMovie; Value * pThis; Value * pArgsWithThisRef; Value * pArgs; unsigned ArgCount; void * pUserData; };
	FunctionHandler() : refs_(1) { ++hostLiveHandlers; }
	virtual ~FunctionHandler() { --hostLiveHandlers; }
	virtual void Call(const Params & params) = 0;
	void AddRef() { ++refs_; }
	void Release() { if (--refs_ == 0) delete this; }
private:
	int refs_;
};

struct HostNode;
class Value
{
public:
	enum ValueType { VT_Undefined, VT_Null, VT_Boolean, VT_Number, VT_String, VT_Object, VT_DisplayObject, VT_Function };
	class DisplayInfo
	{
	public:
		enum { V_x = 0x01, V_y = 0x02, V_rotation = 0x04, V_xscale = 0x08, V_yscale = 0x10, V_alpha = 0x20,
			V_visible = 0x40, V_projMatrix3D = 0x1000, V_viewMatrix3D = 0x2000 };
		DisplayInfo() : VarsSet(0), X(0), Y(0), Rotation(0), XScale(100), YScale(100), Alpha(100), Visible(true) {}
		void SetVisible(bool v) { VarsSet |= V_visible; Visible = v; }
		void SetPosition(double x, double y) { VarsSet |= V_x | V_y; X = x; Y = y; }
		void SetRotation(double r) { VarsSet |= V_rotation; Rotation = r; }
		void SetScale(double x, double y) { VarsSet |= V_xscale | V_yscale; XScale = x; YScale = y; }
		void SetViewMatrix3D(const Render::Matrix3F * m) { VarsSet |= V_viewMatrix3D; View = *m; }
		void SetProjectionMatrix3D(const Render::Matrix4F * m) { VarsSet |= V_projMatrix3D; Projection = *m; }
		double GetRotation() const { assert(VarsSet & V_rotation); return Rotation; }
		bool GetVisible() const { assert(VarsSet & V_visible); return Visible; }
		unsigned VarsSet;
		double X, Y, Rotation, XScale, YScale, Alpha;
		bool Visible;
		Render::Matrix3F View;
		Render::Matrix4F Projection;
	};
	Value() : type_(VT_Undefined), number_(0), boolean_(false) {}
	Value(bool v) : type_(VT_Boolean), number_(0), boolean_(v) {}
	bool IsObject() const { return type_ == VT_Object || type_ == VT_DisplayObject; }
	bool IsDisplayObject() const { return type_ == VT_DisplayObject; }
	bool IsNull() const { return type_ == VT_Null; }
	bool IsNumber() const { return type_ == VT_Number; }
	double GetNumber() const { assert(IsNumber()); return number_; }
	bool IsDisplayObjectActive() const;
	bool GetMember(const char * name, Value * out) const;
	bool SetMember(const char * name, const Value & value);
	bool SetDisplayInfo(const DisplayInfo & info);
	bool GetDisplayInfo(DisplayInfo * info) const;
	bool SetMatrix3D(const Render::Matrix3F & m);
	bool Invoke(const char * name, Value * result, const Value * args, size_t count);
	ValueType type_;
	double number_;
	bool boolean_;
	std::shared_ptr<HostNode> node_;
	Ptr<FunctionHandler> function_;
};

struct HostNode
{
	std::map<std::string, Value> members;
	Value::DisplayInfo info;
	bool active = true;
	int displayWrites = 0;
	int matrixWrites = 0;
	Render::Matrix3F matrix3D;
	std::vector<std::pair<std::string, std::vector<Value>>> invokes;
};

inline bool Value::IsDisplayObjectActive() const { assert(IsDisplayObject()); return node_->active; }
inline bool Value::GetMember(const char * name, Value * out) const
{
	assert(IsObject());
	auto found = node_->members.find(name);
	if (found == node_->members.end()) return false;
	*out = found->second;
	return true;
}
inline bool Value::SetMember(const char * name, const Value & value)
{
	assert(IsObject());
	node_->members[name] = value;
	return true;
}
inline bool Value::SetDisplayInfo(const DisplayInfo & info)
{
	assert(IsDisplayObject());
	DisplayInfo & d = node_->info;
	if (info.VarsSet & DisplayInfo::V_x) d.X = info.X;
	if (info.VarsSet & DisplayInfo::V_y) d.Y = info.Y;
	if (info.VarsSet & DisplayInfo::V_rotation) d.Rotation = info.Rotation;
	if (info.VarsSet & DisplayInfo::V_xscale) d.XScale = info.XScale;
	if (info.VarsSet & DisplayInfo::V_yscale) d.YScale = info.YScale;
	if (info.VarsSet & DisplayInfo::V_visible) d.Visible = info.Visible;
	if (info.VarsSet & DisplayInfo::V_viewMatrix3D) d.View = info.View;
	if (info.VarsSet & DisplayInfo::V_projMatrix3D) d.Projection = info.Projection;
	d.VarsSet |= info.VarsSet;
	++node_->displayWrites;
	return true;
}
inline bool Value::GetDisplayInfo(DisplayInfo * info) const
{
	assert(IsDisplayObject());
	*info = node_->info;
	info->VarsSet |= 0x7f;
	return true;
}
inline bool Value::SetMatrix3D(const Render::Matrix3F & m)
{
	assert(IsDisplayObject());
	node_->matrix3D = m;
	++node_->matrixWrites;
	return true;
}
inline bool Value::Invoke(const char * name, Value *, const Value * args, size_t count)
{
	assert(IsObject());
	node_->invokes.push_back(std::make_pair(std::string(name), std::vector<Value>(args, args + count)));
	return true;
}

class Movie
{
public:
	Movie() { root.type_ = Value::VT_DisplayObject; root.node_ = std::make_shared<HostNode>(); }
	bool GetVariable(Value * out, const char * path) const
	{
		Value current = root;
		std::string rest(path);
		size_t start = 0;
		while (start <= rest.size())
		{
			size_t dot = rest.find('.', start);
			std::string part = rest.substr(start, dot == std::string::npos ? std::string::npos : dot - start);
			if (!current.IsObject() || !current.GetMember(part.c_str(), &current)) return false;
			if (dot == std::string::npos) break;
			start = dot + 1;
		}
		*out = current;
		return true;
	}
	void CreateFunction(Value * out, FunctionHandler * handler, void * = NULL)
	{
		out->type_ = Value::VT_Function;
		handler->AddRef();
		out->function_ = *handler;
		++functions;
	}
	Value root;
	int functions = 0;
};
}
}
using namespace Scaleform;

// --- GUI components (fixtures) ------------------------------------------------------------
namespace BW {
class SimpleGUIComponent : public PyObjectPlus
{
	Py_Header(SimpleGUIComponent, PyObjectPlus)
public:
	SimpleGUIComponent(const BW::string & = "", PyTypePlus * type = &s_type_) : PyObjectPlus(type), updates(0), draws(0) {}
	virtual void update(float, float, float) { ++updates; }
	virtual void drawSelf(bool, bool) { ++draws; }
	// SimpleGUIComponent.position: a mutable (x, y, z) holder.
	PyObject * pyGet_position()
	{
		if (!position_)
		{
			static PyObject * cls = PyObject_CallFunction((PyObject *)&PyType_Type, (char *)"s(O){}",
				"Position", (PyObject *)&PyBaseObject_Type);
			position_ = PyObject_CallObject(cls, NULL);
		}
		Py_XINCREF(position_);
		return position_;
	}
	int pySet_position(PyObject * v) { Py_XINCREF(v); Py_XDECREF(position_); position_ = v; return 0; }
	int updates, draws;
	PyObject * position_ = NULL;
};
PY_TYPEOBJECT(SimpleGUIComponent)
PY_BEGIN_METHODS(SimpleGUIComponent)
PY_END_METHODS()
PY_BEGIN_ATTRIBUTES(SimpleGUIComponent)
	PY_ATTRIBUTE(position)
PY_END_ATTRIBUTES()

namespace ScaleformBW {
class PyMovieView : public PyObjectPlus
{
	Py_Header(PyMovieView, PyObjectPlus)
public:
	PyMovieView() : PyObjectPlus(&s_type_), advances(0), globalMovie(true), movieVisible(false) {}
	GFx::Movie * pMovieView() const { return const_cast<GFx::Movie *>(&movie); }
	void showAsGlobalMovie(bool state) { globalMovie = state; }
	void visible(bool v) { movieVisible = v; }
	GFx::Movie movie;
	int advances;
	bool globalMovie, movieVisible;
};
PY_TYPEOBJECT(PyMovieView)
PY_BEGIN_METHODS(PyMovieView)
PY_END_METHODS()
PyGetSetDef * PyMovieView::s_getGetSets() { return PyObjectPlus::s_getGetSets(); }
typedef SmartPointer<PyMovieView> PyMovieViewPtr;

class FlashGUIComponent : public SimpleGUIComponent
{
	Py_Header(FlashGUIComponent, SimpleGUIComponent)
public:
	FlashGUIComponent(PyMovieView * movie, PyTypePlus * type = &s_type_) : SimpleGUIComponent("", type), pMovieView_(movie)
	{
		pMovieView_->showAsGlobalMovie(false);
		pMovieView_->visible(true);
	}
	void update(float dTime, float w, float h) override
	{
		SimpleGUIComponent::update(dTime, w, h);
		++pMovieView_->advances;
	}
	PyObject * pyGet_movie() { PyObject * o = pMovieView_.get(); Py_INCREF(o); return o; }
	int pySet_movie(PyObject *) { PyErr_SetString(PyExc_TypeError, "movie is read-only"); return -1; }
	PyMovieView * movieView() const { return pMovieView_.get(); }
protected:
	PyMovieViewPtr pMovieView_;
};
PY_TYPEOBJECT(FlashGUIComponent)
PY_BEGIN_METHODS(FlashGUIComponent)
PY_END_METHODS()
PY_BEGIN_ATTRIBUTES(FlashGUIComponent)
	PY_ATTRIBUTE(movie)
PY_END_ATTRIBUTES()
}
}

// The scene-query seam the SPG provider calls: a host-controlled ground plane.
namespace BW {
float hostGroundHeight = 0.f;
int hostSegmentQueries = 0;
float stockGunMarkerSegmentCollision(const Vector3 & start, const Vector3 & end)
{
	++hostSegmentQueries;
	const float a = start.y - hostGroundHeight, b = end.y - hostGroundHeight;
	if (a * b > 0.f || start.y == end.y) return -1.f;
	const float t = a / (start.y - end.y);
	const float dx = end.x - start.x, dy = end.y - start.y, dz = end.z - start.z;
	return t * std::sqrt(dx * dx + dy * dy + dz * dz);
}
}
