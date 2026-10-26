"""Real-client preference event probe for an isolated diagnostic runtime only.

Launch a COPY of the EXE in a directory named preferences-callbacks-<identity>.
Prepend a diagnostic resource overlay to that copy's paths.xml and select this
personality. The engine configuration selected by resources.xml (or --config)
must contain an ABSOLUTE <preferences> path to this directory's
preferences-callbacks-test.xml, with <pathBase>EXE_PATH</pathBase>. There is no
--preferences CLI switch: both bw_winmain and DeviceApp read that config field.
Run first with the test file absent, then reuse the generated file for a second
session to cover both startup snapshots. Never select a user preference path.
Before each launch, write preferences-callbacks-startup.json beside the EXE:
{"format": 1, "preferences_path": "<absolute private XML path>",
 "existed_at_startup": <whether parsing before launch found root children>}.
DeviceApp may save before personality.init; the current file is not a startup
snapshot. Missing or mismatched manifests fail this probe before its first save.

The probe checks the native path before any save. It requests normal quit and
only reports passed after the native pre-PyFinalize job releases its final
subscription and rejects a finalizer's attempt to resubscribe.
"""
from __future__ import print_function
import gc
import json
import os
import sys
import traceback
import weakref
import xml.etree.ElementTree as ET

REPORT = {'format': 1, 'status': 'running', 'python': sys.version,
          'executable': sys.executable, 'checks': [], 'lifecycle': [],
          'original_game_init_called': False, 'disk_writes_isolated': False,
          'exit_receiver_released': False, 'exit_resubscribe_rejected': False}
STATE = {'safe': False, 'exit_requested': False, 'checks_passed': False}
OUTPUT = None
PREFERENCES = None


def save_report():
    if not STATE['safe']:
        return
    temporary = OUTPUT + '.tmp'
    with open(temporary, 'wb') as stream:
        stream.write(json.dumps(REPORT, indent=2))
    if os.path.exists(OUTPUT):
        os.remove(OUTPUT)
    os.rename(temporary, OUTPUT)


def canonical(path):
    return os.path.normcase(os.path.realpath(os.path.abspath(os.path.normpath(path))))


def validate_sandbox():
    import BigWorld
    root = canonical(os.getcwd())
    assert os.path.basename(root).startswith('preferences-callbacks-'), root
    assert canonical(os.path.dirname(sys.executable)) == root, sys.executable
    expected = canonical(os.path.join(root, 'preferences-callbacks-test.xml'))
    actual = canonical(BigWorld.wg_getPreferencesFilePath().decode('utf-8'))
    assert actual == expected, ('Unsafe preference path; no save attempted', actual, expected)
    return expected, os.path.join(root, 'preferences-callbacks-result.json')


def checked_save():
    import BigWorld
    assert canonical(BigWorld.wg_getPreferencesFilePath().decode('utf-8')) == PREFERENCES
    assert BigWorld.savePreferences() is True


def startup_snapshot(preferences):
    path = os.path.join(os.path.dirname(preferences), 'preferences-callbacks-startup.json')
    with open(path, 'rb') as stream:
        manifest = json.load(stream)
    assert type(manifest) is dict and manifest.get('format') == 1, manifest
    declared_path = manifest.get('preferences_path')
    assert isinstance(declared_path, basestring) and os.path.isabs(declared_path), manifest
    assert canonical(declared_path) == preferences, ('Startup manifest path mismatch', declared_path)
    existed = manifest.get('existed_at_startup')
    assert type(existed) is bool, manifest
    return existed


def disk_int(path):
    node = ET.parse(PREFERENCES).getroot().find(path)
    assert node is not None and node.text is not None, path
    return int(node.text.strip())


def invalid(call):
    try:
        call()
    except TypeError:
        return
    raise AssertionError('Invalid preference API arity was accepted')


def shutdown_result():
    if (STATE['checks_passed'] and REPORT['exit_receiver_released'] and
            REPORT['exit_resubscribe_rejected']):
        REPORT['status'] = 'passed'
        print('PREFERENCES_CALLBACKS_RUNTIME_PASSED')
    save_report()


def exit_receiver_gone(reference):
    REPORT['exit_receiver_released'] = reference() is None
    shutdown_result()


class ExitReceiver(object):
    def callback(self, root):
        raise AssertionError('The final shutdown-only receiver was called')

    def __del__(self):
        try:
            import BigWorld
            try:
                BigWorld.wg_subscribeToSavePreferences(None)
            except RuntimeError:
                REPORT['exit_resubscribe_rejected'] = True
            else:
                REPORT['status'] = 'failed'
                REPORT['error'] = 'Native shutdown admitted a new subscription'
            shutdown_result()
        except Exception:
            print('PREFERENCES_CALLBACKS_SHUTDOWN_FAILED')
            traceback.print_exc()


def init(*args):
    global OUTPUT, PREFERENCES
    REPORT['lifecycle'].append('init')
    try:
        import BigWorld
        PREFERENCES, OUTPUT = validate_sandbox()
        STATE['safe'] = True
        REPORT['disk_writes_isolated'] = True
        REPORT['preferences_path'] = PREFERENCES
        expected_startup = startup_snapshot(PREFERENCES)
        observed = BigWorld.wg_preferencesExistedAtStartup()
        assert type(observed) is bool and observed == expected_startup, (observed, expected_startup)
        REPORT['startup_existed'] = observed
        REPORT['checks'].append('actual configured filename and pre-launch parsed startup snapshot')
        BigWorld.wg_unsubAllSavePreferences()
        for call in (lambda: BigWorld.wg_subscribeToSavePreferences(),
                     lambda: BigWorld.wg_subscribeToReadPreferences(None, None),
                     lambda: BigWorld.wg_unsubAllSavePreferences(1),
                     lambda: BigWorld.wg_preferencesExistedAtStartup(1)):
            invalid(call)
        assert len(args) >= 3 and args[2] is not None
        args[2].writeInt('nativePreferencesProbe/scriptMarker', 74)
        events = []
        retained = []

        def first(root):
            assert root['graphicsPreferences'] is not None
            assert root['devicePreferences'].readInt('windowedWidth') > 0
            assert root['scriptsPreferences'].readInt('nativePreferencesProbe/scriptMarker') == 74
            events.append('first')
            retained.append(root)
            root.writeInt('nativePreferencesProbe/marker', 17)

        def second(root):
            assert root is not retained[-1]
            assert root.readInt('nativePreferencesProbe/marker') == 17
            events.append('second')
            root.writeInt('nativePreferencesProbe/marker', 29)

        BigWorld.wg_subscribeToSavePreferences(first)
        BigWorld.wg_subscribeToSavePreferences(None)
        BigWorld.wg_subscribeToSavePreferences(37)
        BigWorld.wg_subscribeToSavePreferences(second)
        checked_save()
        assert events == ['first', 'second'], events
        assert disk_int('nativePreferencesProbe/marker') == 29
        assert retained[0].readInt('nativePreferencesProbe/marker') == 29
        assert BigWorld.wg_preferencesExistedAtStartup() == observed
        REPORT['checks'].append('native fields, original order, shared DataSection and actual XML save')

        BigWorld.wg_unsubAllSavePreferences()
        events[:] = []

        def replacement(root):
            events.append('replacement')
            root.writeInt('nativePreferencesProbe/marker', 51)

        def mutate(root):
            events.append('mutate')
            BigWorld.wg_unsubAllSavePreferences()
            BigWorld.wg_subscribeToSavePreferences(replacement)

        BigWorld.wg_subscribeToSavePreferences(mutate)
        BigWorld.wg_subscribeToSavePreferences(lambda root: events.append('admitted'))
        checked_save()
        assert events == ['mutate', 'admitted'], events
        checked_save()
        assert events == ['mutate', 'admitted', 'replacement'], events
        assert disk_int('nativePreferencesProbe/marker') == 51
        REPORT['checks'].append('clear and resubscribe during admitted dispatch')

        BigWorld.wg_unsubAllSavePreferences()
        events[:] = []
        nested = [False]

        def reenter(root):
            events.append('nested' if nested[0] else 'outer')
            root.writeInt('nativePreferencesProbe/marker', 72 if nested[0] else 61)
            if not nested[0]:
                nested[0] = True
                checked_save()
                nested[0] = False

        BigWorld.wg_subscribeToSavePreferences(reenter)
        checked_save()
        assert events == ['outer', 'nested'], events
        assert disk_int('nativePreferencesProbe/marker') == 61
        assert BigWorld.wg_preferencesExistedAtStartup() == observed
        REPORT['checks'].append('nested native saves retain independent roots and startup snapshot')

        BigWorld.wg_unsubAllSavePreferences()
        events[:] = []

        class Receiver(object):
            def saved(self, root):
                events.append('owned')

            def read(self, key, value):
                events.append((key, value))

        owner = Receiver()
        reference = weakref.ref(owner)
        references = sys.getrefcount(owner)
        BigWorld.wg_subscribeToSavePreferences(owner.saved)
        BigWorld.wg_subscribeToReadPreferences(owner.read)
        assert sys.getrefcount(owner) == references + 2
        del owner
        gc.collect()
        assert reference() is not None
        checked_save()
        assert events == ['owned']
        BigWorld.wg_unsubAllSavePreferences()
        gc.collect()
        assert reference() is None
        checked_save()
        assert events == ['owned']
        BigWorld.wg_unsubAllSavePreferences()
        REPORT['checks'].append('strong receiver ownership and idempotent release of both registries')
        retained[:] = []
        exit_owner = ExitReceiver()
        STATE['exit_receiver'] = weakref.ref(exit_owner, exit_receiver_gone)
        BigWorld.wg_subscribeToSavePreferences(exit_owner.callback)
        del exit_owner
        gc.collect()
        assert STATE['exit_receiver']() is not None
        STATE['checks_passed'] = True
        REPORT['status'] = 'awaiting-native-shutdown'
        save_report()
    except Exception:
        REPORT['status'] = 'failed'
        REPORT['error'] = traceback.format_exc()
        print('PREFERENCES_CALLBACKS_RUNTIME_FAILED')
        print(REPORT['error'])
        save_report()


def start():
    import BigWorld
    REPORT['lifecycle'].append('start')
    STATE['exit_requested'] = True
    save_report()
    BigWorld.quit()


def onInit(isReload):
    REPORT['lifecycle'].append('onInit')


def fini():
    REPORT['lifecycle'].append('fini')
    save_report()


def onFini():
    REPORT['lifecycle'].append('onFini')
    save_report()
