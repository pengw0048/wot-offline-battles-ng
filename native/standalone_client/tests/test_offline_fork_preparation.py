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
    REPLAY_PACKAGE_INPUT, RUNTIME_ROOT, SERVICES_PATH, SERVICES_SOURCE, adapt_instance_guard, adapt_native_math,
    prepare)

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
        for dataset in ('destructibles', 'foliage', 'navgraphs'):
            shutil.copytree(self.original / dataset, self.source / dataset)
        (self.source / 'fork-source.json').write_text('{"revision":"' + PINNED_COMMIT + '"}')
        # The source fixture includes the unchanged v0.10.1 build_wotmod.py output
        # at dist/, just like the production builder's explicit release input.
        replay_package = self.original / REPLAY_PACKAGE_INPUT
        self.assertTrue(replay_package.is_file(),
                        'WOT_FORK_0101_SOURCE must include the pinned release wotmod at ' + str(REPLAY_PACKAGE_INPUT))
        (self.source / REPLAY_PACKAGE_INPUT).parent.mkdir(parents=True)
        shutil.copyfile(replay_package, self.source / REPLAY_PACKAGE_INPUT)
        shutil.copyfile(self.original / 'LICENSE', self.source / 'LICENSE')
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

    def test_pinned_catalogs_are_staged_at_the_runtime_config_root(self):
        prepare(self.source, self.output)
        config = self.output / 'mods/configs/offline_lan_0922'
        for dataset in ('destructibles', 'foliage', 'navgraphs'):
            self.assertEqual(42, len(list((config / dataset).iterdir())), dataset)
        manifest = json.loads((config / 'destructibles/manifest.json').read_text())
        self.assertEqual('wgde-raw-row', manifest['identity']['rule'])
        lakeville = json.loads((config / 'destructibles/07_lakeville.json').read_text())
        self.assertIn([33149, 189], [row[14:16] for row in lakeville['instances']
                                     if row[12].endswith('env420_OldGLightVan_02.model')])
        self.assertEqual((self.source / 'navgraphs/01_karelia.json').read_bytes(),
                         (config / 'navgraphs/01_karelia.json').read_bytes())

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

    def test_missing_or_invalid_replay_package_publishes_nothing(self):
        package = self.source / REPLAY_PACKAGE_INPUT
        original = package.read_bytes()
        package.unlink()
        with self.assertRaisesRegex(ValueError, 'wotmod input is missing'):
            prepare(self.source, self.output)
        self.assertFalse(self.output.exists())
        package.write_bytes(original)
        prepare(self.source, self.output)
        before = {p.relative_to(self.output): (p.read_bytes(), p.stat().st_mtime_ns)
                  for p in self.output.rglob('*') if p.is_file()}
        package.write_bytes(b'not the original release archive')
        with self.assertRaises(ValueError):
            prepare(self.source, self.output)
        self.assertEqual(before, {p.relative_to(self.output): (p.read_bytes(), p.stat().st_mtime_ns)
                                  for p in self.output.rglob('*') if p.is_file()})

    def runtime(self):
        prepare(self.source, self.output)
        return self.output / RUNTIME_ROOT

    def test_runtime_is_the_complete_pinned_payload_with_reviewed_seams(self):
        runtime = self.runtime()
        resources = self.source / PINNED_RESOURCES
        pinned = {p.relative_to(resources) for p in resources.rglob('*') if p.is_file()}
        shipped = {p.relative_to(runtime) for p in runtime.rglob('*') if p.is_file()}
        self.assertEqual(shipped, pinned | {ENTRY_PATH, SERVICES_PATH})
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
        self.assertEqual((runtime / SERVICES_PATH).read_bytes(), SERVICES_SOURCE.read_bytes())
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

    def test_both_roles_suppress_automatic_announcement_before_connect_and_restore(self):
        runtime = self.runtime()
        package = 'gui.mods.offline_lan_0922'
        lobby_ui = types.ModuleType(package + '.lobby_ui')
        exec(compile((runtime / 'scripts/client/gui/mods/offline_lan_0922/lobby_ui.py')
                     .read_text(), 'lobby_ui.py', 'exec'), lobby_ui.__dict__)
        calls = []

        class ChinaController:
            gameSession = types.SimpleNamespace(battlesCount=0)

            def showBrowser(self):
                raise AttributeError('BigWorld.createBrowser is unavailable')

            def onLobbyInited(self, event):
                if self.gameSession.battlesCount % 999 == 0:
                    self.showBrowser()

        original = ChinaController.onLobbyInited
        manual_browser = ChinaController.showBrowser
        modules = {name: types.ModuleType(name) for name in (
            'gui', 'gui.mods', package, package + '.compat',
            package + '.offline_services_ui', package + '.engine_audio',
            'gui.game_control', 'gui.game_control.ChinaController')}
        modules[lobby_ui.__name__] = lobby_ui
        for name, module in list(modules.items()):
            if '.' in name:
                parent, child = name.rsplit('.', 1)
                setattr(modules[parent], child, module)
        modules['gui.game_control.ChinaController'].ChinaController = ChinaController
        modules['gui.game_control'].gc_constants = types.SimpleNamespace(
            BROWSER=types.SimpleNamespace(CHINA_BROWSER_COUNT=999))
        compat = modules[package + '.compat']
        compat.pin_dossier_cache = lambda career: calls.append('dossier')
        compat.pin_account_settings = lambda: calls.append('preferences')
        services = modules[package + '.offline_services_ui']
        services.install = lambda: calls.append('services')
        services.uninstall = lambda: None
        modules[package + '.engine_audio'].uninstall = lambda: None

        def connect(**kwargs):
            ChinaController().onLobbyInited('lobby')
            calls.append('connected')

        raw = (self.original / BOOTSTRAP_PATH).read_text()
        generated = (runtime / BOOTSTRAP_PATH.relative_to(PINNED_RESOURCES)).read_text()
        for source, role, succeeds in ((raw, 'worker', False), (raw, 'player', True),
                                       (generated, 'worker', True),
                                       (generated, 'player', True)):
            with self.subTest(prepared=source is generated, role=role), \
                    patch.dict(sys.modules, modules):
                namespace = dict(
                    sys=sys, port_config=types.SimpleNamespace(SIMULATION_WORKER_MODE='worker'),
                    _login_space_is_ready=lambda: True, _dossier_cache_career=lambda: 'slot',
                    _install_worker_session=lambda: calls.append('worker'),
                    _install_lan_session=lambda: calls.append('player'),
                    _wait_for_lobby=lambda: None, _remove_lobby_listener=lambda: None,
                    _schedule=lambda *args: calls.append('scheduled'),
                    _fail_startup=lambda error: calls.append(type(error).__name__),
                    g_compatibility=types.SimpleNamespace(connect=connect, fini=lambda: None))
                for code in compile(source, 'bootstrap.py', 'exec').co_consts:
                    if isinstance(code, types.CodeType) and code.co_name in (
                            '_wait_for_login_space', '_install_announcement_ui',
                            '_cleanup_runtime'):
                        namespace[code.co_name] = types.FunctionType(code, namespace)
                for unused in range(2):
                    calls[:] = []
                    namespace.update(_login_space_seen=True, _client_mode=role,
                                     _announcement_ui=None, _account_context=None,
                                     _callback_id=None, _session=None,
                                     _worker_presentation=None, _intro_skip=None,
                                     _lobby_view_loaded=False)
                    namespace['_wait_for_login_space']()
                    self.assertEqual(succeeds, 'connected' in calls, calls)
                    self.assertEqual(succeeds, 'scheduled' in calls, calls)
                    self.assertEqual(not succeeds, 'AttributeError' in calls, calls)
                    self.assertEqual(role == 'player', 'preferences' in calls)
                    self.assertEqual(role == 'player', 'services' in calls)
                    self.assertFalse(namespace['_lobby_view_loaded'])
                    if succeeds:
                        installed = ChinaController.onLobbyInited
                        namespace['_install_announcement_ui']()
                        self.assertIs(installed, ChinaController.onLobbyInited)
                    self.assertIs(manual_browser, ChinaController.showBrowser)
                    self.assertIsNone(namespace['_cleanup_runtime']())
                    self.assertIsNone(namespace['_cleanup_runtime']())
                    self.assertIs(original, ChinaController.onLobbyInited)

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
            (BOOTSTRAP_PATH, '            _install_announcement_ui()\n', ''),
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
