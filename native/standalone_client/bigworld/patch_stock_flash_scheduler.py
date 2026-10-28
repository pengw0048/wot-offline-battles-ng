#!/usr/bin/env python3
"""Restore #1513 AVM2 call-frame capacity for authored synchronous scheduler queues."""
import argparse
from pathlib import Path

SOURCE = 'Src/GFx/AS3/AS3_VM.h'
BEFORE = '    //enum {MaxCallStackSize = 1024};\n    enum {MaxCallStackSize = 128};'
AFTER = ('    // #1513 DA09A9: AddFrame raises error 1023 at 1024 live AS frames.\n'
         '    enum {MaxCallStackSize = 1024};')


def patch_source(source):
    if source.count(AFTER) == 1 and not source.count(BEFORE):
        if source.count('enum {MaxCallStackSize =') != 1:
            raise ValueError('Duplicated AVM2 call-frame capacity owner')
        return source
    if source.count(BEFORE) != 1 or AFTER in source or source.count('enum {MaxCallStackSize =') != 2:
        raise ValueError('Pinned AVM2 call-frame capacity seam is partial, duplicated or changed')
    return source.replace(BEFORE, AFTER, 1)


def patch(root):
    path = Path(root) / SOURCE
    source = path.read_text(encoding='utf-8-sig')
    changed = patch_source(source)
    if changed != source:
        path.write_text(changed, encoding='utf-8')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('sdk_root', type=Path)
    patch(parser.parse_args().sdk_root)
