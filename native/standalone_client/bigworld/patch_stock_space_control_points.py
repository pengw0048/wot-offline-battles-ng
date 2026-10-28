#!/usr/bin/env python3
"""Install the original WTCP2 flag scene owner and team material consumers."""
import argparse
from pathlib import Path


def apply(root):
    root = Path(root)
    here = Path(__file__).resolve().parent
    for name in ('stock_space_control_points.hpp', 'stock_space_control_points.cpp',
                 'stock_space_control_points_data.hpp',
                 'stock_control_point_radius.hpp', 'stock_control_point_radius.cpp'):
        target = root / 'lib' / name
        target.parent.mkdir(parents=True, exist_ok=True)
        source = (here / name).read_bytes()
        if not target.exists() or target.read_bytes() != source:
            target.write_bytes(source)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('engine_root', type=Path)
    apply(parser.parse_args().engine_root)
