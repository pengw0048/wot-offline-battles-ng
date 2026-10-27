"""Offline policies for the optional retail services of the independent client.

The independent product has no retail credential store, no encrypted
messenger disk cache and no vibration device. OfflineGarageEntry.install calls
install() after the original game import and before game.init.
"""
import sys

_installed = None
_credential_store = None


class UnavailableVibration(object):
    """BigWorld.WGVibration without a device: it never connects."""
    __slots__ = ()

    def reset(self):
        return None

    def connect(self):
        return False

    def setGain(self, gain):
        # VibroManager owns the requested gain and persists it.
        return None


def _preferences_init(self):
    # The original constructor without its loginInfo read, decrypt or create.
    dict.__init__(self)
    self._Preferences__oldFormat = False
    self._Preferences__invalidateSettingsSanity()


def _write_login_info(self):
    return None


def _restore_messenger_cache():
    return None


def install():
    """Disable credential IO and the messenger cache; publish no-device vibration."""
    global _installed
    if _installed is not None:
        return True
    import BigWorld
    from gui import login
    from messenger import MessengerEntry
    storage = MessengerEntry.g_instance.storage
    if storage._StorageDecorator__storageCache is not None:
        raise RuntimeError('The messenger disk cache was opened before the offline entry')
    vibration = sys.modules.get('Vibroeffects.VibroManager')
    if vibration is not None and vibration.g_instance is not None:
        raise RuntimeError('The vibration manager was built before the offline entry')
    config = login.__dict__['getLoginManagerConfig']

    def get_login_manager_config(manager):
        # Manager.init constructs Preferences inside this original call.
        _disable_credential_store()
        return config(manager)

    if 'restoreFromCache' in storage.__dict__:
        raise RuntimeError('The messenger storage cache seam has changed')
    backend = None
    if not hasattr(BigWorld, 'WGVibration'):
        backend = BigWorld
        BigWorld.WGVibration = UnavailableVibration
    storage.restoreFromCache = _restore_messenger_cache
    login.getLoginManagerConfig = get_login_manager_config
    _installed = (login, config, get_login_manager_config, storage, backend)
    return True


def _disable_credential_store():
    global _credential_store
    from gui.login.Preferences import Preferences
    if _credential_store is not None:
        return
    _credential_store = (Preferences, Preferences.__dict__['__init__'],
                         Preferences.__dict__['writeLoginInfo'])
    Preferences.__init__ = _preferences_init
    Preferences.writeLoginInfo = _write_login_info


def uninstall():
    global _credential_store, _installed
    installed, _installed = _installed, None
    if installed is None:
        return
    login, config, wrapper, storage, backend = installed
    if login.__dict__.get('getLoginManagerConfig') is wrapper:
        login.getLoginManagerConfig = config
    store, _credential_store = _credential_store, None
    if store is not None:
        preferences, init, write = store
        if preferences.__dict__.get('__init__') is _preferences_init:
            preferences.__init__ = init
        if preferences.__dict__.get('writeLoginInfo') is _write_login_info:
            preferences.writeLoginInfo = write
    if storage.__dict__.get('restoreFromCache') is _restore_messenger_cache:
        del storage.restoreFromCache
    if backend is not None and backend.__dict__.get('WGVibration') is UnavailableVibration:
        del backend.WGVibration
