# Python 2.7: actual display owner/PY_AUTO binding, exact SettingsWindow methods.
import gc
import marshal
import os
import types
import weakref
import zipfile

import BigWorld as B
import _display_host as host

calls = []
def expect_type_error(text, *args, **kwargs):
    try:
        B.wg_setAdapterOrdinalNotifyCallback(*args, **kwargs)
    except TypeError as error:
        assert str(error) == text, (str(error), text)
    else:
        raise AssertionError('Callback binding accepted the wrong call shape')
expect_type_error('wg_setAdapterOrdinalNotifyCallback() expects 1 argument of type PyObject')
expect_type_error('wg_setAdapterOrdinalNotifyCallback() expects 1 argument of type PyObject', None, None)
expect_type_error('wg_setAdapterOrdinalNotifyCallback() takes no keyword arguments', callback=None)

class Receiver(object):
    def __call__(self):
        calls.append('retained')
receiver = Receiver()
reference = weakref.ref(receiver)
assert B.wg_setAdapterOrdinalNotifyCallback(receiver) is None
assert not calls
del receiver
gc.collect()
assert reference() is not None
B.setActiveMonitorIndex(1, 1)  # Exclusive is inactive; its changed setting still dispatches.
assert calls == ['retained']
B.setActiveMonitorIndex(1, 1)
assert calls == ['retained']
assert B.wg_setAdapterOrdinalNotifyCallback(None) is None
gc.collect()
assert reference() is None

# The replacement is visible before old receiver destruction; a destructor's
# nested replacement wins and the outer assignment must not overwrite it.
class Retired(object):
    def __call__(self):
        calls.append('old')
    def __del__(self):
        B.setActiveMonitorIndex(0, 1)
        B.wg_setAdapterOrdinalNotifyCallback(lambda: calls.append('nested'))
B.wg_setAdapterOrdinalNotifyCallback(Retired())
B.wg_setAdapterOrdinalNotifyCallback(lambda: calls.append('published'))
assert calls[-1] == 'published'
B.setActiveMonitorIndex(1, 1)
assert calls[-1] == 'nested'

# A callback can clear its only stored reference and synchronously mutate the
# owner. Its admitted reference survives until the outer call completes.
class Clearing(object):
    def __call__(self):
        B.wg_setAdapterOrdinalNotifyCallback(None)
        B.setActiveMonitorIndex(1, 1)
        calls.append('clear-complete')
    def __del__(self):
        calls.append('clear-destroy')
B.wg_setAdapterOrdinalNotifyCallback(Clearing())
B.setActiveMonitorIndex(0, 1)
assert calls[-2:] == ['clear-complete', 'clear-destroy']
assert B.getActiveMonitorIndex(1) == 1  # Nested selection owns the final state.

# Non-callables are admitted by PyObjectPtr and fail only at native dispatch;
# exceptions are reported/cleared so the settings owner continues coherently.
errors = host.errorCount()
B.wg_setAdapterOrdinalNotifyCallback(42)
B.setActiveMonitorIndex(0, 1)
assert host.errorCount() == errors + 1
B.wg_setAdapterOrdinalNotifyCallback(None)
host.windowMonitor(0)  # Align the observed native window state before admission.
B.wg_setAdapterOrdinalNotifyCallback(lambda: calls.append('window'))
host.windowMonitor(1)
host.windowMonitor(1)
assert calls.count('window') == 1
B.wg_setAdapterOrdinalNotifyCallback(None)

# Execute the exact UI lifecycle and onRecreateDevice body. Only unrelated UI,
# vibro and settings-service dependencies are fixtures.
client = os.environ.get('WOT_0922_CLIENT')
if client:
    with zipfile.ZipFile(os.path.join(client, 'res/packages/scripts.pkg')) as archive:
        root = marshal.loads(archive.read('scripts/client/gui/Scaleform/daapi/view/common/settings/SettingsWindow.pyc')[8:])
    cls = next(code for code in root.co_consts
               if isinstance(code, types.CodeType) and code.co_name == 'SettingsWindow')
    class Event(list):
        def __iadd__(self, value):
            self.append(value)
            return self
        def __isub__(self, value):
            self.remove(value)
            return self
    class Base(object):
        def _populate(self):
            calls.append('populate')
        def _dispose(self):
            calls.append('dispose')
    class Parameters(object):
        def getMonitorSettings(self):
            return {'dynamic': 1, 'monitor': B.getActiveMonitorIndex(1)}
    vibro = type('Vibro', (), {'g_instance': type('Instance', (), {'onConnect': Event(), 'onDisconnect': Event()})()})
    graphics = type('Graphics', (), {'DYNAMIC_RENDERER': 'dynamic'})
    namespace = {'__builtins__': __builtins__, 'BigWorld': B, 'VibroManager': vibro,
                 'g_guiResetters': set(), 'getNewSettings': lambda: None, '_getLastTabIndex': lambda: 0, 'settings_constants': type('Constants', (), {'GRAPHICS': graphics})}
    methods = dict((code.co_name, types.FunctionType(code, namespace)) for code in cls.co_consts
                   if isinstance(code, types.CodeType) and code.co_name in
                   ('_populate', '_dispose', '_update', 'onRecreateDevice'))
    SettingsWindow = type('SettingsWindow', (Base,), methods)
    namespace['SettingsWindow'] = SettingsWindow
    window = SettingsWindow()
    window.params = Parameters()
    window._SettingsWindow__redefinedKeyModeEnabled = False
    window.onVibroManagerConnect = window.onVibroManagerDisconnect = lambda: None
    window.stopVoicesPreview = window.stopAltBulbPreview = lambda: None
    updates = []
    window.as_updateVideoSettingsS = updates.append
    window.as_setDataS = window.as_openTabS = lambda value: None
    window._SettingsWindow__getSettings = lambda: {}
    window._populate()
    assert updates == [{'dynamic': 1, 'monitor': 0}]
    updates[:] = []
    B.setActiveMonitorIndex(1, 1)
    assert updates[-1] == {'dynamic': None, 'monitor': 1}
    def dispose_during_notification(value):
        updates.append(value)
        window._dispose()
    window.as_updateVideoSettingsS = dispose_during_notification
    B.setActiveMonitorIndex(0, 1)
    count = len(updates)
    B.setActiveMonitorIndex(1, 1)
    assert len(updates) == count and not namespace['g_guiResetters']
    assert not vibro.g_instance.onConnect and not vibro.g_instance.onDisconnect
    assert calls[-2:] == ['populate', 'dispose']
    # Missing export is the original lifecycle blocker, including disposal.
    saved = B.wg_setAdapterOrdinalNotifyCallback
    del B.wg_setAdapterOrdinalNotifyCallback
    for method in ('_populate', '_dispose'):
        try:
            getattr(window, method)()
        except AttributeError:
            pass
        else:
            raise AssertionError('Original SettingsWindow did not require the binding')
    B.wg_setAdapterOrdinalNotifyCallback = saved

class Finishing(object):
    def __call__(self):
        calls.append('after-fini')
    def __del__(self):
        B.wg_setAdapterOrdinalNotifyCallback(self)
finishing = Finishing()
reference = weakref.ref(finishing)
B.wg_setAdapterOrdinalNotifyCallback(finishing)
del finishing
host.finish()
host.finish()
gc.collect()
assert reference() is None
host.lateWindowEvent()
host.windowMonitor(0)
assert 'after-fini' not in calls
