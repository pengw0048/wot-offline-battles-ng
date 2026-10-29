#!/usr/bin/env python3
"""Keep Hybrid allocation diagnostics available without enabling them by default."""
import argparse
import os
from pathlib import Path
import stat
import tempfile

PATH = 'lib/cstdmf/allocator.cpp'
MARK = '// Allocation diagnostics are opt-in; retain the allocator and BW_GUARD.'
DEFAULT = ('void Allocator::init()\n{\n    if( !s_initialised )\n    {\n'
           '\t\ts_initialised = true;\n\n#if ENABLE_MEMORY_DEBUG\n'
           '\t\tbool disabled = false;')
MODE = ('#endif\n\t\t// check for the callstack mode\n'
        '\t\tif (strstr( commandLine, "-memdebug=disable" ))\n'
        '\t\t{\n\t\t\tdisabled = true;\n\t\t}')
ENABLE = '''#endif
		// Allocation diagnostics are opt-in; retain the allocator and BW_GUARD.
		if (strstr( commandLine, "-memdebug=cs_fast" ) ||
			strstr( commandLine, "-memdebug=cs_complete" ) ||
			strstr( commandLine, "-memdebug=cs_guard" ) ||
			strstr( commandLine, "-memhotspots" ) ||
			strstr( commandLine, "-memreportignoredleaks" ) ||
			strstr( commandLine, "-memdumpstats" ) ||
			strstr( commandLine, "-memreportcachegrind" ) ||
			strstr( commandLine, "-memdebugguard=" ))
		{
			disabled = false;
		}

		// An explicit disable takes precedence over diagnostic options.
		if (strstr( commandLine, "-memdebug=disable" ))
		{
			disabled = true;
		}'''
SEAMS = ((DEFAULT, DEFAULT.replace('disabled = false', 'disabled = true')),
         (MODE, ENABLE))


def _matches(text, final):
    if text.count('void Allocator::init()') != 1 or text.count('void Allocator::fini()') != 1:
        return False
    owner = text[text.index('void Allocator::init()'):text.index('void Allocator::fini()')]
    if any(text.count(pair[bool(final)]) != 1 for pair in SEAMS):
        return False
    if any(owner.count(pair[bool(final)]) != 1 for pair in SEAMS):
        return False
    if text.count(MARK) != int(bool(final)):
        return False
    if final:
        for before, after in SEAMS:
            text = text.replace(after, before, 1)
    return (all(text.count(before) == 1 for before, after in SEAMS) and
            not any(after in text for before, after in SEAMS))


def patch_text(text):
    if _matches(text, True):
        return text
    if not _matches(text, False):
        raise ValueError('Memory diagnostic policy seams are partial, duplicated, or changed')
    for before, after in SEAMS:
        text = text.replace(before, after, 1)
    if not _matches(text, True):
        raise ValueError('Memory diagnostic policy patch did not reach its final state')
    return text


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
