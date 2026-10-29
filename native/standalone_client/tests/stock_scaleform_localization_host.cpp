// Host for the production stock_scaleform_localization.cpp. Placeholders are
// filled with the public BigWorld Script/FxDelegate text, the pinned SDK 4.2.22
// Translator, WStringBuffer, UTF-8 and TextField consumer text, and the
// patched SetResult bodies. Python is the pinned CPython 2.7.
#include <Python.h>
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

typedef unsigned char UInt8;
typedef unsigned int UInt32;
typedef size_t UPInt;
typedef ptrdiff_t SPInt;
#define SF_MAX_UPINT UPInt(~UPInt(0))
#define SF_STDCALL
#define SF_CDECL
#define SF_UNUSED(a) (void)(a)
#define SF_ASSERT(a) assert(a)
#define SF_DEBUG_WARNING(c, m) (void)(c)
#define SF_ALLOC(size, stat) std::malloc(size)
#define SF_FREE(p) std::free(p)

static std::vector<std::string> events;
static int liveObjects = 0;

static void record(const char * format, ...)
{
    char line[1024];
    va_list args;
    va_start(args, format);
    std::vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    events.push_back(line);
}

namespace Scaleform {
enum { Stat_Default_Mem = 0 };
namespace Alg {
template <class T> inline const T Min(const T a, const T b) { return (a < b) ? a : b; }
}

// Kernel RefCountBase/Ptr: a new object starts with one reference.
template <class C, int Stat>
class RefCountBase
{
public:
    RefCountBase() : RefCount(1) { ++liveObjects; }
    virtual ~RefCountBase() { --liveObjects; }
    void AddRef() { ++RefCount; }
    void Release() { if (--RefCount == 0) delete this; }
    int RefCount;
};

template <class C>
class Ptr
{
public:
    Ptr() : p(0) {}
    Ptr(C & object) : p(&object) {}
    Ptr(C * object) : p(object) { if (p) p->AddRef(); }
    Ptr(const Ptr & other) : p(other.p) { if (p) p->AddRef(); }
    ~Ptr() { if (p) p->Release(); }
    Ptr & operator=(C & object) { if (p) p->Release(); p = &object; return *this; }
    Ptr & operator=(C * object) { if (object) object->AddRef(); if (p) p->Release(); p = object; return *this; }
    Ptr & operator=(const Ptr & other) { return *this = other.p; }
    C * operator->() const { return p; }
    C & operator*() const { return *p; }
    operator C *() const { return p; }
    C * GetPtr() const { return p; }

private:
    C * p;
};

class String
{
public:
    String() {}
    String(const char * text) : text_(text) {}
    const char * ToCStr() const { return text_.c_str(); }
    static UPInt BernsteinHashFunction(const void *, UPInt) { return 0; }
    bool operator<(const String & other) const { return text_ < other.text_; }

private:
    std::string text_;
};

// Hash<String, ...> as the delegate uses it: Add, GetAlt, Remove, Clear.
template <class K, class V, class F>
class Hash
{
public:
    void Add(const K & key, const V & value) { map_[key.ToCStr()] = value; }
    V * GetAlt(const char * key)
    {
        typename std::map<std::string, V>::iterator it = map_.find(key);
        return it == map_.end() ? 0 : &it->second;
    }
    void Remove(const K & key) { map_.erase(key.ToCStr()); }
    void Clear() { map_.clear(); }

private:
    std::map<std::string, V> map_;
};

template <class T>
class Array
{
public:
    void PushBack(const T & value) { items_.push_back(value); }
    T & operator[](UPInt index) { return items_[index]; }
    UPInt GetSize() const { return items_.size(); }

private:
    std::vector<T> items_;
};

inline UPInt SF_CDECL SFstrlen(const char * str) { return std::strlen(str); }
@SDK_STD@

namespace UTF8Util {
SPInt SF_STDCALL GetLength(const char * putf8str, SPInt length = -1);
UInt32 SF_STDCALL DecodeNextChar_Advance0(const char ** putf8Buffer);
UPInt SF_STDCALL DecodeString(wchar_t * pbuff, const char * putf8str, SPInt bytesLen = -1);
int SF_STDCALL GetEncodeCharSize(UInt32 ucs_character);
@UTF8@
}

@WSTRING_CLASS@
@WSTRING_BODIES@

namespace GFx {
class Value
{
public:
    enum ValueType { VT_Undefined, VT_Null, VT_Number, VT_String, VT_StringW };
    Value() : type_(VT_Undefined), number_(0) {}
    Value(ValueType type) : type_(type), number_(0) {}
    Value(double number) : type_(VT_Number), number_(number) {}
    Value(const char * text) : type_(VT_String), number_(0), text_(text) {}
    // The SDK stores the pointer; the movie root copies it in SetExternalInterfaceRetVal.
    Value(const wchar_t * text) : type_(VT_StringW), number_(0), wide_(text) {}
    ValueType GetType() const { return type_; }
    bool IsNumber() const { return type_ == VT_Number; }
    double GetNumber() const { assert(IsNumber()); return number_; }
    const wchar_t * GetStringW() const { return wide_; }
    String ToString() const
    {
        char buffer[64];
        switch (type_)
        {
        case VT_Undefined: return String("undefined");
        case VT_Null: return String("null");
        case VT_Number: std::snprintf(buffer, sizeof(buffer), "%.15g", number_); return String(buffer);
        default: return String(text_.c_str());
        }
    }

private:
    ValueType type_;
    double number_;
    std::string text_;
    const wchar_t * wide_ = 0;
};

class Movie
{
public:
    // AS3 MovieRoot::SetExternalInterfaceRetVal converts the value immediately.
    void SetExternalInterfaceRetVal(const Value & value)
    {
        ++retCount;
        retType = value.GetType();
        if (value.GetType() == Value::VT_StringW)
            ret = value.GetStringW();
    }
    int retCount = 0;
    Value::ValueType retType = Value::VT_Undefined;
    std::wstring ret;
};

class ExternalInterface : public RefCountBase<ExternalInterface, Stat_Default_Mem>
{
public:
    virtual void Callback(Movie * pmovieView, const char * methodName, const Value * args, unsigned argCount) = 0;
};

class State : public RefCountBase<State, Stat_Default_Mem>
{
public:
    enum StateType { State_Translator = 9 };
    explicit State(StateType type) : SType(type) {}
    StateType SType;
};

struct WordWrapHelper
{
    enum { WWT_Asian = 0x1, WWT_Prohibition = 0x2, WWT_NoHangulWrap = 0x4, WWT_Last = WWT_NoHangulWrap };
};

@TRANSLATOR_CLASS@

@SET_RESULT@
// Word wrapping is not exercised; the translator keeps WWT_Default.
bool Translator::OnWordWrapping(LineFormatDesc *) { return false; }

// Loader::SetTranslator keeps one reference in the loader state bag.
class Loader
{
public:
    void SetTranslator(Translator * translator) { translator_ = translator; }
    Translator * GetTranslator() const { return translator_; }

private:
    Ptr<Translator> translator_;
};

// GFx::TextField::SetTextValue's local TranslateInfo.
@TEXT_FIELD_INFO@
} // namespace GFx
} // namespace Scaleform

using namespace Scaleform;

class FxDelegate;
class FxDelegateArgs;
@FX_DELEGATE_CLASSES@

using namespace GFx;
@FX_DELEGATE_BODIES@

namespace BW {
typedef std::string string;
typedef std::wstring wstring;
template <class T> using vector = std::vector<T>;
template <class K, class V> using map = std::map<K, V>;
template <class T> void bw_safe_delete(T *& p) { delete p; p = 0; }
} // namespace BW
#define BW_BEGIN_NAMESPACE namespace BW {
#define BW_END_NAMESPACE }
#define BW_NAMESPACE BW::
#define MF_ASSERT(x) assert(x)
#define MF_ASSERT_DEV(x) do { if (!(x)) record("MF_ASSERT_DEV %s", #x); } while (0)
#define MEMTRACKER_SCOPED(x)
#define ENABLE_PROFILER 0
#define ERROR_MSG(...) record("ERROR " __VA_ARGS__)
#define CRITICAL_MSG(...) record("CRITICAL " __VA_ARGS__)
#define CP_UTF8 65001

// MultiByteToWideChar(CP_UTF8, 0, src, -1, ...) for valid UTF-8 into UTF-16 units.
static int MultiByteToWideChar(int, int, const char * src, int, wchar_t * dst, int size)
{
    std::vector<wchar_t> units;
    const unsigned char * p = reinterpret_cast<const unsigned char *>(src);
    for (;;)
    {
        UInt32 c = *p++;
        int more = c >= 0xf0 ? 3 : c >= 0xe0 ? 2 : c >= 0xc0 ? 1 : 0;
        c &= more == 3 ? 0x07 : more == 2 ? 0x0f : more == 1 ? 0x1f : 0x7f;
        while (more--)
            c = (c << 6) | (*p++ & 0x3f);
        if (c >= 0x10000)
        {
            units.push_back(wchar_t(0xd800 + ((c - 0x10000) >> 10)));
            units.push_back(wchar_t(0xdc00 + ((c - 0x10000) & 0x3ff)));
        }
        else
            units.push_back(wchar_t(c));
        if (c == 0)
            break;
    }
    if (dst)
        std::copy(units.begin(), units.begin() + Alg::Min<int>(size, int(units.size())), dst);
    return int(units.size());
}

BW_BEGIN_NAMESPACE
static bool s_isFinalised = false;

namespace Script {
@FINI_JOB_CLASS@
void runFiniTimeJobs();
bool isFinalised();
PyObject * ask(PyObject * pFunction, PyObject * pArgs, const char * errorPrefix, bool okIfFunctionNull,
               bool printException);
PyObject * getData(const BW::string & data);
PyObject * getData(const BW::wstring & data);
@FINI_JOB_BODIES@
} // namespace Script

@SCRIPT_BODIES@

@UTF8TOW@
BW_END_NAMESPACE

@NUMERIC_OWNER@
@PRODUCTION@

// ----- _localization: the Python driver's view of the owner.

static Loader * loader = 0;
static Ptr<FxDelegate> delegate;
static PyObject * fallbackCalls = 0;

class RecordingFallback : public FxDelegateHandler
{
public:
    void Accept(CallbackProcessor *) {}
    // PyExternalInterfaceHandler::handleCallback stands for the DAAPI route.
    static void handle(const FxDelegateArgs & params, const char * methodName)
    {
        PyObject * entry = Py_BuildValue("(si)", methodName, int(params.GetArgCount()));
        PyList_Append(fallbackCalls, entry);
        Py_DECREF(entry);
    }
};

static PyObject * wide(const wchar_t * text, size_t length)
{
    return PyUnicode_FromWideChar(text, Py_ssize_t(length));
}

static PyObject * py_install(PyObject *, PyObject *)
{
    loader = new Loader;
    BW::ScaleformBW::installStockTranslator(*loader);
    Translator * translator = loader->GetTranslator();
    return Py_BuildValue("(II)", translator->GetCaps(), translator->WWMode);
}

static PyObject * py_uninstall(PyObject *, PyObject *)
{
    delete loader;
    loader = 0;
    Py_RETURN_NONE;
}

// TextField::SetTextValue with Cap_ReceiveHtml: the UTF-8 text becomes the key.
static PyObject * py_translate(PyObject *, PyObject * args)
{
    const char * text;
    int html = 0;
    if (!PyArg_ParseTuple(args, "s|i", &text, &html))
        return NULL;
    Ptr<Translator> translator = loader->GetTranslator();
    TranslateInfo info("label");
    if (!translator->CanReceiveHtml())
        return PyErr_Format(PyExc_AssertionError, "ReceiveHtml cap missing");
    info.SetKey(text);
    if (html)
        info.SetKeyHtml();
    translator->Translate(&info);
    if (!info.IsTranslated())
        return Py_BuildValue("(OO)", Py_False, info.IsResultHtml() ? Py_True : Py_False);
    PyObject * result = wide(info.ResultBuf.ToWStr(), std::wcslen(info.ResultBuf.ToWStr()));
    PyObject * tuple = Py_BuildValue("(OONn)", Py_True, info.IsResultHtml() ? Py_True : Py_False, result,
                                     Py_ssize_t(info.ResultBuf.GetLength()));
    return tuple;
}

// The patched SDK SetResult/SetResultHtml bodies on their own.
static PyObject * py_set_result(PyObject *, PyObject * args)
{
    PyObject * value;
    int html = 0;
    if (!PyArg_ParseTuple(args, "O|i", &value, &html))
        return NULL;
    TranslateInfo info("label");
    if (PyString_Check(value))
    {
        if (html)
            info.SetResultHtml(PyString_AsString(value));
        else
            info.SetResult(PyString_AsString(value));
    }
    else if (value == Py_None)
        info.SetResult(static_cast<const wchar_t *>(0));
    else
    {
        std::vector<wchar_t> text(PyUnicode_GetSize(value) + 1);
        PyUnicode_AsWideChar(reinterpret_cast<PyUnicodeObject *>(value), &text[0], text.size() - 1);
        info.SetResult(&text[0]);
    }
    // An untouched reserve buffer holds no text yet.
    PyObject * result = Py_None;
    if (info.IsTranslated())
        result = wide(info.ResultBuf.ToWStr(), std::wcslen(info.ResultBuf.ToWStr()));
    else
        Py_INCREF(result);
    return Py_BuildValue("(OONn)", info.IsTranslated() ? Py_True : Py_False,
                         info.IsResultHtml() ? Py_True : Py_False, result, Py_ssize_t(info.ResultBuf.GetLength()));
}

static PyObject * py_register(PyObject *, PyObject *)
{
    delegate = *new FxDelegate;
    BW::ScaleformBW::registerStockLocalization(*delegate);
    static Ptr<FxDelegateHandler> fallback = *new RecordingFallback;
    delegate->RegisterFallbackHandler(fallback, &RecordingFallback::handle);
    Py_RETURN_NONE;
}

// PyMovieView teardown: ClearAll then drop the delegate.
static PyObject * py_unregister(PyObject *, PyObject *)
{
    delegate->ClearAll();
    delegate = static_cast<FxDelegate *>(0);
    Py_RETURN_NONE;
}

// ExternalInterface.call(name, args...) as the movie delivers it.
static PyObject * py_callback(PyObject *, PyObject * args)
{
    const char * name;
    PyObject * values;
    if (!PyArg_ParseTuple(args, "sO!", &name, &PyTuple_Type, &values))
        return NULL;
    std::vector<std::string> texts(PyTuple_GET_SIZE(values));
    std::vector<GFx::Value> gfx(PyTuple_GET_SIZE(values) + 1);
    for (Py_ssize_t i = 0; i < PyTuple_GET_SIZE(values); ++i)
    {
        PyObject * item = PyTuple_GET_ITEM(values, i);
        if (PyString_Check(item))
        {
            texts[i] = PyString_AsString(item);
            gfx[i] = GFx::Value(texts[i].c_str());
        }
        else if (PyFloat_Check(item) || PyInt_Check(item))
            gfx[i] = GFx::Value(PyFloat_AsDouble(item));
        else if (item == Py_None)
            gfx[i] = GFx::Value(GFx::Value::VT_Null);
    }
    Movie movie;
    delegate->Callback(&movie, name, &gfx[0], unsigned(PyTuple_GET_SIZE(values)));
    if (movie.retCount == 0)
        Py_RETURN_NONE;
    if (movie.retCount != 1 || movie.retType != GFx::Value::VT_StringW)
        return PyErr_Format(PyExc_AssertionError, "unexpected return value");
    return wide(movie.ret.c_str(), movie.ret.size());
}

static PyObject * cached(PyObject * object)
{
    PyObject * result = object ? object : Py_None;
    Py_INCREF(result);
    return result;
}

static PyObject * py_cache(PyObject *, PyObject *)
{
    using namespace BW::ScaleformBW;
    PyObject * first = cached(s_makeString);
    PyObject * second = cached(s_makeStringJSON);
    return Py_BuildValue("(NN)", first, second);
}

// Script::fini runs later jobs (including Personality at INT_MAX) before setting
// isFinalised. Exercise both entry points in that actual fini-loop interval.
class LateLocalizationFini : public BW::Script::FiniTimeJob
{
public:
    LateLocalizationFini() : BW::Script::FiniTimeJob(2), calls(0) {}
    void fini()
    {
        ++calls;
        assert(!BW::Script::isFinalised());
        TranslateInfo info("late-fini-caption");
        info.SetKey("#dialogs:EULA/title");
        loader->GetTranslator()->Translate(&info);
        Movie movie;
        const GFx::Value args[] = {GFx::Value("#dialogs:EULA/title"), GFx::Value("{}")};
        delegate->Callback(&movie, "WG.getFormattedLocalizedString", args, 2);
        assert(!info.IsTranslated() && movie.retCount == 0);
    }
    int calls;
};

static PyObject * py_fini(PyObject *, PyObject *)
{
    static LateLocalizationFini laterJob;
    BW::Script::runFiniTimeJobs();
    return PyInt_FromLong(laterJob.calls);
}

static PyObject * py_set_finalised(PyObject *, PyObject * args)
{
    int value;
    if (!PyArg_ParseTuple(args, "i", &value))
        return NULL;
    BW::s_isFinalised = value != 0;
    Py_RETURN_NONE;
}

static PyObject * py_events(PyObject *, PyObject *)
{
    PyObject * list = PyList_New(0);
    for (size_t i = 0; i < events.size(); ++i)
    {
        PyObject * line = PyString_FromString(events[i].c_str());
        PyList_Append(list, line);
        Py_DECREF(line);
    }
    events.clear();
    return list;
}

static PyObject * py_live(PyObject *, PyObject *)
{
    return PyInt_FromLong(liveObjects);
}

static PyObject * py_numeric_receipt(PyObject *, PyObject *)
{
    return Py_BuildValue("(NIIkLIII)",
        PyUnicode_FromWideChar(lastInput.data(), lastInput.size()),
        numberCalls, currencyCalls, static_cast<unsigned long>(lastFlags), lastTime,
        dateCalls, timeCalls, lastNumber.NumDigits);
}

static PyObject * py_numeric_failure(PyObject *, PyObject * args)
{
    int failed;
    if (!PyArg_ParseTuple(args, "i", &failed))
        return NULL;
    formatFailure = failed != 0;
    Py_RETURN_NONE;
}

static PyMethodDef methods[] = {
    {"install", py_install, METH_NOARGS, NULL},
    {"uninstall", py_uninstall, METH_NOARGS, NULL},
    {"translate", py_translate, METH_VARARGS, NULL},
    {"set_result", py_set_result, METH_VARARGS, NULL},
    {"register", py_register, METH_NOARGS, NULL},
    {"unregister", py_unregister, METH_NOARGS, NULL},
    {"callback", py_callback, METH_VARARGS, NULL},
    {"cache", py_cache, METH_NOARGS, NULL},
    {"fini", py_fini, METH_NOARGS, NULL},
    {"set_finalised", py_set_finalised, METH_VARARGS, NULL},
    {"events", py_events, METH_NOARGS, NULL},
    {"live", py_live, METH_NOARGS, NULL},
    {"numeric_receipt", py_numeric_receipt, METH_NOARGS, NULL},
    {"numeric_failure", py_numeric_failure, METH_VARARGS, NULL},
    {NULL, NULL, 0, NULL}};

int main(int argc, char ** argv)
{
    if (argc < 2)
        return 2;
    Py_SetProgramName(argv[0]);
    Py_Initialize();
    localFixture.tm_year = 124;
    localFixture.tm_mon = 2;
    localFixture.tm_mday = 5;
    PySys_SetArgvEx(argc - 1, argv + 1, 0);
    PyObject * module = Py_InitModule("_localization", methods);
    fallbackCalls = PyList_New(0);
    PyModule_AddObject(module, "fallback_calls", fallbackCalls);
    Py_INCREF(fallbackCalls);
    FILE * script = std::fopen(argv[1], "r");
    if (!script)
        return 2;
    int result = PyRun_SimpleFileEx(script, argv[1], 1);
    Py_Finalize();
    return result == 0 ? 0 : 1;
}
