"""Independent-client entry for the pinned offline gameplay package.

The independent EXE has no retail intro, login view or WGC owner. The native
personality hook calls install() after the original game import and before
game.init. It mounts the generated mod at the original guiModsInit seam,
routes LOGIN through a view-less state, publishes the offline LOGGED_ON
transition to the retained connection consumers only, and installs the
offline policies for optional retail services.
"""
import os
import sys

MOD_MODULE = 'gui.mods.mod_offline_lan_0922'
MODS_DIRECTORY = os.path.join(
    os.path.dirname(os.path.abspath(__file__)), 'gui', 'mods')

_installed = None
_route = None
_entry_state_type = None


def install():
    """Bind the generated mod to personality.init's guiModsInit call once."""
    global _installed
    if _installed is not None:
        return True
    if not os.path.isfile(os.path.join(MODS_DIRECTORY,
                                       'mod_offline_lan_0922.py')):
        raise ImportError('The offline gameplay package is not mounted: ' +
                          MODS_DIRECTORY)
    mods = sys.modules.get('gui.mods')
    personality = sys.modules.get('gui.shared.personality')
    if mods is None or personality is None:
        raise RuntimeError('The original personality has not been imported')
    original = personality.__dict__.get('guiModsInit')
    if original is None or original is not mods.__dict__.get('init'):
        raise RuntimeError('The original guiModsInit binding has changed')
    import OfflineOptionalServices
    OfflineOptionalServices.install()
    try:
        install_route()
    except Exception:
        OfflineOptionalServices.uninstall()
        raise

    def gui_mods_init():
        original()
        _init_offline_mod(mods)

    if MODS_DIRECTORY not in mods.__path__:
        mods.__path__.append(MODS_DIRECTORY)
    personality.guiModsInit = gui_mods_init
    _installed = (personality, original, gui_mods_init, mods, OfflineOptionalServices)
    return True


def uninstall():
    global _installed
    installed, _installed = _installed, None
    if installed is not None:
        personality, original, wrapper, mods, services = installed
        if personality.__dict__.get('guiModsInit') is wrapper:
            personality.guiModsInit = original
        if MODS_DIRECTORY in mods.__path__:
            mods.__path__.remove(MODS_DIRECTORY)
        services.uninstall()
    uninstall_route()


def _init_offline_mod(mods):
    # gui.mods.init rebinds _mods; register beside its discovered modules so
    # the original guiModsFini and sendEvent reach this mod as well.
    registry = mods._mods
    if MOD_MODULE in registry:
        return
    import importlib
    module = importlib.import_module(MOD_MODULE)
    registry[MOD_MODULE] = module
    mods._callModMethod(module, 'init')


def _make_entry_state(login_state, app_state):
    class OfflineEntryState(login_state):
        """The LOGIN space without the retail login view."""
        __slots__ = ()

        def showGUI(self, appFactory, appNS, appState):
            if appState == app_state.INITIALIZED:
                appFactory.attachCursor(appNS)
                self._showDsnDialogIfNeed(appFactory, appNS)

    return OfflineEntryState


def install_route():
    """Replace each stock LoginState transition with the view-less state."""
    global _entry_state_type, _route
    if _route is not None:
        return True
    from gui.app_loader import states
    from gui.app_loader.settings import APP_STATE_ID, GUI_GLOBAL_SPACE_ID
    login_state = states.LoginState
    if _entry_state_type is None or not issubclass(_entry_state_type,
                                                   login_state):
        _entry_state_type = _make_entry_state(login_state, APP_STATE_ID)
    entry_state = _entry_state_type
    start_go_next = states.StartState.__dict__['goNext']
    connection_go_next = states.ConnectionState.__dict__['goNext']

    def start_next(self, ctx):
        state = start_go_next(self, ctx)
        # No intro video: StartState chooses it before the LOGIN space.
        if (type(state) is login_state or
                type(state) is states.IntroVideoState):
            ctx.guiSpaceID = GUI_GLOBAL_SPACE_ID.LOGIN
            return entry_state()
        return state

    def connection_next(self, ctx):
        state = connection_go_next(self, ctx)
        if type(state) is login_state:
            return entry_state()
        return state

    states.StartState.goNext = start_next
    states.ConnectionState.goNext = connection_next
    _route = ((states.StartState, start_go_next, start_next),
              (states.ConnectionState, connection_go_next, connection_next))
    return True


def uninstall_route():
    global _route
    route, _route = _route, None
    for owner, original, installed in route or ():
        if owner.__dict__.get('goNext') is installed:
            owner.goNext = original


def login_space_ready(app_loader):
    """Whether the view-less entry state owns a stable LOGIN space."""
    from gui.app_loader.settings import GUI_GLOBAL_SPACE_ID
    if (_entry_state_type is None or
            app_loader.getSpaceID() != GUI_GLOBAL_SPACE_ID.LOGIN or
            not isinstance(app_loader._AppLoader__state, _entry_state_type)):
        return False
    lobby = app_loader.getDefLobbyApp()
    return lobby is not None and bool(getattr(lobby, 'initialized', True))


def publish_logged_on(manager, login_status):
    """Publish #1513's LOGGED_ON stage without retail WGC or login listeners.

    Exact ConnectionManager.__serverResponseHandler stores the status, calls
    WGC_onServerResponse, onLoggedOn (credential storage) and onConnected.
    Only the status and onConnected have offline consumers.
    """
    manager._ConnectionManager__connectionStatus = login_status.LOGGED_ON
    manager.onConnected()
