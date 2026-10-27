// Host for the patched SDK 4.2.22 TextFieldEx selection setters over the
// SDK's own Text::EditorKit colour owner. Placeholders are filled with text
// from the pinned SDK and patch_scaleform_sdk.py.
#include <cstdio>
#include <string>
#include <vector>

typedef unsigned int UInt32;
typedef unsigned short UInt16;
@SEL_DEFAULTS@
#define SF_UNUSED1(a) (void)(a)
#define SF_DEBUG_ARG(x) , x

static std::vector<std::string> events;

static void record(const char* format, unsigned value)
{
    char line[128];
    std::snprintf(line, sizeof(line), format, value);
    events.push_back(line);
}

namespace Scaleform { namespace GFx {

struct CharacterDef { enum CharacterDefType { Sprite = 1, TextField = 2 }; };

namespace Text {
class EditorKit;

// Render::Text::DocView: the selection colours the renderer reads.
struct DocView
{
    EditorKit* pEditorKit = 0;
    UInt32 textColor = 0, backgroundColor = 0;
    void SetSelectionTextColor(UInt32 color) { textColor = color; record("docview text %08x", color); }
    void SetSelectionBackgroundColor(UInt32 color)
    {
        backgroundColor = color;
        record("docview background %08x", color);
    }
};

class EditorKit
{
public:
    enum { Flags_ShiftPressed = 0x40, Flags_MouseCaptured = 0x20, Flags_Focused = 0x400 };
    explicit EditorKit(DocView* pdocview) : pDocView(pdocview), Flags(0)
    {
        ActiveSelectionBkColor      = GFX_ACTIVE_SEL_BKCOLOR;
        ActiveSelectionTextColor    = GFX_ACTIVE_SEL_TEXTCOLOR;
        InactiveSelectionBkColor    = GFX_INACTIVE_SEL_BKCOLOR;
        InactiveSelectionTextColor  = GFX_INACTIVE_SEL_TEXTCOLOR;
        pdocview->pEditorKit = this;
    }
    void ClearMouseCaptured()   { Flags &= (~Flags_MouseCaptured); }
    void ClearShiftPressed()   { Flags &= (~Flags_ShiftPressed); }
@FOCUS_FLAGS@
@SELECTION_API@
    void OnSetFocus();
    void OnKillFocus();
    void InvalidateSelectionColors();

    DocView*            pDocView;
    UInt32              ActiveSelectionBkColor;
    UInt32              ActiveSelectionTextColor;
    UInt32              InactiveSelectionBkColor;
    UInt32              InactiveSelectionTextColor;
    UInt16              Flags;
};
@EDITORKIT_BODIES@
} // namespace Text

struct DisplayObject
{
    CharacterDef::CharacterDefType type;
    CharacterDef::CharacterDefType GetType() const { return type; }
};

struct TextField : DisplayObject
{
    Text::DocView document;
    Text::EditorKit* GetEditorKit() const { return document.pEditorKit; }
};

namespace AS3 {
struct Object
{
    bool display = true;
};

inline bool AreDisplayObjectTraits(Object* obj) { return obj->display; }

namespace Instances {
namespace fl_display {
struct DisplayObject : Object
{
    GFx::DisplayObject* pDispObj = 0;
};
}
namespace fl_text {
struct TextField : fl_display::DisplayObject
{
    GFx::TextField* GetTextField() const { return static_cast<GFx::TextField*>(pDispObj); }
};
}
}

class Value
{
public:
    enum Kind { Undefined, Null, Number, ObjectKind };
    Value(Kind kind = Undefined, Object* object = 0) : kind_(kind), object_(object) {}
    bool IsNullOrUndefined() const { return kind_ == Undefined || kind_ == Null; }
    bool IsObject() const { return kind_ == ObjectKind; }
    Object* GetObject() const { return object_; }

private:
    Kind kind_;
    Object* object_;
};

class VM
{
public:
    enum ErrorID { eCheckTypeFailedError = 1034, eNullArgumentError = 1507 };
    struct Error
    {
        Error(ErrorID id, VM&, const char* argument) : id(id), argument(argument) {}
        Error(ErrorID id, VM&, const Value&, const char* argument) : id(id), argument(argument) {}
        ErrorID id;
        const char* argument;
    };
    bool exception = false;
    bool IsException() const { return exception; }
    void ThrowArgumentError(const Error& error) { raise("ArgumentError", error); }
    void ThrowTypeError(const Error& error) { raise("TypeError", error); }

private:
    void raise(const char* kind, const Error& error)
    {
        exception = true;
        char line[128];
        std::snprintf(line, sizeof(line), "%s #%d %s", kind, int(error.id), error.argument);
        events.push_back(line);
    }
};

struct ASVM : VM
{
    bool ExtensionsEnabled = true;
};

}
}}

namespace Scaleform { namespace GFx { namespace AS3 {
namespace Classes { namespace fl_gfx {
class TextFieldEx
{
public:
    explicit TextFieldEx(ASVM& vm) : vm_(vm) {}
    VM& GetVM() { return vm_; }
    void setSelectionTextColor(const Value& result, const Value& textField, UInt32 color);
    void setSelectionBkgColor(const Value& result, const Value& textField, UInt32 color);
    void setInactiveSelectionTextColor(const Value& result, const Value& textField, UInt32 color);
    void setInactiveSelectionBkgColor(const Value& result, const Value& textField, UInt32 color);

private:
    ASVM& vm_;
};
@SETTER_BODIES@
}}
}}}

using namespace Scaleform::GFx;
using namespace Scaleform::GFx::AS3;

struct Field
{
    TextField native;
    Instances::fl_text::TextField instance;
    Text::EditorKit* kit = 0;
    explicit Field(bool editor)
    {
        native.type = CharacterDef::TextField;
        instance.pDispObj = &native;
        if (editor)
            kit = new Text::EditorKit(&native.document);
    }
    Value value() { return Value(Value::ObjectKind, &instance); }
    void dump(const char* label)
    {
        std::printf("%s", label);
        for (std::size_t i = 0; i < events.size(); ++i)
            std::printf(" | %s", events[i].c_str());
        if (kit)
            std::printf(" || kit %08x %08x %08x %08x focused=%d view %08x %08x",
                kit->ActiveSelectionTextColor, kit->ActiveSelectionBkColor,
                kit->InactiveSelectionTextColor, kit->InactiveSelectionBkColor,
                (kit->Flags & Text::EditorKit::Flags_Focused) != 0, native.document.textColor,
                native.document.backgroundColor);
        else
            std::printf(" || no editor (view kit %p)", static_cast<void*>(native.document.pEditorKit));
        std::printf("\n");
        events.clear();
    }
};

int main()
{
    ASVM vm;
    Classes::fl_gfx::TextFieldEx ex(vm);
    const Value none;

    // TextManager.setDefTextSelection(field): convert_u 4280163867 and 4288059015.
    Field field(true);
    field.dump("defaults");
    ex.setSelectionTextColor(none, field.value(), 4280163867u);
    ex.setSelectionBkgColor(none, field.value(), 4288059015u);
    ex.setInactiveSelectionTextColor(none, field.value(), 4280163867u);
    ex.setInactiveSelectionBkgColor(none, field.value(), 4288059015u);
    field.dump("text-manager");

    // TextAreaSimple/TextInput: one active colour then the fixed inactive one.
    ex.setSelectionTextColor(none, field.value(), 0xff112233u);
    ex.setSelectionBkgColor(none, field.value(), 0xff445566u);
    field.dump("active-unfocused");
    field.kit->OnSetFocus();
    field.dump("focus");
    ex.setInactiveSelectionBkgColor(none, field.value(), 0x80776655u);
    field.dump("inactive-while-focused");
    ex.setSelectionTextColor(none, field.value(), 0x00ffffffu);
    field.dump("active-while-focused");
    field.kit->OnKillFocus();
    field.dump("blur");

    // Without an EditorKit nothing is created or written.
    Field plain(false);
    ex.setSelectionBkgColor(none, plain.value(), 0xff000001u);
    ex.setInactiveSelectionTextColor(none, plain.value(), 0xff000002u);
    plain.dump("missing-editor");

    // Extensions off: valid fields are ignored, null is still rejected.
    vm.ExtensionsEnabled = false;
    ex.setSelectionTextColor(none, field.value(), 0xff999999u);
    field.dump("extensions-off");
    ex.setInactiveSelectionBkgColor(none, Value(Value::Null), 1u);
    vm.exception = false;
    field.dump("extensions-off-null");
    vm.ExtensionsEnabled = true;

    // Null/undefined are #1507 textField; other values fail the TextField coercion.
    ex.setSelectionTextColor(none, Value(Value::Undefined), 1u);
    vm.exception = false;
    ex.setSelectionBkgColor(none, Value(Value::Number), 1u);
    vm.exception = false;
    Instances::fl_text::TextField sprite;
    DisplayObject spriteNative;
    spriteNative.type = CharacterDef::Sprite;
    sprite.pDispObj = &spriteNative;
    ex.setInactiveSelectionTextColor(none, Value(Value::ObjectKind, &sprite), 1u);
    vm.exception = false;
    Instances::fl_text::TextField plainObject;
    plainObject.display = false;
    ex.setInactiveSelectionBkgColor(none, Value(Value::ObjectKind, &plainObject), 1u);
    vm.exception = false;
    field.dump("rejected");

    // A colour coercion that already threw leaves the editor untouched.
    vm.exception = true;
    ex.setSelectionBkgColor(none, field.value(), 0xff0000ffu);
    vm.exception = false;
    field.dump("pending-exception");
    return 0;
}
