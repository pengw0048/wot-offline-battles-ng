"""Music teardown must stop live NG leases before stock drops their identity."""
import importlib.util
import os
import shutil
import subprocess
from pathlib import Path
import types
import unittest

MODULE = Path(__file__).resolve().parents[1] / (
    'src/res/scripts/client/gui/mods/offline_lan_0922/music_lifecycle.py')


class MusicLifecycleTests(unittest.TestCase):
    def setUp(self):
        spec = importlib.util.spec_from_file_location('music_lifecycle_test', MODULE)
        self.adapter = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(self.adapter)
        self.trace = []
        trace = self.trace

        class Lease:
            def __init__(self, event_id):
                self.event_id = event_id
                self.playing = True

            def getEventId(self):
                return self.event_id

            def destroy(self):
                trace.append(('stop', self.event_id))
                self.playing = False
                self.event_id = None

        class Controller:
            def onLeaveArena(self):
                trace.append('unsubscribe')

            def stop(self):
                self.music.destroy()
                self.ambient.destroy()

            def _getSoundEventById(self, event_id):
                return self.music

        class Environment:
            def __init__(self, event_id):
                self.event = types.SimpleNamespace(
                    _soundEventID=event_id, isEmpty=lambda: False)

            def getMusicEvent(self):
                return self.event

            def stop(self):
                # Exact stock order: _clearMusicEvent replaces the event first;
                # SoundEnv.stop only clears notification state afterward.
                trace.append('clear')
                self.event = types.SimpleNamespace(isEmpty=lambda: True)

        self.Lease = Lease
        self.Controller = Controller
        self.Environment = Environment
        self.module = types.SimpleNamespace(MusicController=Controller,
                                            g_musicController=None)
        self.originals = Controller.onLeaveArena, Environment.stop
        self.adapter.install(self.module, Environment)
        self.addCleanup(self.adapter.uninstall)

    def controller(self, event_id):
        controller = self.Controller()
        controller.music = self.Lease(event_id)
        controller.ambient = self.Lease(100)
        self.module.g_musicController = controller
        return controller

    def test_closing_each_result_stops_live_lease_before_clearing(self):
        for event_id in (4, 5, 6):  # victory, defeat, draw
            with self.subTest(event_id=event_id):
                self.trace[:] = []
                controller = self.controller(event_id)
                environment = self.Environment(event_id)
                environment.stop()
                self.assertEqual([('stop', event_id), 'clear'], self.trace)
                self.assertFalse(controller.music.playing)
                self.assertTrue(controller.ambient.playing)
                environment.stop()
                self.assertEqual([('stop', event_id), 'clear', 'clear'], self.trace)

    def test_stale_results_do_not_stop_new_battle_or_lobby_music(self):
        for next_event in (1, 2, 5):
            with self.subTest(next_event=next_event):
                self.trace[:] = []
                controller = self.controller(next_event)
                self.Environment(4).stop()
                self.assertTrue(controller.music.playing)
                self.assertEqual(['clear'], self.trace)

    def test_completed_or_uninitialised_results_still_clean_up(self):
        environment = self.Environment(4)
        environment.stop()
        self.assertEqual(['clear'], self.trace)
        controller = self.controller(4)
        controller.music.playing = False
        environment.stop()
        self.assertEqual(['clear', 'clear'], self.trace)

    def test_leave_battle_stops_music_and_ambient_even_without_results(self):
        controller = self.controller(2)
        controller.onLeaveArena()
        self.assertEqual(['unsubscribe', ('stop', 2), ('stop', 100)], self.trace)
        self.assertFalse(controller.music.playing)
        self.assertFalse(controller.ambient.playing)

    def test_cleanup_runs_after_stock_failures(self):
        self.adapter.uninstall()

        def failed_leave(controller):
            raise RuntimeError('arena already retired')

        self.Controller.onLeaveArena = failed_leave
        self.adapter.install(self.module, self.Environment)
        controller = self.controller(2)
        with self.assertRaisesRegex(RuntimeError, 'arena already retired'):
            controller.onLeaveArena()
        self.assertFalse(controller.music.playing)
        self.assertFalse(controller.ambient.playing)

    def test_install_is_idempotent_and_uninstall_restores_methods(self):
        installed = self.Controller.onLeaveArena, self.Environment.stop
        self.adapter.install(self.module, self.Environment)
        self.assertEqual(installed, (self.Controller.onLeaveArena, self.Environment.stop))
        self.adapter.uninstall()
        self.assertEqual(self.originals, (self.Controller.onLeaveArena, self.Environment.stop))
        self.adapter.uninstall()


    @unittest.skipUnless(os.environ.get('WOT_0922_CLIENT') and shutil.which('pyenv'),
                         'Exact #1513 client and pyenv Python 2.7 required')
    def test_exact_stock_bytecode_teardown(self):
        script = r"""
import imp, marshal, os, types, zipfile
adapter = imp.load_source('music_lifecycle', __import__('sys').argv[1])
z = zipfile.ZipFile(os.path.join(os.environ['WOT_0922_CLIENT'], 'res/packages/scripts.pkg'))
roots = [marshal.loads(z.read(p)[8:]) for p in (
    'scripts/client/MusicControllerWWISE.pyc', 'scripts/client/gui/sounds/ambients.pyc')]
def code(root, *path):
    for name in path:
        root = next(c for c in root.co_consts if isinstance(c, types.CodeType) and c.co_name == name)
    return root
class Stub(object): pass
class Event(object):
    def __isub__(self, unused): return self
    def clear(self): pass
class EmptySound(object):
    def __init__(self): self.onFinished = Event()
    def isEmpty(self): return True
    def clear(self): pass
class Voice(object):
    def __init__(self): self.isPlaying = True
    def stop(self): self.isPlaying = False
class SoundEnv(object): pass
class BattleResultsEnv(SoundEnv): pass
class MusicController(object):
    _MUSIC_EVENT = 0
    _AMBIENT_EVENT = 1
class MusicEvent(object): pass
g = dict(super=super, MusicController=MusicController, BattleResultsEnv=BattleResultsEnv,
         EmptySound=EmptySound, AMBIENT_EVENT_NONE=100)
g.update({'True': True, 'False': False, 'None': None})
for name in ('__init__', 'destroy', 'getEventId'):
    setattr(MusicEvent, name, types.FunctionType(code(roots[0], 'MusicController', 'MusicEvent', name), g))
for name in ('stop', 'stopMusic', 'stopAmbient', 'onLeaveArena', '_getSoundEventById'):
    setattr(MusicController, name, types.FunctionType(code(roots[0], 'MusicController', name), g))
for name in ('stop', 'getMusicEvent'):
    setattr(SoundEnv, name, types.FunctionType(code(roots[1], 'SoundEnv', name), g))
for name in ('stop', '_clearMusicEvent'):
    setattr(BattleResultsEnv, name, types.FunctionType(code(roots[1], 'BattleResultsEnv', name), g))
player = Stub(); player.arena = Stub(); player.arena.onPeriodChange = Event()
bigworld = Stub(); bigworld.player = lambda: player; g['BigWorld'] = bigworld
music = Stub(); music.MusicController = MusicController

def setup(event_id):
    controller = MusicController()
    controller._MusicController__onArenaStateChanged = lambda: None
    voices = [Voice(), Voice()]
    leases = [MusicEvent(v) for v in voices]
    for lease, identity in zip(leases, (event_id, 100)):
        lease._MusicEvent__eventID = identity
    controller._MusicController__musicEvents = dict(enumerate(leases))
    music.g_musicController = controller
    env = BattleResultsEnv(); env.onChanged = Event(); env._SoundEnv__envID = 1
    env._soundsCtrl = Stub(); env._soundsCtrl.system = Stub()
    env._soundsCtrl.system.onEnvStop = lambda unused: None
    env._ambient = EmptySound(); env._onMusicFinished = lambda: None
    env._music = Stub(); env._music._soundEventID = event_id
    env._music.isEmpty = lambda: False; env._music.onFinished = Event()
    return controller, env, voices

# Demonstrate the failure using stock methods before installing the adapter.
controller, env, voices = setup(4)
env.stop(); controller.onLeaveArena()
assert all(v.isPlaying for v in voices)
adapter.install(music, BattleResultsEnv)
for event_id in (4, 5, 6):
    controller, env, voices = setup(event_id)
    env.stop()
    assert not voices[0].isPlaying and voices[1].isPlaying
    env.stop()
controller, env, voices = setup(2)
controller.onLeaveArena()
assert not any(v.isPlaying for v in voices)
adapter.uninstall()
print('Exact #1513 teardown bytecode regression passed')
"""
        result = subprocess.run(['pyenv', 'exec', 'python2.7', '-c', script, str(MODULE)],
            env=dict(os.environ, PYENV_VERSION='2.7.18', PYTHONDONTWRITEBYTECODE='1'),
            text=True, capture_output=True, timeout=30)
        self.assertEqual(0, result.returncode, result.stdout + result.stderr)


if __name__ == '__main__':
    unittest.main()
