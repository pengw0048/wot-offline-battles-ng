#!/usr/bin/env python3
"""Compile out the profiler, memory debug and the BW_GUARD stack tracker, as #1513's consumer build does."""
import argparse
from pathlib import Path

PATH = 'lib/cstdmf/config.hpp'
# Each define keeps its FORCE_ENABLE_* switch so a diagnostic build can opt back in.
SEAMS = (
    ('(!CONSUMER_CLIENT_BUILD || FORCE_ENABLE_PROFILER)', '(0 || FORCE_ENABLE_PROFILER)'),
    ('((!defined(MF_SERVER) && !CONSUMER_CLIENT_BUILD) || FORCE_ENABLE_MEMORY_DEBUG)', '(0 || FORCE_ENABLE_MEMORY_DEBUG)'),
    ('(!CONSUMER_CLIENT_BUILD || ENABLE_MEMORY_DEBUG || FORCE_ENABLE_STACK_TRACKER)', '(ENABLE_MEMORY_DEBUG || FORCE_ENABLE_STACK_TRACKER)'),
)


def patch_text(text):
    old = [text.count(before) for before, _ in SEAMS]
    new = [text.count(after) for _, after in SEAMS]
    if all(n == 0 for n in old) and all(n == 1 for n in new):
        return text
    if any(n != 1 for n in old) or any(n != 0 for n in new):
        raise ValueError('Consumer instrumentation seams are partial, duplicated, or changed')
    for before, after in SEAMS:
        text = text.replace(before, after, 1)
    return text


def patch(root):
    path = Path(root) / PATH
    data = path.read_bytes()
    bom = b'\xef\xbb\xbf' if data.startswith(b'\xef\xbb\xbf') else b''
    text = data[len(bom):].decode('utf-8')
    updated = patch_text(text)
    if updated != text:
        path.write_bytes(bom + updated.encode('utf-8'))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('engine_root', type=Path)
    patch(parser.parse_args().engine_root)
