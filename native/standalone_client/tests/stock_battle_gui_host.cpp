// CPython 2.7 host: the three production battle-GUI bindings over the
// recording fixture (stock_battle_gui_fixture.hpp), exposed as GUI.* factories.
#include "pch.hpp"
#include "stock_gun_marker_providers.cpp"
#include "stock_battle_crosshair.cpp"
#include "stock_battle_indicators.cpp"

using namespace BW;
using namespace BW::ScaleformBW;

namespace {
// Math.MatrixAnimation: keyframes of (time, Math.Matrix) sampled at `time`.
class HostAnimation : public MatrixProvider
{
public:
	HostAnimation() : MatrixProvider(false, &s_type_), time(0.f), samples(0) {}
	void matrix(Matrix & m) const override
	{
		++samples;
		if (frames.empty()) { m.setIdentity(); return; }
		size_t i = 0;
		while (i + 1 < frames.size() && frames[i + 1].first <= time) ++i;
		m = frames[i].second;
	}
	std::vector<std::pair<float, Matrix>> frames;
	float time;
	mutable int samples;
	static PyTypeObject s_type_;
	static PyMethodDef * s_getMethodDefs() { return PyObjectPlus::s_getMethodDefs(); }
	static PyGetSetDef * s_getGetSets();
};

bool readMatrix(PyObject * object, Matrix & m)
{
	PyObject * values = PyObject_GetAttrString(object, "_values");
	if (!values) return false;
	PyObject * seq = PySequence_Fast(values, "Math.Matrix values");
	Py_DECREF(values);
	if (!seq || PySequence_Fast_GET_SIZE(seq) != 16) { Py_XDECREF(seq); return false; }
	for (int i = 0; i != 16; ++i)
		(&m.m[0][0])[i] = static_cast<float>(PyFloat_AsDouble(PySequence_Fast_GET_ITEM(seq, i)));
	Py_DECREF(seq);
	return !PyErr_Occurred();
}

PyGetSetDef * HostAnimation::s_getGetSets()
{
	static PyGetSetDef defs[] = {
		{(char *)"time", [](PyObject * o, void *) -> PyObject * {
			return PyFloat_FromDouble(static_cast<HostAnimation *>(static_cast<PyObjectPlus *>(o))->time); },
		 [](PyObject * o, PyObject * v, void *) -> int {
			static_cast<HostAnimation *>(static_cast<PyObjectPlus *>(o))->time = static_cast<float>(PyFloat_AsDouble(v));
			return PyErr_Occurred() ? -1 : 0; }, NULL, NULL},
		{(char *)"keyframes", NULL, [](PyObject * o, PyObject * v, void *) -> int {
			HostAnimation * self = static_cast<HostAnimation *>(static_cast<PyObjectPlus *>(o));
			PyObject * seq = PySequence_Fast(v, "keyframes");
			if (!seq) return -1;
			std::vector<std::pair<float, Matrix>> frames;
			for (Py_ssize_t i = 0; i != PySequence_Fast_GET_SIZE(seq); ++i)
			{
				PyObject * frame = PySequence_Fast_GET_ITEM(seq, i);
				Matrix m;
				if (!PyTuple_Check(frame) || PyTuple_GET_SIZE(frame) != 2 ||
					!readMatrix(PyTuple_GET_ITEM(frame, 1), m))
				{ Py_DECREF(seq); PyErr_SetString(PyExc_TypeError, "keyframe"); return -1; }
				frames.push_back(std::make_pair(static_cast<float>(PyFloat_AsDouble(PyTuple_GET_ITEM(frame, 0))), m));
			}
			Py_DECREF(seq);
			self->frames = frames;
			return 0; }, NULL, NULL},
		{NULL, NULL, NULL, NULL, NULL}};
	return defs;
}
PyTypeObject HostAnimation::s_type_ = hostType<HostAnimation>("Math.MatrixAnimation", &MatrixProvider::s_type_);

template <class T> T * as(PyObject * o) { return static_cast<T *>(static_cast<PyObjectPlus *>(o)); }

PyMovieView * movieArg(PyObject * o)
{
	if (!PyMovieView::Check(o)) { PyErr_SetString(PyExc_TypeError, "movie expected"); return NULL; }
	return as<PyMovieView>(o);
}

std::shared_ptr<GFx::HostNode> nodeAt(PyMovieView * movie, const char * path)
{
	GFx::Value v;
	if (!movie->pMovieView()->GetVariable(&v, path) || !v.node_) return nullptr;
	return v.node_;
}

PyObject * matrixTuple(const float * values, int n)
{
	PyObject * t = PyTuple_New(n);
	for (int i = 0; i != n; ++i) PyTuple_SET_ITEM(t, i, PyFloat_FromDouble(values[i]));
	return t;
}

bool floatsArg(PyObject * seq, float * out, int n)
{
	PyObject * fast = PySequence_Fast(seq, "floats");
	if (!fast) return false;
	if (PySequence_Fast_GET_SIZE(fast) != n) { Py_DECREF(fast); PyErr_SetString(PyExc_ValueError, "length"); return false; }
	for (int i = 0; i != n; ++i) out[i] = static_cast<float>(PyFloat_AsDouble(PySequence_Fast_GET_ITEM(fast, i)));
	Py_DECREF(fast);
	return !PyErr_Occurred();
}

PyObject * hostCreateMovie(PyObject *, PyObject *) { return new PyMovieView(); }

// addClip(movie, path, kind): kind 'display' | 'object' | 'null' | 'function' | number.
PyObject * hostAddClip(PyObject *, PyObject * args)
{
	PyObject * m; const char * path; PyObject * kind;
	if (!PyArg_ParseTuple(args, "OsO", &m, &path, &kind)) return NULL;
	PyMovieView * movie = movieArg(m);
	if (!movie) return NULL;
	GFx::Value current = movie->movie.root;
	std::string p(path);
	size_t start = 0;
	while (true)
	{
		size_t dot = p.find('.', start);
		std::string part = p.substr(start, dot == std::string::npos ? std::string::npos : dot - start);
		GFx::Value next;
		const bool last = dot == std::string::npos;
		if (last)
		{
			if (PyFloat_Check(kind) || PyInt_Check(kind)) { next.type_ = GFx::Value::VT_Number; next.number_ = PyFloat_AsDouble(kind); }
			else
			{
				const char * k = PyString_AsString(kind);
				if (!k) return NULL;
				if (!strcmp(k, "null")) next.type_ = GFx::Value::VT_Null;
				else if (!strcmp(k, "function")) next.type_ = GFx::Value::VT_Function;
				else { next.type_ = strcmp(k, "object") ? GFx::Value::VT_DisplayObject : GFx::Value::VT_Object; next.node_ = std::make_shared<GFx::HostNode>(); }
			}
			current.node_->members[part] = next;
			break;
		}
		if (!current.GetMember(part.c_str(), &next))
		{
			next.type_ = GFx::Value::VT_DisplayObject;
			next.node_ = std::make_shared<GFx::HostNode>();
			current.node_->members[part] = next;
		}
		current = next;
		start = dot + 1;
	}
	Py_RETURN_NONE;
}

PyObject * hostSetActive(PyObject *, PyObject * args)
{
	PyObject * m; const char * path; int active;
	if (!PyArg_ParseTuple(args, "Osi", &m, &path, &active)) return NULL;
	PyMovieView * movie = movieArg(m);
	if (!movie) return NULL;
	auto node = nodeAt(movie, path);
	if (!node) { PyErr_SetString(PyExc_KeyError, path); return NULL; }
	node->active = active != 0;
	Py_RETURN_NONE;
}

PyObject * hostSetInfo(PyObject *, PyObject * args)
{
	PyObject * m; const char * path; double rotation; int visible;
	if (!PyArg_ParseTuple(args, "Osdi", &m, &path, &rotation, &visible)) return NULL;
	PyMovieView * movie = movieArg(m);
	if (!movie) return NULL;
	auto node = nodeAt(movie, path);
	if (!node) { PyErr_SetString(PyExc_KeyError, path); return NULL; }
	node->info.Rotation = rotation;
	node->info.Visible = visible != 0;
	Py_RETURN_NONE;
}

PyObject * hostInfo(PyObject *, PyObject * args)
{
	PyObject * m; const char * path;
	if (!PyArg_ParseTuple(args, "Os", &m, &path)) return NULL;
	PyMovieView * movie = movieArg(m);
	if (!movie) return NULL;
	auto node = nodeAt(movie, path);
	if (!node) { PyErr_SetString(PyExc_KeyError, path); return NULL; }
	const GFx::Value::DisplayInfo & d = node->info;
	PyObject * invokes = PyList_New(0);
	for (const auto & call : node->invokes)
	{
		PyObject * values = PyList_New(0);
		for (const GFx::Value & v : call.second)
		{
			PyObject * item = v.type_ == GFx::Value::VT_Boolean ? PyBool_FromLong(v.boolean_) : (Py_INCREF(Py_None), Py_None);
			PyList_Append(values, item);
			Py_DECREF(item);
		}
		PyObject * entry = Py_BuildValue("(sN)", call.first.c_str(), values);
		PyList_Append(invokes, entry);
		Py_DECREF(entry);
	}
	return Py_BuildValue("{s:d,s:d,s:d,s:d,s:d,s:O,s:i,s:i,s:i,s:N,s:N,s:N,s:N}",
		"x", d.X, "y", d.Y, "rotation", d.Rotation, "xscale", d.XScale, "yscale", d.YScale,
		"visible", d.Visible ? Py_True : Py_False, "vars", int(d.VarsSet), "writes", node->displayWrites,
		"matrixWrites", node->matrixWrites, "matrix3D", matrixTuple(&node->matrix3D.M[0][0], 12),
		"projection", matrixTuple(&d.Projection.M[0][0], 16), "view", matrixTuple(&d.View.M[0][0], 12),
		"invokes", invokes);
}

PyObject * hostMember(PyObject *, PyObject * args)
{
	PyObject * m; const char * path;
	if (!PyArg_ParseTuple(args, "Os", &m, &path)) return NULL;
	PyMovieView * movie = movieArg(m);
	if (!movie) return NULL;
	GFx::Value v;
	if (!movie->pMovieView()->GetVariable(&v, path)) Py_RETURN_NONE;
	static const char * names[] = {"undefined", "null", "boolean", "number", "string", "object", "display", "function"};
	return PyString_FromString(names[v.type_]);
}

// Calls an AS3 function slot installed through Movie::CreateFunction.
PyObject * hostCallFunction(PyObject *, PyObject * args)
{
	PyObject * m; const char * path;
	if (!PyArg_ParseTuple(args, "Os", &m, &path)) return NULL;
	PyMovieView * movie = movieArg(m);
	if (!movie) return NULL;
	GFx::Value v;
	if (!movie->pMovieView()->GetVariable(&v, path) || v.type_ != GFx::Value::VT_Function || !v.function_)
	{ PyErr_SetString(PyExc_KeyError, path); return NULL; }
	GFx::FunctionHandler::Params params = {};
	params.pMovie = movie->pMovieView();
	v.function_->Call(params);
	Py_RETURN_NONE;
}

PyObject * hostUpdate(PyObject *, PyObject * args)
{
	PyObject * c; float dt;
	if (!PyArg_ParseTuple(args, "Of", &c, &dt)) return NULL;
	if (!SimpleGUIComponent::Check(c)) { PyErr_SetString(PyExc_TypeError, "component"); return NULL; }
	as<SimpleGUIComponent>(c)->update(dt, 1.f, 1.f);
	Py_RETURN_NONE;
}

PyObject * hostDraw(PyObject *, PyObject * args)
{
	PyObject * c;
	if (!PyArg_ParseTuple(args, "O", &c)) return NULL;
	as<SimpleGUIComponent>(c)->drawSelf(true, false);
	Py_RETURN_NONE;
}

PyObject * hostComponent(PyObject *, PyObject * args)
{
	PyObject * c;
	if (!PyArg_ParseTuple(args, "O", &c)) return NULL;
	if (!FlashGUIComponent::Check(c)) { PyErr_SetString(PyExc_TypeError, "component"); return NULL; }
	FlashGUIComponent * f = as<FlashGUIComponent>(c);
	PyMovieView * movie = f->movieView();
	return Py_BuildValue("{s:i,s:i,s:i,s:O,s:O,s:n}", "updates", f->updates, "draws", f->draws,
		"advances", movie->advances, "global", movie->globalMovie ? Py_True : Py_False,
		"movieVisible", movie->movieVisible ? Py_True : Py_False, "refs", Py_REFCNT(c));
}

PyObject * hostSetCamera(PyObject *, PyObject * args)
{
	PyObject *view, *invView, *vp; float hw, hh, nearPlane, farPlane;
	if (!PyArg_ParseTuple(args, "OOOffff", &view, &invView, &vp, &hw, &hh, &nearPlane, &farPlane)) return NULL;
	Moo::RenderContext & rc = Moo::rc();
	if (!floatsArg(view, &rc.view_.m[0][0], 16) || !floatsArg(invView, &rc.invView_.m[0][0], 16) ||
		!floatsArg(vp, &rc.viewProjection_.m[0][0], 16)) return NULL;
	rc.halfWidth_ = hw; rc.halfHeight_ = hh;
	rc.camera_.nearPlane_ = nearPlane; rc.camera_.farPlane_ = farPlane;
	Py_RETURN_NONE;
}

PyObject * hostMatrix(PyObject *, PyObject * args)
{
	PyObject * values;
	if (!PyArg_ParseTuple(args, "O", &values)) return NULL;
	HostMatrix * m = new HostMatrix();
	if (!floatsArg(values, &m->value.m[0][0], 16)) { Py_DECREF(m); return NULL; }
	return m;
}

PyObject * hostSetMatrix(PyObject *, PyObject * args)
{
	PyObject * o, * values;
	if (!PyArg_ParseTuple(args, "OO", &o, &values)) return NULL;
	if (!PyObject_TypeCheck(o, &HostMatrix::s_type_)) { PyErr_SetString(PyExc_TypeError, "matrix"); return NULL; }
	if (!floatsArg(values, &as<HostMatrix>(o)->value.m[0][0], 16)) return NULL;
	Py_RETURN_NONE;
}

PyObject * hostSamples(PyObject *, PyObject * args)
{
	PyObject * o;
	if (!PyArg_ParseTuple(args, "O", &o)) return NULL;
	if (PyObject_TypeCheck(o, &HostMatrix::s_type_)) return PyInt_FromLong(as<HostMatrix>(o)->samples);
	if (PyObject_TypeCheck(o, &HostAnimation::s_type_)) return PyInt_FromLong(as<HostAnimation>(o)->samples);
	PyErr_SetString(PyExc_TypeError, "provider");
	return NULL;
}

PyObject * hostSample(PyObject *, PyObject * args)
{
	PyObject * o;
	if (!PyArg_ParseTuple(args, "O", &o)) return NULL;
	if (!MatrixProvider::Check(o)) { PyErr_SetString(PyExc_TypeError, "provider"); return NULL; }
	Matrix m;
	m.setZero();
	as<MatrixProvider>(o)->matrix(m);
	return matrixTuple(&m.m[0][0], 16);
}

PyObject * hostAnimation(PyObject *, PyObject *) { return new HostAnimation(); }

PyObject * hostProvider(PyObject *, PyObject * args)
{
	PyObject * o;
	if (!PyArg_ParseTuple(args, "O", &o)) return NULL;
	if (!WGAbstractGunMarkerDataProvider::Check(o)) { PyErr_SetString(PyExc_TypeError, "provider"); return NULL; }
	WGAbstractGunMarkerDataProvider * p = as<WGAbstractGunMarkerDataProvider>(o);
	PyObject * result = Py_BuildValue("{s:O,s:n}", "acquired", p->acquired() ? Py_True : Py_False, "refs", Py_REFCNT(o));
	if (WGSPGGunMarkerDataProvider::Check(o))
	{
		const auto & s = as<WGSPGGunMarkerDataProvider>(o)->state();
		PyObject * points = PyList_New(0);
		for (const auto & v : s.output) { PyObject * t = Py_BuildValue("(fff)", v.x, v.y, v.z); PyList_Append(points, t); Py_DECREF(t); }
		PyObject * current = PyList_New(0);
		for (const auto & v : s.current) { PyObject * t = Py_BuildValue("(fff)", v.x, v.y, v.z); PyList_Append(current, t); Py_DECREF(t); }
		PyDict_SetItemString(result, "output", points);
		PyDict_SetItemString(result, "current", current);
		PyObject * size = Py_BuildValue("(fff)", s.size, s.previousSize, s.outputSize);
		PyDict_SetItemString(result, "sizes", size);
		PyObject * angle = PyFloat_FromDouble(s.dispersionAngle);
		PyDict_SetItemString(result, "angle", angle);
		Py_DECREF(points); Py_DECREF(current); Py_DECREF(size); Py_DECREF(angle);
	}
	return result;
}

PyObject * hostSetGround(PyObject *, PyObject * args)
{
	float h;
	if (!PyArg_ParseTuple(args, "f", &h)) return NULL;
	BW::hostGroundHeight = h;
	Py_RETURN_NONE;
}

PyObject * hostState(PyObject *, PyObject *)
{
	return Py_BuildValue("{s:i,s:i,s:i}", "deallocations", BW::hostDeallocations,
		"handlers", Scaleform::GFx::hostLiveHandlers, "segmentQueries", BW::hostSegmentQueries);
}

PyObject * hostScreenResolution(PyObject *, PyObject *)
{
	return Py_BuildValue("(ff)", Moo::rc().halfWidth_ * 2.f, Moo::rc().halfHeight_ * 2.f);
}

std::vector<PyObject *> hostRoots;
PyObject * hostAddRoot(PyObject *, PyObject * args)
{
	PyObject * c;
	if (!PyArg_ParseTuple(args, "O", &c)) return NULL;
	Py_INCREF(c);
	hostRoots.push_back(c);
	Py_RETURN_NONE;
}
PyObject * hostDelRoot(PyObject *, PyObject * args)
{
	PyObject * c;
	if (!PyArg_ParseTuple(args, "O", &c)) return NULL;
	for (auto i = hostRoots.begin(); i != hostRoots.end(); ++i)
		if (*i == c) { hostRoots.erase(i); Py_DECREF(c); break; }
	Py_RETURN_NONE;
}
PyObject * hostRootCount(PyObject *, PyObject *) { return PyInt_FromSize_t(hostRoots.size()); }

PyObject * hostInitScripts(PyObject *, PyObject *)
{
	PyTypeObject * types[] = {&PyObjectPlus::s_type_, &SimpleGUIComponent::s_type_, &PyMovieView::s_type_,
		&FlashGUIComponent::s_type_, &MatrixProvider::s_type_, &HostMatrix::s_type_, &HostAnimation::s_type_,
		&WGAbstractGunMarkerDataProvider::s_type_, &WGGunMarkerDataProvider::s_type_,
		&WGSPGGunMarkerDataProvider::s_type_, &WGCrosshairGUIComponent::s_type_,
		&WGSPGCrosshairGUIComponent::s_type_, &WGHitIndicatorGUIComponent::s_type_,
		&WGTankIndicatorGUIComponent::s_type_};
	for (PyTypeObject * type : types)
		if (PyType_Ready(type) < 0) return NULL;
	PyObject * names = PyList_New(0);
	for (const HostFactory & factory : hostFactories())
	{
		PyObject * module = PyImport_AddModule(factory.module);
		PyMethodDef * def = new PyMethodDef{factory.name, (PyCFunction)factory.function, METH_VARARGS | METH_KEYWORDS, NULL};
		PyObject * function = PyCFunction_New(def, NULL);
		if (!module || !function || PyObject_SetAttrString(module, factory.name, function) != 0) return NULL;
		Py_DECREF(function);
		PyObject * name = PyString_FromFormat("%s.%s", factory.module, factory.name);
		PyList_Append(names, name);
		Py_DECREF(name);
	}
	return names;
}

PyObject * hostTypeInfo(PyObject *, PyObject * args)
{
	const char * name;
	if (!PyArg_ParseTuple(args, "s", &name)) return NULL;
	PyTypeObject * types[] = {&WGAbstractGunMarkerDataProvider::s_type_, &WGGunMarkerDataProvider::s_type_,
		&WGSPGGunMarkerDataProvider::s_type_, &WGCrosshairGUIComponent::s_type_,
		&WGSPGCrosshairGUIComponent::s_type_, &WGHitIndicatorGUIComponent::s_type_,
		&WGTankIndicatorGUIComponent::s_type_};
	for (PyTypeObject * type : types)
	{
		if (strcmp(type->tp_name, name)) continue;
		PyObject * methods = PyList_New(0);
		for (PyMethodDef * m = type->tp_methods; m && m->ml_name; ++m)
		{ PyObject * s = PyString_FromString(m->ml_name); PyList_Append(methods, s); Py_DECREF(s); }
		PyObject * getsets = PyList_New(0);
		for (PyGetSetDef * g = type->tp_getset; g && g->name; ++g)
		{ PyObject * s = PyString_FromString(g->name); PyList_Append(getsets, s); Py_DECREF(s); }
		return Py_BuildValue("{s:N,s:N,s:s}", "methods", methods, "getsets", getsets, "base", type->tp_base->tp_name);
	}
	PyErr_SetString(PyExc_KeyError, name);
	return NULL;
}

PyMethodDef hostMethods[] = {
	{"initScripts", hostInitScripts, METH_NOARGS, NULL},
	{"typeInfo", hostTypeInfo, METH_VARARGS, NULL},
	{"createMovie", hostCreateMovie, METH_NOARGS, NULL},
	{"addClip", hostAddClip, METH_VARARGS, NULL},
	{"setActive", hostSetActive, METH_VARARGS, NULL},
	{"setInfo", hostSetInfo, METH_VARARGS, NULL},
	{"info", hostInfo, METH_VARARGS, NULL},
	{"member", hostMember, METH_VARARGS, NULL},
	{"callFunction", hostCallFunction, METH_VARARGS, NULL},
	{"update", hostUpdate, METH_VARARGS, NULL},
	{"draw", hostDraw, METH_VARARGS, NULL},
	{"component", hostComponent, METH_VARARGS, NULL},
	{"setCamera", hostSetCamera, METH_VARARGS, NULL},
	{"matrix", hostMatrix, METH_VARARGS, NULL},
	{"setMatrix", hostSetMatrix, METH_VARARGS, NULL},
	{"samples", hostSamples, METH_VARARGS, NULL},
	{"sample", hostSample, METH_VARARGS, NULL},
	{"animation", hostAnimation, METH_NOARGS, NULL},
	{"provider", hostProvider, METH_VARARGS, NULL},
	{"setGround", hostSetGround, METH_VARARGS, NULL},
	{"state", hostState, METH_NOARGS, NULL},
	{"screenResolution", hostScreenResolution, METH_NOARGS, NULL},
	{"addRoot", hostAddRoot, METH_VARARGS, NULL},
	{"delRoot", hostDelRoot, METH_VARARGS, NULL},
	{"rootCount", hostRootCount, METH_NOARGS, NULL},
	{NULL, NULL, 0, NULL},
};
}

int main(int argc, char ** argv)
{
	if (argc < 2) return 2;
	Py_Initialize();
	PySys_SetArgv(argc - 1, argv + 1);
	Py_InitModule("_battle_gui_host", hostMethods);
	FILE * script = std::fopen(argv[1], "r");
	if (!script) return 2;
	const int failed = PyRun_SimpleFileEx(script, argv[1], 1);
	Py_Finalize();
	return failed ? 1 : 0;
}
