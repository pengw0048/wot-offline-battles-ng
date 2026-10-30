#!/usr/bin/env python3
"""Route every BigWorld allocation size to the system heap instead of the pooled front end."""
import argparse
import os
from pathlib import Path
import stat
import tempfile

PATH = 'lib/cstdmf/allocator.cpp'
ANCHOR = '#include <time.h>\n'
MARK = '// Every free in the pooled front end scans all pool spans; nedmalloc serves all sizes.'
OVERRIDE = (MARK + '\n#undef ENABLE_FIXED_SIZED_POOL_ALLOCATOR\n'
            '#define ENABLE_FIXED_SIZED_POOL_ALLOCATOR 0\n')


def patch_text(text):
    if text.count(MARK) == 1 and text.count(ANCHOR + '\n' + OVERRIDE) == 1:
        return text
    if text.count(MARK) or text.count(ANCHOR) != 1 or 'ENABLE_FIXED_SIZED_POOL_ALLOCATOR 0' in text:
        raise ValueError('Allocator routing seam is partial, duplicated, or changed')
    result = text.replace(ANCHOR, ANCHOR + '\n' + OVERRIDE, 1)
    if patch_text(result) != result:
        raise ValueError('Allocator routing patch verification failed')
    return result


def patch(root):
    path = Path(root) / PATH
    data = path.read_bytes()
    bom = b'\xef\xbb\xbf' if data.startswith(b'\xef\xbb\xbf') else b''
    text = data[len(bom):].decode('utf-8')
    crlf = text.count('\r\n')
    if (crlf and (crlf != text.count('\n') or crlf != text.count('\r'))) or (not crlf and '\r' in text):
        raise ValueError('Unsupported line endings in ' + PATH)
    newline = '\r\n' if crlf else '\n'
    original = text.replace('\r\n', '\n')
    updated = patch_text(original)
    if updated == original:
        return False
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(dir=path.parent, prefix=path.name + '.',
                                         delete=False) as stream:
            temporary = Path(stream.name)
            stream.write(bom + updated.replace('\n', newline).encode('utf-8'))
        temporary.chmod(stat.S_IMODE(path.stat().st_mode))
        os.replace(temporary, path)
    finally:
        if temporary is not None and temporary.exists():
            temporary.unlink()
    return True


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('engine_root', type=Path)
    patch(parser.parse_args().engine_root)
