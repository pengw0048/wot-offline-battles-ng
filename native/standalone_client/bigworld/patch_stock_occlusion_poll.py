#!/usr/bin/env python3
"""Skip the device poll of an occlusion query in the same call that issues it."""
import argparse
import os
from pathlib import Path
import stat
import tempfile

PATH = 'lib/romp/occlusion_query_helper.cpp'
BEFORE = """	//won't have finished this particular one by now,
	//but we check our other frame_lag queries
	this->getVizResults(h);
}"""
AFTER = """	// The query issued above cannot be ready yet; each GetData is a driver
	// round trip, so poll only the older frame_lag queries.
	for ( int f=0; f<numFrames_; f++ )
	{
		uint16 older = resultIndex( h, f );
		if ( older != result && resultPending_[older] )
		{
			hr = queries_[older]->GetData( &results_[older], sizeof(DWORD), 0 );
			resultPending_[older] = ( hr != D3D_OK );
		}
	}
}"""


def patch_text(text):
    if text.count(AFTER) == 1 and BEFORE not in text:
        return text
    if text.count(BEFORE) != 1 or AFTER in text:
        raise ValueError('Occlusion query poll seam is partial, duplicated, or changed')
    return text.replace(BEFORE, AFTER, 1)


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
