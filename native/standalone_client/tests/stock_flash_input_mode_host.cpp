// Real CPython 2.7 runs the engine's attribute macros, Script::setData/getData(uint),
// SimpleGUIComponent::handleKeyEvent, SimpleGUI's root list and key loop, and the
// pristine or patched FlashGUIComponent. PyMovieView is a recording fixture: it stands
// for the GFx movie below FlashGUIComponent and decides only whether a key is consumed.
#include <Python.h>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#define BW_BEGIN_NAMESPACE namespace BW {
#define BW_END_NAMESPACE }
#define BW_GUARD
#define BW_GUARD_PROFILER(name)
#define INLINE inline

typedef unsigned int uint;

std::vector<std::string> hostEvents;
void record(const std::string & text) { hostEvents.push_back(text); }

#define WARNING_MSG(text) record(std::string("warning:") + text)

namespace BW {
template <class T> using vector = std::vector<T>;
typedef std::string string;
typedef PyTypeObject PyTypePlus;

struct Vector2 { float x, y; };
struct Vector3 { float x, y, z; };

// Only the key code and direction cross FlashGUIComponent; the movie fixture records them.
class KeyEvent {
public:
    KeyEvent(int key, bool down) : key_(key), down_(down) {}
    int key() const { return key_; }
    bool isKeyDown() const { return down_; }
    bool isMouseButton() const { return key_ >= 0x100 && key_ <= 0x107; }
    Vector2 cursorPosition() const { Vector2 v = {0.f, 0.f}; return v; }
private:
    int key_;
    bool down_;
};
class MouseEvent {};

template <class T> class SmartPointer {
public:
    SmartPointer(T * p = NULL) : p_(p) { Py_XINCREF(object()); }
    SmartPointer(const SmartPointer & o) : p_(o.p_) { Py_XINCREF(object()); }
    ~SmartPointer() { Py_XDECREF(object()); }
    SmartPointer & operator=(const SmartPointer & o) {
        T * old = p_; p_ = o.p_; Py_XINCREF(object()); Py_XDECREF(static_cast<PyObject *>(old)); return *this;
    }
    T * getObject() const { return p_; }
    T * operator->() const { return p_; }
    bool operator!() const { return p_ == NULL; }
    operator bool() const { return p_ != NULL; }
private:
    PyObject * object() const { return static_cast<PyObject *>(p_); }
    T * p_;
};

namespace Script {
int setData(PyObject * pObject, uint & rUint, const char * varName);
PyObject * getData(const uint data);
inline PyObject * getData(const PyObject * data) {
    PyObject * ret = (data != NULL) ? const_cast<PyObject *>(data) : Py_None;
    Py_INCREF(ret);
    return ret;
}
template <class T> PyObject * getReadOnlyData(const T & d) { return getData(d); }
inline PyObject * getData(const KeyEvent & event) { return Py_BuildValue("(ii)", event.key(), int(event.isKeyDown())); }
// Fixture: a script answer is an int-like truth value.
inline int setData(PyObject * o, bool & value, const char * name) {
    if (!PyInt_Check(o)) { PyErr_Format(PyExc_TypeError, "%s must be set to a bool", name); return -1; }
    value = PyInt_AsLong(o) != 0;
    return 0;
}
inline PyObject * ask(PyObject * function, PyObject * args, const char *, bool = false, bool = true) {
    PyObject * result = (function && args) ? PyObject_CallObject(function, args) : NULL;
    if (!result) PyErr_Print();
    Py_XDECREF(function);
    Py_XDECREF(args);
    return result;
}
template <class C> bool setAnswer(PyObject * result, C & value, const char * name) {
    if (!result) return false;
    int err = setData(result, value, name);
    Py_DECREF(result);
    if (err) PyErr_Print();
    return err == 0;
}
}
// ---- Public lib/pyscript/script.cpp uint conversions ----
@SCRIPT_UINT@

struct HostFactory { const char * module; const char * name; PyTypeObject * type; };
std::vector<HostFactory> & hostFactories() { static std::vector<HostFactory> v; return v; }
std::vector<PyTypeObject *> & hostTypes() { static std::vector<PyTypeObject *> v; return v; }
PyObject * hostMemberNames(PyObject * self, bool methods) {
    PyObject * names = PyList_New(0);
    for (PyTypeObject * t = Py_TYPE(self); t; t = t->tp_base) {
        if (methods) {
            for (PyMethodDef * m = t->tp_methods; m && m->ml_name; ++m) PyList_Append(names, PyString_FromString(m->ml_name));
        } else {
            for (PyGetSetDef * g = t->tp_getset; g && g->name; ++g)
                if (g->name[0] != '_') PyList_Append(names, PyString_FromString(g->name));
        }
    }
    return names;
}

class PyObjectPlus : public PyObject {
public:
    explicit PyObjectPlus(PyTypeObject * type) { PyObject_Init(this, type); }
    virtual ~PyObjectPlus() {}
    static void * operator new(std::size_t size) { return PyObject_Malloc(size); }
    static void operator delete(void * p) { PyObject_Free(p); }
    const char * typeName() const { return Py_TYPE(this)->tp_name; }
    static PyTypeObject s_type_;
};
PyTypeObject PyObjectPlus::s_type_ = { PyVarObject_HEAD_INIT(&PyType_Type, 0) };

template <class T> PyTypeObject hostType(const char * name, PyTypeObject * base) {
    PyTypeObject type = { PyVarObject_HEAD_INIT(&PyType_Type, 0) };
    type.tp_name = name;
    type.tp_basicsize = sizeof(T);
    type.tp_dealloc = [](PyObject * o) { delete static_cast<T *>(static_cast<PyObjectPlus *>(o)); };
    type.tp_getattro = PyObject_GenericGetAttr;
    type.tp_setattro = PyObject_GenericSetAttr;
    type.tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE;
    type.tp_methods = T::s_getMethodDefs();
    type.tp_getset = T::s_getAttributeDefs();
    type.tp_base = base;
    type.tp_new = (newfunc)T::_pyNew;
    return type;
}
struct HostTypeRegistration { explicit HostTypeRegistration(PyTypeObject * t) { hostTypes().push_back(t); } };
struct HostFactoryRegistration {
    HostFactoryRegistration(const char * m, const char * n, PyTypeObject * t) { hostFactories().push_back(HostFactory{m, n, t}); }
};

#define Py_Header(CLASS, SUPER) \
    public: \
        typedef CLASS This; typedef SUPER Super; \
        static PyTypeObject s_type_; \
        static bool Check(PyObject * o) { return o && PyObject_TypeCheck(o, &s_type_); } \
        static PyMethodDef * s_getMethodDefs(); \
        static PyGetSetDef * s_getAttributeDefs(); \
        static This * getSelf(PyObject * o) { return static_cast<This *>(static_cast<PyObjectPlus *>(o)); } \
        PyObject * pyGet___members__() { return BW::hostMemberNames(this, false); } \
        int pySet___members__(PyObject *) { PyErr_SetString(PyExc_TypeError, "__members__ is read-only"); return -1; } \
        PyObject * pyGet___methods__() { return BW::hostMemberNames(this, true); } \
        int pySet___methods__(PyObject *) { PyErr_SetString(PyExc_TypeError, "__methods__ is read-only"); return -1; } \
    private:
#define PY_FACTORY_DECLARE() \
    static PyObject * pyNew(PyObject * args); \
    static PyObject * _pyNew(PyObject *, PyObject * args, PyObject *) { return This::pyNew(args); }
#define PY_TYPEOBJECT(CLASS) \
    PyTypeObject CLASS::s_type_ = BW::hostType<CLASS>(#CLASS, &Super::s_type_); \
    static BW::HostTypeRegistration CLASS##_hostType(&CLASS::s_type_);
#define PY_BEGIN_METHODS(CLASS) \
    PyMethodDef * CLASS::s_getMethodDefs() { static std::vector<PyMethodDef> defs; if (defs.empty()) {
#define PY_END_METHODS() defs.push_back(PyMethodDef{NULL, NULL, 0, NULL}); } return defs.data(); }
// PyFactoryMethodLink: the type itself becomes MODULE.NAME and tp_name is qualified.
#define PY_FACTORY_NAMED(CLASS, NAME, MODULE) \
    static BW::HostFactoryRegistration CLASS##_hostFactory(#MODULE, NAME, &CLASS::s_type_);

// ---- Public lib/pyscript/pyobject_plus.hpp attribute macros ----
@PY_ATTRIBUTE_DECLARE_MACROS@
@PY_ATTRIBUTE_DEFINE_MACROS@

// ---- SimpleGUIComponent: the public input guards around a recording component ----
class SimpleGUIComponent;
typedef SmartPointer<SimpleGUIComponent> SimpleGUIComponentPtr;

template <class T> class WeakPyPtr {
public:
    WeakPyPtr() : p_(NULL) {}
    bool exists() const { return p_ != NULL; }
    T * operator->() const { return p_; }
    void set(T * p) { p_ = p; }
private:
    T * p_;
};

class SimpleGUIComponent : public PyObjectPlus {
    Py_Header(SimpleGUIComponent, PyObjectPlus)
public:
    SimpleGUIComponent(const BW::string & textureName, PyTypePlus * pType = &s_type_)
        : PyObjectPlus(pType), visible_(true), focus_(false), moveFocus_(false), textureName_(textureName) {
        position_.x = position_.y = position_.z = 0.f;
    }
    virtual ~SimpleGUIComponent() {}
    virtual void update(float, float, float) {}
    virtual void drawSelf(bool, bool) {}
    virtual bool handleKeyEvent(const KeyEvent & event);
    virtual bool handleMouseButtonEvent(const KeyEvent &) { return false; }
    virtual bool handleMouseEvent(const MouseEvent &) { return false; }
    SmartPointer<PyObject> script() { return pScriptObject_; }
    bool focus() const { return focus_; }
    bool handlesInput() const;
    bool allowDelete() const { return true; }
    const Vector3 & position() const { return position_; }
    static PyObject * _pyNew(PyObject *, PyObject *, PyObject *) {
        return new SimpleGUIComponent("", &SimpleGUIComponent::s_type_);
    }
    static PyGetSetDef s_hostAttributes_[];

    bool visible_, focus_, moveFocus_;
    Vector3 position_;
    std::string textureName_;
    WeakPyPtr<SimpleGUIComponent> parent_;
    typedef std::vector<std::pair<std::string, SimpleGUIComponentPtr> > ChildRecVector;
    ChildRecVector children_;
    SmartPointer<PyObject> pScriptObject_;
};

@HANDLES_INPUT@

@COMPONENT_HANDLE_KEY@

// Position proxy for component.position.z = ..., as every stock root writes it.
struct PositionProxy { PyObject_HEAD SimpleGUIComponent * owner; };
static void positionDealloc(PyObject * o) {
    Py_DECREF(reinterpret_cast<PositionProxy *>(o)->owner);
    PyObject_Del(o);
}
static PyObject * positionGetZ(PyObject * o, void *) {
    return PyFloat_FromDouble(reinterpret_cast<PositionProxy *>(o)->owner->position_.z);
}
static int positionSetZ(PyObject * o, PyObject * value, void *) {
    double z = PyFloat_AsDouble(value);
    if (PyErr_Occurred()) return -1;
    reinterpret_cast<PositionProxy *>(o)->owner->position_.z = float(z);
    return 0;
}
static PyGetSetDef positionAttributes[] = {
    {(char *)"z", positionGetZ, positionSetZ, NULL, NULL}, {NULL, NULL, NULL, NULL, NULL}};
static PyTypeObject PositionType = { PyVarObject_HEAD_INIT(&PyType_Type, 0) };

static PyObject * boolGet(bool value) { return PyBool_FromLong(value); }
static int boolSet(bool & field, PyObject * value) {
    int truth = PyObject_IsTrue(value);
    if (truth < 0) return -1;
    field = truth != 0;
    return 0;
}
#define HOST_BOOL(NAME, FIELD) \
    {(char *)#NAME, [](PyObject * s, void *) { return boolGet(SimpleGUIComponent::getSelf(s)->FIELD); }, \
     [](PyObject * s, PyObject * v, void *) { return boolSet(SimpleGUIComponent::getSelf(s)->FIELD, v); }, NULL, NULL}
PyGetSetDef SimpleGUIComponent::s_hostAttributes_[] = {
    HOST_BOOL(visible, visible_),
    HOST_BOOL(focus, focus_),
    HOST_BOOL(moveFocus, moveFocus_),
    {(char *)"script", [](PyObject * s, void *) { return Script::getData(getSelf(s)->pScriptObject_.getObject()); },
     [](PyObject * s, PyObject * v, void *) {
         getSelf(s)->pScriptObject_ = SmartPointer<PyObject>(v == Py_None ? NULL : v); return 0; }, NULL, NULL},
    {(char *)"position", [](PyObject * s, void *) {
         PositionProxy * p = PyObject_New(PositionProxy, &PositionType);
         p->owner = getSelf(s); Py_INCREF(p->owner); return (PyObject *)p; }, NULL, NULL, NULL},
    {(char *)"size", NULL, [](PyObject *, PyObject * v, void *) { return PyTuple_Check(v) && PyTuple_Size(v) == 2 ? 0 : -1; },
     NULL, NULL},
    {NULL, NULL, NULL, NULL, NULL}};
PyMethodDef * SimpleGUIComponent::s_getMethodDefs() { static PyMethodDef none[] = {{NULL, NULL, 0, NULL}}; return none; }
PyGetSetDef * SimpleGUIComponent::s_getAttributeDefs() { return s_hostAttributes_; }
PyTypeObject SimpleGUIComponent::s_type_ = BW::hostType<SimpleGUIComponent>("SimpleGUIComponent", &PyObjectPlus::s_type_);
static BW::HostTypeRegistration SimpleGUIComponent_hostType(&SimpleGUIComponent::s_type_);

// ---- SimpleGUI: the public root list, depth sort and key loop ----
class SimpleGUI {
public:
    typedef std::vector<SimpleGUIComponent *> Components;
    typedef std::vector<SimpleGUIComponent *> FocusList;
    static SimpleGUI & instance() { static SimpleGUI gui; return gui; }
    void addSimpleComponent(SimpleGUIComponent & c);
    void removeSimpleComponent(SimpleGUIComponent & c);
    void reSort(void);
    bool handleKeyEvent(const KeyEvent & event);
    // Key routing: the cursor is inactive, so the mouse-button branch is not taken.
    struct Cursor { bool isActive() const { return false; } };
    Cursor mouseCursor() const { return Cursor(); }
    SimpleGUIComponent * closestHitTest(FocusList &, const Vector2 &) { return NULL; }
    bool processClickKey(const KeyEvent &) { return false; }
    bool processDragKey(const KeyEvent &) { return false; }
    void recalcDrawOrders() {}
    Components components_;
    FocusList mouseButtonFocusList_;
};

@SIMPLE_GUI@

// ---- PyMovieView fixture ----
class PyMovieView : public PyObjectPlus {
public:
    explicit PyMovieView(const std::string & name) : PyObjectPlus(&s_type_), name_(name), consumes_(true) {
        record(name_ + ":created");
    }
    ~PyMovieView() { record(name_ + ":destroyed"); }
    static bool Check(PyObject * o) { return o && PyObject_TypeCheck(o, &s_type_); }
    void showAsGlobalMovie(bool b) { record(name_ + ":showAsGlobalMovie:" + std::to_string(int(b))); }
    void visible(bool b) { record(name_ + ":visible:" + std::to_string(int(b))); }
    bool handleKeyEvent(const KeyEvent & event) {
        record(name_ + ":key:" + std::to_string(event.key()) + ":" + std::to_string(int(event.isKeyDown())));
        return consumes_;
    }
    bool handleMouseEvent(const MouseEvent &) { record(name_ + ":mouse"); return consumes_; }
    bool handleMouseButtonEvent(const KeyEvent &) { record(name_ + ":mouseButton"); return consumes_; }
    static PyTypeObject s_type_;
    std::string name_;
    bool consumes_;
};
typedef SmartPointer<PyMovieView> PyMovieViewPtr;
PyTypeObject PyMovieView::s_type_ = { PyVarObject_HEAD_INIT(&PyType_Type, 0) };

namespace ScaleformBW {
using BW::PyMovieView;
using BW::PyMovieViewPtr;

@FLASH_CLASS@

@FLASH_DEFINITIONS@
// Drawing and ticking are outside input routing and never run here.
void FlashGUIComponent::update(float, float, float) { std::abort(); }
void FlashGUIComponent::drawSelf(bool, bool) { std::abort(); }

@FLASH_INPUT@
}
}

using BW::SimpleGUI;
using BW::SimpleGUIComponent;
using BW::PyMovieView;

static PyObject * movieGetAttr(PyObject * self, PyObject * name) { return PyObject_GenericGetAttr(self, name); }
static int movieSetAttr(PyObject * self, PyObject * name, PyObject * value) {
    PyObject * text = PyObject_Repr(value);
    record(static_cast<PyMovieView *>(static_cast<BW::PyObjectPlus *>(self))->name_ + ":set:" +
           PyString_AsString(name) + "=" + PyString_AsString(text));
    Py_DECREF(text);
    return 0;
}
static PyObject * movieCallback(PyObject * self, PyObject * args, const char * kind) {
    PyObject * callback;
    if (!PyArg_ParseTuple(args, "O", &callback)) return NULL;
    record(static_cast<PyMovieView *>(static_cast<BW::PyObjectPlus *>(self))->name_ + ":" + kind);
    Py_RETURN_NONE;
}
static PyMethodDef movieMethods[] = {
    {"setFSCommandCallback", [](PyObject * s, PyObject * a) { return movieCallback(s, a, "setFSCommandCallback"); },
     METH_VARARGS, NULL},
    {"setExternalInterfaceCallback",
     [](PyObject * s, PyObject * a) { return movieCallback(s, a, "setExternalInterfaceCallback"); }, METH_VARARGS, NULL},
    {"setFocussed", [](PyObject * s, PyObject * a) -> PyObject * {
         const char * path; if (!PyArg_ParseTuple(a, "s", &path)) return NULL;
         record(static_cast<PyMovieView *>(static_cast<BW::PyObjectPlus *>(s))->name_ + ":setFocussed:" + path);
         Py_RETURN_NONE; }, METH_VARARGS, NULL},
    {NULL, NULL, 0, NULL}};

struct MovieDef { PyObject_HEAD char path[256]; };
static PyObject * movieDefCreate(PyObject * self, PyObject *) {
    std::string path = reinterpret_cast<MovieDef *>(self)->path;
    return new PyMovieView(path.substr(path.rfind('/') + 1));
}
static PyMethodDef movieDefMethods[] = {{"createInstance", movieDefCreate, METH_NOARGS, NULL}, {NULL, NULL, 0, NULL}};
static PyTypeObject MovieDefType = { PyVarObject_HEAD_INIT(&PyType_Type, 0) };
static PyObject * movieDefNew(PyTypeObject *, PyObject * args, PyObject *) {
    const char * path;
    if (!PyArg_ParseTuple(args, "s", &path)) return NULL;
    MovieDef * def = PyObject_New(MovieDef, &MovieDefType);
    std::strncpy(def->path, path, sizeof(def->path) - 1);
    def->path[sizeof(def->path) - 1] = 0;
    record(std::string("MovieDef:") + path);
    return (PyObject *)def;
}

static SimpleGUIComponent * component(PyObject * o) {
    if (!SimpleGUIComponent::Check(o)) { PyErr_SetString(PyExc_TypeError, "expected a GUI component"); return NULL; }
    return SimpleGUIComponent::getSelf(o);
}
static PyMethodDef guiMethods[] = {
    {"addRoot", [](PyObject *, PyObject * a) -> PyObject * {
         PyObject * o; if (!PyArg_ParseTuple(a, "O", &o) || !component(o)) return NULL;
         SimpleGUI::instance().addSimpleComponent(*component(o)); Py_RETURN_NONE; }, METH_VARARGS, NULL},
    {"delRoot", [](PyObject *, PyObject * a) -> PyObject * {
         PyObject * o; if (!PyArg_ParseTuple(a, "O", &o) || !component(o)) return NULL;
         SimpleGUI::instance().removeSimpleComponent(*component(o)); Py_RETURN_NONE; }, METH_VARARGS, NULL},
    {"reSort", [](PyObject *, PyObject *) -> PyObject * { SimpleGUI::instance().reSort(); Py_RETURN_NONE; },
     METH_NOARGS, NULL},
    {"roots", [](PyObject *, PyObject *) -> PyObject * {
         PyObject * list = PyList_New(0);
         for (SimpleGUIComponent * c : SimpleGUI::instance().components_) PyList_Append(list, c);
         return list; }, METH_NOARGS, NULL},
    {NULL, NULL, 0, NULL}};

static PyMethodDef hostMethods[] = {
    // GUI.handleKeyEvent: one key through SimpleGUI's root loop.
    {"key", [](PyObject *, PyObject * a) -> PyObject * {
         int key, down = 1; if (!PyArg_ParseTuple(a, "i|i", &key, &down)) return NULL;
         return PyBool_FromLong(SimpleGUI::instance().handleKeyEvent(BW::KeyEvent(key, down != 0))); },
     METH_VARARGS, NULL},
    // One key straight into a component's virtual handleKeyEvent.
    {"componentKey", [](PyObject *, PyObject * a) -> PyObject * {
         PyObject * o; int key; if (!PyArg_ParseTuple(a, "Oi", &o, &key) || !component(o)) return NULL;
         return PyBool_FromLong(component(o)->handleKeyEvent(BW::KeyEvent(key, true))); }, METH_VARARGS, NULL},
    {"events", [](PyObject *, PyObject *) -> PyObject * {
         PyObject * list = PyList_New(0);
         for (const std::string & e : hostEvents) PyList_Append(list, PyString_FromString(e.c_str()));
         hostEvents.clear(); return list; }, METH_NOARGS, NULL},
    {"consumes", [](PyObject *, PyObject * a) -> PyObject * {
         PyObject * o; int value; if (!PyArg_ParseTuple(a, "Oi", &o, &value) || !PyMovieView::Check(o)) return NULL;
         static_cast<PyMovieView *>(static_cast<BW::PyObjectPlus *>(o))->consumes_ = value != 0; Py_RETURN_NONE; },
     METH_VARARGS, NULL},
    {"addChild", [](PyObject *, PyObject * a) -> PyObject * {
         PyObject * p, * c; if (!PyArg_ParseTuple(a, "OO", &p, &c) || !component(p) || !component(c)) return NULL;
         component(p)->children_.push_back(std::make_pair(std::string("child"), BW::SimpleGUIComponentPtr(component(c))));
         component(c)->parent_.set(component(p)); Py_RETURN_NONE; }, METH_VARARGS, NULL},
    {"refcount", [](PyObject *, PyObject * a) -> PyObject * {
         PyObject * o; if (!PyArg_ParseTuple(a, "O", &o)) return NULL; return PyInt_FromSsize_t(Py_REFCNT(o) - 1); },
     METH_VARARGS, NULL},
    {NULL, NULL, 0, NULL}};

int main(int argc, char ** argv) {
    Py_SetProgramName(argv[0]);
    Py_Initialize();
    BW::PyObjectPlus::s_type_.tp_name = "PyObjectPlus";
    BW::PyObjectPlus::s_type_.tp_basicsize = sizeof(BW::PyObjectPlus);
    BW::PyObjectPlus::s_type_.tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE;
    assert(PyType_Ready(&BW::PyObjectPlus::s_type_) == 0);
    for (PyTypeObject * type : BW::hostTypes()) assert(PyType_Ready(type) == 0);
    BW::PositionType.tp_name = "Vector3";
    BW::PositionType.tp_basicsize = sizeof(BW::PositionProxy);
    BW::PositionType.tp_dealloc = BW::positionDealloc;
    BW::PositionType.tp_flags = Py_TPFLAGS_DEFAULT;
    BW::PositionType.tp_getset = BW::positionAttributes;
    assert(PyType_Ready(&BW::PositionType) == 0);
    PyTypeObject & movie = PyMovieView::s_type_;
    movie.tp_name = "_Scaleform.PyMovieView";
    movie.tp_basicsize = sizeof(PyMovieView);
    movie.tp_dealloc = [](PyObject * o) { delete static_cast<PyMovieView *>(static_cast<BW::PyObjectPlus *>(o)); };
    movie.tp_getattro = movieGetAttr;
    movie.tp_setattro = movieSetAttr;
    movie.tp_flags = Py_TPFLAGS_DEFAULT;
    movie.tp_methods = movieMethods;
    assert(PyType_Ready(&movie) == 0);
    MovieDefType.tp_name = "_Scaleform.MovieDef";
    MovieDefType.tp_basicsize = sizeof(MovieDef);
    MovieDefType.tp_flags = Py_TPFLAGS_DEFAULT;
    MovieDefType.tp_methods = movieDefMethods;
    MovieDefType.tp_new = movieDefNew;
    assert(PyType_Ready(&MovieDefType) == 0);

    PyObject * gui = Py_InitModule("GUI", guiMethods);
    PyObject * scaleform = Py_InitModule("_Scaleform", NULL);
    PyObject * host = Py_InitModule("_flashhost", hostMethods);
    assert(gui && scaleform && host);
    Py_INCREF(&MovieDefType);
    PyModule_AddObject(scaleform, "MovieDef", (PyObject *)&MovieDefType);
    Py_INCREF(&SimpleGUIComponent::s_type_);
    PyModule_AddObject(host, "Component", (PyObject *)&SimpleGUIComponent::s_type_);
    static std::vector<std::string> names;
    for (const BW::HostFactory & f : BW::hostFactories()) names.push_back(std::string(f.module) + "." + f.name);
    for (size_t i = 0; i < names.size(); ++i) {
        const BW::HostFactory & f = BW::hostFactories()[i];
        assert(std::string(f.module) == "GUI");
        f.type->tp_name = names[i].c_str();
        Py_INCREF(f.type);
        PyModule_AddObject(gui, f.name, (PyObject *)f.type);
    }
    PySys_SetArgv(argc - 1, argv + 1);
    FILE * script = std::fopen(argv[1], "r");
    if (!script) return 2;
    int result = PyRun_SimpleFileEx(script, argv[1], 1);
    Py_Finalize();
    return result == 0 ? 0 : 1;
}
