"""Prepare the pinned fork's native source without modifying the input tree."""
import argparse
import json
from pathlib import Path
import shutil
import tempfile

from patch_offline_math_bindings import METHODS, PINNED_COMMIT, SOURCES, patch

NATIVE_MATH_PATH = Path('src/res/scripts/client/gui/mods/offline_lan_0922/native_math.py')
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
        consumer = staged / NATIVE_MATH_PATH
        consumer.parent.mkdir(parents=True)
        consumer.write_text(adapt_native_math((source / NATIVE_MATH_PATH).read_text()),
                            encoding='utf-8')
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
