"""Prepare the pinned fork's native source and offline runtime payload.

The input tree is never modified. The offline package is staged complete under
offline-res with only the reviewed independent-client seams adapted.
"""
import argparse
import re
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
_spec = importlib.util.spec_from_file_location(
    'patch_stock_worker_runtime', Path(__file__).with_name('patch_stock_worker_runtime.py'))
worker_runtime = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(worker_runtime)
_spec = importlib.util.spec_from_file_location(
    'stock_snapshot_bindings', Path(__file__).with_name('stock_snapshot_bindings.py'))
snapshot_bindings = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(snapshot_bindings)
_spec = importlib.util.spec_from_file_location(
    'stock_native_client_replica_bindings', Path(__file__).with_name('stock_native_client_replica_bindings.py'))
replica_bindings = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(replica_bindings)
_spec = importlib.util.spec_from_file_location(
    'stock_native_client_query_bindings', Path(__file__).with_name('stock_native_client_query_bindings.py'))
client_query_bindings = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(client_query_bindings)
_spec = importlib.util.spec_from_file_location(
    'stock_native_client_transport_bindings', Path(__file__).with_name('stock_native_client_transport_bindings.py'))
transport_bindings = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(transport_bindings)
_spec = importlib.util.spec_from_file_location(
    'stock_native_client_wire_bindings', Path(__file__).with_name('stock_native_client_wire_bindings.py'))
wire_bindings = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(wire_bindings)
_spec = importlib.util.spec_from_file_location(
    'stock_native_client_environment_bindings', Path(__file__).with_name('stock_native_client_environment_bindings.py'))
client_environment_bindings = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(client_environment_bindings)
_spec = importlib.util.spec_from_file_location(
    'stock_native_client_input_bindings', Path(__file__).with_name('stock_native_client_input_bindings.py'))
client_input_bindings = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(client_input_bindings)
_spec = importlib.util.spec_from_file_location(
    'stock_native_client_weapon_runtime_bindings', Path(__file__).with_name('stock_native_client_weapon_runtime_bindings.py'))
client_weapon_bindings = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(client_weapon_bindings)
_spec = importlib.util.spec_from_file_location(
    'stock_native_client_projectile_bindings', Path(__file__).with_name('stock_native_client_projectile_bindings.py'))
client_projectile_bindings = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(client_projectile_bindings)
_spec = importlib.util.spec_from_file_location(
    'stock_native_client_vehicle_hooks_bindings', Path(__file__).with_name('stock_native_client_vehicle_hooks_bindings.py'))
client_vehicle_hooks_bindings = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(client_vehicle_hooks_bindings)
_spec = importlib.util.spec_from_file_location(
    'stock_native_client_critical_bindings', Path(__file__).with_name('stock_native_client_critical_bindings.py'))
client_critical_bindings = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(client_critical_bindings)
_spec = importlib.util.spec_from_file_location(
    'stock_native_client_lifecycle_bindings', Path(__file__).with_name('stock_native_client_lifecycle_bindings.py'))
client_lifecycle_bindings = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(client_lifecycle_bindings)

_spec = importlib.util.spec_from_file_location(
    'stock_native_battle_shell', Path(__file__).with_name('stock_native_battle_shell.py'))
battle_shell = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(battle_shell)

NATIVE_MATH_PATH = Path('src/res/scripts/client/gui/mods/offline_lan_0922/native_math.py')
INSTANCE_GUARD_PATH = NATIVE_MATH_PATH.with_name('instance_guard.py')
BOOTSTRAP_PATH = NATIVE_MATH_PATH.with_name('bootstrap.py')
COMPAT_PATH = NATIVE_MATH_PATH.with_name('compat.py')
WORKER_PRESENTATION_PATH = NATIVE_MATH_PATH.with_name('worker_presentation.py')
PINNED_RESOURCES = Path('src/res')
RUNTIME_ROOT = Path('offline-res')
ENTRY_SOURCE = Path(__file__).with_name('offline_entry') / 'OfflineGarageEntry.py'
ENTRY_PATH = Path('scripts/client/OfflineGarageEntry.py')
SERVICES_SOURCE = ENTRY_SOURCE.with_name('OfflineOptionalServices.py')
SERVICES_PATH = ENTRY_PATH.with_name(SERVICES_SOURCE.name)
NATIVE_MODULE_ROOT = Path('scripts/client/gui/mods/offline_lan_0922')
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
    COMPAT_PATH: lambda source: battle_shell.compat(client_vehicle_hooks_bindings.compat(
        client_query_bindings.compat(adapt_compat(source)))),
    # The independent EXE never shows its worker; see patch_stock_worker_runtime.py.
    WORKER_PRESENTATION_PATH: worker_runtime.adapt_worker_presentation,
}
ADAPTERS[NATIVE_MATH_PATH.with_name('battle_runtime.py')] = battle_shell.battle
ADAPTERS[NATIVE_MATH_PATH.with_name('authority_worker.py')] = battle_shell.worker
ADAPTERS[NATIVE_MATH_PATH.with_name('engine_audio.py')] = battle_shell.engine_audio
ADAPTERS[NATIVE_MATH_PATH.with_name('snapshot_delta.py')] = (
    lambda source: transport_bindings.delta(snapshot_bindings.delta(source)))
ADAPTERS[NATIVE_MATH_PATH.with_name('lan_client.py')] = (
    lambda source: client_lifecycle_bindings.lan_client(
        wire_bindings.client(transport_bindings.client(snapshot_bindings.client(source)))))
ADAPTERS[NATIVE_MATH_PATH.with_name('snapshot_sync.py')] = replica_bindings.snapshot
ADAPTERS[NATIVE_MATH_PATH.with_name('offline_replay.py')] = (
    lambda source: client_lifecycle_bindings.offline_replay(wire_bindings.offline_replay(source)))
ADAPTERS[NATIVE_MATH_PATH.with_name('replay_transport.py')] = wire_bindings.replay_transport
# The native battle world owns the destructible space, catalog and detached turrets, and the native
# Bot authority owns the round start; these pinned battle-only modules and their battle-only
# importers are not shipped. Native replay owners also replace the process-backed
# recorder and reader modules.
RETIRED = frozenset(NATIVE_MATH_PATH.parent / name for name in (
    'destructibles_sensor.py', 'destructibles_authority.py', 'destructibles_compat.py',
    'prebaked_destructibles.py', 'native_destructibles.py', 'destructible_layout.py',
    'entities/detached_turret.py', 'entities/turret_obstacles.py', 'rigid_turret.py',
    'entities/native_remote_vehicle.py',
    'world_collision.py', 'native_world.py', 'native_engine_query.py', 'native_motion_core.py',
    'native_navigation_query.py',
    'bot_runtime.py', 'native_simulation.py', 'native_control_core.py', 'native_weapons.py',
    'native_navigation.py', 'native_visibility.py', 'ai/adapter.py', 'ai/driver.py', 'ai/navigation.py',
    'ai/planner.py', 'ai/traffic.py', 'bot_tactics_runtime.py', 'initial_allocation.py', 'spg_positions.py',
    'ballistics.py', 'battle_visibility.py', 'collision_flags.py', 'critical_statistics.py',
    'environment_push.py', 'gun_pitch_limits.py', 'hull_aiming.py', 'radio.py', 'artillery_controller.py',
    'native_navigation_core.py', 'replay_process.py', 'replay_reader.py',
    'projectile_manager.py', 'projectile_runtime.py'))


def retired_imports(path, source):
    """Module-level imports of a retired module in one shipped (Python 2.7) file."""
    names = set(retired.stem for retired in RETIRED)
    found = []
    for statement in re.findall(r'^(?:import|from) [^\n(]*(?:\([^)]*\))?', source, re.M):
        found.extend(name for name in re.findall(r'\w+', statement) if name in names)
    return found


def stage_runtime(source, staged):
    """Copy the complete pinned payload and adapt only the reviewed seams."""
    resources = source / PINNED_RESOURCES
    runtime = staged / RUNTIME_ROOT
    files = sorted(path for path in resources.rglob('*') if path.is_file())
    adapted = []
    missing = [path for path in RETIRED if not (source / path).is_file()]
    if missing:
        raise ValueError('A retired pinned module is missing: ' + str(missing[0]))
    for path in files:
        if path.suffix.lower() in ('.pyc', '.pyo', '.pyd', '.dll'):
            raise ValueError('The pinned payload contains a binary module: ' + str(path))
        relative = path.relative_to(source)
        if relative in RETIRED:
            continue
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
    for path in sorted(runtime.rglob('*.py')):
        imported = retired_imports(path, path.read_text(encoding='utf-8'))
        if imported:
            raise ValueError('A shipped module imports a retired one: %s -> %s' % (path, imported[0]))
    for source_path, path in ((ENTRY_SOURCE, ENTRY_PATH), (SERVICES_SOURCE, SERVICES_PATH)):
        if (runtime / path).exists():
            raise ValueError('The pinned payload already owns the independent entry')
        shutil.copyfile(source_path, runtime / path)
    native_modules = (
        ('stock_snapshot_static.py', 'stock_snapshot_static.py'),
        ('stock_native_replay.py', 'native_replay.py'),
        ('stock_native_client_query.py', 'native_client_query.py'),
    )
    for source_name, module_name in native_modules:
        output = runtime / NATIVE_MODULE_ROOT / module_name
        if output.exists():
            raise ValueError('The pinned payload already owns the native Bot entry')
        shutil.copyfile(Path(__file__).with_name(source_name), output)
    (staged / 'offline-res.json').write_text(json.dumps({
        'revision': PINNED_COMMIT,
        'source': PINNED_RESOURCES.as_posix(),
        'adapted': sorted(adapted),
        'entry': ENTRY_PATH.as_posix(),
        'services': SERVICES_PATH.as_posix(),
        'files': len(files) - len(RETIRED) + 2 + len(native_modules),
    }, indent=2, sort_keys=True) + '\n')


def stage_native_drive_law(native):
    """Expose the fixed release's pure drive law without its Python binding."""
    source = (native / 'offline_release_drive_python.inc').read_text()
    marker = 'PyObject *WOT_CDECL release_drive(PyObject *,PyObject *args) {'
    if source.count(marker) != 1 or source.count('namespace release_drive_law {') != 1:
        raise ValueError('Pinned release drive law boundary changed')
    law = source.split(marker, 1)[0]
    (native / 'offline_release_drive_law.hpp').write_text(
        '#pragma once\n#include <algorithm>\n#include <cmath>\n' + law)


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
        patch(staged, rotation_query=True)
        stage_native_drive_law(native)
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
        # Retire the exact generated sidecar from older prepared outputs. The
        # native recorder and reader do not load a Python process runtime.
        old_replay = destination / 'mods/0.9.22.0.1/org.colorfulmeans.offline_lan_0922_0.10.1.wotmod'
        if old_replay.is_file():
            old_replay.unlink()
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
