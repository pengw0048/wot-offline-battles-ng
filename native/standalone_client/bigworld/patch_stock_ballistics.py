#!/usr/bin/env python3
"""Tick the #1513 PyBallisticsSimulator owners once per world frame."""

import argparse
from pathlib import Path

INCLUDE = ('#include "pch.hpp"',
           '#include "pch.hpp"\n#include "stock_ballistics_simulator.hpp"')
TICK = ('        GlobalEmbodiments::tick( dGameTime );\n',
        '        GlobalEmbodiments::tick( dGameTime );\n'
        '        tickStockBallistics();\n')


def patch_engine(root):
    path = Path(root) / 'client' / 'world_app.cpp'
    source = path.read_text()
    for before, after in (INCLUDE, TICK):
        if after in source:
            continue
        if source.count(before) != 1:
            raise ValueError('Pinned ballistics tick anchor changed: ' + path.name)
        source = source.replace(before, after, 1)
    path.write_text(source)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('engine_root', type=Path)
    patch_engine(parser.parse_args().engine_root)
