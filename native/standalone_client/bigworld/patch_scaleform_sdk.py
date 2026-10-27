#!/usr/bin/env python3
"""Connect public BigWorld to the pinned source-built Scaleform 4.2.22 SDK."""
import argparse
from pathlib import Path


def replace_once(source, before, after, label):
    if source.count(after) == 1:
        return source
    if source.count(before) != 1:
        raise ValueError('Unrecognized pinned Scaleform seam: ' + label)
    return source.replace(before, after)


def source_encoding(path):
    # AS3_VM.cpp carries one Windows-1252 comment byte; keep such files bytewise.
    try:
        path.read_bytes().decode('utf-8-sig')
        return 'utf-8'
    except UnicodeDecodeError:
        return 'latin-1'


def upgrade(edit, path, previous, after, label):
    # Replaces the text an earlier revision of an edit produced, when present.
    try:
        edit(path, previous, after, label)
    except ValueError:
        pass


def read_source(path):
    encoding = source_encoding(path)
    return path.read_text(encoding='utf-8-sig' if encoding == 'utf-8' else encoding)


def sdk_edits(sdk_root, edit):
    # Existing AMP collection implementations are retained. Give only the
    # actual BW manager access, rather than exporting SDK internals globally.
    amp = sdk_root / 'Src/GFx/AMP/Amp_Server.h'
    edit(amp, 'namespace Scaleform {\n',
         'namespace BW { namespace ScaleformBW { class Manager; } }\n\nnamespace Scaleform {\n',
         'BW profiler owner declaration')
    edit(amp, 'class Server : public RefCountBase<Server, StatAmp_Server>, public AmpServer\n{',
         'class Server : public RefCountBase<Server, StatAmp_Server>, public AmpServer\n{\n'
         '    friend class ::BW::ScaleformBW::Manager;', 'BW profiler private access')
    png = sdk_root / '3rdParty/libpng-1.5.13/png.h'
    for symbol in ('png_benign_error', 'png_chunk_benign_error'):
        edit(png, '#  undef ' + symbol,
             '// Preserve the private static-build prefix for ' + symbol + '.',
             'PNG static symbol isolation ' + symbol)

    simd = sdk_root / 'Src/Kernel/SF_SIMD_SSE.h'
    # MSVC's first __m128i union member is signed char. Explicit conversion
    # preserves every byte of the constant without list-initialization narrowing.
    for index in range(4):
        for mask, shift in (('000000FF', '0 '), ('0000FF00', '8'),
                            ('00FF0000', '16'), ('FF000000', '24')):
            value = '((i%d&0x%s) >> %s)' % (index, mask, shift)
            edit(simd, value, 'static_cast<char>(%s)' % value,
                 'MSVC SIMD constant byte %d %s' % (index, mask))

    byte_array = sdk_root / 'Src/GFx/AS3/Obj/Utils/AS3_Obj_Utils_ByteArray.cpp'
    edit(byte_array,
         '#endif // SF_ENABLE_ZLIB\n//##protect##"methods"',
         '#endif // SF_ENABLE_ZLIB\n'
         '// Z_PREFIX must not rename the ActionScript ByteArray methods.\n'
         '// The native ZStream helper above has already used the prefixed API.\n'
         '#undef compress\n#undef uncompress\n#undef deflate\n#undef inflate\n'
         '//##protect##"methods"', 'zlib and ActionScript method names')
    # The #1513 client runs Scaleform 4.6.34. Its currentScene (00dbb750)
    # gives an unmatched timeline the sprite's own scene, as gui/flash/lobby.swf
    # has no DefineSceneAndFrameLabelData; currentLabels (00dbb6d0) checks it.
    movie_clip = sdk_root / 'Src/GFx/AS3/Obj/Display/AS3_Obj_Display_MovieClip.cpp'
    edit(movie_clip,
         '        currentSceneGet(scene);\n        scene->labelsGet(result);\n',
         '        currentSceneGet(scene);\n        if (scene)\n            scene->labelsGet(result);\n'
         '        else\n            result = NULL;\n', 'MovieClip currentLabels scene owner')
    edit(movie_clip,
         '        else\n            result = CreateFakeScene();\n'
         '//##protect##"instance::MovieClip::currentSceneGet()"',
         '        if (!result)\n            result = CreateFakeScene();\n'
         '//##protect##"instance::MovieClip::currentSceneGet()"', 'MovieClip scene-less timeline')
    edit(sdk_root / 'Src/GFx/AS3/Obj/AS3_Obj_Date.cpp',
         'NumberUtil::IsNaNOrInfinity(time) || (abs(time) > 8.64e15)',
         'NumberUtil::IsNaNOrInfinity(time) || (fabs(time) > 8.64e15)',
         'Date floating-point absolute value')
    class_alias_edits(sdk_root, edit)
    amf3_edits(sdk_root, edit)
    text_selection_edits(sdk_root, edit)


# #1513 4.6.34 scaleform.gfx.TextFieldEx (table 015e4b58..015e4c08) adds the
# selection colours after getNoTranslate; original TextAreaSimple, TextInput,
# TextManager and RssNewsFeedRenderer call the four setters as (TextField, uint).
TEXT_SELECTION_SETTERS = (
    ('setSelectionTextColor', 'SetActiveSelectionTextColor', '00e25ae0', '+0x74'),
    ('setSelectionBkgColor', 'SetActiveSelectionBackgroundColor', '00e25c00', '+0x70'),
    ('setInactiveSelectionTextColor', 'SetInactiveSelectionTextColor', '00e25d20', '+0x7c'),
    ('setInactiveSelectionBkgColor', 'SetInactiveSelectionBackgroundColor', '00e25e40', '+0x78'),
)


def text_selection_edits(sdk_root, edit):
    header = sdk_root / 'Src/GFx/AS3/Obj/Gfx/AS3_Obj_Gfx_TextFieldEx.h'
    names = [name for name, unused, unused, unused in TEXT_SELECTION_SETTERS]
    edit(header, '        enum { ThunkInfoNum = 10 };\n', '        enum { ThunkInfoNum = 14 };\n',
         'TextFieldEx thunk count')
    edit(header, '            mid_getNoTranslate, \n        };\n',
         '            mid_getNoTranslate, \n' + ''.join('            mid_%s, \n' % name for name in names) +
         '        };\n', 'TextFieldEx selection method IDs')
    # 4.2.22's ThunkFunc2 passes a non-TextField through as a pointer, so the
    # setters take the Value and apply 4.6.34's TextField coercion.
    getter = '        void getNoTranslate(bool& result, Instances::fl_text::TextField* textField);\n'
    edit(header, getter, getter + ''.join(
        '        void %s(const Value& result, const Value& textField, UInt32 color);\n' % name
        for name in names), 'TextFieldEx selection declarations')

    source = sdk_root / 'Src/GFx/AS3/Obj/Gfx/AS3_Obj_Gfx_TextFieldEx.cpp'
    edit(source, '#include "GFx/AS3/AS3_AvmTextField.h"\n',
         '#include "GFx/AS3/AS3_AvmTextField.h"\n#include "../Text/AS3_Obj_Text_TextField.h"\n',
         'TextFieldEx selection includes')
    body_anchor = '//##protect##"class_::TextFieldEx::getNoTranslate()"\n    }\n'
    bodies = (
        '    // #1513 00e25ae0/00e25c00/00e25d20/00e25e40 behind the TextField coercion of\n'
        '    // the first argument: null is #1507, extensions off or no EditorKit return,\n'
        '    // and an EditorKit is never created here.\n'
        '    static Text::EditorKit* SelectionEditorKit(VM& vm, const Value& textField)\n'
        '    {\n'
        '        if (vm.IsException())\n'
        '            return NULL;\n'
        '        if (textField.IsNullOrUndefined())\n'
        '        {\n'
        '            vm.ThrowArgumentError(VM::Error(VM::eNullArgumentError, vm SF_DEBUG_ARG("textField")));\n'
        '            return NULL;\n'
        '        }\n'
        '        AS3::Object* object = textField.IsObject() ? textField.GetObject() : NULL;\n'
        '        if (!object || !AreDisplayObjectTraits(object) ||\n'
        '            static_cast<Instances::fl_display::DisplayObject*>(object)->pDispObj->GetType() !=\n'
        '                CharacterDef::TextField)\n'
        '        {\n'
        '            vm.ThrowTypeError(VM::Error(VM::eCheckTypeFailedError, vm\n'
        '                SF_DEBUG_ARG(textField) SF_DEBUG_ARG("flash.text.TextField")));\n'
        '            return NULL;\n'
        '        }\n'
        '        if (!static_cast<ASVM&>(vm).ExtensionsEnabled)\n'
        '            return NULL;\n'
        '        return static_cast<Instances::fl_text::TextField*>(object)->GetTextField()->GetEditorKit();\n'
        '    }\n')
    for name, setter, address, offset in TEXT_SELECTION_SETTERS:
        bodies += (
            '    // %s: EditorKit%s, then InvalidateSelectionColors (00caeeb0).\n'
            '    void TextFieldEx::%s(const Value& result, const Value& textField, UInt32 color)\n'
            '    {\n'
            '        SF_UNUSED1(result);\n'
            '        Text::EditorKit* editor = SelectionEditorKit(GetVM(), textField);\n'
            '        if (editor)\n'
            '            editor->%s(color);\n'
            '    }\n' % (address, offset, name, setter))
    edit(source, body_anchor, body_anchor + bodies, 'TextFieldEx selection setters')
    method_anchor = ('template <> const TFunc_Classes_TextFieldEx_getNoTranslate::TMethod '
                     'TFunc_Classes_TextFieldEx_getNoTranslate::Method = '
                     '&Classes::fl_gfx::TextFieldEx::getNoTranslate;\n')
    thunks = ''.join(
        'typedef ThunkFunc2<Classes::fl_gfx::TextFieldEx, Classes::fl_gfx::TextFieldEx::mid_%s, const Value, '
        'const Value&, UInt32> TFunc_Classes_TextFieldEx_%s;\n'
        'template <> const TFunc_Classes_TextFieldEx_%s::TMethod TFunc_Classes_TextFieldEx_%s::Method = '
        '&Classes::fl_gfx::TextFieldEx::%s;\n' % (name, name, name, name, name) for name in names)
    edit(source, method_anchor, method_anchor + thunks, 'TextFieldEx selection thunks')
    entry_anchor = ('        {TFunc_Classes_TextFieldEx_getNoTranslate::Func, &AS3::fl::BooleanTI, '
                    '"getNoTranslate", NULL, Abc::NS_Public, CT_Method, 1, 1},\n')
    entries = entry_anchor + ''.join(
        '        {TFunc_Classes_TextFieldEx_%s::Func, NULL, "%s", NULL, Abc::NS_Public, CT_Method, 2, 2},\n'
        % (name, name) for name in names)
    edit(source, entry_anchor, entries, 'TextFieldEx selection registration')
    # A duplicated or reordered family is rejected rather than completed.
    for path, final in ((header, '        enum { ThunkInfoNum = 14 };\n'),
                        (source, entries + '    };\n')):
        edit(path, final, final, 'TextFieldEx selection final state')


def class_alias_edits(sdk_root, edit):
    # #1513's 4.6.34 flash.net table (015c30b0) adds getClassByAlias (1, 1) and
    # registerClassAlias (2, 2) after navigateToURL; gui/flash/lobby.swf's
    # LobbyApp constructor registers its VO aliases through the latter.
    global_h = sdk_root / 'Src/GFx/AS3/Obj/AS3_Obj_Global.h'
    # 4.2.22's ThunkFunc2 coercion passes a non-Class through as a pointer, so
    # registerClassAlias takes the Value and applies 4.6.34's Class coercion.
    navigate = '            navigateToURL(Value::GetUndefined(), request, window);\n        }\n'
    get_declaration = '        void getClassByAlias(SPtr<Class>& result, const ASString& aliasName);\n'
    upgrade(edit, global_h,
            navigate + get_declaration +
            '        void registerClassAlias(const Value& result, const ASString& aliasName, Class* classObject);\n',
            navigate + get_declaration +
            '        void registerClassAlias(const Value& result, const ASString& aliasName, const Value& classObject);\n',
            'flash.net class alias declarations')
    edit(global_h, navigate,
         navigate + get_declaration +
         '        void registerClassAlias(const Value& result, const ASString& aliasName, const Value& classObject);\n',
         'flash.net class alias declarations')
    global_cpp = sdk_root / 'Src/GFx/AS3/Obj/AS3_Obj_Global.cpp'
    navigate_method = ('TFunc_Instances_GlobalObjectCPP_navigateToURL::Method = '
                       '&Instances::fl::GlobalObjectCPP::navigateToURL;\n')
    get_thunk = ('typedef ThunkFunc1<Instances::fl::GlobalObjectCPP, __LINE__, SPtr<Class>, const ASString&> '
                 'TFunc_Instances_GlobalObjectCPP_getClassByAlias;\n')
    methods = ('template <> const TFunc_Instances_GlobalObjectCPP_getClassByAlias::TMethod '
               'TFunc_Instances_GlobalObjectCPP_getClassByAlias::Method = '
               '&Instances::fl::GlobalObjectCPP::getClassByAlias;\n'
               'template <> const TFunc_Instances_GlobalObjectCPP_registerClassAlias::TMethod '
               'TFunc_Instances_GlobalObjectCPP_registerClassAlias::Method = '
               '&Instances::fl::GlobalObjectCPP::registerClassAlias;\n')
    register_thunk = ('typedef ThunkFunc2<Instances::fl::GlobalObjectCPP, __LINE__, const Value, const ASString&, '
                      'const Value&> TFunc_Instances_GlobalObjectCPP_registerClassAlias;\n')
    upgrade(edit, global_cpp,
            navigate_method + get_thunk +
            'typedef ThunkFunc2<Instances::fl::GlobalObjectCPP, __LINE__, const Value, const ASString&, Class*> '
            'TFunc_Instances_GlobalObjectCPP_registerClassAlias;\n' + methods,
            navigate_method + get_thunk + register_thunk + methods, 'flash.net class alias thunks')
    edit(global_cpp, navigate_method, navigate_method + get_thunk + register_thunk + methods,
         'flash.net class alias thunks')
    edit(global_cpp,
         '"navigateToURL", NULL, Abc::NS_Public, CT_Method, 1, 2},\n',
         '"navigateToURL", NULL, Abc::NS_Public, CT_Method, 1, 2},\n'
         '                {TFunc_Instances_GlobalObjectCPP_getClassByAlias::Func, &AS3::ClassClassTI, '
         '"getClassByAlias", NULL, Abc::NS_Public, CT_Method, 1, 1},\n'
         '                {TFunc_Instances_GlobalObjectCPP_registerClassAlias::Func, NULL, '
         '"registerClassAlias", NULL, Abc::NS_Public, CT_Method, 2, 2},\n',
         'flash.net class alias registration')
    # 4.6.34's thunks (00da4320, 00da5690) stop on an exception left by the
    # argument coercion; SDK 4.2.22's ThunkFunc2 calls the method with the
    # uncoerced value, so the bodies check first.
    implementations_previous = (
        '//##protect##"instance::::navigateToURL()"\n    }\n'
        '    // 00d99f10: an unregistered alias is ReferenceError #1014.\n'
        '    void GlobalObjectCPP::getClassByAlias(SPtr<Class>& result, const ASString& aliasName)\n'
        '    {\n'
        '        VM& vm = GetVM();\n'
        '        Class* registered = vm.GetClassByAlias(aliasName);\n'
        '        if (registered)\n'
        '        {\n'
        '            result = registered;\n'
        '            return;\n'
        '        }\n'
        '        vm.ThrowReferenceError(VM::Error(VM::eClassNotFoundError, vm SF_DEBUG_ARG(Value(aliasName))));\n'
        '    }\n'
        '    // 00d99fa0: a null class is ignored; the argument coercion rejects a non-Class.\n'
        '    void GlobalObjectCPP::registerClassAlias(const Value& result, const ASString& aliasName, '
        'Class* classObject)\n'
        '    {\n'
        '        SF_UNUSED1(result);\n'
        '        if (classObject)\n'
        '            GetVM().RegisterClassAlias(aliasName, *classObject);\n'
        '    }\n')
    implementations = (
        '//##protect##"instance::::navigateToURL()"\n    }\n'
        '    // 00d99f10: an unregistered alias is ReferenceError #1014.\n'
        '    void GlobalObjectCPP::getClassByAlias(SPtr<Class>& result, const ASString& aliasName)\n'
        '    {\n'
        '        VM& vm = GetVM();\n'
        '        if (vm.IsException())\n'
        '            return;\n'
        '        Class* registered = vm.GetClassByAlias(aliasName);\n'
        '        if (registered)\n'
        '        {\n'
        '            result = registered;\n'
        '            return;\n'
        '        }\n'
        '        vm.ThrowReferenceError(VM::Error(VM::eClassNotFoundError, vm SF_DEBUG_ARG(Value(aliasName))));\n'
        '    }\n'
        '    // 00d99fa0 behind the Class coercion of its argument: null and undefined\n'
        '    // are ignored, any other non-Class is TypeError #1034.\n'
        '    void GlobalObjectCPP::registerClassAlias(const Value& result, const ASString& aliasName, '
        'const Value& classObject)\n'
        '    {\n'
        '        SF_UNUSED1(result);\n'
        '        VM& vm = GetVM();\n'
        '        if (vm.IsException() || classObject.IsNullOrUndefined())\n'
        '            return;\n'
        '        if (!classObject.IsClass())\n'
        '        {\n'
        '            vm.ThrowTypeError(VM::Error(VM::eCheckTypeFailedError, vm\n'
        '                SF_DEBUG_ARG(classObject) SF_DEBUG_ARG("Class")));\n'
        '            return;\n'
        '        }\n'
        '        vm.RegisterClassAlias(aliasName, const_cast<Class&>(classObject.AsClass()));\n'
        '    }\n')
    upgrade(edit, global_cpp, implementations_previous, implementations, 'flash.net class alias implementations')
    edit(global_cpp, '//##protect##"instance::::navigateToURL()"\n    }\n', implementations,
         'flash.net class alias implementations')

    vm_h = sdk_root / 'Src/GFx/AS3/AS3_VM.h'
    edit(vm_h,
         '    VMFile(VM& vm, VMAppDomain& appDomain);\n    virtual ~VMFile();\n',
         '    VMFile(VM& vm, VMAppDomain& appDomain);\n    virtual ~VMFile();\n\n'
         '    // The flash.net aliases of this file\'s classes (4.6.34 VMFile+0xac).\n'
         '    void AddClassAlias(const ASString& alias) { ClassAliases.Set(alias); }\n'
         '    void RemoveClassAlias(const ASString& alias) { ClassAliases.Remove(alias); }\n',
         'VMFile class alias owner')
    edit(vm_h,
         '    ArrayLH<SPtr<ClassTraits::Traits>, StatMV_VM_VMAbcFile_Mem> LoadedClasses;\n'
         '}; // class VMFile\n',
         '    ArrayLH<SPtr<ClassTraits::Traits>, StatMV_VM_VMAbcFile_Mem> LoadedClasses;\n'
         '    HashSetUncachedLH<ASString, ASStringHashFunctor, ASStringHashFunctor, '
         'StatMV_VM_VMAbcFile_Mem> ClassAliases;\n'
         '}; // class VMFile\n',
         'VMFile class alias set')
    # Revision dbb0e3e0 integrated these three edits without the reverse map
    # (vm+0x1b0); upgrade(...) moves an SDK patched by it to the current text.
    api_previous = (
        '    // flash.net class aliases (4.6.34 00e59390 and 00e59350).\n'
        '    void RegisterClassAlias(const ASString& alias, Class& cls);\n'
        '    Class* GetClassByAlias(const ASString& alias) const;\n'
        '    void RemoveClassAlias(const ASString& alias);\n\n'
        '    void AddVMAbcFileWeak(VMAbcFile* f);\n')
    api = (
        '    // flash.net class aliases (4.6.34 00e59390, 00e59350 and 00e592a0).\n'
        '    void RegisterClassAlias(const ASString& alias, Class& cls);\n'
        '    Class* GetClassByAlias(const ASString& alias) const;\n'
        '    ASString GetAliasByClass(Class& cls);\n'
        '    void RemoveClassAlias(const ASString& alias);\n\n'
        '    void AddVMAbcFileWeak(VMAbcFile* f);\n')
    upgrade(edit, vm_h, api_previous, api, 'VM class alias API')
    edit(vm_h, '    void AddVMAbcFileWeak(VMAbcFile* f);\n', api, 'VM class alias API')
    map_previous = (
        '    // Raw like 4.6.34: each class\'s file removes its aliases when destroyed.\n'
        '    ASStringHash<Class*> ClassAliases;\n\n'
        '    ///////////////////////////////////////////////////////////////////////\n'
        '    VMAppDomain* SystemDomain;\n')
    alias_map = (
        '    // Raw like 4.6.34: each class\'s file removes its aliases when destroyed.\n'
        '    ASStringHash<Class*> ClassAliases;\n'
        '    // The latest alias of each class (4.6.34 vm+0x1b0), read by the AMF3 writer.\n'
        '    HashLH<Class*, ASString, FixedSizeHash<Class*>, StatMV_VM_VM_Mem> ClassAliasNames;\n\n'
        '    ///////////////////////////////////////////////////////////////////////\n'
        '    VMAppDomain* SystemDomain;\n')
    upgrade(edit, vm_h, map_previous, alias_map, 'VM class alias map')
    edit(vm_h,
         '    ///////////////////////////////////////////////////////////////////////\n'
         '    VMAppDomain* SystemDomain;\n', alias_map, 'VM class alias map')
    vm_cpp = sdk_root / 'Src/GFx/AS3/AS3_VM.cpp'
    edit(vm_cpp,
         'VMFile::~VMFile()\n{\n}\n',
         'VMFile::~VMFile()\n{\n'
         '    // 00e4fa20: an alias must not outlive the file that owns its class.\n'
         '    HashSetUncachedLH<ASString, ASStringHashFunctor, ASStringHashFunctor, '
         'StatMV_VM_VMAbcFile_Mem>::ConstIterator it = ClassAliases.Begin();\n'
         '    for (; !it.IsEnd(); ++it)\n'
         '        VMRef.RemoveClassAlias(*it);\n'
         '}\n',
         'VMFile class alias teardown')
    register_head = (
        'void VM::RegisterClassAlias(const ASString& alias, Class& cls)\n{\n'
        '    // 00e59390: the same pair is unchanged; a moved alias leaves its old file.\n'
        '    Class** registered = ClassAliases.Get(alias);\n'
        '    if (registered)\n'
        '    {\n'
        '        if (*registered == &cls)\n'
        '            return;\n'
        '        VMFile* previous = (*registered)->GetTraits().GetFilePtr();\n'
        '        if (previous)\n'
        '            previous->RemoveClassAlias(alias);\n')
    lookup = (
        'Class* VM::GetClassByAlias(const ASString& alias) const\n{\n'
        '    Class* const* registered = ClassAliases.Get(alias);\n'
        '    return registered ? *registered : NULL;\n'
        '}\n\n')
    implementation_previous = (
        register_head +
        '    }\n'
        '    VMFile* file = cls.GetTraits().GetFilePtr();\n'
        '    if (file)\n'
        '        file->AddClassAlias(alias);\n'
        '    ClassAliases.Set(alias, &cls);\n'
        '}\n\n' + lookup +
        'void VM::RemoveClassAlias(const ASString& alias)\n{\n'
        '    ClassAliases.Remove(alias);\n'
        '}\n\n'
        'void VM::AddVMAbcFileWeak(VMAbcFile* f)\n')
    implementation = (
        register_head +
        '        ClassAliasNames.Remove(*registered);\n'
        '    }\n'
        '    VMFile* file = cls.GetTraits().GetFilePtr();\n'
        '    if (file)\n'
        '        file->AddClassAlias(alias);\n'
        '    ClassAliases.Set(alias, &cls);\n'
        '    ClassAliasNames.Set(&cls, alias);\n'
        '}\n\n' + lookup +
        'ASString VM::GetAliasByClass(Class& cls)\n{\n'
        '    // 00e592a0: an unaliased class has the empty name.\n'
        '    const ASString* name = ClassAliasNames.Get(&cls);\n'
        '    return name ? *name : GetStringManager().CreateEmptyString();\n'
        '}\n\n'
        'void VM::RemoveClassAlias(const ASString& alias)\n{\n'
        '    // 00e4fa20 also drops the reverse entry of the class the alias names.\n'
        '    Class** registered = ClassAliases.Get(alias);\n'
        '    if (!registered)\n'
        '        return;\n'
        '    ClassAliasNames.Remove(*registered);\n'
        '    ClassAliases.Remove(alias);\n'
        '}\n\n'
        'void VM::AddVMAbcFileWeak(VMAbcFile* f)\n')
    upgrade(edit, vm_cpp, implementation_previous, implementation, 'VM class alias implementation')
    edit(vm_cpp, 'void VM::AddVMAbcFileWeak(VMAbcFile* f)\n', implementation, 'VM class alias implementation')



AMF3_SOURCE = Path(__file__).resolve().parent / 'scaleform_amf3.inc'


def amf3_edits(sdk_root, edit):
    # #1513's 4.6.34 ByteArray.readObject (00e1c140) and writeObject (00e1c860)
    # run its AMF3 codec; SDK 4.2.22 reports both as not implemented, and
    # gui/flash/lobby.swf DataUtils.cloneObject needs them.
    byte_array_h = sdk_root / 'Src/GFx/AS3/Obj/Utils/AS3_Obj_Utils_ByteArray.h'
    edit(byte_array_h,
         '        CheckResult ReadUTFBytes(ASString& result, UInt32 len);\n',
         '        CheckResult ReadUTFBytes(ASString& result, UInt32 len);\n\n'
         '        // #1513 4.6.34 AMF3 codec.\n'
         '        struct AMF3WriteScope;\n'
         '        struct AMF3ReadScope;\n'
         '        void AMF3WriteByte(UInt8 v);\n'
         '        void AMF3WriteU29(UInt32 v);\n'
         '        void AMF3WriteU32(UInt32 v);\n'
         '        void AMF3WriteDouble(Value::Number v);\n'
         '        void AMF3WriteString(AMF3WriteScope& scope, const ASString& s);\n'
         '        bool AMF3WriteObjectRef(AMF3WriteScope& scope, UInt8 marker, AS3::Object& o);\n'
         '        bool AMF3WriteGeneric(AMF3WriteScope& scope, AS3::Object& o);\n'
         '        bool AMF3WriteArray(AMF3WriteScope& scope, Instances::fl::Array& a);\n'
         '        bool AMF3WriteObject(AMF3WriteScope& scope, AS3::Object& o);\n'
         '        bool AMF3WriteValue(AMF3WriteScope& scope, const Value& v);\n'
         '        bool AMF3ReadByte(UInt8& v);\n'
         '        UInt32 AMF3ReadU29();\n'
         '        UInt32 AMF3ReadU32();\n'
         '        Value::Number AMF3ReadDouble();\n'
         '        ASString AMF3ReadString(AMF3ReadScope& scope);\n'
         '        bool AMF3ReadObjectRef(AMF3ReadScope& scope, UInt32 u, Value& result);\n'
         '        void AMF3ReadObject(AMF3ReadScope& scope, Value& result);\n'
         '        void AMF3ReadArray(AMF3ReadScope& scope, Value& result);\n'
         '        void AMF3ReadVector(AMF3ReadScope& scope, UInt8 marker, Value& result);\n'
         '        void AMF3ReadDictionary(AMF3ReadScope& scope, Value& result);\n'
         '        bool AMF3ReadValue(AMF3ReadScope& scope, Value& result);\n',
         'ByteArray AMF3 declarations')
    edit(byte_array_h,
         '        ArrayLH_POD<UInt8> Data;\n//##protect##"instance$data"\n',
         '        ArrayLH_POD<UInt8> Data;\n'
         '        // The active AMF3 scopes; readExternal and writeExternal share them.\n'
         '        AMF3ReadScope* AMF3Reader;\n'
         '        AMF3WriteScope* AMF3Writer;\n'
         '        bool AMF3InReadExternal;\n'
         '        bool AMF3InWriteExternal;\n'
         '//##protect##"instance$data"\n',
         'ByteArray AMF3 state')

    byte_array = sdk_root / 'Src/GFx/AS3/Obj/Utils/AS3_Obj_Utils_ByteArray.cpp'
    edit(byte_array, '#include "../../AS3_Marshalling.h"\n',
         '#include "../../AS3_Marshalling.h"\n'
         '#include "../AS3_Obj_Array.h"\n#include "../AS3_Obj_Date.h"\n#include "../AS3_Obj_XML.h"\n'
         '#include "AS3_Obj_Utils_Dictionary.h"\n#include "AS3_Obj_Utils_IExternalizable.h"\n'
         '#include "../Vec/AS3_Obj_Vec_Vector_int.h"\n#include "../Vec/AS3_Obj_Vec_Vector_uint.h"\n'
         '#include "../Vec/AS3_Obj_Vec_Vector_double.h"\n#include "../Vec/AS3_Obj_Vec_Vector_String.h"\n'
         '#include "../Vec/AS3_Obj_Vec_Vector_object.h"\n#include "../Vec/AS3_Obj_Vec_Vector.h"\n',
         'ByteArray AMF3 includes')
    edit(byte_array,
         '    , Length(0)\n//##protect##"instance::ByteArray::ByteArray()$data"\n',
         '    , Length(0)\n    , AMF3Reader(NULL)\n    , AMF3Writer(NULL)\n'
         '    , AMF3InReadExternal(false)\n    , AMF3InWriteExternal(false)\n'
         '//##protect##"instance::ByteArray::ByteArray()$data"\n',
         'ByteArray AMF3 state initialization')
    edit(byte_array,
         '    , Endian(Instances::fl_utils::ByteArray::GetDefaultEndian())\n    , Position(0)\n    {\n    }\n',
         '    , Endian(Instances::fl_utils::ByteArray::GetDefaultEndian())\n    , Position(0)\n'
         '    , AMF3Reader(NULL)\n    , AMF3Writer(NULL)\n'
         '    , AMF3InReadExternal(false)\n    , AMF3InWriteExternal(false)\n    {\n    }\n',
         'ByteArray encoding constructor AMF3 state')
    edit(byte_array,
         '        SF_UNUSED1(result);\n        NOT_IMPLEMENTED("ByteArray::readObject()");\n',
         '        // 00e1c140: a fresh reference context unless readExternal is running.\n'
         '        if (AMF3InReadExternal && AMF3Reader)\n'
         '        {\n'
         '            AMF3ReadValue(*AMF3Reader, result);\n'
         '            return;\n'
         '        }\n'
         '        AMF3ReadScope scope;\n'
         '        AMF3ReadScope* const outer = AMF3Reader;\n'
         '        AMF3Reader = &scope;\n'
         '        AMF3ReadValue(scope, result);\n'
         '        AMF3Reader = outer;\n',
         'ByteArray readObject')
    edit(byte_array,
         '        SF_UNUSED2(result, object);\n        NOT_IMPLEMENTED("ByteArray::writeObject()");\n',
         '        SF_UNUSED1(result);\n'
         '        // 00e1c860: a fresh reference scope unless writeExternal is running.\n'
         '        if (AMF3InWriteExternal && AMF3Writer)\n'
         '        {\n'
         '            AMF3WriteValue(*AMF3Writer, object);\n'
         '            return;\n'
         '        }\n'
         '        AMF3WriteScope scope;\n'
         '        AMF3WriteScope* const outer = AMF3Writer;\n'
         '        AMF3Writer = &scope;\n'
         '        AMF3WriteValue(scope, object);\n'
         '        AMF3Writer = outer;\n',
         'ByteArray writeObject')
    edit(byte_array, 'namespace Instances { namespace fl_utils\n{\n    ByteArray::ByteArray(InstanceTraits::Traits& t)\n',
         'namespace Instances { namespace fl_utils\n{\n' + read_source(AMF3_SOURCE) +
         '\n    ByteArray::ByteArray(InstanceTraits::Traits& t)\n', 'ByteArray AMF3 codec')

    vm_h = sdk_root / 'Src/GFx/AS3/AS3_VM.h'
    edit(vm_h, '    friend class Instances::Function; // Because of ExecuteCode().\n',
         '    friend class Instances::Function; // Because of ExecuteCode().\n'
         '    friend class Instances::fl_utils::ByteArray; // Because of ExecuteCode() in AMF3 readObject.\n',
         'VM AMF3 constructor execution')
    edit(vm_h, 'namespace Instances { namespace fl\n',
         'namespace Instances { namespace fl_utils\n{\n    class ByteArray;\n}}\n\n'
         'namespace Instances { namespace fl\n', 'VM ByteArray declaration')
    edit(vm_h, '        eSWFHasInvalidData                              = 2136,\n',
         '        eSWFHasInvalidData                              = 2136,\n'
         '        eReadExternalNotImplementedError                = 2173,\n',
         'AMF3 externalizable error id')
    vm_cpp = sdk_root / 'Src/GFx/AS3/AS3_VM.cpp'
    edit(vm_cpp, '        2136,\n    };\n', '        2136,\n        2173,\n    };\n', 'AMF3 error mapping')
    edit(vm_cpp, '        /* 2136,145*/ "The SWF file {0} contains invalid data.",\n',
         '        /* 2136,145*/ "The SWF file {0} contains invalid data.",\n'
         '        /* 2173,146*/ "Unable to read object in stream.  The class {0} does not implement '
         'flash.utils.IExternalizable but is aliased to an externalizable class.",\n',
         'AMF3 error message')

    date_h = sdk_root / 'Src/GFx/AS3/Obj/AS3_Obj_Date.h'
    edit(date_h, '//##protect##"forward_declaration"\n//##protect##"forward_declaration"\n',
         '//##protect##"forward_declaration"\n'
         'namespace Instances { namespace fl_utils\n{\n    class ByteArray;\n}}\n'
         '//##protect##"forward_declaration"\n', 'Date AMF3 owner declaration')
    edit(date_h, '        friend class Classes::fl::Date;\n',
         '        friend class Classes::fl::Date;\n'
         '        friend class Instances::fl_utils::ByteArray; // AMF3 time value.\n',
         'Date AMF3 owner')
    dictionary_h = sdk_root / 'Src/GFx/AS3/Obj/Utils/AS3_Obj_Utils_Dictionary.h'
    edit(dictionary_h, '        friend class InstanceTraits::fl_utils::Dictionary;\n',
         '        friend class InstanceTraits::fl_utils::Dictionary;\n'
         '        friend class ByteArray; // AMF3 weak keys and entries.\n',
         'Dictionary AMF3 owner')


def log_edits(engine_root, edit):
    edit(engine_root / 'lib/scaleform/log.cpp',
         '\tBW_GUARD;\n\tif ( messageId & Log_Warning )\n'
         '\t\tWARNING_MSG( pfmt, argList );\n'
         '\telse if ( messageId & Log_Message )\n'
         '\t\tINFO_MSG( pfmt, argList );\n'
         '\telse\n\t\tERROR_MSG( pfmt, argList );',
         '\tBW_GUARD;\n'
         '\t// A va_list cannot be forwarded as one variadic argument.\n'
         '\tchar message[16384];\n'
         '\tBW::LogMsg::formatMessage(message, sizeof(message), pfmt, argList);\n'
         '\tif ( messageId & Log_Warning )\n'
         '\t\tWARNING_MSG( "%s", message );\n'
         '\telse if ( messageId & Log_Message )\n'
         '\t\tINFO_MSG( "%s", message );\n'
         '\telse\n\t\tERROR_MSG( "%s", message );',
         'Scaleform variadic diagnostic forwarding')


def movie_view_edits(engine_root, edit):
    # #1513 keeps the IME focus owner (00aa47a0/00aa4750) without the optional
    # Win32 IME manager, which this SDK lacks; IME::init and the message hooks
    # stay behind SCALEFORM_IME.
    ime_h = engine_root / 'lib/scaleform/ime.hpp'
    edit(ime_h, '#include "config.hpp"\n\n#if SCALEFORM_IME\n\nBW_BEGIN_NAMESPACE\n',
         '#include "config.hpp"\n#include "cstdmf/bw_string.hpp"\n\nBW_BEGIN_NAMESPACE\n',
         'IME focus owner declaration guard')
    edit(ime_h,
         'public:\n\tstatic bool init( const BW::string& imeMovie );\n'
         '\tstatic bool handleIMMMessage( HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam );\n'
         '\tstatic bool handleIMEMessage( HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam );\n',
         'public:\n#if SCALEFORM_IME\n\tstatic bool init( const BW::string& imeMovie );\n'
         '\tstatic bool handleIMMMessage( HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam );\n'
         '\tstatic bool handleIMEMessage( HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam );\n#endif\n',
         'IME manager declarations')
    edit(ime_h, '\tstatic void setFocussedMovie(class PyMovieView *m);\n',
         '\tstatic void setFocussedMovie( class PyMovieView * m, const BW::string & imePath );\n',
         'IME focus path argument')
    edit(ime_h, 'BW_END_NAMESPACE\n\n#endif\n\n#endif', 'BW_END_NAMESPACE\n\n#endif // SCALEFORM_IME_HPP',
         'IME focus owner declaration end')
    ime_cpp = engine_root / 'lib/scaleform/ime.cpp'
    edit(ime_cpp,
         '#include "ime.hpp"\n\n#if SCALEFORM_IME\n\n#include "manager.hpp"\n'
         '#include "resmgr/bwresource.hpp"\n#include "moo/render_context.hpp"\n'
         '#include "cstdmf/bw_util.hpp"\n#include <GFx_IMEManagerWin32.h>\n\n'
         'BW_BEGIN_NAMESPACE\n\nnamespace ScaleformBW\n{\n'
         '\tint IME_token = 0;\n\tPyMovieView * IME::s_pFocussed = NULL;\n',
         '#include "ime.hpp"\n#include "manager.hpp"\n#include "py_movie_view.hpp"\n'
         '#include "resmgr/bwresource.hpp"\n#include "GFx/IME/GFx_IMEManager.h"\n\n'
         '#if SCALEFORM_IME\n#include "moo/render_context.hpp"\n'
         '#include "cstdmf/bw_util.hpp"\n#include <GFx_IMEManagerWin32.h>\n#endif\n\n'
         'BW_BEGIN_NAMESPACE\n\nnamespace ScaleformBW\n{\n'
         '\tPyMovieView * IME::s_pFocussed = NULL;\n\n#if SCALEFORM_IME\n\tint IME_token = 0;\n',
         'IME focus owner definition guard')
    edit(ime_cpp,
         '\tvoid IME::setFocussedMovie(class PyMovieView *m)\n\t{\n'
         '\t\tif (m != s_pFocussed)\n\t\t{\n'
         '\t\t\tif (s_pFocussed != NULL)\n'
         '\t\t\t\ts_pFocussed->pMovieView()->HandleEvent(GFx::Event::KillFocus);\n\n'
         '\t\t\ts_pFocussed = m;\n\n'
         '\t\t\tif (s_pFocussed != NULL)\n'
         '\t\t\t\ts_pFocussed->pMovieView()->HandleEvent(GFx::Event::SetFocus);\n'
         '\t\t}\n\t}\n',
         '#endif // SCALEFORM_IME\n\n'
         '\t// 00aa47a0. SDK 4.2.22 reads SetFocusEvent key states, so pass that type.\n'
         '\tvoid IME::setFocussedMovie( PyMovieView * m, const BW::string & imePath )\n\t{\n'
         '\t\tif (m == s_pFocussed)\n\t\t\treturn;\n'
         '\t\tif (s_pFocussed != NULL)\n'
         '\t\t\ts_pFocussed->pMovieView()->HandleEvent( GFx::Event( GFx::Event::KillFocus ) );\n'
         '\t\ts_pFocussed = m;\n'
         '\t\tif (s_pFocussed == NULL)\n\t\t\treturn;\n\n'
         '\t\t// 00aa4820: an installed IME manager loads <imePath>/IME.swf.\n'
         '\t\tPtr<GFx::IMEManagerBase> imeManager = Manager::instance().pLoader()->GetIMEManager();\n'
         '\t\tif (imeManager)\n\t\t{\n'
         '\t\t\tBW::string moviePath = imePath;\n'
         '\t\t\tif (!moviePath.empty() && moviePath[moviePath.size() - 1] != \'\\\\\' &&\n'
         '\t\t\t\t\tmoviePath[moviePath.size() - 1] != \'/\')\n'
         '\t\t\t\tmoviePath += \'/\';\n'
         '\t\t\tmoviePath += "IME.swf";\n'
         '\t\t\timeManager->SetIMEMoviePath( BWResolver::resolveFilename( moviePath ).c_str() );\n'
         '\t\t}\n'
         '\t\ts_pFocussed->pMovieView()->HandleEvent( GFx::SetFocusEvent() );\n\t}\n',
         'IME focus owner transition')
    edit(ime_cpp, 'BW_END_NAMESPACE\n\n#endif // SCALEFORM_IME\n#endif //#if SCALEFORM_SUPPORT',
         'BW_END_NAMESPACE\n\n#endif //#if SCALEFORM_SUPPORT', 'IME focus owner definition end')

    view_h = engine_root / 'lib/scaleform/py_movie_view.hpp'
    edit(view_h, '\t\tvoid setFocussed();\n\t\tPY_AUTO_METHOD_DECLARE( RETVOID, setFocussed, END );\n',
         '\t\tvoid setFocussed( const BW::string & imePath );\n'
         '\t\tPY_AUTO_METHOD_DECLARE( RETVOID, setFocussed, ARG( BW::string, END ) );\n',
         'PyMovieView.setFocussed path argument')
    view_cpp = engine_root / 'lib/scaleform/py_movie_view.cpp'
    edit(view_cpp, '#include "moo/render_context.hpp"\n\n#if SCALEFORM_IME\n#include "ime.hpp"\n#endif\n',
         '#include "moo/render_context.hpp"\n#include "ime.hpp"\n', 'PyMovieView focus owner include')
    edit(view_cpp,
         '\t\tManager::instance().removeMovieView(this);\n#if SCALEFORM_IME\n'
         '\t\tIME::onDeleteMovieView(this);\n#endif\n',
         '\t\tManager::instance().removeMovieView(this);\n\t\tIME::onDeleteMovieView(this);\n',
         'PyMovieView focus owner teardown')
    edit(view_cpp,
         '\tvoid PyMovieView::setFocussed()\n\t{\n\t\tBW_GUARD;\n#if SCALEFORM_IME\n'
         '\t\tIME::setFocussedMovie(this);\n#endif\n\t}\n',
         '\t// 00aa5ab0 takes exactly one BW::string; 00aa47a0 owns the focus.\n'
         '\tvoid PyMovieView::setFocussed( const BW::string & imePath )\n\t{\n\t\tBW_GUARD;\n'
         '\t\tIME::setFocussedMovie( this, imePath );\n\t}\n',
         'PyMovieView.setFocussed focus owner')
    # 00aa5d80 offers an AS3 root variable only for a name the Python object
    # rejects, so a native attribute such as backgroundAlpha is not also set
    # on the document class.
    edit(view_cpp,
         '\t\tbool res = PyObjectPlus::pySetAttribute( attrObj, value );\n\n'
         '\t\tGFx::Value gfxValue = PyGFxValue::convertValueToGFx( \n'
         '\t\t\tvalue.get(), this );\n'
         '\t\tif (pMovieView_->SetVariable( attrObj.c_str(),\n'
         '\t\t\t\tgfxValue, GFx::Movie::SV_Normal ))\n'
         '\t\t{\n\t\t\tres = true;\n\t\t}\n\n\t\treturn res;\n',
         '\t\tif (PyObjectPlus::pySetAttribute( attrObj, value ))\n\t\t\treturn true;\n\n'
         '\t\tGFx::Value gfxValue = PyGFxValue::convertValueToGFx( \n'
         '\t\t\tvalue.get(), this );\n'
         '\t\tif (!pMovieView_->SetVariable( attrObj.c_str(),\n'
         '\t\t\t\tgfxValue, GFx::Movie::SV_Normal ))\n'
         '\t\t{\n\t\t\treturn false;\n\t\t}\n\n\t\tPyErr_Clear();\n\t\treturn true;\n',
         'PyMovieView native attribute before AS3 root variable')


def patch_engine(engine_root, sdk_root):
    engine_root, sdk_root = Path(engine_root), Path(sdk_root)
    changes = {}

    def edit(path, before, after, label):
        source = changes.get(path)
        if source is None:
            source = read_source(path)
        changes[path] = replace_once(source, before, after, label)

    edit(engine_root / 'build/cmake/BWConfiguration_client.cmake',
         'SET( BW_SCALEFORM_SUPPORT OFF )', 'SET( BW_SCALEFORM_SUPPORT ON )',
         'client feature flag')
    edit(engine_root / 'CMakeLists.txt', '# Add libraries\n',
         '# Build the pinned source SDK before its BigWorld consumers.\n'
         'IF( BW_SCALEFORM_SUPPORT )\n'
         '\tIF( NOT EXISTS "${WOT_SCALEFORM_SOURCE_CMAKE}" )\n'
         '\t\tMESSAGE( FATAL_ERROR "WOT_SCALEFORM_SOURCE_CMAKE is required" )\n'
         '\tENDIF()\n'
         '\tINCLUDE( "${WOT_SCALEFORM_SOURCE_CMAKE}" )\n'
         'ENDIF()\n\n# Add libraries\n', 'SDK target ownership')
    for path, before in (
            (engine_root / 'lib/scaleform/CMakeLists.txt',
             'FIND_PACKAGE( ScaleformSDK 4.1.19 EXACT )'),
            (engine_root / 'client/CMakeLists.txt',
             '\tFIND_PACKAGE( ScaleformSDK )')):
        edit(path, before,
             'IF( NOT TARGET ScaleformSDK::ScaleformSDK )\n'
             '\tMESSAGE( FATAL_ERROR "The source-built Scaleform SDK target is missing" )\n'
             'ENDIF()', 'source SDK consumer ' + str(path.relative_to(engine_root)))

    config = engine_root / 'lib/scaleform/config.hpp'
    edit(config, '#include <GFx_Sound_FMOD.h>',
         '#if FMOD_SUPPORT\n#include <GFx_Sound_FMOD.h>\n#endif', 'FMOD convenience header')
    edit(config, '#include <Sound/Sound_SoundRendererFMOD.h>',
         '#if FMOD_SUPPORT\n#include <Sound/Sound_SoundRendererFMOD.h>\n#endif', 'FMOD backend header')
    edit(config, '#include <Video/Video_VideoPC.h>',
         '// This SDK does not include the optional CRI video addon.', 'uninstalled video addon')
    edit(config,
         '#ifdef GFX_AMP_SERVER\n#include <Amp/GFx_AmpServer.h>\n#include <Amp/Amp_ProfileFrame.h>\n#endif',
         '#ifdef SF_AMP_SERVER\n#include <GFx/AMP/Amp_Server.h>\n#include <GFx/AMP/Amp_ProfileFrame.h>\n#endif',
         'SDK AMP header names')

    manager = engine_root / 'lib/scaleform/manager.cpp'
    log_edits(engine_root, edit)
    movie_view_edits(engine_root, edit)
    edit(manager, '\t\tGFx::setRestrictFlashFilters(true);',
         '\t\t// SDK 4.2.22 uses its stock filter policy; the BW-specific\n'
         '\t\t// setRestrictFlashFilters extension is absent from this source SDK.',
         'unavailable BW filter extension')
    edit(manager, '\t\t\tAmpServer::Uninit();',
         '\t\t\t// GFx::System owns the AMP server through teardown. Clearing\n'
         '\t\t\t// the base singleton here lets GetInstance install a static\n'
         '\t\t\t// fallback that GFx::System later releases as a heap Server.',
         'AMP server lifetime while profiling is disabled')
    edit(manager,
         '\t\t/* GFx Sound System initialization */\n',
         '#if FMOD_SUPPORT\n\t\t/* GFx Sound System initialization */\n',
         'optional FMOD initialization start')
    edit(manager, '\t\t\tpLoader_->SetAudio(pAudioState);\n\n\t\t}\n',
         '\t\t\tpLoader_->SetAudio(pAudioState);\n\n\t\t}\n#endif // FMOD_SUPPORT\n',
         'optional FMOD initialization end')
    edit(manager, '\t\tpLoader_->SetVideo(NULL);',
         '#ifdef GFX_ENABLE_VIDEO\n\t\tpLoader_->SetVideo(NULL);\n#endif',
         'optional video teardown')
    edit(manager,
         '\t\tif (SoundManager::instance().isInitialised())\n\t\t{\n'
         '\t\t\tGFx::Audio *pAudio = (GFx::Audio*)pLoader_->GetAudio().GetPtr();\n'
         '\t\t\tpAudio->GetRenderer()->SetMasterVolume(mute ? 0.0f : 1.0f);\n\t\t}',
         '\t\tPtr<GFx::AudioBase> audio = pLoader_->GetAudio();\n'
         '\t\tif (audio && audio->GetRenderer())\n\t\t{\n'
         '\t\t\taudio->GetRenderer()->SetMasterVolume(mute ? 0.0f : 1.0f);\n\t\t}',
         'abstract audio owner mute')

    # The upstream raw-pointer return outlives its local Ptr only after a
    # successful copy added a reference. Failed texture locks must return NULL.
    loader = engine_root / 'lib/scaleform/loader.cpp'
    edit(loader, '\t\tif (tex)\n', '\t\tbool copied = false;\n\t\tif (tex && pimage)\n',
         'protocol image allocation guard')
    edit(loader, '\t\t\t\t\tpimage->AddRef();',
         '\t\t\t\t\tpimage->AddRef();\n\t\t\t\t\tcopied = true;',
         'protocol image retained ownership')
    edit(loader, '\t\treturn pimage;', '\t\treturn copied ? pimage.GetPtr() : NULL;',
         'failed protocol image ownership')

    sdk_edits(sdk_root, edit)
    for path, source in changes.items():
        path.write_text(source, encoding=source_encoding(path))
    print('Public BigWorld connected to source Scaleform 4.2.22; optional FMOD/video remain uninstalled')


def patch_sdk(sdk_root):
    changes = {}

    def edit(path, before, after, label):
        source = changes.get(path)
        if source is None:
            source = read_source(path)
        changes[path] = replace_once(source, before, after, label)

    sdk_edits(Path(sdk_root), edit)
    for path, source in changes.items():
        path.write_text(source, encoding=source_encoding(path))
    print('Scaleform SDK source seams prepared for private static linking')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk-only', action='store_true')
    parser.add_argument('source_roots', type=Path, nargs='+')
    args = parser.parse_args()
    if args.sdk_only:
        if len(args.source_roots) != 1:
            parser.error('--sdk-only takes one SDK source root')
        patch_sdk(args.source_roots[0])
    else:
        if len(args.source_roots) != 2:
            parser.error('Pass the engine and SDK source roots')
        patch_engine(*args.source_roots)
