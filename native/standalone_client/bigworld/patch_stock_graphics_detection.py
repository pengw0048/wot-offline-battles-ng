#!/usr/bin/env python3
"""Publish authored detector profiles without changing hardware measurement."""
import argparse
from pathlib import Path


def patch(root):
    root = Path(root)
    path = root / 'lib/moo/graphics_settings_picker.cpp'
    source = path.read_text(encoding='utf-8-sig')
    include = '#include "stock_graphics_detection.hpp"'
    if include not in source:
        anchor = '#include "graphics_settings_picker.hpp"'
        if source.count(anchor) != 1:
            raise ValueError('Original graphics detector include changed')
        source = source.replace(anchor, anchor + '\n' + include, 1)
    publication = ('\tconfigureStockGraphicsDetection( pConfig,\n'
                   '\t\tBWResource::instance().openSection( s_autoConfigSettingsFileName.value() ) );')
    if publication not in source:
        anchor = '\tinitConfig();\n\treturn pConfig.exists();'
        if source.count(anchor) != 1:
            raise ValueError('Original graphics detector profile publication changed')
        source = source.replace(anchor, '\tinitConfig();\n' + publication + '\n\treturn pConfig.exists();', 1)
    application = Path(__file__).resolve().parent
    copies = ((application / 'stock_graphics_detection.hpp', root / 'lib/stock_graphics_detection.hpp'),
              (application.parent / 'include/wot/stock_graphics_detection.hpp', root / 'lib/wot/stock_graphics_detection.hpp'))
    contents = [(destination, original.read_bytes()) for original, destination in copies]
    path.write_text(source, encoding='utf-8')
    for destination, data in contents:
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_bytes(data)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('engine_root', type=Path)
    patch(parser.parse_args().engine_root)
