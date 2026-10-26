"""Connect the original vehicle-sticker owner to script and render lifecycles."""
import argparse
from pathlib import Path


def replace(source, anchor, replacement):
    if replacement in source:
        return source
    if source.count(anchor) != 1:
        raise ValueError('Original vehicle-sticker owner anchor changed: ' + repr(anchor))
    return source.replace(anchor, replacement, 1)


def patch(root):
    root = Path(root)
    pending = {}
    for relative, anchor, replacement in (
        ('client/world_app.cpp', '    ForwardDecalsManager::getInstance()->draw();',
         '    ForwardDecalsManager::getInstance()->draw();\n'
         '    if (!drawStockVehicleStickers())\n'
         '        ERROR_MSG("WGStickerModel: original projected sticker draw failed\\n");'),
        ('client/script_app.cpp', '\tBigWorldClientScript::fini();',
         '\tfiniStockVehicleStickers();\n\tBigWorldClientScript::fini();'),
        ('client/script_bigworld.cpp', '\tMath_token |',
         '\tStockVehicleStickers_token |\n\tMath_token |')):
        path = root / relative
        source = path.read_text(encoding='utf-8-sig')
        if '#include "stock_vehicle_stickers.hpp"' not in source:
            source = replace(source, '#include "pch.hpp"',
                             '#include "pch.hpp"\n#include "stock_vehicle_stickers.hpp"')
        pending[path] = replace(source, anchor, replacement)
    header = Path(__file__).with_name('stock_vehicle_stickers.hpp').read_text()
    pending[root / 'lib/stock_vehicle_stickers.hpp'] = header
    for path, source in pending.items():
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(source)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('engine_root', type=Path)
    patch(parser.parse_args().engine_root)
