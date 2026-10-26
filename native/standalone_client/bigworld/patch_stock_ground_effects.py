#!/usr/bin/env python3
"""Install the original ground-effect resource and airstrike runtime owners."""
import argparse
from pathlib import Path


def replace(source, anchor, replacement):
    if replacement in source:
        return source
    if source.count(anchor) != 1:
        raise ValueError('Original ground-effect lifecycle anchor changed: ' + repr(anchor))
    return source.replace(anchor, replacement, 1)


def apply(root):
    root = Path(root)
    pending = {}
    path = root / 'lib/romp/water.hpp'
    source = path.read_text(encoding='utf-8-sig')
    declaration = '\tfloat stockGroundRayDistance(const Vector3& start, const Vector3& end) const;'
    if declaration not in source:
        anchor = '\tbool visible() const { return visible_; }'
        if source.count(anchor) != 1:
            raise ValueError('Original Water visibility owner anchor changed')
        source = source.replace(anchor, declaration + '\n' + anchor, 1)
    here = Path(__file__).resolve().parent
    copied = {root / 'lib' / name: (here / name).read_text() for name in (
        'stock_ground_effects.hpp', 'stock_ground_effects.cpp', 'stock_ground_effects_data.hpp')}
    pending[path] = source
    for relative, anchor, replacement in (
        ('client/world_app.cpp', '        tickStockTPSRuntime( dGameTime );',
         '        tickStockGroundEffects( dGameTime );\n        tickStockTPSRuntime( dGameTime );'),
        ('client/script_app.cpp', '\tfiniStockTPSRuntime();',
         '\tfiniStockGroundEffects();\n\tfiniStockTPSRuntime();'),
        ('client/script_bigworld.cpp', '\tMath_token |',
         '\tStockGroundEffects_token |\n\tMath_token |')):
        path = root / relative
        source = path.read_text(encoding='utf-8-sig')
        if '#include "stock_ground_effects.hpp"' not in source:
            source = replace(source, '#include "pch.hpp"',
                             '#include "pch.hpp"\n#include "stock_ground_effects.hpp"')
        pending[path] = replace(source, anchor, replacement)
    pending.update(copied)
    for path, source in pending.items():
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(source)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('engine_root', type=Path)
    apply(parser.parse_args().engine_root)
