#!/usr/bin/env python3
"""Preserve an enclosing SDK scope's frame-behind warning policy."""
import argparse
import os
from pathlib import Path
import stat
import tempfile

PATH = 'lib/duplo/pymodelnode.hpp'
ORIGINAL = '''class ScopedDisableFrameBehindWarning
{
public:
	ScopedDisableFrameBehindWarning()
	{
		PyModelNode::disableFrameBehindWarning( true );
	}

	~ScopedDisableFrameBehindWarning()
	{
		PyModelNode::disableFrameBehindWarning( false );
	}
};'''
PATCHED = '''class ScopedDisableFrameBehindWarning
{
public:
	ScopedDisableFrameBehindWarning() :
		previous_( PyModelNode::disableFrameBehindWarning() )
	{
		PyModelNode::disableFrameBehindWarning( true );
	}

	~ScopedDisableFrameBehindWarning()
	{
		PyModelNode::disableFrameBehindWarning( previous_ );
	}

private:
	bool previous_;
};'''


def patch_text(text):
    # The friend declaration also names this class; match the definition only.
    if text.count('class ScopedDisableFrameBehindWarning\n{') != 1:
        raise ValueError('Frame-behind guard definition is missing or duplicated')
    if text.count(PATCHED) == 1 and ORIGINAL not in text:
        return text
    if text.count(ORIGINAL) != 1 or PATCHED in text:
        raise ValueError('Frame-behind guard seam is partial or changed')
    return text.replace(ORIGINAL, PATCHED, 1)


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
