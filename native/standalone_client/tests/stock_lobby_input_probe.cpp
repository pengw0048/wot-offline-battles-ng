// Win32 probe: the patched lib/scaleform/util.cpp key bodies and the production
// language query against real USER32/NLS state on the probe's own thread. Key
// state changes use SetKeyboardState, which affects only the calling thread.
#include <windows.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "stock_lobby_input.hpp"

#define BW_GUARD
#define PY_AUTO_MODULE_FUNCTION(...)
typedef unsigned int uint32;
typedef int int32;

namespace BW {
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

namespace {
int failures = 0;

void check(bool passed, const char * text)
{
    std::printf("%s %s\n", passed ? "ok" : "FAIL", text);
    failures += passed ? 0 : 1;
}

GFx::KeyEvent translate(int key, int state, unsigned modifiers)
{
    return BW::ScaleformBW::translateKeyEvent(BW::KeyEvent(key, state, modifiers));
}

void setLock(const BYTE * saved, int key, BYTE value)
{
    BYTE state[256];
    std::memcpy(state, saved, sizeof(state));
    state[key] = value;
    SetKeyboardState(state);
}

std::string independentCode(LCID locale)
{
    wchar_t name[LOCALE_NAME_MAX_LENGTH] = {};
    wchar_t abbreviation[16] = {};
    if (!LCIDToLocaleName(locale, name, LOCALE_NAME_MAX_LENGTH, 0) ||
        !GetLocaleInfoEx(name, LOCALE_SABBREVLANGNAME, abbreviation, 16))
        return "<none>";
    std::string code;
    for (int i = 0; i < 2 && abbreviation[i]; ++i)
        code.push_back(char(abbreviation[i]));
    return code;
}
}

int main()
{
    using namespace BW::ScaleformBW;
    MSG message;
    PeekMessageW(&message, NULL, 0, 0, PM_NOREMOVE);
    BYTE saved[256];
    GetKeyboardState(saved);
    std::printf("thread locks before: caps=%02x num=%02x scroll=%02x\n", saved[VK_CAPITAL], saved[VK_NUMLOCK],
                saved[VK_SCROLL]);

    // Toggle bit 0 is the lock state; the down bit 0x80 alone is not.
    const struct { int key; bool (*getter)(); unsigned modifier; const char * name; } locks[] = {
        {VK_CAPITAL, wg_isCapsLockOn, 0x08, "caps"}, {VK_NUMLOCK, wg_isNumLockOn, 0x10, "num"},
        {VK_SCROLL, wg_isScrollLockOn, 0x20, "scroll"}};
    for (const auto & lock : locks)
    {
        for (BYTE value : {BYTE(0x00), BYTE(0x01), BYTE(0x80), BYTE(0x81)})
        {
            setLock(saved, lock.key, value);
            const bool on = (value & 1) != 0;
            char text[160];
            std::snprintf(text, sizeof(text), "%s state %02x: GetKeyState=%04x getter=%d modifier=%d", lock.name,
                          value, unsigned(GetKeyState(lock.key)) & 0xffff, int(lock.getter()),
                          int((translate(0x1E, 0, 0).Modifiers.States & lock.modifier) != 0));
            check(lock.getter() == on && ((translate(0x1E, 0, 0).Modifiers.States & lock.modifier) != 0) == on,
                  text);
        }
    }
    SetKeyboardState(saved);

    // One mode byte selects the table for every later key event.
    check(translate(0x30, 0, 0).KeyCode == SF::Key::None, "default mode drops B");
    wg_setRedefineKeysMode(true);
    check(translate(0x30, 0, 0).KeyCode == SF::Key::B, "redefine mode forwards B");
    check(translate(0x3C, 0, 0).KeyCode == SF::Key::F2, "redefine mode forwards F2");
    check(translate(0xB8, 0, 4).KeyCode == SF::Key::RightAlt && translate(0xB8, 0, 4).Modifiers.IsAltPressed(),
          "redefine mode: right Alt is RightAlt with AltPressed");
    check(translate(0x36, 0, 1).KeyCode == SF::Key::RightShift && translate(0x36, 0, 1).Modifiers.IsShiftPressed(),
          "right Shift is RightShift with ShiftPressed");
    check(translate(0x2A, 0, 1).KeyCode == SF::Key::Shift && !translate(0x2A, 0, 1).Modifiers.IsShiftPressed(),
          "left Shift is Shift without ShiftPressed");
    check(translate(0x30, 4, 0).Type == GFx::Event::KeyDown && translate(0x30, -1, 0).Type == GFx::Event::KeyUp,
          "repeat is KeyDown and release is KeyUp");
    wg_setRedefineKeysMode(false);
    check(translate(0x30, 0, 0).KeyCode == SF::Key::None && translate(0xB8, 0, 4).KeyCode == SF::Key::None,
          "default mode drops B and right Alt again");
    check(translate(0x9D, 0, 2).KeyCode == SF::Key::RightControl && translate(0x9D, 0, 2).Modifiers.IsCtrlPressed(),
          "default mode: right Ctrl is RightControl with CtrlPressed");

    BYTE after[256];
    GetKeyboardState(after);
    check(after[VK_CAPITAL] == saved[VK_CAPITAL] && after[VK_NUMLOCK] == saved[VK_NUMLOCK] &&
          after[VK_SCROLL] == saved[VK_SCROLL], "thread lock state restored");

    // The live layout of this thread and NLS abbreviations for other LCIDs.
    HKL layouts[32];
    const int count = GetKeyboardLayoutList(32, layouts);
    std::printf("installed layouts: %d", count);
    for (int i = 0; i < count; ++i)
        std::printf(" %08lx", static_cast<unsigned long>(reinterpret_cast<ULONG_PTR>(layouts[i])));
    std::printf("\n");
    const HKL active = GetKeyboardLayout(0);
    const std::string live = wot::stock_lobby_input::languageCode();
    const LCID activeLocale = LOWORD(reinterpret_cast<ULONG_PTR>(active));
    char text[160];
    std::snprintf(text, sizeof(text), "active layout %08lx: wg_getLangCode=%s independent=%s",
                  static_cast<unsigned long>(reinterpret_cast<ULONG_PTR>(active)), live.c_str(),
                  independentCode(activeLocale).c_str());
    check(live.size() == 2 && live == independentCode(activeLocale), text);
    for (LCID locale : {0x0409u, 0x0809u, 0x0419u, 0x0804u, 0x0404u, 0x0411u, 0x0c0cu, 0x0407u})
    {
        const std::string code = wot::stock_lobby_input::localeLanguageCode(locale);
        std::snprintf(text, sizeof(text), "NLS LCID %04lx: %s independent=%s", static_cast<unsigned long>(locale),
                      code.c_str(), independentCode(locale).c_str());
        check(code == independentCode(locale) && code.size() == 2, text);
    }
    // An LCID the OS cannot describe takes the contained failure path and yields "".
    for (LCID locale : {0x0badu, 0x1000u})
    {
        SetLastError(0);
        const int size = GetLocaleInfoW(locale, LOCALE_SABBREVLANGNAME, NULL, 0);
        const DWORD error = GetLastError();
        const std::string code = wot::stock_lobby_input::localeLanguageCode(locale);
        std::snprintf(text, sizeof(text), "LCID %04lx: length query=%d error=%lu code='%s'",
                      static_cast<unsigned long>(locale), size, static_cast<unsigned long>(error), code.c_str());
        if (size <= 0)
            check(code.empty(), text);
        else
            std::printf("info %s\n", text);
    }
    std::printf("failures=%d\n", failures);
    return failures;
}
