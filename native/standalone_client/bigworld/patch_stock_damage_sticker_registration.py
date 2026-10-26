"""Retain the real damage-sticker registration module in the client link."""
import argparse
from pathlib import Path


def patch(root):
    root = Path(root)
    path = root / 'client/script_bigworld.cpp'
    source = path.read_text(encoding='utf-8-sig')
    replacements = (
        ('#include "pch.hpp"', '#include "pch.hpp"\n#include "stock_damage_sticker_registration.hpp"',
         '#include "stock_damage_sticker_registration.hpp"'),
        ('\tMath_token |', '\tStockDamageStickerRegistration_token |\n\tMath_token |',
         '\tStockDamageStickerRegistration_token |'))
    for before, after, present in replacements:
        if source.count(present) == 1: continue
        if source.count(before) != 1:
            raise ValueError('Pinned damage-sticker module link seam changed: ' + repr(before))
        source = source.replace(before, after, 1)
    header = Path(__file__).with_name('stock_damage_sticker_registration.hpp').read_bytes()
    path.write_text(source, encoding='utf-8')
    (root / 'lib/stock_damage_sticker_registration.hpp').write_bytes(header)
    print('Original damage-sticker registration module retained')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('engine_root', type=Path)
    patch(parser.parse_args().engine_root)
