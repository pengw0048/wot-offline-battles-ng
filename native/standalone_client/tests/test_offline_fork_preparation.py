"""Exercise the real fixed-source preparation, its payload and consumers."""
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import types
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'bigworld'))
from prepare_offline_fork import (
    ACCOUNT_ATTEMPT, ADAPTERS, BOOTSTRAP_PATH, COMPAT_PATH, ENTRY_PATH, ENTRY_SOURCE,
    INSTANCE_GUARD_PATH, NATIVE_MATH_PATH, PINNED_COMMIT, PINNED_RESOURCES,
    RUNTIME_ROOT, adapt_instance_guard, adapt_native_math, prepare)

PY27 = Path(os.environ.get('WOT_TEST_PY27', '/Users/peng/.pyenv/versions/2.7.18/bin/python2.7'))
ROOT_COMPAT = ROOT.parents[1] / 'src/res/scripts/client/gui/mods/offline_lan_0922/compat.py'


class OfflineForkPreparationTests(unittest.TestCase):
    def setUp(self):
        source = os.environ.get('WOT_FORK_0101_SOURCE')
        if not source:
            self.skipTest('The fixed v0.10.1 source input is required')
        self.original = Path(source)
        self.temporary = tempfile.TemporaryDirectory(prefix='wot-fork-prepare-')
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.source = self.root / 'input'
        shutil.copytree(self.original / 'native', self.source / 'native')
        shutil.copytree(self.original / 'src', self.source / 'src')
        (self.source / 'fork-source.json').write_text('{"revision":"' + PINNED_COMMIT + '"}')
        self.output = self.root / 'output'

    def test_preparation_preserves_input_and_unchanged_output_timestamps(self):
        inputs = {p.relative_to(self.source): p.read_bytes()
                  for p in self.source.rglob('*') if p.is_file()}
        prepare(self.source, self.output)
        outputs = {p: (p.read_bytes(), p.stat().st_mtime_ns)
                   for p in self.output.rglob('*') if p.is_file()}
        self.assertTrue(outputs)
        self.assertFalse(list(self.output.rglob('*.pyd')))
        self.assertIn('sim_control_source_still',
                      (self.output / 'offline_fork_methods.hpp').read_text())
        prepare(self.source, self.output)
        self.assertEqual(outputs, {p: (p.read_bytes(), p.stat().st_mtime_ns) for p in outputs})
        self.assertEqual(inputs, {p.relative_to(self.source): p.read_bytes()
                                 for p in self.source.rglob('*') if p.is_file()})

    def test_wrong_revision_and_changed_consumer_publish_nothing(self):
        identity = self.source / 'fork-source.json'
        identity.write_text('{"revision":"other"}')
        with self.assertRaises(ValueError):
            prepare(self.source, self.output)
        self.assertFalse(self.output.exists())
        identity.write_text('{"revision":"' + PINNED_COMMIT + '"}')
        (self.source / NATIVE_MATH_PATH).write_text('changed loader')
        with self.assertRaises(ValueError):
            prepare(self.source, self.output)
        self.assertFalse(self.output.exists())

    def runtime(self):
        prepare(self.source, self.output)
        return self.output / RUNTIME_ROOT

    def test_runtime_is_the_complete_pinned_payload_with_reviewed_seams(self):
        runtime = self.runtime()
        resources = self.source / PINNED_RESOURCES
        pinned = {p.relative_to(resources) for p in resources.rglob('*') if p.is_file()}
        shipped = {p.relative_to(runtime) for p in runtime.rglob('*') if p.is_file()}
        self.assertEqual(shipped, pinned | {ENTRY_PATH})
        self.assertFalse([p for p in shipped if p.suffix in ('.pyc', '.pyo', '.pyd', '.dll')])
        adapted = {relative.relative_to(PINNED_RESOURCES) for relative in ADAPTERS}
        for relative in pinned:
            original = (resources / relative).read_bytes()
            output = (runtime / relative).read_bytes()
            if relative in adapted:
                adapt = ADAPTERS[PINNED_RESOURCES / relative]
                self.assertEqual(output, adapt(original.decode('utf-8')).encode('utf-8'))
                self.assertNotEqual(output, original)
            else:
                self.assertEqual(output, original, relative)
        self.assertEqual((runtime / ENTRY_PATH).read_bytes(), ENTRY_SOURCE.read_bytes())
        manifest = json.loads((self.output / 'offline-res.json').read_text())
        self.assertEqual(manifest['revision'], PINNED_COMMIT)
        self.assertEqual(manifest['files'], len(shipped))
        self.assertEqual(manifest['adapted'], sorted(p.as_posix() for p in ADAPTERS))

    def test_runtime_keeps_pinned_gameplay_rather_than_root_source(self):
        runtime = self.runtime() / 'scripts/client/gui/mods'
        bootstrap = (runtime / 'offline_lan_0922/bootstrap.py').read_text()
        compat = (runtime / 'offline_lan_0922/compat.py').read_text()
        # Target-only behavior that root's older gameplay tree does not have.
        for marker in ('offline_services_ui.install()', 'depot_styles.install()',
                       '_autostart_selected_replay'):
            self.assertIn(marker, bootstrap)
        self.assertNotEqual(compat, ROOT_COMPAT.read_text())
        self.assertIn('OfflineGarageEntry.publish_logged_on(', compat)
        self.assertNotIn("progress(1, runtime.login_status.LOGGED_ON, '{}')", compat)
        self.assertIn('self._discard_partial_account(attempt)', compat)
        self.assertIn('OfflineGarageEntry.login_space_ready(g_appLoader)', bootstrap)
        self.assertEqual((runtime / 'mod_offline_lan_0922.py').read_bytes(),
                         (self.source / PINNED_RESOURCES /
                          'scripts/client/gui/mods/mod_offline_lan_0922.py').read_bytes())

    def test_rollback_block_is_roots_reviewed_account_attempt(self):
        source = ROOT_COMPAT.read_text()
        begin = source.index('    def _live_entities(self):')
        end = source.index('    def restore_lobby_account(self):')
        self.assertEqual(ACCOUNT_ATTEMPT.read_text(), source[begin:end])

    def test_runtime_compiles_and_package_imports_resolve_on_python27(self):
        if not PY27.is_file():
            self.skipTest('CPython 2.7 is required')
        runtime = self.runtime() / 'scripts/client'
        result = subprocess.run([str(PY27), '-E', '-s', '-B', '-c', '''
import os, sys
count = 0
for base, unused, files in os.walk(sys.argv[1]):
    for name in files:
        if name.endswith('.py'):
            path = os.path.join(base, name)
            compile(open(path, 'rb').read(), path, 'exec')
            count += 1
print(count)
''', str(runtime)], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertGreater(int(result.stdout), 180)
        package = 'gui.mods.offline_lan_0922'
        names = set()
        for path in runtime.rglob('*.py'):
            text = path.read_text(encoding='utf-8')
            names.update(re.findall(r'\b' + re.escape(package) + r'((?:\.\w+)+)', text))
            for module, imported in re.findall(
                    r'from ' + re.escape(package) + r'((?:\.\w+)*) import ([\w, ()\n\\]+)', text):
                for name in re.findall(r'\w+', imported):
                    names.add(module + '.' + name)
        self.assertGreater(len(names), 50)
        root = runtime / 'gui/mods/offline_lan_0922'
        # internal_hit_layouts falls back to no-op tracing without this module.
        optional = ('.logging',)
        for dotted in names:
            if dotted.startswith(optional):
                continue
            parts = dotted.strip('.').split('.')
            candidate = root.joinpath(*parts)
            if candidate.with_suffix('.py').is_file() or (candidate / '__init__.py').is_file():
                continue
            # "from package.module import attribute" names an attribute.
            owner = root.joinpath(*parts[:-1])
            self.assertTrue(owner.with_suffix('.py').is_file() or
                            (owner / '__init__.py').is_file(), dotted)

    def test_stale_runtime_files_are_removed(self):
        runtime = self.runtime()
        stale = runtime / 'scripts/client/gui/mods/offline_lan_0922/stale.py'
        stale.write_text('stale')
        prepare(self.source, self.output)
        self.assertFalse(stale.exists())

    def test_changed_entry_seams_publish_nothing(self):
        mutations = (
            (BOOTSTRAP_PATH, 'GUI_GLOBAL_SPACE_ID.LOGIN:', 'GUI_GLOBAL_SPACE_ID.LOBBY:'),
            (COMPAT_PATH, "progress(1, runtime.login_status.LOGGED_ON, '{}')", 'progress()'),
            (COMPAT_PATH, '            clear_all_spaces()\n', '            pass\n'))
        for relative, before, after in mutations:
            path = self.source / relative
            original = path.read_text()
            self.assertIn(before, original)
            path.write_text(original.replace(before, after))
            with self.assertRaises(ValueError):
                prepare(self.source, self.output)
            self.assertFalse(self.output.exists())
            path.write_text(original)

    def consumer(self):
        namespace = {'__name__': 'isolated_native_math'}
        exec(compile(adapt_native_math((self.original / NATIVE_MATH_PATH).read_text()),
                     'native_math.py', 'exec'), namespace)
        namespace['_attempted'] = False
        return namespace

    def test_consumer_uses_registered_builtin_and_rejects_old_sidecar(self):
        builtin = types.ModuleType('offline_math_batch_native')
        builtin._wot_offline_fork_revision = PINNED_COMMIT
        with patch.dict(sys.modules, {'offline_math_batch_native': builtin}):
            namespace = self.consumer()
            self.assertIs(namespace['_load'](), builtin)
            self.assertIs(namespace['_load'](), builtin)
        old_sidecar = types.ModuleType('offline_math_batch_native')
        with patch.dict(sys.modules, {'offline_math_batch_native': old_sidecar}):
            namespace = self.consumer()
            self.assertIsNone(namespace['_load']())
            self.assertIsNone(namespace['_backend'])

    def platform_consumer(self):
        namespace = {'__name__': 'isolated_instance_guard'}
        exec(compile(adapt_instance_guard((self.original / INSTANCE_GUARD_PATH).read_text()),
                     'instance_guard.py', 'exec'), namespace)
        return namespace

    def platform(self):
        builtin = types.ModuleType('offline_instance_guard_native')
        builtin._wot_offline_fork_revision = PINNED_COMMIT
        builtin.install_exception_trail = lambda: 301
        builtin.hide_process_windows = lambda: 2
        builtin.show_process_windows = lambda: 1
        builtin.apply_standard_gameplay_mask = lambda: 0
        builtin.restore_standard_gameplay_mask = lambda: 0
        return builtin

    def test_process_policy_uses_builtin_without_original_pe_repairs(self):
        builtin = self.platform()
        with patch.dict(sys.modules, {'offline_instance_guard_native': builtin}):
            namespace = self.platform_consumer()
            self.assertFalse(namespace['release_if_requested']({}))
            self.assertIs(namespace['_load_native_bridge'](), builtin)
            requested = {namespace['ALLOW_MULTIPLE_CLIENTS_ENV']: '1'}
            self.assertTrue(namespace['release_if_requested'](requested))
            self.assertTrue(namespace['release_if_requested'](requested))
            self.assertEqual(namespace['hide_process_windows'](), 2)
            self.assertEqual(namespace['show_process_windows'](), 1)
            builtin.hide_process_windows = lambda: -170
            with self.assertRaises(namespace['ClientInstanceGuardError']):
                namespace['hide_process_windows']()

    def test_process_policy_rejects_wrong_revision_and_incomplete_module(self):
        for mutation in ('revision', 'missing'):
            builtin = self.platform()
            if mutation == 'revision':
                builtin._wot_offline_fork_revision = 'original-exe-sidecar'
            else:
                del builtin.restore_standard_gameplay_mask
            with patch.dict(sys.modules, {'offline_instance_guard_native': builtin}):
                namespace = self.platform_consumer()
                requested = {namespace['ALLOW_MULTIPLE_CLIENTS_ENV']: '1'}
                for unused in range(2):
                    with self.assertRaises(ImportError):
                        namespace['release_if_requested'](requested)
                self.assertIsNone(namespace['_native_bridge'])

    def test_diagnostic_trail_failure_does_not_reject_process_policy(self):
        builtin = self.platform()
        def failed_trail():
            raise OSError('diagnostic output unavailable')
        builtin.install_exception_trail = failed_trail
        with patch.dict(sys.modules, {'offline_instance_guard_native': builtin}):
            namespace = self.platform_consumer()
            self.assertTrue(namespace['release_if_requested'](
                {namespace['ALLOW_MULTIPLE_CLIENTS_ENV']: '1'}))


if __name__ == '__main__':
    unittest.main()
