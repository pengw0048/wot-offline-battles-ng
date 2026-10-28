#!/usr/bin/env python3
"""Restore #1513 TextFieldEx vertical autosize using the existing DocView layout owner."""
import argparse
from pathlib import Path

HEADER = 'Src/GFx/AS3/Obj/Gfx/AS3_Obj_Gfx_TextFieldEx.h'
SOURCE = 'Src/GFx/AS3/Obj/Gfx/AS3_Obj_Gfx_TextFieldEx.cpp'
BODIES = '''    // #1513 e24ec0/e25020: vertical autosize shares DocView's actual
    // autosize-Y and vertical alignment; horizontal autosize is independent.
    static GFx::TextField* VerticalAutoSizeTextField(VM& vm, const Value& value)
    {
        if (vm.IsException()) return NULL;
        if (value.IsNullOrUndefined())
        {
            vm.ThrowArgumentError(VM::Error(VM::eNullArgumentError, vm SF_DEBUG_ARG("textField")));
            return NULL;
        }
        AS3::Object* object = value.IsObject() ? value.GetObject() : NULL;
        if (!object || !AreDisplayObjectTraits(object) ||
            !static_cast<Instances::fl_display::DisplayObject*>(object)->pDispObj ||
            static_cast<Instances::fl_display::DisplayObject*>(object)->pDispObj->GetType() != CharacterDef::TextField)
        {
            vm.ThrowTypeError(VM::Error(VM::eCheckTypeFailedError, vm
                SF_DEBUG_ARG(value) SF_DEBUG_ARG("flash.text.TextField")));
            return NULL;
        }
        return static_cast<Instances::fl_text::TextField*>(object)->GetTextField();
    }

    void TextFieldEx::setVerticalAutoSize(const Value& result, const Value& textField, const ASString& value)
    {
        SF_UNUSED1(result);
        GFx::TextField* field = VerticalAutoSizeTextField(GetVM(), textField);
        if (!field || !static_cast<ASVM&>(GetVM()).ExtensionsEnabled) return;
        if (value == "none")
        {
            field->ClearAutoSizeY();
            field->SetVAlignment(Text::DocView::VAlign_None);
        }
        else
        {
            field->SetAutoSizeY();
            if (value == "top") field->SetVAlignment(Text::DocView::VAlign_Top);
            else if (value == "bottom") field->SetVAlignment(Text::DocView::VAlign_Bottom);
            else if (value == "center") field->SetVAlignment(Text::DocView::VAlign_Center);
        }
        field->SetDirtyFlag();
    }

    void TextFieldEx::getVerticalAutoSize(ASString& result, const Value& textField)
    {
        GFx::TextField* field = VerticalAutoSizeTextField(GetVM(), textField);
        if (!field) return;
        result = "none";
        if (field->IsAutoSizeY())
            switch (field->GetVAlignment())
            {
            case Text::DocView::VAlign_Top: result = "top"; break;
            case Text::DocView::VAlign_Bottom: result = "bottom"; break;
            case Text::DocView::VAlign_Center: result = "center"; break;
            default: break;
            }
    }
'''
SEAMS = (
    ('Src/GFx/AS3/Obj/Gfx/AS3_Obj_Gfx_TextFieldEx.h', '        enum { MemberInfoNum = 7 };', '        enum { MemberInfoNum = 11 };'),
    ('Src/GFx/AS3/Obj/Gfx/AS3_Obj_Gfx_TextFieldEx.h', '        enum { ThunkInfoNum = 14 };', '        enum { ThunkInfoNum = 16 };'),
    ('Src/GFx/AS3/Obj/Gfx/AS3_Obj_Gfx_TextFieldEx.h', '            mid_getNoTranslate, \n', '            mid_getNoTranslate, \n            mid_setVerticalAutoSize,\n            mid_getVerticalAutoSize,\n'),
    ('Src/GFx/AS3/Obj/Gfx/AS3_Obj_Gfx_TextFieldEx.h', '        void getNoTranslate(bool& result, Instances::fl_text::TextField* textField);', '        void getNoTranslate(bool& result, Instances::fl_text::TextField* textField);\n        void setVerticalAutoSize(const Value& result, const Value& textField, const ASString& value);\n        void getVerticalAutoSize(ASString& result, const Value& textField);'),
    ('Src/GFx/AS3/Obj/Gfx/AS3_Obj_Gfx_TextFieldEx.h', '        const char* VALIGN_NONE;\n', '        const char* VALIGN_NONE;\n        const char* VAUTOSIZE_NONE;\n        const char* VAUTOSIZE_TOP;\n        const char* VAUTOSIZE_CENTER;\n        const char* VAUTOSIZE_BOTTOM;\n'),
    ('Src/GFx/AS3/Obj/Gfx/AS3_Obj_Gfx_TextFieldEx.cpp', '    , VALIGN_NONE("none")', '    , VALIGN_NONE("none")\n    , VAUTOSIZE_NONE("none")\n    , VAUTOSIZE_TOP("top")\n    , VAUTOSIZE_CENTER("center")\n    , VAUTOSIZE_BOTTOM("bottom")'),
    ('Src/GFx/AS3/Obj/Gfx/AS3_Obj_Gfx_TextFieldEx.cpp', '    void TextFieldEx::setVerticalAlign(', BODIES + '    void TextFieldEx::setVerticalAlign('),
    ('Src/GFx/AS3/Obj/Gfx/AS3_Obj_Gfx_TextFieldEx.cpp', 'template <> const TFunc_Classes_TextFieldEx_getNoTranslate::TMethod TFunc_Classes_TextFieldEx_getNoTranslate::Method = &Classes::fl_gfx::TextFieldEx::getNoTranslate;\n', 'template <> const TFunc_Classes_TextFieldEx_getNoTranslate::TMethod TFunc_Classes_TextFieldEx_getNoTranslate::Method = &Classes::fl_gfx::TextFieldEx::getNoTranslate;\ntypedef ThunkFunc2<Classes::fl_gfx::TextFieldEx, Classes::fl_gfx::TextFieldEx::mid_setVerticalAutoSize, const Value, const Value&, const ASString&> TFunc_Classes_TextFieldEx_setVerticalAutoSize;\ntemplate <> const TFunc_Classes_TextFieldEx_setVerticalAutoSize::TMethod TFunc_Classes_TextFieldEx_setVerticalAutoSize::Method = &Classes::fl_gfx::TextFieldEx::setVerticalAutoSize;\ntypedef ThunkFunc1<Classes::fl_gfx::TextFieldEx, Classes::fl_gfx::TextFieldEx::mid_getVerticalAutoSize, ASString, const Value&> TFunc_Classes_TextFieldEx_getVerticalAutoSize;\ntemplate <> const TFunc_Classes_TextFieldEx_getVerticalAutoSize::TMethod TFunc_Classes_TextFieldEx_getVerticalAutoSize::Method = &Classes::fl_gfx::TextFieldEx::getVerticalAutoSize;\n'),
    ('Src/GFx/AS3/Obj/Gfx/AS3_Obj_Gfx_TextFieldEx.cpp', '        {"VALIGN_NONE", NULL, OFFSETOF(Classes::fl_gfx::TextFieldEx, VALIGN_NONE), Abc::NS_Public, SlotInfo::BT_ConstChar, 1},\n', '        {"VALIGN_NONE", NULL, OFFSETOF(Classes::fl_gfx::TextFieldEx, VALIGN_NONE), Abc::NS_Public, SlotInfo::BT_ConstChar, 1},\n        {"VAUTOSIZE_NONE", NULL, OFFSETOF(Classes::fl_gfx::TextFieldEx, VAUTOSIZE_NONE), Abc::NS_Public, SlotInfo::BT_ConstChar, 1},\n        {"VAUTOSIZE_TOP", NULL, OFFSETOF(Classes::fl_gfx::TextFieldEx, VAUTOSIZE_TOP), Abc::NS_Public, SlotInfo::BT_ConstChar, 1},\n        {"VAUTOSIZE_CENTER", NULL, OFFSETOF(Classes::fl_gfx::TextFieldEx, VAUTOSIZE_CENTER), Abc::NS_Public, SlotInfo::BT_ConstChar, 1},\n        {"VAUTOSIZE_BOTTOM", NULL, OFFSETOF(Classes::fl_gfx::TextFieldEx, VAUTOSIZE_BOTTOM), Abc::NS_Public, SlotInfo::BT_ConstChar, 1},\n'),
    ('Src/GFx/AS3/Obj/Gfx/AS3_Obj_Gfx_TextFieldEx.cpp', '        {TFunc_Classes_TextFieldEx_getNoTranslate::Func, &AS3::fl::BooleanTI, "getNoTranslate", NULL, Abc::NS_Public, CT_Method, 1, 1},\n', '        {TFunc_Classes_TextFieldEx_getNoTranslate::Func, &AS3::fl::BooleanTI, "getNoTranslate", NULL, Abc::NS_Public, CT_Method, 1, 1},\n        {TFunc_Classes_TextFieldEx_setVerticalAutoSize::Func, NULL, "setVerticalAutoSize", NULL, Abc::NS_Public, CT_Method, 2, 2},\n        {TFunc_Classes_TextFieldEx_getVerticalAutoSize::Func, &AS3::fl::StringTI, "getVerticalAutoSize", NULL, Abc::NS_Public, CT_Method, 1, 1},\n'),
)
FILES = (HEADER, SOURCE)


def patch_sources(sources):
    final = all(sources[name].count(after) == 1 and sources[name].count(before) == after.count(before)
                for name, before, after in SEAMS)
    if final: return dict(sources)
    if not all(sources[name].count(before) == 1 and sources[name].count(after) == 0
               for name, before, after in SEAMS):
        raise ValueError('Pinned TextFieldEx vertical autosize seams are partial, duplicated or changed')
    result = dict(sources)
    for name, before, after in SEAMS:
        result[name] = result[name].replace(before, after, 1)
    return result


def patch(root):
    root = Path(root)
    sources = {name:(root/name).read_text(encoding='utf-8-sig') for name in FILES}
    result = patch_sources(sources)
    for name in FILES:
        if result[name] != sources[name]: (root/name).write_text(result[name], encoding='utf-8')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('sdk_root', type=Path)
    patch(parser.parse_args().sdk_root)
