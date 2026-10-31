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
    # Each seam is applied once and left alone when an earlier run already applied it.
    for before, after in SEAMS:
        old, new = text.count(before), text.count(after)
        if old == 0 and new == 1:
            continue
        if old != 1 or new != 0:
            raise ValueError('Consumer instrumentation seam is duplicated or changed: ' + before)
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
