"""Prepare the pinned fork's native source without modifying the input tree."""
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

NATIVE_MATH_PATH = Path('src/res/scripts/client/gui/mods/offline_lan_0922/native_math.py')
INSTANCE_GUARD_PATH = NATIVE_MATH_PATH.with_name('instance_guard.py')
BOOTSTRAP_PATH = NATIVE_MATH_PATH.with_name('bootstrap.py')
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
    )
    for before, after in replacements:
        if source.count(before) != 1:
            raise ValueError('The pinned bootstrap process policy changed')
        source = source.replace(before, after, 1)
    return source


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
        for relative, adapt in ((NATIVE_MATH_PATH, adapt_native_math),
                                (INSTANCE_GUARD_PATH, adapt_instance_guard),
                                (BOOTSTRAP_PATH, adapt_bootstrap)):
            consumer = staged / relative
            consumer.parent.mkdir(parents=True, exist_ok=True)
            consumer.write_text(adapt((source / relative).read_text()), encoding='utf-8')
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


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('destination', type=Path)
    args = parser.parse_args()
    prepare(args.source, args.destination)
