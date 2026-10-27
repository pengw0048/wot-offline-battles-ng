"""Run the independent offline entry against exact #1513 startup consumers."""
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import tarfile
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'bigworld'))
import patch_stock_offline_entry

ENTRY = ROOT / 'bigworld/offline_entry/OfflineGarageEntry.py'
HARNESS = Path(__file__).with_name('offline_entry_harness.py')
PY27 = Path(os.environ.get('WOT_TEST_PY27', '/Users/peng/.pyenv/versions/2.7.18/bin/python2.7'))
FAKE_MOD = '''calls = []
def init(): calls.append('init')
def fini(): calls.append('fini')
def onAccountShowGUI(*args): calls.append(('onAccountShowGUI',) + args)
'''
HOST = r'''
#include <Python.h>
#include <cstdarg>
#include <cstdio>
static int g_critical = 0;
static void criticalInitError( const char * format, ... )
{
    ++g_critical;
    va_list args; va_start( args, format ); vfprintf( stderr, format, args ); va_end( args );
}
static bool initPersonality()
{
@HOOK@
    return true;
}
int main( int argc, char ** argv )
{
    Py_NoSiteFlag = 1;
    Py_Initialize();
    PySys_SetPath( argv[1] );
    const bool result = initPersonality();
    std::printf( "%d %d %d\n", int( result ), g_critical, int( PyErr_Occurred() != NULL ) );
    Py_Finalize();
    return 0;
}
'''


class OfflineEntryConsumerTests(unittest.TestCase):
    def setUp(self):
        client = os.environ.get('WOT_0922_CLIENT')
        if not client or not PY27.is_file():
            self.skipTest('WOT_0922_CLIENT and CPython 2.7 are required')
        self.package = str(Path(client) / 'res/packages/scripts.pkg')
        self.temporary = tempfile.TemporaryDirectory(prefix='offline-entry-')
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)

    def harness(self, entry_root, case):
        result = subprocess.run(
            [str(PY27), '-E', '-s', '-B', str(HARNESS), self.package, str(entry_root), case],
            capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn('PASS', result.stdout)

    def test_app_loader_has_no_intro_or_login_view(self):
        self.harness(ENTRY.parent, 'routing')

    def test_connection_publishes_logged_on_without_wgc(self):
        self.harness(ENTRY.parent, 'connection')

    def test_mod_init_runs_at_the_original_gui_mods_seam(self):
        mods = self.root / 'gui/mods'
        mods.mkdir(parents=True)
        shutil.copyfile(ENTRY, self.root / ENTRY.name)
        (mods / 'mod_offline_lan_0922.py').write_text(FAKE_MOD)
        self.harness(self.root, 'mods')


class NativeEntryHookTests(unittest.TestCase):
    def test_patch_is_anchored_and_idempotent(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / 'client/finale_app.cpp'
            path.parent.mkdir()
            path.write_text('x\n' + patch_stock_offline_entry.ANCHOR + 'y\n')
            patch_stock_offline_entry.patch(temporary)
            patched = path.read_text()
            patch_stock_offline_entry.patch(temporary)
            self.assertEqual(path.read_text(), patched)
            self.assertEqual(patched.count('OfflineGarageEntry'), 1)
            path.write_text('x\n')
            with self.assertRaises(ValueError):
                patch_stock_offline_entry.patch(temporary)

    def test_pristine_finale_app_has_one_anchor(self):
        archive = Path(os.environ.get('WOT_PUBLIC_ENGINE_ARCHIVE',
                                      '/tmp/wot-bigworld-engine-14.4.1.tar.gz'))
        if not archive.is_file():
            self.skipTest('pristine engine archive required')
        with tarfile.open(archive) as source:
            for member in source:
                if member.name.endswith('/programming/bigworld/client/finale_app.cpp'):
                    text = source.extractfile(member).read().decode('utf-8-sig')
                    break
            else:
                self.fail('finale_app.cpp is missing from the engine archive')
        self.assertEqual(text.count(patch_stock_offline_entry.ANCHOR), 1)
        # Personality::import precedes the hook; game.init follows it.
        anchor = text.index(patch_stock_offline_entry.ANCHOR)
        self.assertLess(text.index('Personality::import( persName )'), anchor)
        self.assertLess(anchor, text.index('Personality::instance().callMethod( "init"'))

    def test_hook_fails_startup_unless_install_returns_true(self):
        config = PY27.with_name('python2.7-config')
        if not config.is_file():
            self.skipTest('CPython 2.7 development runtime required')
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            hook = patch_stock_offline_entry.HOOK[len(patch_stock_offline_entry.ANCHOR):]
            (directory / 'host.cpp').write_text(HOST.replace('@HOOK@', hook))
            flags = shlex.split(subprocess.check_output([str(config), '--cflags'], text=True))
            libs = shlex.split(subprocess.check_output([str(config), '--ldflags'], text=True))
            subprocess.run(['c++', '-std=c++14', '-Wno-deprecated-register', str(directory / 'host.cpp'),
                            *flags, *libs, '-o', str(directory / 'host')], check=True)
            cases = (('def install():\n    return True\n', '1 0 0'),
                     ('def install():\n    return False\n', '0 1 0'),
                     ('def install():\n    raise RuntimeError("missing payload")\n', '0 1 0'),
                     (None, '0 1 0'))
            for index, (body, expected) in enumerate(cases):
                modules = directory / ('case%d' % index)
                modules.mkdir()
                if body is not None:
                    (modules / 'OfflineGarageEntry.py').write_text(body)
                result = subprocess.run([str(directory / 'host'), str(modules)],
                                        capture_output=True, text=True,
                                        env=dict(os.environ, PYTHONDONTWRITEBYTECODE='1'))
                self.assertEqual(result.stdout.split('\n')[-2], expected, result.stderr)


if __name__ == '__main__':
    unittest.main()
