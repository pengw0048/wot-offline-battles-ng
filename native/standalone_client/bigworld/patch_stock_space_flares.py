#!/usr/bin/env python3
"""Install original BWfr2 flares and LensEffect activation consumers."""
import argparse
from pathlib import Path


def apply(root):
    root = Path(root)
    pending = {}

    def edit(relative, old, new, marker=None):
        source = pending.get(relative, (root / relative).read_text(encoding='utf-8-sig'))
        if (marker or new) in source:
            return
        if source.count(old) != 1:
            raise ValueError('Original flare owner anchor changed: ' + relative + ': ' + repr(old))
        pending[relative] = source.replace(old, new, 1)

    header = 'lib/romp/lens_effect.hpp'
    edit(header, '\tfloat visibility() const { return visibility_; }',
         '\tuint32 stockActType() const { return stockActType_; }\n'
         '\tfloat visibility() const { return visibility_; }')
    edit(header, '\tuint32\t\tcolour_;\n\n\tOcclusionLevels',
         '\tuint32\t\tcolour_;\n\tuint32 stockActType_;\n\n\tOcclusionLevels')
    cpp = 'lib/romp/lens_effect.cpp'
    edit(cpp, '\tarea_(),', '\tarea_( 1.f ),')
    edit(cpp, '\tvisibility_( 1.f ),', '\tvisibility_( 0.f ),')
    edit(cpp, '\tcolour_( 0xffffffff ),\n\tclampToFarPlane_',
         '\tcolour_( 0xffffffff ),\n\tstockActType_( 3 ),\n\tclampToFarPlane_')
    edit(cpp, '\tocclusionLevels_.clear();\n\n\tthis->maxDistance_',
         '\tocclusionLevels_.clear();\n\n'
         '\tstockActType_ = pSection->readInt( "actType", stockActType_ );\n\n\tthis->maxDistance_')
    edit(cpp, 'void LensEffect::tick( float /* dTime */, float visibility )\n{\n\tBW_GUARD;',
         'void LensEffect::tick( float /* dTime */, float visibility )\n{\n\tBW_GUARD;\n'
         '\tif (!(stockActType_ & 1)) return;')
    edit(cpp, 'void LensEffect::draw()\n{\n\tBW_GUARD;',
         'void LensEffect::draw()\n{\n\tBW_GUARD;\n\tif (!(stockActType_ & 2)) return;')
    edit(cpp, '\tvisibilityType_ = other.visibilityType_;',
         '\tvisibilityType_ = other.visibilityType_;\n\tstockActType_ = other.stockActType_;')
    here = Path(__file__).resolve().parent
    copied = {root / 'lib' / name: (here / name).read_text() for name in (
        'stock_space_flares.hpp', 'stock_space_flares.cpp', 'stock_space_flares_data.hpp')}
    for relative, source in pending.items():
        (root / relative).write_text(source)
    for path, source in copied.items():
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(source)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('engine_root', type=Path)
    apply(parser.parse_args().engine_root)
