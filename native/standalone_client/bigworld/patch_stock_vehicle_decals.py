"""Connect vehicle attachment decals to the real deferred manager lifetime."""
import argparse
from pathlib import Path


def patch(root):
    root = Path(root)
    name = 'lib/moo/deferred_decals_manager.cpp'
    source = (root / name).read_text(encoding='utf-8-sig')
    edits = [
        ('#include "deferred_decals_manager.hpp"', '#include "deferred_decals_manager.hpp"\n#include "stock_vehicle_decals.hpp"'),
        ('DecalsManager::~DecalsManager()\n{', 'DecalsManager::~DecalsManager()\n{\n    finaliseStockVehicleDecals();'),
        ('    return initialiseStockSpaceDecals() && initialiseStockDynamicDecals();',
         '    return initialiseStockSpaceDecals() && initialiseStockDynamicDecals() && initialiseStockVehicleDecals();'),
        ('    setStockDynamicDecalQuality(option);', '    setStockDynamicDecalQuality(option);\n    setStockVehicleDecalsQuality(option);'),
    ]
    for before, after in edits:
        if source.count(after) == 1:
            continue
        if source.count(before) != 1:
            raise ValueError('Pinned vehicle decal owner seam changed: ' + repr(before))
        source = source.replace(before, after, 1)
    header = Path(__file__).with_name('stock_vehicle_decals.hpp').read_bytes()
    (root / name).write_text(source, encoding='utf-8')
    (root / 'lib/stock_vehicle_decals.hpp').write_bytes(header)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('engine_root', type=Path)
    patch(parser.parse_args().engine_root)
