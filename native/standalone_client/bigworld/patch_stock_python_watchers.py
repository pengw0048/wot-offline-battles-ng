#!/usr/bin/env python3
"""Give the BigWorld Python watcher functions #1513's consumer-build contract."""
import argparse
from pathlib import Path

PATH = 'lib/pyscript/pywatcher.cpp'
SPLIT = '\n#else // ENABLE_WATCHERS\n'
# The ENABLE_WATCHERS registrations that #1513 does not expose.
REGISTRATIONS = (
    'PY_MODULE_FUNCTION( setWatcher, BigWorld )\n',
    'PY_MODULE_FUNCTION( getWatcher, BigWorld )\n',
    'PY_MODULE_FUNCTION( getWatcherDir, BigWorld )\n',
    'PY_MODULE_FUNCTION( addWatcher, BigWorld )\n',
    'PY_MODULE_FUNCTION( delWatcher, BigWorld )                                      \n',
)
NAMES = ('setWatcher', 'getWatcher', 'getWatcherDir', 'addWatcher', 'delWatcher')
ANCHOR = '\nBW_END_NAMESPACE\n'
BLOCK = (
    '\n// #1513 registers these BigWorld functions to one METH_VARARGS function\n'
    '// (010c6d60) that returns None without reading its arguments. C++ watchers\n'
    '// and their diagnostics stay enabled.\n'
    'static PyObject * py_consumerWatcherFunction( PyObject *, PyObject * )\n'
    '{\n'
    '\tPy_RETURN_NONE;\n'
    '}\n' + ''.join(
        'static PyModuleMethodLink s_link_py_%s(\n'
        '\t"BigWorld", "%s", py_consumerWatcherFunction, NULL );\n' % (name, name)
        for name in NAMES))


def decode(data):
    """Return (text with LF newlines, BOM, newline) or fail closed."""
    bom = b'\xef\xbb\xbf' if data.startswith(b'\xef\xbb\xbf') else b''
    text = data[len(bom):].decode('utf-8')
    crlf = text.count('\r\n')
    if (crlf and (crlf != text.count('\n') or crlf != text.count('\r'))) or (not crlf and '\r' in text):
        raise ValueError('Unsupported line endings in ' + PATH)
    return text.replace('\r\n', '\n'), bom, '\r\n' if crlf else '\n'


def state(text):
    """Return 'pristine', 'final' or None for a partial, duplicated or changed seam."""
    if text.count(SPLIT) != 1:
        return None
    enabled = text.split(SPLIT)[0]
    if not enabled.endswith(ANCHOR) or enabled.count(ANCHOR) < 1:
        return None
    counts = [enabled.count(line) for line in REGISTRATIONS]
    blocks = text.count(BLOCK)
    if counts == [1] * len(REGISTRATIONS) and blocks == 0 and 'py_consumerWatcherFunction' not in text:
        return 'pristine'
    if counts == [0] * len(REGISTRATIONS) and blocks == 1 and \
            enabled.endswith(BLOCK + ANCHOR) and text.count('py_consumerWatcherFunction') == 1 + len(NAMES):
        return 'final'
    return None


def patch_text(text):
    current = state(text)
    if current == 'final':
        return text
    if current != 'pristine':
        raise ValueError('BigWorld Python watcher seam is partial, duplicated or changed')
    enabled, disabled = text.split(SPLIT)
    for line in REGISTRATIONS:
        enabled = enabled.replace(line, '', 1)
    enabled = enabled[:-len(ANCHOR)] + BLOCK + ANCHOR
    result = enabled + SPLIT + disabled
    if state(result) != 'final':
        raise ValueError('BigWorld Python watcher patch did not reach its final state')
    return result


def patch(root):
    path = Path(root) / PATH
    data = path.read_bytes()
    text, bom, newline = decode(data)
    patched = patch_text(text)
    if patched != text:
        path.write_bytes(bom + patched.replace('\n', newline).encode('utf-8'))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('engine_root', type=Path)
    patch(parser.parse_args().engine_root)
