#!/usr/bin/env python3
"""Count actual non-flushing lens-query reads, not getVizResults callers."""
import argparse
from pathlib import Path


PATH = 'lib/romp/occlusion_query_helper.cpp'
BEFORE = '''			HRESULT hr = queries_[result]->GetData(
				&results_[result], sizeof(DWORD), 0 );

			resultPending_[result] = ( hr != D3D_OK );'''
AFTER = '''			HRESULT hr;
			{
				PROFILER_SCOPED( OQH_QueryGetData );
				hr = queries_[result]->GetData(
					&results_[result], sizeof(DWORD), 0 );
			}
			if (hr == D3D_OK)
			{
				PROFILER_SCOPED( OQH_QueryReady );
			}
			else if (hr == S_FALSE)
			{
				PROFILER_SCOPED( OQH_QueryNotReady );
			}
			else
			{
				PROFILER_SCOPED( OQH_QueryOtherResult );
			}

			resultPending_[result] = ( hr != D3D_OK );'''
TOKENS = ('OQH_QueryGetData', 'OQH_QueryReady', 'OQH_QueryNotReady', 'OQH_QueryOtherResult')


def patch_source(source):
    if source.count(AFTER) == 1 and all(source.count(token) == 1 for token in TOKENS):
        if source.replace(AFTER, BEFORE, 1).count(BEFORE) == 1:
            return source
    if source.count(BEFORE) != 1 or any(token in source for token in TOKENS):
        raise ValueError('Pinned lens occlusion read seam is partial, duplicated or changed')
    return source.replace(BEFORE, AFTER, 1)


def patch(root):
    path = Path(root) / PATH
    data = path.read_bytes()
    bom = b'\xef\xbb\xbf' if data.startswith(b'\xef\xbb\xbf') else b''
    text = data[len(bom):].decode('utf-8')
    crlf = '\r\n' in text
    if '\r' in text.replace('\r\n', '') or (crlf and text.count('\r\n') != text.count('\n')):
        raise ValueError('Mixed occlusion helper line endings')
    source = text.replace('\r\n', '\n')
    final = patch_source(source)
    if final != source:
        path.write_bytes(bom + final.replace('\n', '\r\n' if crlf else '\n').encode('utf-8'))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('engine_root', type=Path)
    patch(parser.parse_args().engine_root)
