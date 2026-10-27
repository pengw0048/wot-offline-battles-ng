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


def class_alias_edits(sdk_root, edit):
    # #1513's 4.6.34 flash.net table (015c30b0) adds getClassByAlias (1, 1) and
    # registerClassAlias (2, 2) after navigateToURL; gui/flash/lobby.swf's
    # LobbyApp constructor registers its VO aliases through the latter.
    global_h = sdk_root / 'Src/GFx/AS3/Obj/AS3_Obj_Global.h'
    edit(global_h,
         '            navigateToURL(Value::GetUndefined(), request, window);\n        }\n',
         '            navigateToURL(Value::GetUndefined(), request, window);\n        }\n'
         '        void getClassByAlias(SPtr<Class>& result, const ASString& aliasName);\n'
         '        void registerClassAlias(const Value& result, const ASString& aliasName, Class* classObject);\n',
         'flash.net class alias declarations')
    global_cpp = sdk_root / 'Src/GFx/AS3/Obj/AS3_Obj_Global.cpp'
    edit(global_cpp,
         'TFunc_Instances_GlobalObjectCPP_navigateToURL::Method = '
         '&Instances::fl::GlobalObjectCPP::navigateToURL;\n',
         'TFunc_Instances_GlobalObjectCPP_navigateToURL::Method = '
         '&Instances::fl::GlobalObjectCPP::navigateToURL;\n'
         'typedef ThunkFunc1<Instances::fl::GlobalObjectCPP, __LINE__, SPtr<Class>, const ASString&> '
         'TFunc_Instances_GlobalObjectCPP_getClassByAlias;\n'
         'typedef ThunkFunc2<Instances::fl::GlobalObjectCPP, __LINE__, const Value, const ASString&, Class*> '
         'TFunc_Instances_GlobalObjectCPP_registerClassAlias;\n'
         'template <> const TFunc_Instances_GlobalObjectCPP_getClassByAlias::TMethod '
         'TFunc_Instances_GlobalObjectCPP_getClassByAlias::Method = '
         '&Instances::fl::GlobalObjectCPP::getClassByAlias;\n'
         'template <> const TFunc_Instances_GlobalObjectCPP_registerClassAlias::TMethod '
         'TFunc_Instances_GlobalObjectCPP_registerClassAlias::Method = '
         '&Instances::fl::GlobalObjectCPP::registerClassAlias;\n',
         'flash.net class alias thunks')
    edit(global_cpp,
         '"navigateToURL", NULL, Abc::NS_Public, CT_Method, 1, 2},\n',
         '"navigateToURL", NULL, Abc::NS_Public, CT_Method, 1, 2},\n'
         '                {TFunc_Instances_GlobalObjectCPP_getClassByAlias::Func, &AS3::ClassClassTI, '
         '"getClassByAlias", NULL, Abc::NS_Public, CT_Method, 1, 1},\n'
         '                {TFunc_Instances_GlobalObjectCPP_registerClassAlias::Func, NULL, '
         '"registerClassAlias", NULL, Abc::NS_Public, CT_Method, 2, 2},\n',
         'flash.net class alias registration')
    edit(global_cpp,
         '//##protect##"instance::::navigateToURL()"\n    }\n',
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
         '    }\n',
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
    edit(vm_h,
         '    void AddVMAbcFileWeak(VMAbcFile* f);\n',
         '    // flash.net class aliases (4.6.34 00e59390 and 00e59350).\n'
         '    void RegisterClassAlias(const ASString& alias, Class& cls);\n'
         '    Class* GetClassByAlias(const ASString& alias) const;\n'
         '    void RemoveClassAlias(const ASString& alias);\n\n'
         '    void AddVMAbcFileWeak(VMAbcFile* f);\n',
         'VM class alias API')
    edit(vm_h,
         '    ///////////////////////////////////////////////////////////////////////\n'
         '    VMAppDomain* SystemDomain;\n',
         '    // Raw like 4.6.34: each class\'s file removes its aliases when destroyed.\n'
         '    ASStringHash<Class*> ClassAliases;\n\n'
         '    ///////////////////////////////////////////////////////////////////////\n'
         '    VMAppDomain* SystemDomain;\n',
         'VM class alias map')
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
    edit(vm_cpp,
         'void VM::AddVMAbcFileWeak(VMAbcFile* f)\n',
         'void VM::RegisterClassAlias(const ASString& alias, Class& cls)\n{\n'
         '    // 00e59390: the same pair is unchanged; a moved alias leaves its old file.\n'
         '    Class** registered = ClassAliases.Get(alias);\n'
         '    if (registered)\n'
         '    {\n'
         '        if (*registered == &cls)\n'
         '            return;\n'
         '        VMFile* previous = (*registered)->GetTraits().GetFilePtr();\n'
         '        if (previous)\n'
         '            previous->RemoveClassAlias(alias);\n'
         '    }\n'
         '    VMFile* file = cls.GetTraits().GetFilePtr();\n'
         '    if (file)\n'
         '        file->AddClassAlias(alias);\n'
         '    ClassAliases.Set(alias, &cls);\n'
         '}\n\n'
         'Class* VM::GetClassByAlias(const ASString& alias) const\n{\n'
         '    Class* const* registered = ClassAliases.Get(alias);\n'
         '    return registered ? *registered : NULL;\n'
         '}\n\n'
         'void VM::RemoveClassAlias(const ASString& alias)\n{\n'
         '    ClassAliases.Remove(alias);\n'
         '}\n\n'
         'void VM::AddVMAbcFileWeak(VMAbcFile* f)\n',
         'VM class alias implementation')


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
