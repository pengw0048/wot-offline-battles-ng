"""Select authored #1513 sound banks at the native camera-space notification."""
import argparse
from pathlib import Path

PATH = 'client/facade_app.cpp'
BEFORE = '''        lastCameraSpace_ = pSpace;
        enviro.activate();'''
AFTER = '''        lastCameraSpace_ = pSpace;
        setStockAudioSpaceBankSet( enviro.pData() );
        enviro.activate();'''
INCLUDE = '#include "stock_audio.hpp"'


def patch(root):
    path = Path(root) / PATH
    source = path.read_text(encoding='utf-8-sig')
    if source.count(AFTER) != 1:
        if source.count(BEFORE) != 1 or 'setStockAudioSpaceBankSet' in source:
            raise ValueError('Pinned camera-space audio bank seam changed')
        source = source.replace(BEFORE, AFTER, 1)
    if INCLUDE not in source:
        if source.count('#include "pch.hpp"') != 1:
            raise ValueError('Pinned FacadeApp include seam changed')
        source = source.replace('#include "pch.hpp"', '#include "pch.hpp"\n' + INCLUDE, 1)
    elif source.count(INCLUDE) != 1:
        raise ValueError('Duplicate camera-space audio bank include')
    path.write_text(source, encoding='utf-8')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('engine_root', type=Path)
    patch(parser.parse_args().engine_root)
