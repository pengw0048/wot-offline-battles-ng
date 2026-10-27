// Real CPython 2.7 runs the engine's PY_AUTO macros, the pristine or patched
// lib/scaleform/util.cpp key bodies and the production wg_getLangCode binding.
// GetKeyState and the keyboard-layout queries are recording fixtures here.
#include <Python.h>
#include <cassert>
#include <cstdarg>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <strings.h>
#include <vector>

#define BW_BEGIN_NAMESPACE namespace BW {
#define BW_END_NAMESPACE }
#define BW_GUARD
#define BW_STATIC_ASSERT(expression, message) static_assert(expression, #message)
#define _stricmp strcasecmp

typedef unsigned int uint32;
typedef int int32;
typedef unsigned int UINT;

// ---- Win32 fixture: thread key state and the active keyboard layout ----
typedef short SHORT;
typedef unsigned long DWORD;
typedef DWORD LCID;
typedef std::uintptr_t ULONG_PTR;
typedef void * HKL;
typedef const wchar_t * LPCWCH;
typedef char * LPSTR;
typedef const char * LPCCH;
typedef int BOOL;
#define VK_CAPITAL 0x14
#define VK_NUMLOCK 0x90
#define VK_SCROLL 0x91
#define LOCALE_SABBREVLANGNAME 0x3
#define CP_UTF8 65001
#define LOWORD(value) ((unsigned short)((ULONG_PTR)(value) & 0xffff))

std::vector<std::string> hostCalls;
std::map<int, int> keyStates;
std::uintptr_t activeLayout = 0x04090409;
std::map<unsigned, std::wstring> localeNames;
DWORD lastError = 0;

inline SHORT GetKeyState(int key)
{
    hostCalls.push_back("GetKeyState:" + std::to_string(key));
    return SHORT(keyStates[key]);
}
inline HKL GetKeyboardLayout(DWORD thread)
{
    hostCalls.push_back("GetKeyboardLayout:" + std::to_string(thread));
    return reinterpret_cast<HKL>(activeLayout);
}
inline int GetLocaleInfoW(LCID locale, DWORD type, wchar_t * data, int size)
{
    hostCalls.push_back("GetLocaleInfoW:" + std::to_string(locale) + ":" + std::to_string(type) + ":" +
                        std::to_string(size));
    std::map<unsigned, std::wstring>::const_iterator found = localeNames.find(locale);
    if (found == localeNames.end() || type != LOCALE_SABBREVLANGNAME)
    {
        lastError = 87;
        return 0;
    }
    const int needed = int(found->second.size() + 1);
    if (size == 0)
        return needed;
    if (size < needed)
    {
        lastError = 122;
        return 0;
    }
    std::memcpy(data, found->second.c_str(), sizeof(wchar_t) * needed);
    return needed;
}
inline int WideCharToMultiByte(unsigned page, DWORD, LPCWCH text, int count, LPSTR output, int size,
                               LPCCH, BOOL *)
{
    assert(page == CP_UTF8 && count == -1);
    std::string utf8;
    for (const wchar_t * c = text; ; ++c)
    {
        const unsigned value = unsigned(*c);
        if (value < 0x80)
            utf8.push_back(char(value));
        else if (value < 0x800)
        {
            utf8.push_back(char(0xc0 | value >> 6));
            utf8.push_back(char(0x80 | (value & 0x3f)));
        }
        else
        {
            utf8.push_back(char(0xe0 | value >> 12));
            utf8.push_back(char(0x80 | (value >> 6 & 0x3f)));
            utf8.push_back(char(0x80 | (value & 0x3f)));
        }
        if (!value)
            break;
    }
    if (size == 0)
        return int(utf8.size());
    if (size < int(utf8.size()))
        return 0;
    std::memcpy(output, utf8.data(), utf8.size());
    return int(utf8.size());
}
inline DWORD GetLastError() { return lastError; }

namespace BW {
typedef std::string string;

struct HostLink { const char * module; const char * name; PyCFunction function; };
std::vector<HostLink> & hostLinks() { static std::vector<HostLink> links; return links; }
struct PyModuleMethodLink
{
    PyModuleMethodLink(const char * module, const char * name, PyCFunction function, const char *)
    {
        hostLinks().push_back(HostLink{module, name, function});
    }
};

namespace Script {
template <class T> struct IsValidArgType { static const bool isValid = true; };
int setData(PyObject * pObject, bool & rBool, const char * varName = "");
PyObject * getData(const bool data);
PyObject * argCountError(const char * fn, int optas, int allas, ...);
// Public script.hpp: RETDATA returns through getData.
template <class T> PyObject * getRetData(const T & data) { return getData(data); }
}

@SCRIPT_BODIES@

@KEY_CODES@

// Public lib/input/input.hpp accessors over the #1513 KeyEvent fields.
class KeyEvent
{
public:
    KeyEvent(int key, int32 state, uint32 modifiers) : key_(KeyCode::Key(key)), state_(state),
        modifiers_(modifiers) {}
    KeyCode::Key key() const { return key_; }
    bool isKeyDown() const { return state_ >= 0; }
    bool isShiftDown() const { return !!(modifiers_ & MODIFIER_SHIFT); }
    bool isCtrlDown() const { return !!(modifiers_ & MODIFIER_CTRL); }
    bool isAltDown() const { return !!(modifiers_ & MODIFIER_ALT); }
private:
    KeyCode::Key key_;
    int32 state_;
    uint32 modifiers_;
};
}

@PYAUTO_MACROS@

namespace Scaleform {
typedef unsigned char UInt8;
typedef unsigned char UByte;
typedef unsigned int UInt32;
typedef unsigned int UInt;
template <int> class NewOverrideBase {};
enum { Stat_Default_Mem };
#define SF_DEBUG_EXPR(arg) ((void)0)

@SF_KEYS@

namespace GFx {
@GFX_EVENTS@
}
}
namespace SF = Scaleform;
namespace GFx = Scaleform::GFx;
typedef SF::UInt UInt;

namespace BW {
namespace ScaleformBW {
@UTIL@
}
}

#include "stock_lobby_input.cpp"

namespace {
PyObject * translate(PyObject *, PyObject * args)
{
    int key, state;
    unsigned modifiers;
    if (!PyArg_ParseTuple(args, "iiI", &key, &state, &modifiers))
        return NULL;
    const GFx::KeyEvent event = BW::ScaleformBW::translateKeyEvent(BW::KeyEvent(key, state, modifiers));
    return Py_BuildValue("(iiiiii)", int(event.Type), int(event.Modifiers.States), int(event.KeyCode),
                         int(event.AsciiCode), int(event.WcharCode), int(event.KeyboardIndex));
}

PyObject * setKeyState(PyObject *, PyObject * args)
{
    int key, state;
    if (!PyArg_ParseTuple(args, "ii", &key, &state))
        return NULL;
    keyStates[key] = state;
    Py_RETURN_NONE;
}

PyObject * setLayout(PyObject *, PyObject * args)
{
    unsigned long layout;
    if (!PyArg_ParseTuple(args, "k", &layout))
        return NULL;
    activeLayout = layout;
    Py_RETURN_NONE;
}

PyObject * setLocaleName(PyObject *, PyObject * args)
{
    unsigned locale;
    PyObject * name;
    if (!PyArg_ParseTuple(args, "IO", &locale, &name))
        return NULL;
    if (name == Py_None)
    {
        localeNames.erase(locale);
        Py_RETURN_NONE;
    }
    if (!PyUnicode_Check(name))
        return NULL;
    std::wstring text;
    const Py_UNICODE * data = PyUnicode_AS_UNICODE(name);
    for (Py_ssize_t i = 0; i < PyUnicode_GET_SIZE(name); ++i)
        text.push_back(wchar_t(data[i]));
    localeNames[locale] = text;
    Py_RETURN_NONE;
}

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

PyMethodDef inputMethods[] = {
    {"translate", translate, METH_VARARGS, NULL},
    {"setKeyState", setKeyState, METH_VARARGS, NULL},
    {"setLayout", setLayout, METH_VARARGS, NULL},
    {"setLocaleName", setLocaleName, METH_VARARGS, NULL},
    {"calls", calls, METH_NOARGS, NULL},
    {NULL, NULL, 0, NULL}};
}

int main(int argc, char ** argv)
{
    assert(argc >= 2);
    Py_Initialize();
    static std::vector<PyMethodDef> bigWorld;
    for (const BW::HostLink & link : BW::hostLinks())
    {
        assert(std::strcmp(link.module, "BigWorld") == 0);
        bigWorld.push_back(PyMethodDef{link.name, link.function, METH_VARARGS, NULL});
    }
    bigWorld.push_back(PyMethodDef{NULL, NULL, 0, NULL});
    Py_InitModule("BigWorld", bigWorld.data());
    Py_InitModule("_input", inputMethods);
    PySys_SetArgv(argc - 1, argv + 1);
    FILE * script = std::fopen(argv[1], "r");
    assert(script);
    const int failed = PyRun_SimpleFileEx(script, argv[1], 1);
    Py_Finalize();
    return failed ? 1 : 0;
}
