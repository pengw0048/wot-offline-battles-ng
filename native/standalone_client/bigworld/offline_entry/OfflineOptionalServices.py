"""Offline policies for the optional retail services of the independent client.

The independent product has no retail credential store, no encrypted
messenger disk cache, no lobby chat or contacts UI and no vibration device;
in-battle chat stays. OfflineGarageEntry.install calls install() after the
original game import and before game.init.
"""
import sys

LOBBY_MESSENGER_VIEWS = 'messenger.gui.Scaleform.view.lobby'
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


def _set_battle_gui_entry(decorator):
    # The original setGUIEntries without its lobby channel/contact entry.
    from messenger.gui.Scaleform.battle_entry import BattleEntry
    from messenger.m_constants import MESSENGER_SCOPE
    decorator.setEntry(MESSENGER_SCOPE.BATTLE, BattleEntry())


def install():
    """Disable credential IO, the messenger cache and lobby chat; bind no-device vibration."""
    global _installed
    if _installed is not None:
        return True
    import BigWorld
    from gui import login
    from gui.Scaleform.daapi.settings import config
    import messenger.gui
    from messenger import MessengerEntry
    from messenger.gui import entry_decorator
    storage = MessengerEntry.g_instance.storage
    if storage._StorageDecorator__storageCache is not None:
        raise RuntimeError('The messenger disk cache was opened before the offline entry')
    if MessengerEntry.g_instance.gui._GUIDecorator__entries:
        raise RuntimeError('The messenger GUI was initialized before the offline entry')
    vibration = sys.modules.get('Vibroeffects.VibroManager')
    if vibration is not None and vibration.g_instance is not None:
        raise RuntimeError('The vibration manager was built before the offline entry')
    login_config = login.__dict__['getLoginManagerConfig']
    set_entries = entry_decorator.__dict__['setGUIEntries']
    lobby_packages = config.LOBBY_PACKAGES
    if ('restoreFromCache' in storage.__dict__ or
            set_entries is not messenger.gui.setGUIEntries or
            lobby_packages.count(LOBBY_MESSENGER_VIEWS) != 1):
        raise RuntimeError('An optional messenger seam has changed')

    def get_login_manager_config(manager):
        # Manager.init constructs Preferences inside this original call.
        _disable_credential_store()
        return login_config(manager)

    # Lobby channels, contacts and their views stay unregistered; the stock
    # unknown GUI entry and missing view alias paths answer their callers.
    battle_only = tuple(name for name in lobby_packages if name != LOBBY_MESSENGER_VIEWS)
    bindings = [(login, 'getLoginManagerConfig', get_login_manager_config, login_config),
                (entry_decorator, 'setGUIEntries', _set_battle_gui_entry, set_entries),
                (config, 'LOBBY_PACKAGES', battle_only, lobby_packages),
                (storage, 'restoreFromCache', _restore_messenger_cache, None)]
    if not hasattr(BigWorld, 'WGVibration'):
        bindings.append((BigWorld, 'WGVibration', UnavailableVibration, None))
    for owner, name, value, unused in bindings:
        setattr(owner, name, value)
    _installed = bindings
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
    bindings, _installed = _installed, None
    for owner, name, value, original in reversed(bindings or ()):
        if owner.__dict__.get(name) is value:
            if original is None:
                delattr(owner, name)
            else:
                setattr(owner, name, original)
    store, _credential_store = _credential_store, None
    if store is not None:
        preferences, init, write = store
        if preferences.__dict__.get('__init__') is _preferences_init:
            preferences.__init__ = init
        if preferences.__dict__.get('writeLoginInfo') is _write_login_info:
            preferences.writeLoginInfo = write
