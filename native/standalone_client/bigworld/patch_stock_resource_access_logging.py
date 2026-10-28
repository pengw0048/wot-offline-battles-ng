"""Keep per-resource main-thread access tracing opt-in during normal rendering."""
import argparse
from pathlib import Path

BEFORE = '''    // Only care on warning about loading of files
    // in the main thread if we are drawing the world
    BWResource::watchAccessFromCallingThread(gWorldDrawEnabled);'''
AFTER = '''    // File-access tracing is an explicit diagnostic. Enabling it here
    // logs every package lookup on the render thread, including expected
    // misses, and can turn first-use resource loads into visible stalls.
    // Keep the BWResource tracing API available without arming it per frame.'''


def patch(root):
    path = Path(root) / 'client/finale_app.cpp'
    source = path.read_text(encoding='utf-8-sig')
    if AFTER in source:
        return
    if source.count(BEFORE) != 1:
        raise ValueError('The pinned FinaleApp file-access tracing seam changed')
    path.write_text(source.replace(BEFORE, AFTER, 1), encoding='utf-8')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('engine_root', type=Path)
    patch(parser.parse_args().engine_root)
