#!/usr/bin/env python3
"""Match #1513's engine policy by leaving Python profiling opt-in."""
import argparse
from pathlib import Path

PATH = 'lib/pyscript/script.cpp'
INSTALL = 'PyEval_SetProfile( &profileFunc, NULL );'
MARK = '/* #1513 does not install a Python profile hook here. */'
THREAD = ('void __onThreadStart( const BW::string & threadname )\n'
          '{\n#if ENABLE_PROFILER\n\tg_profiler.addThisThread( threadname.c_str() );\n'
          '#endif\n#if ENABLE_STACK_TRACKER || ENABLE_PROFILER\n\t' + INSTALL + '\n#endif\n}')
INIT = ('\t#if ENABLE_STACK_TRACKER || ENABLE_PROFILER\n\t\t' + INSTALL +
        '\n\t#endif\n\n\t// Run any init time jobs')
SEAMS = (THREAD, INIT)


def patch_text(text):
    old_counts = [text.count(s) for s in SEAMS]
    new_seams = tuple(s.replace(INSTALL, MARK) for s in SEAMS)
    new_counts = [text.count(s) for s in new_seams]
    if old_counts == [0, 0] and new_counts == [1, 1] and text.count(INSTALL) == 0 and text.count(MARK) == 2:
        return text
    if old_counts != [1, 1] or new_counts != [0, 0] or text.count(INSTALL) != 2 or text.count(MARK) != 0:
        raise ValueError('Python profile installation seams are partial, duplicated, or changed')
    result = text
    for old, new in zip(SEAMS, new_seams):
        result = result.replace(old, new, 1)
    if patch_text(result) != result:
        raise ValueError('Python profile installation patch verification failed')
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
    if updated != original:
        path.write_bytes(bom + updated.replace('\n', newline).encode('utf-8'))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('engine_root', type=Path)
    patch(parser.parse_args().engine_root)
