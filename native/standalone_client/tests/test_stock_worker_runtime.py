"""Hidden simulation worker: role, never-shown window, cursor isolation and frames without draw/Present.

The engine is rebuilt from the pinned public archive with the build script's
patch chain; patch_stock_worker_runtime.py runs right after
patch_stock_audio.py and every later patcher must still apply. The host
harness compiles the resulting production function bodies (and the
pre-patch bodies as negative controls) against bounded engine/Win32 fakes.
The platform probe embeds real CPython 2.7 with the production window
functions and the adapted pinned WorkerPresentation.
"""
import importlib.util
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
REPOSITORY = ROOT.parents[1]
TESTS = Path(__file__).resolve().parent
ARCHIVE = Path(os.environ.get('WOT_PUBLIC_ENGINE_ARCHIVE', '/tmp/wot-bigworld-engine-14.4.1.tar.gz'))
FORK = Path(os.environ.get('WOT_FORK_0101_SOURCE', str(
    Path.home() / '.cache/wot-independent-client/evidence/native-rewrite/upstream-v0101-review/source-144619f6')))
PY27 = Path(os.environ.get('WOT_PYTHON27', str(Path.home() / '.pyenv/versions/2.7.18/bin/python2.7')))
PRESENTATION = Path('src/res/scripts/client/gui/mods/offline_lan_0922/worker_presentation.py')
WORKER = 'simulation_worker'
PATCHER = 'patch_stock_worker_runtime.py'
ENGINE_DIRECTORIES = ('client', 'lib', 'third_party/python/Include', 'third_party/python/Modules',
                      'third_party/python/Objects', 'third_party/python/Python')

spec = importlib.util.spec_from_file_location('patch_stock_worker_runtime', ROOT / 'bigworld' / PATCHER)
patcher = importlib.util.module_from_spec(spec)
spec.loader.exec_module(patcher)


def engine_chain():
    """Engine-only patchers in tools/build_bigworld_client.ps1 order."""
    text = (REPOSITORY / 'tools/build_bigworld_client.ps1').read_text()
    names = re.findall(r'Join-Path \$StandaloneRoot "(patch_[a-z_0-9]+\.py)"\),\s*\$EngineRoot\)', text)
    if PATCHER in names:
        # Root's integration must keep the worker runtime after audio.
        assert names.index(PATCHER) > names.index('patch_stock_audio.py')
        names.remove(PATCHER)
    return names


def run_patchers(names, engine):
    for name in names:
        subprocess.run([sys.executable, str(ROOT / 'bigworld' / name), str(engine)], check=True,
                       capture_output=True, env=dict(os.environ, PYTHONDONTWRITEBYTECODE='1'))


def tree_bytes(root):
    return {path.relative_to(root).as_posix(): path.read_bytes()
            for path in root.rglob('*') if path.is_file()}


def function(source, signature):
    """The brace-balanced body that follows a unique signature."""
    if source.count(signature) != 1:
        raise AssertionError('Signature is not unique: ' + signature)
    start = source.index('{', source.index(signature) + len(signature))
    depth, index = 0, start
    while True:
        depth += {'{': 1, '}': -1}.get(source[index], 0)
        index += 1
        if depth == 0:
            return source[start:index]


def between(source, begin, end):
    start = source.index(begin)
    return source[start:source.index(end, start) + len(end)]


def calls(output):
    """Map scenario -> [(name, value)] from the host's trace."""
    scenarios, current = {}, None
    for line in output.splitlines():
        name, unused, value = line.partition(' ')
        if name == 'scenario':
            current = scenarios.setdefault(value, [])
        elif current is not None and re.fullmatch(r'-?\d+', value):
            current.append((name, int(value)))
    return scenarios


def names(trace):
    return [name for name, unused in trace]


class Engine(object):
    """One pinned-archive build of the reference and worker-patched engine trees."""
    directory = None

    @classmethod
    def build(cls, test):
        if not ARCHIVE.is_file():
            test.skipTest('Pinned public BigWorld engine archive required')
        if cls.directory is not None:
            return cls
        cls.temporary = tempfile.TemporaryDirectory(prefix='worker-runtime-engine-')
        work = Path(cls.temporary.name)
        prefix = subprocess.run(['tar', '-tzf', str(ARCHIVE)], check=True, capture_output=True,
                                text=True).stdout.split('/', 1)[0]
        members = [prefix + '/programming/bigworld/' + name for name in ENGINE_DIRECTORIES]
        subprocess.run(['tar', '-xzf', str(ARCHIVE), '-C', str(work)] + members, check=True)
        source = work / prefix / 'programming/bigworld'
        names = engine_chain()
        audio = names.index('patch_stock_audio.py') + 1
        cls.after_audio = work / 'after-audio'
        source.rename(cls.after_audio)
        run_patchers(names[:audio], cls.after_audio)
        cls.integrated = work / 'integrated'
        shutil.copytree(cls.after_audio, cls.integrated)
        patcher.patch(cls.integrated)
        run_patchers(names[audio:], cls.integrated)
        cls.reference = work / 'reference'
        shutil.copytree(cls.after_audio, cls.reference)
        run_patchers(names[audio:], cls.reference)
        cls.directory = work
        return cls

    @classmethod
    def text(cls, tree, relative):
        return (tree / relative).read_text(encoding='utf-8')


def compile_host(test, directory, subject, replacements, extra_sources=(), python=False):
    include = directory / 'include'
    include.mkdir(parents=True, exist_ok=True)
    (include / 'windows.h').write_text('#pragma once\n#include "%s"\n' % (TESTS / 'stock_worker_runtime_win32.hpp'))
    text = (TESTS / 'stock_worker_runtime_host.cpp').read_text()
    for key, value in replacements.items():
        test.assertEqual(text.count(key), 1, key)
        text = text.replace(key, value)
    source = directory / (subject + '.cpp')
    source.write_text(text)
    flags, libraries = [], []
    if python:
        config = PY27.with_name('python2.7-config')
        if not config.exists():
            test.skipTest('Pinned CPython 2.7 development files unavailable')
        flags = [flag for flag in shlex.split(subprocess.check_output([str(config), '--cflags'], text=True))
                 if flag not in ('-DNDEBUG', '-Wstrict-prototypes')]
        libraries = shlex.split(subprocess.check_output([str(config), '--ldflags'], text=True))
    binary = directory / subject
    result = subprocess.run(
        ['c++', '-std=c++17', *flags, '-Wall', '-Werror', '-Wno-unused-function', '-Wno-unused-variable',
         '-Wno-unused-but-set-variable', '-Wno-deprecated-register', '-Wno-register', '-Wno-writable-strings',
         '-D' + subject.upper().replace('-', '_'), '-I', str(include),
         '-I', str(Engine.integrated / 'lib'), str(source), *[str(path) for path in extra_sources],
         *libraries, '-o', str(binary)], capture_output=True, text=True)
    test.assertEqual(result.returncode, 0, result.stdout + result.stderr)
    return binary


def run(binary, mode=None, *arguments, **environment):
    env = {key: value for key, value in os.environ.items() if key != 'OFFLINE_LAN_0922_CLIENT_MODE'}
    if mode is not None:
        env['OFFLINE_LAN_0922_CLIENT_MODE'] = mode
    env.update(environment)
    result = subprocess.run([str(binary), *arguments], capture_output=True, text=True, env=env)
    if result.returncode:
        raise AssertionError(result.stdout + result.stderr)
    return result.stdout


class WorkerRuntimePatchTest(unittest.TestCase):
    def setUp(self):
        self.engine = Engine.build(self)

    def test_integrated_after_audio_and_later_chain(self):
        reference, integrated = tree_bytes(self.engine.reference), tree_bytes(self.engine.integrated)
        changed = {path for path in set(reference) | set(integrated)
                   if reference.get(path) != integrated.get(path)}
        self.assertEqual(changed, set(patcher.FILES) | {patcher.HEADER})
        self.assertEqual(integrated[patcher.HEADER], (ROOT / 'bigworld/stock_worker_runtime.hpp').read_bytes())
        # The same final bodies whether it runs after audio or after the whole chain.
        with tempfile.TemporaryDirectory() as directory:
            at_end = Path(directory)
            (at_end / patcher.HEADER).parent.mkdir(parents=True)
            for path in patcher.FILES:
                (at_end / path).parent.mkdir(parents=True, exist_ok=True)
                (at_end / path).write_bytes(reference[path])
            patcher.patch(at_end)
            for path in patcher.FILES + (patcher.HEADER,):
                self.assertEqual((at_end / path).read_bytes(), integrated[path], path)
            patcher.patch(at_end)
            for path in patcher.FILES + (patcher.HEADER,):
                self.assertEqual((at_end / path).read_bytes(), integrated[path], path)

    def copy_targets(self, tree, directory):
        root = Path(directory)
        (root / patcher.HEADER).parent.mkdir(parents=True, exist_ok=True)
        for path in patcher.FILES:
            (root / path).parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(tree / path, root / path)
        return root

    def assert_refused_unchanged(self, root, header=None):
        before = tree_bytes(root)
        with self.assertRaises(ValueError):
            patcher.patch(root)
        self.assertEqual(tree_bytes(root), before)
        if header is None:
            self.assertFalse((root / patcher.HEADER).exists())

    def test_unexpected_partial_duplicate_and_stale_states_fail_closed(self):
        mutations = [('changed', path, before) for path, before, unused in patcher.EDITS]
        mutations += [('duplicated', path, before) for path, before, unused in patcher.EDITS]
        mutations += [('partial', path, None) for path in patcher.FILES]
        for mutation, path, anchor in mutations:
            with self.subTest(mutation=mutation, path=path, anchor=anchor), \
                    tempfile.TemporaryDirectory() as directory:
                root = self.copy_targets(self.engine.after_audio, directory)
                target = root / path
                text = target.read_text()
                if mutation == 'changed':
                    text = text.replace(anchor, anchor[:-1] + ' // changed\n', 1)
                elif mutation == 'duplicated':
                    text += '\n' + anchor
                else:
                    text = Engine.text(self.engine.integrated, path)
                target.write_text(text)
                self.assert_refused_unchanged(root)
        with tempfile.TemporaryDirectory() as directory:
            root = self.copy_targets(self.engine.after_audio, directory)
            (root / patcher.HEADER).write_bytes(b'// stale\n')
            self.assert_refused_unchanged(root, header=True)
        with tempfile.TemporaryDirectory() as directory:
            root = self.copy_targets(self.engine.after_audio, directory)
            (root / patcher.HEADER).parent.rmdir()
            self.assert_refused_unchanged(root)

    def test_line_endings_and_bom_are_preserved_or_refused(self):
        with tempfile.TemporaryDirectory() as directory:
            lf = self.copy_targets(self.engine.after_audio, Path(directory) / 'lf')
            crlf = self.copy_targets(self.engine.after_audio, Path(directory) / 'crlf')
            app = crlf / 'client/app.cpp'
            app.write_bytes(b'\xef\xbb\xbf' + app.read_bytes().replace(b'\n', b'\r\n'))
            patcher.patch(lf)
            patcher.patch(crlf)
            expected = b'\xef\xbb\xbf' + (lf / 'client/app.cpp').read_bytes().replace(b'\n', b'\r\n')
            self.assertEqual(app.read_bytes(), expected)
            patcher.patch(crlf)
            self.assertEqual(app.read_bytes(), expected)
        with tempfile.TemporaryDirectory() as directory:
            root = self.copy_targets(self.engine.after_audio, directory)
            target = root / 'lib/romp/progress.cpp'
            target.write_bytes(target.read_bytes().replace(b'\n', b'\r\n', 3))
            self.assert_refused_unchanged(root)


class WorkerPresentationAdapterTest(unittest.TestCase):
    def pinned(self):
        path = FORK / PRESENTATION
        if not path.is_file():
            self.skipTest('The pinned v0.10.1 gameplay source is required')
        return path.read_text(encoding='utf-8')

    def test_adapter_is_exact_idempotent_and_fails_closed(self):
        source = self.pinned()
        adapted = patcher.adapt_worker_presentation(source)
        self.assertEqual(adapted, source.replace(patcher.PINNED_HIDE, patcher.INDEPENDENT_HIDE, 1))
        self.assertEqual(patcher.adapt_worker_presentation(adapted), adapted)
        for changed in (source.replace('hidden_count < 1', 'hidden_count < 2'), source + patcher.PINNED_HIDE):
            with self.assertRaises(ValueError):
                patcher.adapt_worker_presentation(changed)
        if PY27.exists():
            subprocess.run([str(PY27), '-c', 'import sys; compile(sys.stdin.read(), "worker_presentation.py", "exec")'],
                           input=adapted, text=True, check=True)

    def test_pinned_worker_presentation_suite_passes_on_adapted_module(self):
        adapted = patcher.adapt_worker_presentation(self.pinned())
        with tempfile.TemporaryDirectory() as directory:
            module_path = Path(directory) / 'worker_presentation.py'
            module_path.write_text(adapted)
            spec = importlib.util.spec_from_file_location(
                'pinned_worker_presentation_tests', FORK / 'tests/test_port_0922_worker_presentation.py')
            tests = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(tests)
            tests.MODULE_PATH = module_path
            suite = unittest.defaultTestLoader.loadTestsFromModule(tests)
            result = unittest.TextTestRunner(stream=open(os.devnull, 'w')).run(suite)
            self.assertTrue(result.wasSuccessful(), result.failures + result.errors)
            self.assertGreaterEqual(result.testsRun, 13)


class WorkerRuntimeHostTest(unittest.TestCase):
    def setUp(self):
        self.engine = Engine.build(self)
        self.directory = tempfile.TemporaryDirectory(prefix='worker-runtime-host-')
        self.addCleanup(self.directory.cleanup)
        self.work = Path(self.directory.name)

    def source(self, relative, tree=None):
        return Engine.text(tree or self.engine.integrated, relative)

    def test_role_is_exact_cached_once_and_shared_across_translation_units(self):
        units = []
        for suffix in ('A', 'B'):
            unit = self.work / ('role%s.cpp' % suffix)
            unit.write_text('#include <windows.h>\n#include "cstdmf/stock_worker_runtime.hpp"\n'
                            'bool role%s() { return wot::stockSimulationWorker(); }\n'
                            'const void * roleAddress%s() { return (const void *)&wot::stockSimulationWorker; }\n'
                            % (suffix, suffix))
            units.append(unit)
        binary = compile_host(self, self.work, 'worker-host-role', {}, units)
        values = (None, '', 'player', 'Simulation_Worker', 'SIMULATION_WORKER', ' simulation_worker',
                  'simulation_worker ', 'simulation_workerx', 'simulation_worke', 'simulation_worker\t', WORKER)
        for value in values:
            with self.subTest(value=value):
                flipped = '' if value == WORKER else WORKER
                output = run(binary, value, flipped)
                expected = 1 if value == WORKER else 0
                self.assertEqual(output, 'first %d\nsecond %d\nreread %d\nsame-owner 1\n'
                                 % (expected, expected, 1 - expected))

    def frame_host(self, tree, name):
        body = function(self.source('client/app.cpp', tree), 'bool App::updateFrame(bool active)')
        return compile_host(self, self.work / name, 'worker-host-frame', {'@BODY@': body})

    def test_frames_run_complete_update_without_draw_or_present(self):
        binary = self.frame_host(self.engine.integrated, 'patched')
        player, worker = calls(run(binary)), calls(run(binary, WORKER))
        prefix = [('calculateFrameTime', 0), ('updateMouseClipping', 0), ('flushModificationMonitor', 0)]
        # The visible player keeps the original active/inactive dispatch.
        self.assertEqual(player['active'], prefix + [
            ('tick.game', 16), ('tick.render', 17), ('checkDevice', 1), ('updateAnimations', 16),
            ('draw', 0), ('result', 1)])
        self.assertEqual(player['inactive'], prefix + [
            ('inactiveTick.game', 21), ('inactiveTick.render', 23), ('result', 1)])
        self.assertEqual(player['device-lost'], prefix + [
            ('tick.game', 16), ('tick.render', 17), ('checkDevice', 0), ('result', 1)])
        self.assertEqual(player['no-elapsed'], prefix + [('result', 1)])
        # The worker always runs the full active update; no draw, audio exactly once.
        for scenario, game, render in (('active', 16, 17), ('inactive', 21, 23)):
            self.assertEqual(worker[scenario], prefix + [
                ('tick.game', game), ('tick.render', render), ('checkDevice', 1),
                ('updateAnimations', game), ('preloadDeviceResources', 2), ('tickStockAudio', render),
                ('SimpleGUI.draw', 0), ('result', 1)], scenario)
        for scenario in ('device-lost', 'device-lost-inactive'):
            self.assertEqual(worker[scenario], prefix + [
                ('tick.game', 16), ('tick.render', 17), ('checkDevice', 0), ('result', 1)], scenario)
        for scenario in ('no-elapsed', 'no-elapsed-inactive'):
            self.assertEqual(worker[scenario], prefix + [('result', 1)], scenario)
        for trace in worker.values():
            self.assertNotIn('draw', names(trace))
            self.assertNotIn('inactiveTick.game', names(trace))
        # Negative control: the pre-patch production body draws and goes inactive.
        control = calls(run(self.frame_host(self.engine.reference, 'control'), WORKER))
        self.assertIn('draw', names(control['active']))
        self.assertIn('inactiveTick.game', names(control['inactive']))
        self.assertNotIn('tickStockAudio', names(control['active']))
        self.assertEqual(names(control['active']).count('SimpleGUI.draw'), 0)
        self.assertNotEqual(control['inactive'], worker['inactive'])

    def progress_host(self, tree, name):
        return compile_host(self, self.work / name, 'worker-host-progress', {
            '@PROGRESS_BODY@': function(self.source('lib/romp/progress.cpp', tree),
                                        'bool ProgressDisplay::draw( bool force )'),
            '@GUI_PROGRESS_BODY@': function(self.source('lib/ashes/gui_progress.cpp', tree),
                                            'bool GUIProgressDisplay::draw( bool force )')})

    def test_startup_progress_keeps_callbacks_and_skips_every_frame(self):
        binary = self.progress_host(self.engine.integrated, 'patched')
        player, worker = calls(run(binary)), calls(run(binary, WORKER))
        self.assertEqual(names(player['draw']), [
            'callback', 'checkDevice', 'Clear', 'beginScene', 'SetSoftwareVertexProcessing', 'nextFrame',
            'SetSoftwareVertexProcessing', 'drawConsoleString', 'drawConsoleString', 'drawBar',
            'endScene', 'present', 'result', 'lastDrawn'])
        self.assertEqual(worker['draw'], [('callback', 1), ('checkDevice', 1), ('preloadDeviceResources', 2),
                                          ('nextFrame', 0), ('result', 1), ('lastDrawn', 1000)])
        for role in (player, worker):
            self.assertEqual(role['throttled'], [('result', 1), ('lastDrawn', 1000)])
            self.assertEqual(role['cancelled'], [('callback', 0), ('result', 0), ('lastDrawn', 2000)])
            self.assertEqual(role['device-lost'], [('callback', 1), ('checkDevice', 0), ('result', 0),
                                                   ('lastDrawn', 3000)])
            self.assertEqual(role['gui-cancelled'], [('callback', 0), ('result', 0), ('lastTime', -1000)])
        presentation = {'Clear', 'beginScene', 'endScene', 'present', 'push', 'pop',
                        'drawBar', 'drawConsoleString', 'SetSoftwareVertexProcessing'}
        self.assertTrue(presentation & set(names(player['gui-draw'])))
        for trace in worker.values():
            self.assertFalse(presentation & set(names(trace)))
        updates = [value for name, value in player['gui-draw'] if name == 'SimpleGUI.update']
        self.assertGreater(len(updates), 1)
        self.assertEqual([value for name, value in worker['gui-draw'] if name == 'SimpleGUI.update'], updates)
        for name in ('nextFrame', 'preloadDeviceResources', 'SimpleGUI.draw'):
            self.assertEqual(names(worker['gui-draw']).count(name), len(updates))
        # SimpleGUI::draw owns the component update pass in both roles.
        self.assertEqual(names(player['gui-draw']).count('SimpleGUI.draw'), len(updates))
        self.assertEqual(names(player['gui-draw']).count('present'), len(updates))
        self.assertEqual(worker['gui-draw'][:3], [('callback', 1), ('setProgress', 500), ('Script.call', 0)])
        self.assertEqual(worker['gui-draw'][-2:], player['gui-draw'][-2:])
        # Negative control: the pre-patch bodies render and present for the worker.
        control = calls(run(self.progress_host(self.engine.reference, 'control'), WORKER))
        self.assertIn('present', names(control['draw']))
        self.assertIn('present', names(control['gui-draw']))

    def simple_gui_host(self, tree, name):
        return compile_host(self, self.work / name, 'worker-host-simple-gui', {
            '@SIMPLE_GUI_DRAW_BODY@': function(self.source('lib/ashes/simple_gui.cpp', tree),
                                               'void SimpleGUI::draw()')})

    def test_gui_update_pass_advances_components_without_drawing(self):
        binary = self.simple_gui_host(self.engine.integrated, 'patched')
        player, worker = calls(run(binary)), calls(run(binary, WORKER))
        update = [('getViewport', 0)] + [('component.update', 20), ('component.width', 320),
                                         ('component.applyShaders', 20)] * 2
        state = [('resolutionChanged', 1), ('realResolutionCounter', 1)]
        focus = [('tickCrossFocusEvents', 0), ('tickDragFocusEvents', 0)]
        self.assertEqual(player['draw'][:len(update)], update)
        self.assertEqual(names(player['draw']).count('component.draw'), 2)
        self.assertIn(('DrawContext.flush', 0), player['draw'])
        self.assertEqual(player['draw'][-len(focus) - len(state):], focus + state)
        self.assertEqual(worker['draw'], update + focus + state)
        for role in (player, worker):
            self.assertEqual(role['update-disabled'], [])
        control = calls(run(self.simple_gui_host(self.engine.reference, 'control'), WORKER))
        self.assertEqual(control, player)

    def cursor_host(self, tree, name):
        cursor = self.source('lib/ashes/mouse_cursor.cpp', tree)
        return compile_host(self, self.work / name, 'worker-host-cursor', {
            '@POSITION_BODY@': function(cursor, 'void MouseCursor::position( const Vector2 & pos )'),
            '@CLIPPED_BODY@': function(cursor, 'void MouseCursor::clipped( bool state )'),
            '@FOCUS_DEFINITIONS@': between(cursor, 'bool MouseCursor::s_inFocus_ = false;',
                                           '\ts_inFocus_ = focus;\n}\n'),
            '@DELETE_BODY@': function(cursor, 'void MouseCursor::deleteUnmanagedObjects()'),
            '@UPDATE_BODY@': function(cursor, '/*static*/ void MouseCursor::updateMouseClipping()'),
            '@CLIP_BODY@': function(cursor, 'void clipCursorToWindow( bool clip )\n')})

    def test_cursor_lifecycle_never_touches_the_desktop_cursor_for_the_worker(self):
        binary = self.cursor_host(self.engine.integrated, 'patched')
        player_output, worker_output = run(binary), run(binary, WORKER)
        player, worker = calls(player_output), calls(worker_output)
        self.assertEqual(names(player['foreground-clip']), ['ClipCursor(rect)', 'ClipCursor(rect)'])
        self.assertEqual(names(player['background-release']), ['ClipCursor(NULL)'])
        self.assertEqual(names(player['focus-and-reset']), ['ClipCursor(NULL)'] * 3)
        self.assertEqual(names(player['position']), ['SetCursorPos', 'SetCursorPosition'])
        for trace in worker.values():
            self.assertEqual(trace, [])
        for output in (player_output, worker_output):
            self.assertIn('inFocus=0\ninFocus=1\n', output)
            self.assertIn('inactivePos=500,-500\n', output)
        control = calls(run(self.cursor_host(self.engine.reference, 'control'), WORKER))
        self.assertEqual(control, player)

    def render_host(self, tree, name):
        render = self.source('lib/moo/render_context.cpp', tree)
        create = render[render.index('bool RenderContext::createDevice( HWND hWnd, uint32 deviceIndex,'):]
        return compile_host(self, self.work / name, 'worker-host-render', {
            '@CHANGE_MODE_BODY@': function(render, 'bool RenderContext::changeMode( uint32 modeIndex, bool windowed, '
                                                   'bool testCooperative, uint32 backBufferWidthOverride )'),
            '@RELEASE_BODY@': function(render, 'void RenderContext::releaseDevice( void )'),
            '@CREATE_PREFIX@': '#define ENABLE_ASSET_PIPE 0\n' +
                               create[:create.index('    fillPresentationParameters();\n')]})

    def test_display_modes_stay_windowed_and_never_show_the_worker(self):
        binary = self.render_host(self.engine.integrated, 'patched')
        player, worker = calls(run(binary)), calls(run(binary, WORKER))
        popup, show, quiet = ('SetWindowLong', 0x80000000), ('SetWindowPos', 0x40), ('SetWindowPos', 0x10 | 0x04)
        self.assertEqual(player['create-fullscreen-request'][-2:], [('windowed_', 0), ('visible', 0)])
        self.assertIn(show, player['change-to-windowed'])
        self.assertIn(popup, player['change-to-fullscreen'])
        self.assertIn(show, player['restore-from-fullscreen'])
        self.assertIn(show, player['release-fullscreen'])
        self.assertEqual(player['release-fullscreen'][-1], ('visible', 1))
        for scenario, trace in worker.items():
            self.assertNotIn(popup, trace, scenario)
            self.assertNotIn(('changeModePriv.windowed', 0), trace, scenario)
            self.assertEqual(trace[-1], ('visible', 0), scenario)
            self.assertEqual([item for item in trace if item[0] == 'SetWindowPos'],
                             [quiet] if scenario in ('restore-from-fullscreen', 'release-fullscreen') else [],
                             scenario)
        for scenario in ('create-fullscreen-request', 'recreate-fullscreen-request', 'change-to-fullscreen',
                         'change-to-windowed'):
            self.assertEqual(worker[scenario][-2], ('windowed_', 1), scenario)
        control = calls(run(self.render_host(self.engine.reference, 'control'), WORKER))
        self.assertEqual(control, player)

    def winmain_host(self, tree, name):
        region = between(self.source('client/bw_winmain.cpp', tree), '\t\thWnd = CreateWindow(',
                         '\t\tWTSRegisterSessionNotification( hWnd, NOTIFY_FOR_THIS_SESSION );\n')
        return compile_host(self, self.work / name, 'worker-host-winmain', {'@WINMAIN_REGION@': region})

    def test_winmain_never_shows_and_marks_the_worker_window(self):
        winmain = self.source('client/bw_winmain.cpp')
        # The role is cached before App construction, window creation and Moo.
        cache = winmain.index('\twot::stockSimulationWorker();\n')
        for later in ('App app( configFilename', 'Moo::init(', 'hWnd = CreateWindow('):
            self.assertLess(cache, winmain.index(later))
        binary = self.winmain_host(self.engine.integrated, 'patched')
        player, worker = calls(run(binary)), calls(run(binary, WORKER))
        failed = calls(run(binary, WORKER, 'fail-mark'))
        self.assertEqual(player['create'], [
            ('CreateWindow', 0xCF0000), ('ShowWindow(show)', 1), ('UpdateWindow', 0),
            ('WTSRegisterSessionNotification', 0), ('createAndShow', 0), ('visible', 1), ('marked-self', 0)])
        self.assertEqual(worker['create'], [
            ('CreateWindow', 0xCF0000), ('SetPropW', 0), ('WTSRegisterSessionNotification', 0),
            ('createAndShow', 0), ('visible', 0), ('marked-self', 1)])
        self.assertEqual(failed['create-mark-failure'], [
            ('CreateWindow', 0xCF0000), ('SetPropW', 0), ('ERROR_MSG', 0),
            ('WTSRegisterSessionNotification', 0), ('createAndShow', 0), ('visible', 0), ('marked-self', 0)])
        control = calls(run(self.winmain_host(self.engine.reference, 'control'), WORKER))
        self.assertEqual(control, player)

    def test_native_handshake_with_adapted_pinned_worker_presentation(self):
        presentation = FORK / PRESENTATION
        if not presentation.is_file():
            self.skipTest('The pinned v0.10.1 gameplay source is required')
        platform = (ROOT / 'bigworld/stock_offline_platform.cpp').read_text()
        declarations = '\n'.join(
            [line for line in platform.splitlines() if line.startswith('#define MAX_HIDDEN_WINDOWS')] +
            [between(platform, 'typedef struct %s {' % name, '} %s;' % name)
             for name in ('HiddenWindow', 'HideContext', 'WorkerWindowContext')] +
            [line for line in platform.splitlines() if re.match(
                r'static (HiddenWindow g_hidden_windows|unsigned int g_hidden_window_count|LONG g_window_busy|'
                r'LONG g_window_generation|const WCHAR WINDOW_GENERATION|LONG g_closed)', line)])
        windows = platform[platform.index('// ShowWindow/SetWindowPlacement may synchronously'):
                           platform.index('static PyMethodDef methods[] = {')]
        binary = compile_host(self, self.work, 'worker-host-platform', {
            '@PLATFORM_DECLARATIONS@': declarations, '@PLATFORM_WINDOWS@': windows}, python=True)
        module = self.work / 'worker_presentation.py'
        module.write_text(patcher.adapt_worker_presentation(presentation.read_text(encoding='utf-8')))
        driver = str(TESTS / 'stock_worker_runtime_presentation.py')

        def reports(mode):
            output = run(binary, mode, driver, WORKER_PRESENTATION_MODULE=str(module),
                         PYTHONHOME=str(PY27.parents[1]))
            return dict(line.split(' ', 1) for line in output.splitlines())

        worker, player = reports(WORKER), reports(None)
        # Negative control: the unadapted pinned module refuses the never-shown worker.
        module.write_text(presentation.read_text(encoding='utf-8'))
        self.assertEqual(reports(WORKER)['activate-never-shown'],
                         repr('refused: native worker window was not found'))
        refused = repr('refused: native worker window was not found')
        self.assertEqual(worker, {
            'role': '1', 'confirm-without-window': '-1400', 'confirm-foreign-only': '-1400',
            'hide-never-shown': '0', 'confirm-never-shown': '1', 'confirm-repeated': '1',
            'activate-never-shown': 'True', 'window-hidden-flag': 'False', 'activate-again': 'True',
            'deactivate-restore': 'True', 'main-visible-after-restore': 'False', 'shown-after-restore': '0',
            'activate-for-exit': 'True', 'deactivate-exit': 'True', 'confirm-visible': '-5023',
            'activate-visible': 'True', 'window-hidden-flag-visible': 'True', 'deactivate-visible': 'True',
            'main-visible-after-visible-restore': 'False', 'shown-after-visible-restore': '0',
            'confirm-with-foreign-mark-value': '-1400', 'confirm-two-marked': '-1400', 'confirm-busy': '-170',
            'activate-hide-busy': refused,
            'confirm-destroyed': '-1400', 'activate-destroyed': refused, 'confirm-closed': '-995'})
        self.assertEqual(player, {
            'role': '0', 'confirm-without-window': '-50', 'confirm-foreign-only': '-50',
            'hide-never-shown': '0', 'confirm-never-shown': '-50', 'confirm-repeated': '-50',
            'activate-never-shown': refused, 'window-hidden-flag': 'False', 'activate-again': refused,
            'deactivate-restore': 'True', 'main-visible-after-restore': 'False', 'shown-after-restore': '0',
            'activate-visible': 'True', 'main-visible-after-hide': 'False', 'deactivate-visible': 'True',
            'main-visible-after-visible-restore': 'True', 'shown-after-visible-restore': '2'})


if __name__ == '__main__':
    unittest.main()
