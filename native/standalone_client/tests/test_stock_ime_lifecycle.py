"""#1513 LangBar input-language owner on the Scaleform loader.

The patch tests run the engine and SDK patchers over the pinned sources in both
orders. The command host drives the production command owner over a scripted
layout table, and MinGW compiles the owner and the guarded SDK TUs against the
real SDK headers. None of this runs Win32 keyboard layouts or the AS3 VM; the
Windows probe in stock_language_bar_probe.cpp does.
"""
from pathlib import Path
import os
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
import zipfile
import zlib

sys.path.insert(0, str(Path(__file__).resolve().parent))
from test_stock_scaleform_scene import ROOT, REVISION, SDK_FILES, load_patch
from test_stock_scaleform_localization import (ENGINE_FILES, load, pinned_engine, pinned_sdk, snapshot, text,
                                               write)

IME_MANAGER = 'Src/GFx/IME/GFx_IMEManager.h'
MANAGER = 'lib/scaleform/manager.cpp'
CONFIG = 'lib/scaleform/config.hpp'
HELPERS = ('lib/scaleform/stock_language_bar.hpp', 'lib/scaleform/stock_language_bar_commands.hpp')
COMMANDS = {'LangBar_OnInit', 'LangBar_GetInstalledLanguages', 'LangBar_OnSelectInputLang',
            'LangBar_GetIMENames', 'LangBar_OnSelectIME'}


def run_patch(*arguments):
    command = [sys.executable, str(ROOT / 'bigworld/patch_stock_ime_lifecycle.py')] + [str(a) for a in arguments]
    return subprocess.run(command, capture_output=True, text=True,
                          env=dict(os.environ, PYTHONDONTWRITEBYTECODE='1'))


class ImeLifecyclePatchTest(unittest.TestCase):
    def roots(self, directory):
        root = Path(directory)
        write(root / 'engine', pinned_engine(self, ENGINE_FILES))
        write(root / 'sdk', pinned_sdk(self, SDK_FILES + (IME_MANAGER,)))
        return root / 'engine', root / 'sdk'

    def test_fresh_roots_reach_one_final_state(self):
        patch = load('patch_stock_ime_lifecycle')
        with tempfile.TemporaryDirectory(prefix='ime-lifecycle-patch-') as directory:
            engine, sdk = self.roots(directory)
            pristine = (engine / MANAGER).read_bytes(), (sdk / IME_MANAGER).read_bytes()
            self.assertIn(b'\r\n', pristine[1])
            patch.patch(engine, sdk)
            manager = text((engine / MANAGER).read_bytes())
            self.assertEqual(manager.count('#include "ime.hpp"\n#include "stock_language_bar.hpp"\n'), 1)
            install = manager.index('\t\t::wot::stock_language_bar::install( *pLoader_ );\n')
            # After both ASSupports exist and before any movie can be created.
            self.assertLess(manager.index('pLoader_->SetAS3Support(pASSupport);'), install)
            self.assertLess(manager.index('pLoader_->SetAS2Support(pAS2Support);'), install)
            self.assertLess(install, manager.index('pFontMap_ = *new GFx::FontMap();'))
            header = (sdk / IME_MANAGER).read_bytes()
            bare = header.replace(b'\r\n', b'').count(b'\n')
            self.assertEqual(bare, pristine[1].replace(b'\r\n', b'').count(b'\n'))
            guarded = text(header)
            self.assertIn('return pasimemanager ? pasimemanager->HandleFocus(pmovie, poldFocusedItem, '
                          'pnewFocusingItem, ptopMostItem) : pnewFocusingItem;', guarded)
            self.assertEqual(guarded.count('pASIMEManager && pASIMEManager->IsTextFieldFocused('), 2)
            # Only the pointer guards change; the rest of the header is byte-identical.
            self.assertEqual(len(text(pristine[1]).splitlines()) + 1, len(guarded.splitlines()))
            for helper in HELPERS:
                self.assertEqual((engine / helper).read_bytes(),
                                 (ROOT / 'bigworld' / Path(helper).name).read_bytes())
            final = snapshot(engine, (MANAGER,) + HELPERS), (sdk / IME_MANAGER).read_bytes()
            patch.patch(engine, sdk)
            self.assertEqual(final, (snapshot(engine, (MANAGER,) + HELPERS), (sdk / IME_MANAGER).read_bytes()))

    def test_prepared_roots_compose_in_either_order(self):
        sdk_patch = load_patch()
        localization = load('patch_stock_scaleform_localization')
        patch = load('patch_stock_ime_lifecycle')
        results = []
        for order in ('existing-first', 'owner-first'):
            with tempfile.TemporaryDirectory(prefix='ime-lifecycle-compose-') as directory:
                engine, sdk = self.roots(directory)
                if order == 'owner-first':
                    patch.patch(engine, sdk)
                sdk_patch.patch_engine(engine, sdk)
                localization.patch(engine)
                patch.patch(engine, sdk)
                # The existing focus owner and translator seams are intact.
                manager = text((engine / MANAGER).read_bytes())
                self.assertEqual(manager.count('installStockTranslator( *pLoader_ );'), 1)
                self.assertEqual(manager.count('::wot::stock_language_bar::install( *pLoader_ );'), 1)
                ime = text((engine / 'lib/scaleform/ime.cpp').read_bytes())
                self.assertIn('Manager::instance().pLoader()->GetIMEManager()', ime)
                self.assertIn('\t\ts_pFocussed->pMovieView()->HandleEvent( GFx::SetFocusEvent() );', ime)
                results.append((snapshot(engine, (MANAGER, 'lib/scaleform/ime.cpp') + HELPERS),
                                (sdk / IME_MANAGER).read_bytes()))
        self.assertEqual(results[0], results[1])

    def test_sdk_only_cli_matches_the_engine_build(self):
        with tempfile.TemporaryDirectory(prefix='ime-lifecycle-sdk-') as directory:
            engine, sdk = self.roots(directory)
            only = Path(directory) / 'sdk-only'
            write(only, pinned_sdk(self, (IME_MANAGER,)))
            result = run_patch('--sdk-only', only)
            self.assertEqual(result.returncode, 0, result.stderr)
            result = run_patch(engine, sdk)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual((only / IME_MANAGER).read_bytes(), (sdk / IME_MANAGER).read_bytes())
            self.assertNotEqual(run_patch(engine).returncode, 0)
            self.assertNotEqual(run_patch('--sdk-only', engine, sdk).returncode, 0)

    def test_windows_text_mode_engine_files_keep_crlf(self):
        patch = load('patch_stock_ime_lifecycle')
        with tempfile.TemporaryDirectory(prefix='ime-lifecycle-crlf-') as directory:
            engine, sdk = self.roots(directory)
            for name in (MANAGER, CONFIG):
                path = engine / name
                path.write_bytes(path.read_bytes().replace(b'\n', b'\r\n'))
            patch.patch(engine, sdk)
            manager = (engine / MANAGER).read_bytes()
            self.assertNotIn(b'\n', manager.replace(b'\r\n', b''))
            self.assertIn(b'\t\t::wot::stock_language_bar::install( *pLoader_ );\r\n', manager)

    def test_changed_or_partial_seams_write_nothing(self):
        patch = load('patch_stock_ime_lifecycle')
        with tempfile.TemporaryDirectory(prefix='ime-lifecycle-reject-') as directory:
            engine, sdk = self.roots(directory)
            files = snapshot(engine, (MANAGER, CONFIG)), (sdk / IME_MANAGER).read_bytes()
            manager, config, header = text(files[0][MANAGER]), text(files[0][CONFIG]), files[1].decode('latin-1')
            install = '\t\tpLoader_->SetAS2Support(pAS2Support);\n'
            changed = [
                (MANAGER, manager.replace(install, '\t\tpLoader_->SetAS2Support( pAS2Support );\n')),
                (MANAGER, manager.replace('#include "ime.hpp"\n', '#include "ime.hpp"\n#include "stock_language_bar.hpp"\n')),
                (MANAGER, manager.replace(install, install + install)),
                (CONFIG, config.replace('#define SCALEFORM_IME 0', '#define SCALEFORM_IME 1')),
                (IME_MANAGER, header.replace('return GetASIMEManager()->HandleFocus(', 'return GetASIMEManager()->Focus(')),
                (IME_MANAGER, header.replace('\tbool IsTextFieldFocused() const {return pASIMEManager->',
                                             '\tbool IsTextFieldFocused() const {return pASIMEManager && pASIMEManager->')),
            ]
            for index, (name, data) in enumerate(changed):
                write(engine, files[0])
                (sdk / IME_MANAGER).write_bytes(files[1])
                target = sdk / name if name == IME_MANAGER else engine / name
                target.write_bytes(data.encode('latin-1'))
                before = snapshot(engine, (MANAGER, CONFIG)), (sdk / IME_MANAGER).read_bytes()
                with self.assertRaises(ValueError, msg=index):
                    patch.patch(engine, sdk)
                self.assertEqual(before, (snapshot(engine, (MANAGER, CONFIG)), (sdk / IME_MANAGER).read_bytes()),
                                 index)
                for helper in HELPERS:
                    self.assertFalse((engine / helper).exists(), index)


class LangBarContractTest(unittest.TestCase):
    def test_original_swf_sends_only_the_owned_commands(self):
        client = Path(os.environ.get('WOT_0922_CLIENT',
                                     '/Users/peng/Downloads/World_of_Tanks_0.09.22.00.01_CH_1513_HD'))
        package = client / 'res/packages/gui.pkg'
        if not package.is_file():
            self.skipTest('Exact #1513 client required')
        with zipfile.ZipFile(package) as archive:
            data = archive.read('gui/flash/LangBar.swf')
        self.assertEqual(data[:3], b'CWS')
        body = zlib.decompress(data[8:])
        commands = set(m.decode() for m in re.findall(rb'LangBar_[A-Za-z]+', body)) - {'LangBar_fla'}
        self.assertEqual(commands, COMMANDS)
        for event in (b'SET_CURRENT_INPUT_LANGUAGE', b'SET_SUPPORTED_LANGUAGES', b'SET_SUPPORTED_IMENAMES',
                      b'SET_IMENAME', b'SendLangBarMessage', b'IMENameTab_mc', b'InputLangTab_mc', b'@@'):
            self.assertIn(event, body)
        source = (ROOT / 'bigworld/stock_language_bar_commands.hpp').read_text()
        self.assertEqual(set(re.findall(r'"(LangBar_[A-Za-z]+)"', source)), COMMANDS)


class LangBarCommandHostTest(unittest.TestCase):
    def test_command_answers_follow_the_os_state(self):
        compiler = shutil.which('c++')
        if not compiler:
            self.skipTest('Host C++ compiler required')
        with tempfile.TemporaryDirectory(prefix='langbar-commands-') as directory:
            binary = Path(directory) / 'commands'
            build = subprocess.run([compiler, '-std=c++11', '-Wall', '-Wextra', '-Werror',
                                    '-I' + str(ROOT / 'bigworld'),
                                    str(ROOT / 'tests/stock_language_bar_commands_host.cpp'), '-o', str(binary)],
                                   capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout)
            self.assertEqual(result.stdout.strip(), 'PASSED 0')


class LangBarSdkCompileTest(unittest.TestCase):
    def test_owner_probe_and_guarded_sdk_compile_against_real_headers(self):
        compiler = shutil.which('i686-w64-mingw32-g++')
        if not compiler:
            self.skipTest('i686 MinGW compiler required')
        checkout = Path(os.environ.get('WOT_SCALEFORM_SDK_GIT',
                                       '/Users/peng/.cache/wot-independent-client/scaleform-sdk-4.2'))
        with tempfile.TemporaryDirectory(prefix='langbar-sdk-compile-') as directory:
            root = Path(directory)
            try:
                archive = subprocess.run(['git', '-C', str(checkout), 'archive', REVISION, 'Src', 'Include',
                                          '3rdParty/expat-2.1.0/lib', '3rdParty/libpng-1.5.13/png.h'], check=True, capture_output=True).stdout
            except (OSError, subprocess.CalledProcessError):
                self.skipTest('Pinned Scaleform SDK checkout unavailable')
            subprocess.run(['tar', '-x', '-C', str(root)], input=archive, check=True)
            load_patch().patch_sdk(root)
            load('patch_stock_ime_lifecycle').patch(sdk_root=root)
            # As in the translator link test: MinGW lacks MSVC's implied inline.
            types = root / 'Src/Kernel/SF_Types.h'
            before = '# define SF_INLINE          __attribute__((always_inline))'
            types.write_text(types.read_text().replace(
                before, '# define SF_INLINE          inline __attribute__((always_inline))', 1))
            flags = ['-std=c++14', '-DSF_BUILD_STATICLIB', '-DWIN32', '-D_WIN32', '-DNOMINMAX', '-ISrc', '-IInclude',
                     '-ISrc/GFx', '-I3rdParty/expat-2.1.0/lib', '-ISrc/GFx/AS3/Obj/Utils',
                     '-I' + str(ROOT / 'bigworld')]
            # The production SDK TUs that inline the guarded IMEManagerBase members.
            for index, source in enumerate(('Src/GFx/GFx_PlayerImpl.cpp', 'Src/GFx/GFx_TextField.cpp',
                                            'Src/GFx/IME/GFx_IMEManager.cpp',
                                            'Src/GFx/AS3/IME/AS3_IMEManager.cpp')):
                result = subprocess.run([compiler] + flags + ['-w', '-c', source, '-o', 'sdk%d.o' % index],
                                        cwd=root, capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stderr[-4000:])
            owner = root / 'owner.cpp'
            owner.write_text('#include <windows.h>\n#include <GFx.h>\n#include "stock_language_bar.hpp"\n'
                             'void installForTest( Scaleform::GFx::Loader & loader )\n'
                             '{ wot::stock_language_bar::install( loader ); }\n')
            for source in (owner, ROOT / 'tests/stock_language_bar_probe.cpp'):
                result = subprocess.run([compiler] + flags + ['-Wall', '-fsyntax-only', str(source)],
                                        cwd=root, capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stderr[-4000:])
                own = [line for line in result.stderr.splitlines()
                       if 'stock_language_bar' in line and 'warning' in line]
                self.assertEqual(own, [], source)


if __name__ == '__main__':
    unittest.main()
