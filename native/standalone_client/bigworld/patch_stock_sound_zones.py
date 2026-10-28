"""Connect original sound zones to native mapping and script lifetimes.

Run after patch_stock_space_lifecycle.py. Its publication/cleared-space guard
must enclose the mapping callback and UDO constructors below.
"""
import argparse
from pathlib import Path


def once(source, before, after, marker=None):
    marker = marker or after
    if source.count(marker) == 1:
        return source
    if marker in source or source.count(before) != 1:
        raise ValueError('Sound-zone lifecycle seam changed: ' + repr(before))
    return source.replace(before, after, 1)


def patch(root):
    root = Path(root)
    pending = []
    for relative in ('lib/compiled_space/compiled_space_mapping.cpp',
                     'client/script_bigworld.cpp', 'client/script_app.cpp'):
        path = root / relative
        source = path.read_text(encoding='utf-8-sig')
        source = once(source, '#include "pch.hpp"',
            '#include "pch.hpp"\n#include "stock_sound_zones.hpp"',
            '#include "stock_sound_zones.hpp"')
        if relative.endswith('compiled_space_mapping.cpp'):
            before = 'space_.mappingLoaded( this );'
            source = once(source, before,
                'stockSoundZonesMappingLoaded( path_ );\n\t\t\t' + before,
                'stockSoundZonesMappingLoaded( path_ );')
        elif relative.endswith('script_bigworld.cpp'):
            source = once(source, '\tMath_token |', '\tStockSoundZones_token |\n\tMath_token |',
                          '\tStockSoundZones_token |')
        else:
            source = once(source, '\tBigWorldClientScript::fini();',
                '\tfiniStockSoundZones();\n\tBigWorldClientScript::fini();',
                '\tfiniStockSoundZones();')
        pending.append((path, source))
    header = Path(__file__).with_name('stock_sound_zones.hpp').read_bytes()
    for path, source in pending:
        path.write_text(source, encoding='utf-8')
    (root / 'lib/stock_sound_zones.hpp').write_bytes(header)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('engine_root', type=Path)
    patch(parser.parse_args().engine_root)
