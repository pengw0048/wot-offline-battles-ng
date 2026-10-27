// Real CPython 2.7 runs the engine's PY_AUTO argument macros and the pristine
// or patched PyMovieView focus/setattr bodies over recording GFx fakes.
#include <Python.h>
#include <cassert>
#include <cstdarg>
#include <cstring>
#include <string>
#include <vector>

#define BW_BEGIN_NAMESPACE namespace BW {
#define BW_END_NAMESPACE }
#define BW_GUARD
#define BW_STATIC_ASSERT(expression, message) static_assert(expression, #message)
#define SCALEFORM_IME 0

namespace BW {
typedef std::string string;

class PyObjectPtr
{
public:
    enum StealReference { STEAL_REFERENCE };
    PyObjectPtr() : p_(NULL) {}
    PyObjectPtr(PyObject * p, StealReference) : p_(p) {}
    PyObjectPtr(const PyObjectPtr & other) : p_(other.p_) { Py_XINCREF(p_); }
    PyObjectPtr & operator=(const PyObjectPtr & other)
    {
        Py_XINCREF(other.p_);
        Py_XDECREF(p_);
        p_ = other.p_;
        return *this;
    }
    ~PyObjectPtr() { Py_XDECREF(p_); }
    PyObject * get() const { return p_; }
private:
    PyObject * p_;
};

namespace Script {
template <class T> struct IsValidArgType { static const bool isValid = true; };
int setData(PyObject * pObject, BW::string & rString, const char * varName);
PyObject * argCountError(const char * fn, int optas, int allas, ...);
}

@SCRIPT_BODIES@
}

@PYAUTO_MACROS@

std::vector<std::string> events;
bool setVariableResult = true;

template <class T> class Ptr
{
public:
    Ptr(T * p) : p_(p) {}
    T * operator->() const { return p_; }
    operator bool() const { return p_ != NULL; }
private:
    T * p_;
};

namespace GFx {
struct Event
{
    enum EventType { Unknown, MouseMove, MouseDown, MouseUp, MouseWheel, KeyDown, KeyUp,
                     SceneResize, SetFocus, KillFocus };
    Event(EventType type = Unknown) : Type(type), EventClassSize(sizeof(Event)) {}
    EventType Type;
    unsigned EventClassSize;
};
// SDK 4.2.22 MovieImpl::HandleEvent reads these states for every SetFocus.
struct SetFocusEvent : Event
{
    SetFocusEvent() : Event(SetFocus) { EventClassSize = sizeof(SetFocusEvent); }
    unsigned KeysStates[4] = {};
};
struct Value { PyObject * source; };
struct Movie
{
    enum SetVarType { SV_Normal };
    std::string name;
    unsigned HandleEvent(const Event & event)
    {
        if (event.Type == Event::SetFocus)
            events.push_back(name + (event.EventClassSize == sizeof(SetFocusEvent) ? ":SetFocus" :
                                                                                     ":SetFocus(truncated)"));
        else
            events.push_back(name + (event.Type == Event::KillFocus ? ":KillFocus" : ":Other"));
        return 0;
    }
    bool SetVariable(const char * path, const Value &, SetVarType type)
    {
        assert(type == SV_Normal);
        events.push_back(name + ":SetVariable:" + path);
        return setVariableResult;
    }
};
struct IMEManagerBase
{
    void SetIMEMoviePath(const char * path) { events.push_back(std::string("IMEPath:") + path); }
};
IMEManagerBase imeManager;
bool imeInstalled = false;
struct Loader
{
    Ptr<IMEManagerBase> GetIMEManager() const { return Ptr<IMEManagerBase>(imeInstalled ? &imeManager : NULL); }
};
}

namespace BW {
struct BWResolver
{
    static BW::string resolveFilename(const BW::string & path)
    {
        events.push_back("resolve:" + path);
        return "C:/res/" + path;
    }
};
struct ScriptString
{
    PyObject * object;
    const char * c_str() const { return PyString_AsString(object); }
    PyObject * get() const { return object; }
};
struct ScriptObject
{
    PyObject * object;
    PyObject * get() const { return object; }
};

namespace ScaleformBW {
struct Manager
{
    static Manager & instance() { static Manager manager; return manager; }
    GFx::Loader * pLoader() { return &loader; }
    void removeMovieView(PyObject *) { events.push_back("removeMovieView"); }
    GFx::Loader loader;
};
struct PyGFxValue
{
    static GFx::Value convertValueToGFx(PyObject * value, PyObject *) { return GFx::Value{value}; }
};
// PyObject_GenericSetAttr without a __dict__: only the declared attributes exist.
struct PyObjectPlus : PyObject
{
    float backgroundAlpha = 1.0f;
    bool pySetAttribute(const ScriptString & attr, const ScriptObject & value)
    {
        if (std::strcmp(attr.c_str(), "backgroundAlpha") == 0)
        {
            backgroundAlpha = float(PyFloat_AsDouble(value.get()));
            return true;
        }
        PyErr_Format(PyExc_AttributeError, "'PyMovieView' object has no attribute '%s'", attr.c_str());
        return false;
    }
};
}
}

#if HOST_PATCHED
#include "lib/scaleform/ime.hpp"
#endif

namespace BW { namespace ScaleformBW {
class PyMovieView : public PyObjectPlus
{
public:
    typedef PyMovieView This;
    static PyMovieView * getSelf(PyObject * self) { return static_cast<PyMovieView *>(self); }
    GFx::Movie * pMovieView() { return pMovieView_; }
    GFx::Movie movie;
    GFx::Movie * pMovieView_ = &movie;

@DECLARE@
    bool pySetAttribute(const ScriptString & attrObj, const ScriptObject & value);
    void teardown()
    {
@TEARDOWN@
    }
};

@SETFOCUSSED@
@SETATTR@
#if HOST_PATCHED
@IME@
#endif
}}

using BW::ScaleformBW::PyMovieView;
PyTypeObject movieType = { PyVarObject_HEAD_INIT(&PyType_Type, 0) };

void deallocMovie(PyObject * self)
{
    PyMovieView * view = static_cast<PyMovieView *>(self);
    view->teardown();
    view->~PyMovieView();
    PyObject_Free(self);
}

int setattrMovie(PyObject * self, PyObject * name, PyObject * value)
{
    assert(value);
    return static_cast<PyMovieView *>(self)->pySetAttribute(BW::ScriptString{name}, BW::ScriptObject{value}) ? 0 : -1;
}

PyObject * getattrMovie(PyObject * self, PyObject * name)
{
    if (std::strcmp(PyString_AsString(name), "backgroundAlpha") == 0)
        return PyFloat_FromDouble(static_cast<PyMovieView *>(self)->backgroundAlpha);
    return PyObject_GenericGetAttr(self, name);
}

PyMethodDef movieMethods[] = {
    {"setFocussed", (PyCFunction)PyMovieView::_py_setFocussed, METH_VARARGS | METH_KEYWORDS, NULL},
    {NULL, NULL, 0, NULL}};

PyObject * create(PyObject *, PyObject * args)
{
    const char * name;
    if (!PyArg_ParseTuple(args, "s", &name)) return NULL;
    void * memory = PyObject_Malloc(sizeof(PyMovieView));
    PyMovieView * view = new (memory) PyMovieView();
    PyObject_Init(view, &movieType);
    view->movie.name = name;
    return view;
}

PyObject * takeEvents(PyObject *, PyObject *)
{
    PyObject * list = PyList_New(0);
    for (size_t i = 0; i < events.size(); ++i)
    {
        PyObject * item = PyString_FromString(events[i].c_str());
        PyList_Append(list, item);
        Py_DECREF(item);
    }
    events.clear();
    return list;
}

PyObject * focussed(PyObject *, PyObject *)
{
#if HOST_PATCHED
    PyMovieView * view = BW::ScaleformBW::IME::pFocussedMovie();
    if (view) return PyString_FromString(view->movie.name.c_str());
#endif
    Py_RETURN_NONE;
}

PyObject * configure(PyObject *, PyObject * args)
{
    int ime, setVariable;
    if (!PyArg_ParseTuple(args, "ii", &ime, &setVariable)) return NULL;
    GFx::imeInstalled = ime != 0;
    setVariableResult = setVariable != 0;
    Py_RETURN_NONE;
}

PyMethodDef moduleMethods[] = {
    {"create", create, METH_VARARGS, NULL}, {"events", takeEvents, METH_NOARGS, NULL},
    {"focussed", focussed, METH_NOARGS, NULL}, {"configure", configure, METH_VARARGS, NULL},
    {NULL, NULL, 0, NULL}};

int main(int argc, char ** argv)
{
    assert(argc == 3);
    Py_Initialize();
    movieType.tp_name = "PyMovieView";
    movieType.tp_basicsize = sizeof(PyMovieView);
    movieType.tp_flags = Py_TPFLAGS_DEFAULT;
    movieType.tp_dealloc = deallocMovie;
    movieType.tp_setattro = setattrMovie;
    movieType.tp_getattro = getattrMovie;
    movieType.tp_methods = movieMethods;
    if (PyType_Ready(&movieType) < 0) return 2;
    Py_InitModule("_Scaleform", moduleMethods);
    char * pyArgv[] = {argv[1], argv[2]};
    PySys_SetArgv(2, pyArgv);
    FILE * script = std::fopen(argv[1], "r");
    int result = PyRun_SimpleFileExFlags(script, argv[1], 1, NULL);
    Py_Finalize();
    return result == 0 ? 0 : 1;
}
