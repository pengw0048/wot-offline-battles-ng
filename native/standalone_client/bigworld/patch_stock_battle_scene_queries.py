#!/usr/bin/env python3
"""Declare the #1513 alpha-aware Water segment query used by wg_collideWater."""
import argparse
from pathlib import Path

DECLARATION = ('\tfloat stockWaterRayDistance(const Vector3& start, const Vector3& end, '
               'bool edgeAlpha) const;')
ANCHOR = '\tbool visible() const { return visible_; }'


def apply(root):
    path = Path(root) / 'lib/romp/water.hpp'
    source = path.read_text(encoding='utf-8-sig')
    if source.count(DECLARATION) == 1:
        return
    if DECLARATION in source or source.count(ANCHOR) != 1:
        raise ValueError('Original Water visibility owner anchor changed')
    path.write_text(source.replace(ANCHOR, DECLARATION + '\n' + ANCHOR, 1))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('engine_root', type=Path)
    apply(parser.parse_args().engine_root)
