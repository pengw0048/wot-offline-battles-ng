"""#1513 Scaleform localization: WGGFxStringTranslator and WG.getFormattedLocalizedString.

The patch tests run the SDK and engine patchers over the pinned sources in build
order. The host runs the production owner over the public Script/FxDelegate text
and the existing SDK 4.2.22 Translator, driven by the original helpers.i18n and
gettext PYC with the original MO files. Neither proves GFx text rendering.
"""
from pathlib import Path
import importlib.util
import os
import shlex
import shutil
import subprocess
import sys
import tarfile
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
from test_stock_scaleform_scene import ROOT, REVISION, SDK_FILES, load_patch
from stock_battle_gui_support import client_executable
from test_stock_numeric_format import NLS_HOST

ENGINE_FILES = ('CMakeLists.txt', 'build/cmake/BWConfiguration_client.cmake', 'client/CMakeLists.txt',
                'lib/scaleform/CMakeLists.txt', 'lib/scaleform/config.hpp', 'lib/scaleform/ime.cpp',
                'lib/scaleform/ime.hpp', 'lib/scaleform/loader.cpp', 'lib/scaleform/log.cpp',
                'lib/scaleform/manager.cpp', 'lib/scaleform/manager.hpp', 'lib/scaleform/py_movie_view.cpp',
                'lib/scaleform/py_movie_view.hpp')
PUBLIC_FILES = ('lib/pyscript/script.cpp', 'lib/script/init_time_job.hpp', 'lib/script/init_time_job.cpp',
                'lib/cstdmf/string_utils.cpp', 'lib/scaleform/fx_game_delegate.hpp',
                'lib/scaleform/fx_game_delegate.cpp')
SEAM_FILES = ('lib/scaleform/manager.cpp', 'lib/scaleform/py_movie_view.cpp')
LOADER = 'Src/GFx/GFx_Loader.cpp'
LOADER_IMPL = 'Src/GFx/GFx_LoaderImpl.cpp'
HOST_SDK_FILES = ('Src/GFx/GFx_Loader.h', 'Src/Kernel/SF_Std.cpp', 'Src/Kernel/SF_UTF8Util.cpp',
                  'Src/Kernel/SF_WString.h', 'Src/Kernel/SF_WString.cpp', 'Src/GFx/GFx_TextField.cpp',
                  LOADER_IMPL)


def load(name):
    spec = importlib.util.spec_from_file_location(name, ROOT / 'bigworld' / (name + '.py'))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def pinned_sdk(test, names):
    checkout = Path(os.environ.get('WOT_SCALEFORM_SDK_GIT',
                                   '/Users/peng/.cache/wot-independent-client/scaleform-sdk-4.2'))
    try:
        return {name: subprocess.run(['git', '-C', str(checkout), 'show', REVISION + ':' + name],
                                     check=True, capture_output=True).stdout for name in names}
    except (OSError, subprocess.CalledProcessError):
        test.skipTest('Pinned Scaleform SDK checkout unavailable')


def pinned_engine(test, names):
    archive = Path(os.environ.get('WOT_PUBLIC_ENGINE_ARCHIVE', '/tmp/wot-bigworld-engine-14.4.1.tar.gz'))
    if not archive.is_file():
        test.skipTest('Pinned public engine source archive required')
    files = {}
    with tarfile.open(archive) as tar:
        for member in tar:
            for name in names:
                if member.name.endswith('/programming/bigworld/' + name):
                    files[name] = tar.extractfile(member).read()
            if len(files) == len(names):
                break
    test.assertEqual(set(files), set(names))
    return files


def write(root, files):
    for name, data in files.items():
        (root / name).parent.mkdir(parents=True, exist_ok=True)
        (root / name).write_bytes(data)


def snapshot(root, names):
    return {name: (root / name).read_bytes() for name in names}


def text(data):
    return data.decode('latin-1').replace('\r\n', '\n')


def between(source, begin, end, include_end=True):
    start = source.index(begin)
    stop = source.index(end, start)
    return source[start:stop + (len(end) if include_end else 0)]


def prepared_engine(test, root):
    """Engine and SDK roots after patch_scaleform_sdk.py, as the build leaves them."""
    write(root / 'engine', pinned_engine(test, ENGINE_FILES))
    write(root / 'sdk', pinned_sdk(test, SDK_FILES))
    load_patch().patch_engine(root / 'engine', root / 'sdk')
    return root / 'engine'


class ScaleformLocalizationPatchTest(unittest.TestCase):
    def test_sdk_definitions_keep_the_existing_owner_and_migrate_old_trees(self):
        patch = load_patch()
        with tempfile.TemporaryDirectory(prefix='scaleform-translator-patch-') as directory:
            root = Path(directory)
            files = pinned_sdk(self, SDK_FILES)
            original = text(files[LOADER])
            owner = text(files[LOADER_IMPL])
            self.assertNotIn('Translator::', original)
            self.assertEqual(owner.count('void Translator::TranslateInfo::SetResult('), 2)
            self.assertEqual(owner.count('bool Translator::OnWordWrapping('), 1)
            write(root, files)
            patch.patch_sdk(root)
            final = snapshot(root, SDK_FILES)
            self.assertEqual(final[LOADER], files[LOADER])
            self.assertEqual(final[LOADER_IMPL], files[LOADER_IMPL])
            patch.patch_sdk(root)
            self.assertEqual(final, snapshot(root, SDK_FILES))

            # Upgrade an SDK already prepared by 765fa706 without losing its owner.
            legacy = patch.LEGACY_TRANSLATOR_DEFINITIONS
            anchor = '// ***** URLBuilder implementation\n'
            previous = original.replace(anchor, legacy + anchor, 1)
            (root / LOADER).write_text(previous)
            patch.patch_sdk(root)
            migrated = snapshot(root, SDK_FILES)
            self.assertEqual(text(migrated[LOADER]), original)
            self.assertEqual(migrated[LOADER_IMPL], files[LOADER_IMPL])
            patch.patch_sdk(root)
            self.assertEqual(migrated, snapshot(root, SDK_FILES))

            changed = [
                (LOADER, previous.replace(legacy, legacy * 2)),
                (LOADER, previous.replace('    pResult->Resize(resultLen + 1);\n',
                                          '    pResult->Resize(resultLen);\n')),
                (LOADER, previous.replace(anchor, '// ***** URLBuilder\n')),
                (LOADER, previous.replace(legacy, legacy[:legacy.index('// 00b51fd0')])),
                (LOADER_IMPL, owner.replace('bool Translator::OnWordWrapping(',
                                            'bool Translator::MissingOnWordWrapping(')),
                (LOADER_IMPL, owner + owner[owner.index('bool Translator::OnWordWrapping('):]),
            ]
            for index, (name, data) in enumerate(changed):
                write(root, final)
                (root / name).write_text(data)
                before = snapshot(root, SDK_FILES)
                with self.assertRaises(ValueError, msg=index):
                    patch.patch_sdk(root)
                self.assertEqual(before, snapshot(root, SDK_FILES), index)

    def test_real_sdk_loader_objects_link_with_one_translator_owner(self):
        compiler = shutil.which('i686-w64-mingw32-g++')
        linker = shutil.which('i686-w64-mingw32-ld')
        if not compiler or not linker:
            self.skipTest('i686 MinGW compiler/linker required for the SDK object-link regression')
        checkout = Path(os.environ.get('WOT_SCALEFORM_SDK_GIT',
                                       '/Users/peng/.cache/wot-independent-client/scaleform-sdk-4.2'))
        with tempfile.TemporaryDirectory(prefix='scaleform-translator-link-') as directory:
            root = Path(directory)
            archive = subprocess.run(['git', '-C', str(checkout), 'archive', REVISION,
                                      'Src', 'Include', '3rdParty/libpng-1.5.13/png.h'],
                                     check=True, capture_output=True).stdout
            subprocess.run(['tar', '-x', '-C', str(root)], input=archive, check=True)
            patch = load_patch()
            patch.patch_sdk(root)
            # The pinned MinGW branch lacks C++ inline on header definitions;
            # MSVC's __forceinline supplies it. Match that compiler behavior in
            # this private fixture without changing any production SDK body.
            types = root / 'Src/Kernel/SF_Types.h'
            source = types.read_text()
            before = '# define SF_INLINE          __attribute__((always_inline))'
            self.assertEqual(source.count(before), 1)
            types.write_text(source.replace(before,
                             '# define SF_INLINE          inline __attribute__((always_inline))', 1))

            def compile_source(source, output):
                command = [compiler, '-std=c++14', '-w', '-c', '-DSF_BUILD_STATICLIB',
                           '-DSF_BUILD_SHIPPING', '-ISrc', '-IInclude', '-ISrc/GFx', source,
                           '-o', output]
                result = subprocess.run(command, cwd=root, capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stderr[-4000:])

            compile_source(LOADER, 'loader.o')
            compile_source(LOADER_IMPL, 'owner.o')
            # A relocatable link permits unrelated SDK imports but still rejects
            # duplicate strong definitions across the two real SDK objects.
            command = [linker, '-r', 'loader.o', 'owner.o', '-o', 'combined.o']
            result = subprocess.run(command, cwd=root, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)

            # The old injection must fail at the same ownership boundary.
            loader = root / LOADER
            loader.write_text(loader.read_text().replace('// ***** URLBuilder implementation\n',
                              patch.LEGACY_TRANSLATOR_DEFINITIONS + '// ***** URLBuilder implementation\n', 1))
            compile_source(LOADER, 'loader.o')
            result = subprocess.run(command, cwd=root, capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('multiple definition', result.stderr)
            self.assertIn('SetResult', result.stderr)
            self.assertIn('OnWordWrapping', result.stderr)

    def test_engine_seams_follow_the_sdk_patch_in_either_order(self):
        localization = load('patch_stock_scaleform_localization')
        glyph = load('patch_stock_glyph_cache')
        with tempfile.TemporaryDirectory(prefix='scaleform-localization-engine-') as directory:
            engine = prepared_engine(self, Path(directory))
            prepared = snapshot(engine, ENGINE_FILES)
            localization.patch(engine)
            glyph.patch(engine)
            final = snapshot(engine, ENGINE_FILES)
            self.assertEqual((engine / 'lib/stock_scaleform_localization.hpp').read_bytes(),
                             (ROOT / 'bigworld/stock_scaleform_localization.hpp').read_bytes())
            write(engine, prepared)
            glyph.patch(engine)
            localization.patch(engine)
            localization.patch(engine)
            self.assertEqual(final, snapshot(engine, ENGINE_FILES))
        manager = final['lib/scaleform/manager.cpp'].decode()
        view = final['lib/scaleform/py_movie_view.cpp'].decode()
        self.assertIn('\t\tpLoader_->SetFontLib(pFontLib_);\n\t\tinstallStockTranslator( *pLoader_ );\n'
                      '\t\tpTexMgr_ =', manager)
        self.assertIn('\t\tgameDelegate_ =  *new FxDelegate;\n\t\tregisterStockLocalization( *gameDelegate_ );\n'
                      '\t\tpMovieView_->SetExternalInterface(', view)
        # Teardown still clears every handler before dropping the delegate.
        self.assertIn('\t\tgameDelegate_->ClearAll();\n\t\tgameDelegate_ = NULL;\n', view)
        for name in SEAM_FILES:
            removed = set(prepared[name].decode().splitlines()) - set(final[name].decode().splitlines())
            self.assertEqual(removed, set(), name)

    def test_engine_patch_rejects_partial_duplicate_or_changed_state_without_writing(self):
        localization = load('patch_stock_scaleform_localization')
        with tempfile.TemporaryDirectory(prefix='scaleform-localization-reject-') as directory:
            engine = prepared_engine(self, Path(directory))
            pristine = {name: (engine / name).read_text() for name in SEAM_FILES}
            localization.patch(engine)
            patched = {name: (engine / name).read_text() for name in SEAM_FILES}
            mutations = {}
            for name, edits in localization.FILES:
                for index, (before, after) in enumerate(edits):
                    label = '%s edit %d ' % (name, index)
                    mutations[label + 'reverted'] = (name, patched[name].replace(after, before))
                    mutations[label + 'applied alone'] = (name, pristine[name].replace(before, after))
                    mutations[label + 'duplicated'] = (name, patched[name] + after)
                    mutations[label + 'anchor removed'] = (name, pristine[name].replace(before, ''))
            mutations['translator moved'] = ('lib/scaleform/manager.cpp', patched['lib/scaleform/manager.cpp'].replace(
                '\t\tinstallStockTranslator( *pLoader_ );\n', '').replace(
                '\t\tpTexMgr_ =', '\t\tinstallStockTranslator( *pLoader_ );\n\t\tpTexMgr_ ='))
            mutations['registration in manager'] = ('lib/scaleform/manager.cpp',
                                                    patched['lib/scaleform/manager.cpp'] +
                                                    '// registerStockLocalization\n')
            for label, (name, data) in mutations.items():
                for other in SEAM_FILES:
                    (engine / other).write_text(pristine[other] if other != name else data)
                before = {other: (engine / other).read_text() for other in SEAM_FILES}
                with self.assertRaises(ValueError, msg=label):
                    localization.patch(engine)
                self.assertEqual(before, {other: (engine / other).read_text() for other in SEAM_FILES}, label)
            # One file final and the other pristine completes to the same result.
            (engine / SEAM_FILES[0]).write_text(patched[SEAM_FILES[0]])
            (engine / SEAM_FILES[1]).write_text(pristine[SEAM_FILES[1]])
            localization.patch(engine)
            self.assertEqual(patched, {name: (engine / name).read_text() for name in SEAM_FILES})


def generated_host(sdk, public, set_result):
    loader_h, std = text(sdk['Src/GFx/GFx_Loader.h']), text(sdk['Src/Kernel/SF_Std.cpp'])
    wstring_h, wstring_cpp = text(sdk['Src/Kernel/SF_WString.h']), text(sdk['Src/Kernel/SF_WString.cpp'])
    utf8, field = text(sdk['Src/Kernel/SF_UTF8Util.cpp']), text(sdk['Src/GFx/GFx_TextField.cpp'])
    script, jobs = text(public['lib/pyscript/script.cpp']), text(public['lib/script/init_time_job.cpp'])
    delegate_h = text(public['lib/scaleform/fx_game_delegate.hpp'])
    delegate_cpp = text(public['lib/scaleform/fx_game_delegate.cpp'])
    production = (ROOT / 'bigworld/stock_scaleform_localization.cpp').read_text()
    numeric = (ROOT / 'bigworld/stock_numeric_format.cpp').read_text()
    numeric = numeric[numeric.index('BW_BEGIN_NAMESPACE'):numeric.index('\nbool readInteger(')] + '\n}\nBW_END_NAMESPACE\n' + \
        numeric[numeric.index('bool wot::stock_numeric_format::format('):]
    replacements = {
        '@SDK_STD@': between(std, 'UPInt   SF_CDECL SFwcslen(', '\n}\n') +
        between(std, 'wchar_t* SF_CDECL SFwcsncpy(', '\n}\n'),
        '@UTF8@': between(utf8, 'SPInt     SF_STDCALL GetLength(', '#ifdef UTF8_UNIT_TEST', False),
        '@WSTRING_CLASS@': between(wstring_h, 'class WStringBuffer\n{', '\n};\n'),
        '@WSTRING_BODIES@': between(wstring_cpp, 'WStringBuffer::~WStringBuffer()', '\n}\n') +
        between(wstring_cpp, 'bool     WStringBuffer::Resize(UPInt size)', '\n}\n'),
        '@TRANSLATOR_CLASS@': between(loader_h, 'class Translator : public State\n{', '\n};\n'),
        '@SET_RESULT@': set_result,
        '@TEXT_FIELD_INFO@': between(field, 'class TranslateInfo : public Translator::TranslateInfo',
                                     '            } translateInfo(', False) + '            };\n',
        '@FX_DELEGATE_CLASSES@': between(delegate_h, 'class FxDelegateHandler : public RefCountBase',
                                         '\tFallbackDefn    Fallback;\n};\n'),
        '@FX_DELEGATE_BODIES@': between(delegate_cpp, 'class AddCallbackVisitor',
                                        'void FxDelegate::UnregisterHandler', False) +
        between(delegate_cpp, 'void FxDelegate::Callback(', '\n}\n'),
        '@FINI_JOB_CLASS@': between(text(public['lib/script/init_time_job.hpp']), 'class FiniTimeJob\n{', '\n};\n'),
        '@FINI_JOB_BODIES@': between(jobs, 'typedef BW::vector< FiniTimeJob * >', '\n} // namespace Script', False),
        '@SCRIPT_BODIES@': between(script, 'bool Script::isFinalised()', '\n}\n') +
        between(script, 'PyObject * Script::ask( PyObject * pFunction,', '\n}\n') +
        between(script, 'PyObject * Script::getData( const BW::string & data )', '\n}\n') +
        between(script, 'PyObject * Script::getData( const BW::wstring & data )', '\n}\n'),
        # The _WIN32 conversion (the first definition), over a MultiByteToWideChar stand-in.
        '@UTF8TOW@': between(text(public['lib/cstdmf/string_utils.cpp']),
                             'bool bw_utf8tow( const char * src, BW::wstring& output )', '\n}\n'),
        '@PRODUCTION@': ''.join(line for line in production.splitlines(True) if not line.startswith('#include')),
        '@NUMERIC_OWNER@': (ROOT / 'bigworld/stock_numeric_format.hpp').read_text().replace('#pragma once', '') +
        NLS_HOST + numeric,
    }
    host = (ROOT / 'tests/stock_scaleform_localization_host.cpp').read_text()
    for key, value in replacements.items():
        assert host.count(key) == 1, key
        host = host.replace(key, value)
    return host


class ScaleformLocalizationOwnerTest(unittest.TestCase):
    def test_original_i18n_drives_the_production_owner(self):
        client = os.environ.get('WOT_0922_CLIENT')
        if not client:
            self.skipTest('WOT_0922_CLIENT is required for the original PYC and MO files')
        python = Path(os.environ.get('WOT_PYTHON27', '/Users/peng/.pyenv/versions/2.7.18/bin/python2.7'))
        config = python.with_name('python2.7-config')
        if not config.exists():
            self.skipTest('Pinned CPython 2.7 development files unavailable')
        sdk = pinned_sdk(self, HOST_SDK_FILES)
        public = pinned_engine(self, PUBLIC_FILES)
        set_result = between(text(sdk[LOADER_IMPL]),
                             'void Translator::TranslateInfo::SetResult(const wchar_t*',
                             '}} // Scaleform::GFx', False)
        with tempfile.TemporaryDirectory(prefix='scaleform-localization-host-') as directory:
            root = Path(directory)
            (root / 'host.cpp').write_text(generated_host(sdk, public, set_result))
            flags = [flag for flag in shlex.split(subprocess.check_output([str(config), '--cflags'], text=True))
                     if flag != '-DNDEBUG']
            libs = shlex.split(subprocess.check_output([str(config), '--ldflags'], text=True))
            # python2.7-config adds -Wall; the pinned SDK text keeps unused members.
            subprocess.run(['c++', '-std=c++14', '-UNDEBUG', '-Werror', str(root / 'host.cpp'), *flags,
                            '-Wno-deprecated-register', '-Wno-writable-strings', '-Wno-unused-function',
                            '-Wno-unused-variable', '-Wno-unused-private-field', *libs, '-o', str(root / 'host')],
                           check=True)
            result = subprocess.run([str(root / 'host'), str(ROOT / 'tests/stock_scaleform_localization_consumer.py'),
                                     client], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertTrue(result.stdout.endswith('localization owner: pass\n'), result.stdout)


class ScaleformLocalizationOriginalTest(unittest.TestCase):
    def test_original_numeric_callback_registration_and_nls_calls(self):
        exe = client_executable(self)
        callbacks = (
            ('Gold', 0x150c00c, 0xab3447, 0xab345a, 0xab3c50, 0xab3cac, 0x6f4070),
            ('Integral', 0x150c020, 0xab349a, 0xab34b3, 0xab3d40, 0xab3d9c, 0x6f4120),
            ('Fractional', 0x150c038, 0xab34f1, 0xab350a, 0xab3b60, 0xab3bbd, 0x6f3eb0),
            ('NiceNumber', 0x150c050, 0xab3548, 0xab3561, 0xab4010, 0xab406d, 0x6f41d0),
            ('ShortTime', 0x150c068, 0xab359f, 0xab35b8, 0xab41f0, 0xab424e, 0x6f42b0),
            ('LongTime', 0x150c080, 0xab35f6, 0xab360f, 0xab3f20, 0xab3f7d, 0x6f42b0),
            ('ShortDate', 0x150c098, 0xab364d, 0xab3666, 0xab4100, 0xab415e, 0x6f3d50),
            ('LongDate', 0x150c0b0, 0xab36a4, 0xab36bd, 0xab3e30, 0xab3e8e, 0x6f3d50),
            ('YMDate', 0x150c0c8, 0xab36fb, 0xab3714, 0xab42e0, 0xab433e, 0x6f3d50),
        )
        for name, string, registration, function, body, call, nls in callbacks:
            with self.subTest(name=name):
                self.assertEqual(exe.cstr(string), 'WG.get%sFormat' % name)
                self.assertEqual(exe.code(registration, 5), b'\x68' + string.to_bytes(4, 'little'))
                self.assertEqual(exe.code(function, 5), b'\x68' + body.to_bytes(4, 'little'))
                self.assertEqual(exe.code(body + 44, 8), bytes.fromhex('8b4608f20f104010'))
                instruction = exe.code(call, 5)
                self.assertEqual(instruction[0], 0xe8)
                self.assertEqual(call + 5 + int.from_bytes(instruction[1:], 'little', signed=True), nls)

    def test_original_owner_bodies(self):
        exe = client_executable(self)
        # WGGFxStringTranslator vtable 014bf270: dtor, GetCaps, Translate, OnWordWrapping.
        self.assertEqual([exe.word(0x14bf270 + 4 * i) for i in range(4)],
                         [0x6f4ff0, 0x9c0050, 0x6f5250, 0xb52030])
        self.assertEqual(exe.code(0x9c0050, 6), bytes.fromhex('b801000000c3'))   # Cap_ReceiveHtml
        # 00621af0: the new translator becomes loader state 1 (SetState through vtable +8).
        self.assertEqual(exe.code(0x621af0, 5)[0], 0xe8)
        self.assertEqual(exe.code(0x621b06, 3), bytes.fromhex('56' '6a01'))
        # 00aa4cc5: each PyMovieView constructs WGExtensions (00ab3400).
        call = exe.code(0xaa4cc5, 5)
        self.assertEqual(0xaa4cc5 + 5 + int.from_bytes(call[1:], 'little', signed=True), 0xab3400)
        # 00ab3752: Accept registers the formatted string callback 00ab37c0.
        self.assertEqual(exe.code(0xab3752, 5), b'\x68' + (0x150c0dc).to_bytes(4, 'little'))
        self.assertEqual(exe.cstr(0x150c0dc), 'WG.getFormattedLocalizedString')
        self.assertEqual(exe.code(0xab376b, 5), b'\x68' + (0xab37c0).to_bytes(4, 'little'))

if __name__ == '__main__':
    unittest.main()
