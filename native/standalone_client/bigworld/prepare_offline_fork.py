"""Prepare the pinned fork's native source and offline runtime payload.

The input tree is never modified. The offline package is staged complete under
offline-res with only the reviewed independent-client seams adapted.
"""
import argparse
import importlib.util
import json
from pathlib import Path
import shutil
import tempfile

_spec = importlib.util.spec_from_file_location(
    'offline_math_bindings', Path(__file__).with_name('patch_offline_math_bindings.py'))
_bindings = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_bindings)
METHODS, PINNED_COMMIT, SOURCES = _bindings.METHODS, _bindings.PINNED_COMMIT, _bindings.SOURCES
patch = _bindings.patch
_spec = importlib.util.spec_from_file_location(
    'stock_catalog_identity', Path(__file__).with_name('stock_catalog_identity.py'))
stock_catalog_identity = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(stock_catalog_identity)

NATIVE_MATH_PATH = Path('src/res/scripts/client/gui/mods/offline_lan_0922/native_math.py')
INSTANCE_GUARD_PATH = NATIVE_MATH_PATH.with_name('instance_guard.py')
BOOTSTRAP_PATH = NATIVE_MATH_PATH.with_name('bootstrap.py')
COMPAT_PATH = NATIVE_MATH_PATH.with_name('compat.py')
PINNED_RESOURCES = Path('src/res')
RUNTIME_ROOT = Path('offline-res')
ENTRY_SOURCE = Path(__file__).with_name('offline_entry') / 'OfflineGarageEntry.py'
ENTRY_PATH = Path('scripts/client/OfflineGarageEntry.py')
SERVICES_SOURCE = ENTRY_SOURCE.with_name('OfflineOptionalServices.py')
SERVICES_PATH = ENTRY_PATH.with_name(SERVICES_SOURCE.name)
ACCOUNT_ATTEMPT = (Path(__file__).with_name('offline_entry') /
                   'compat_account_attempt.txt')
OLD_LOADER = '''    path = os.path.join(os.path.dirname(os.path.abspath(sys.executable)),
                        'mods', '0.9.22.0.1', MODULE_NAME + '.pyd')
    try:
        import imp
        _backend = imp.load_dynamic(MODULE_NAME, path)
        sys.modules[MODULE_NAME] = _backend
'''
NEW_LOADER = '''    try:
        # The independent EXE publishes its rebuilt module before personality.
        backend = __import__(MODULE_NAME)
        if getattr(backend, '_wot_offline_fork_revision', None) != '%s':
            raise ImportError('The pinned builtin numerical module is unavailable')
        _backend = backend
''' % PINNED_COMMIT


def adapt_native_math(source):
    if source.count(OLD_LOADER) != 1:
        raise ValueError('The pinned numerical consumer loading seam changed')
    return source.replace(OLD_LOADER, NEW_LOADER, 1)


def replace_function(source, name, following, replacement, expected):
    begin = source.index('\ndef ' + name + '(') + 1
    end = source.index('\ndef ' + following + '(', begin) + 1
    original = source[begin:end]
    if any(original.count(fragment) != 1 for fragment in expected):
        raise ValueError('The pinned runtime consumer changed: ' + name)
    return source[:begin] + replacement.rstrip() + '\n\n\n' + source[end:]


def adapt_instance_guard(source):
    source = replace_function(source, '_load_native_bridge', '_install_exception_trail', '''
def _load_native_bridge():
    global _native_bridge
    if _native_bridge is not None:
        return _native_bridge
    bridge = __import__(NATIVE_MODULE_NAME)
    if getattr(bridge, '_wot_offline_fork_revision', None) != '%s':
        raise ImportError('The pinned builtin offline runtime is unavailable')
    for method_name in ('install_exception_trail', 'hide_process_windows',
                        'show_process_windows', 'apply_standard_gameplay_mask',
                        'restore_standard_gameplay_mask'):
        if not callable(getattr(bridge, method_name, None)):
            raise ImportError('The builtin offline runtime is missing ' + method_name)
    # The independent EnviroMinder owns its sky/time settings directly. It has
    # no original shared AtmosphereSupport pointer or executable patch to install.
    _install_exception_trail(bridge)
    _native_bridge = bridge
    return bridge
''' % PINNED_COMMIT, ('bridge = imp_module.load_dynamic(NATIVE_MODULE_NAME, path)',
                     'status = int(bridge.install_atmosphere_owner_guard())'))
    source = replace_function(source, '_release_native', '_window_operation', '''
def _release_native(native_bridge=None):
    bridge = _load_native_bridge() if native_bridge is None else native_bridge
    if getattr(bridge, '_wot_offline_fork_revision', None) != '%s':
        raise ImportError('The pinned builtin offline runtime is unavailable')
    # This application has no WGC owner or client mutex. Acknowledge the
    # requested process policy; do not claim to run the original WGC teardown.
    return True
''' % PINNED_COMMIT, ('status = int(native_bridge.release_client_guard())',))
    before = '"""Tear down #1513\'s WGC client guard once for an opted-in process."""'
    after = '"""Allow requested additional independent clients once per process."""'
    if source.count(before) != 1:
        raise ValueError('The pinned multi-client entry changed')
    return source.replace(before, after, 1)


def adapt_bootstrap(source):
    replacements = (
        ("        # Complete #1513's native WGC teardown before scheduling callbacks;\n"
         "        # the Python callback thread does not own WGC's client mutex.\n",
         "        # Validate the independent runtime before allowing a worker.\n"
         "        # This application does not create the original WGC client guard.\n"),
        ("            '[Offline LAN 0.9.22] released wot_client_mutex for another '\n"
         "            'offline client\\n')",
         "            '[Offline LAN 0.9.22] enabled another independent offline client\\n')"),
        ("'[Offline LAN 0.9.22] client guard release failed: %s\\n'",
         "'[Offline LAN 0.9.22] independent runtime setup failed: %s\\n'"),
        ("        if _client_mode == port_config.SIMULATION_WORKER_MODE:\n"
         "            # The worker needs a native lobby only as a safe map-lifecycle\n"
         "            # bridge. It owns no Battle button, announcement or preference\n"
         "            # profile and therefore cannot mutate the player's UI settings.\n",
         "        # Both roles create a stock lobby. Disable its automatic CN\n"
         "        # announcement before either Account can fire LOBBY_VIEW_LOADED;\n"
         "        # the worker has no browser service or interactive first-run UI.\n"
         "        _install_announcement_ui()\n"
         "        if _client_mode == port_config.SIMULATION_WORKER_MODE:\n"
         "            # The worker owns no Battle button or preference profile.\n"),
        ("            _install_announcement_ui()\n"
         "            _install_lan_session()\n",
         "            _install_lan_session()\n"),
    )
    for before, after in replacements:
        if source.count(before) != 1:
            raise ValueError('The pinned bootstrap process policy changed')
        source = source.replace(before, after, 1)
    # No LoginView exists here: only the view-less entry state may own LOGIN.
    return replace_function(source, '_login_space_is_ready', '_native_lobby_is_ready', '''
def _login_space_is_ready():
    """Whether the independent entry state owns a stable LOGIN space."""
    from gui.app_loader import g_appLoader
    import OfflineGarageEntry
    return OfflineGarageEntry.login_space_ready(g_appLoader)
''', ('if g_appLoader.getSpaceID() != GUI_GLOBAL_SPACE_ID.LOGIN:',
       'lobby = g_appLoader.getDefLobbyApp()'))


OLD_LOGGED_ON = '''            try:
                # The progress callback mutates connection state and invokes
                # arbitrary native listeners.  It belongs to the same
                # transaction as Account construction, not before rollback.
                progress(1, runtime.login_status.LOGGED_ON, '{}')
                compatibility._create_account_player()
'''
NEW_LOGGED_ON = '''            try:
                # No retail login or WGC owner exists here. Publish LOGGED_ON
                # to the retained connection consumers in the same transaction
                # as Account construction, before rollback.
                import OfflineGarageEntry
                OfflineGarageEntry.publish_logged_on(
                    runtime.connection_manager, runtime.login_status)
                compatibility._create_account_player()
'''


def replace_method(source, name, following, replacement, expected):
    begin = source.index('\n    def ' + name + '(self') + 1
    end = source.index('\n    def ' + following + '(self', begin) + 1
    original = source[begin:end]
    if any(original.count(fragment) != 1 for fragment in expected):
        raise ValueError('The pinned runtime consumer changed: ' + name)
    return source[:begin] + replacement + source[end:]


def adapt_compat(source):
    if source.count(OLD_LOGGED_ON) != 1:
        raise ValueError('The pinned offline connection seam changed')
    source = source.replace(OLD_LOGGED_ON, NEW_LOGGED_ON, 1)
    # Root's reviewed rollback: release only what the failed attempt owns.
    return replace_method(
        source, '_discard_partial_account', 'restore_lobby_account',
        ACCOUNT_ATTEMPT.read_text(encoding='utf-8'),
        ('def _discard_partial_account(self):\n',
         'clear_all_spaces()\n',
         'space_id = runtime.bigworld.createSpace()',
         'self._discard_partial_account()\n'))


ADAPTERS = {
    NATIVE_MATH_PATH: adapt_native_math,
    INSTANCE_GUARD_PATH: adapt_instance_guard,
    BOOTSTRAP_PATH: adapt_bootstrap,
    COMPAT_PATH: adapt_compat,
}


def stage_runtime(source, staged):
    """Copy the complete pinned payload and adapt only the reviewed seams."""
    resources = source / PINNED_RESOURCES
    runtime = staged / RUNTIME_ROOT
    files = sorted(path for path in resources.rglob('*') if path.is_file())
    adapted = []
    for path in files:
        if path.suffix.lower() in ('.pyc', '.pyo', '.pyd', '.dll'):
            raise ValueError('The pinned payload contains a binary module: ' + str(path))
        relative = path.relative_to(source)
        output = runtime / path.relative_to(resources)
        output.parent.mkdir(parents=True, exist_ok=True)
        if relative in ADAPTERS:
            # Bytes, so a Windows build keeps the archive's LF line endings.
            output.write_bytes(ADAPTERS[relative](
                path.read_bytes().decode('utf-8')).encode('utf-8'))
            adapted.append(relative.as_posix())
        else:
            shutil.copyfile(path, output)
    if sorted(adapted) != sorted(path.as_posix() for path in ADAPTERS):
        raise ValueError('The pinned payload is missing an adapted consumer')
    for source_path, path in ((ENTRY_SOURCE, ENTRY_PATH), (SERVICES_SOURCE, SERVICES_PATH)):
        if (runtime / path).exists():
            raise ValueError('The pinned payload already owns the independent entry')
        shutil.copyfile(source_path, runtime / path)
    (staged / 'offline-res.json').write_text(json.dumps({
        'revision': PINNED_COMMIT,
        'source': PINNED_RESOURCES.as_posix(),
        'adapted': sorted(adapted),
        'entry': ENTRY_PATH.as_posix(),
        'services': SERVICES_PATH.as_posix(),
        'files': len(files) + 2,
    }, indent=2, sort_keys=True) + '\n')


def prepare(source, destination):
    source, destination = Path(source), Path(destination)
    identity = json.loads((source / 'fork-source.json').read_text(encoding='utf-8-sig'))
    if identity.get('revision') != PINNED_COMMIT:
        raise ValueError('The gameplay source must be the pinned v0.10.1 release')
    with tempfile.TemporaryDirectory(prefix='wot-offline-fork-') as temporary:
        staged = Path(temporary)
        native = staged / 'native'
        native.mkdir()
        for path in (source / 'native').iterdir():
            if path.is_file() and path.suffix in ('.cpp', '.hpp', '.h', '.inc'):
                shutil.copyfile(path, native / path.name)
        for name in SOURCES:
            if not (native / name).is_file():
                raise ValueError('Missing pinned numerical source: ' + name)
        patch(staged)
        stage_runtime(source, staged)
        # Pinned catalogs with #1513 raw WGDE row identities, at mod_dir().
        stock_catalog_identity.stage(source, staged)
        names = '\n'.join('    "' + name + '",' for name in METHODS)
        (staged / 'offline_fork_methods.hpp').write_text(
            '// Generated from the validated v0.10.1 method table.\n'
            'static const char * const OFFLINE_FORK_METHODS[] = {\n' + names + '\n};\n')
        (staged / 'offline_fork_sources.cmake').write_text(
            'set(WOT_OFFLINE_MATH_SOURCES\n' + ''.join(
                '    "${CMAKE_CURRENT_LIST_DIR}/native/' + name + '"\n'
                for name in SOURCES) + ')\n')
        for path in staged.rglob('*'):
            if not path.is_file():
                continue
            output = destination / path.relative_to(staged)
            content = path.read_bytes()
            if not output.exists() or output.read_bytes() != content:
                output.parent.mkdir(parents=True, exist_ok=True)
                output.write_bytes(content)
        # The runtime and catalog trees are shipped whole; drop stale files.
        for published in (destination / RUNTIME_ROOT,
                          destination / stock_catalog_identity.CONFIG_ROOT):
            for path in sorted(published.rglob('*'), reverse=True):
                if not (staged / path.relative_to(destination)).exists():
                    path.unlink() if path.is_file() else path.rmdir()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('destination', type=Path)
    args = parser.parse_args()
    prepare(args.source, args.destination)
