#!/usr/bin/env python3
"""Restore #1513's nested material override scope on the pinned DrawContext."""
import argparse
from pathlib import Path


def patch_engine(root):
    path = Path(root) / 'lib/moo/draw_context.hpp'
    source = path.read_text(encoding='utf-8')
    changes = [(
        'class BoundingBox;\n', 'class BoundingBox;\nclass PyModel;\n'), (
        '\t\tbool\tisValid() const;',
        '\t\tbool\tisValid() const;\n'
        '\t\tComplexEffectMaterial* stockMaterial( bool skinned ) const\n'
        '\t\t\t{ return materials_[skinned ? 1 : 0].get(); }'), (
        '\tvoid\t\t\tpopOverrideBlock( OverrideBlock* overrideBlock );',
        '\tvoid\t\t\tpopOverrideBlock( OverrideBlock* overrideBlock );\n'
        '\t// #1513 0093e910/0093cf10 preserves an existing override.\n'
        '\tOverrideBlock* stockExchangeOverrideBlock( OverrideBlock* next )\n'
        '\t{ OverrideBlock* previous = activeOverride_; activeOverride_ = next; return previous; }\n'
        '\tconst PyModel* stockApprovedReflectionModel() const { return stockApprovedReflectionModel_; }\n'
        '\tconst PyModel* stockExchangeApprovedReflectionModel( const PyModel* next )\n'
        '\t{ const PyModel* previous = stockApprovedReflectionModel_; stockApprovedReflectionModel_ = next; return previous; }'), (
        '\tOverrideBlock*\t\t\tactiveOverride_;',
        '\tOverrideBlock*\t\t\tactiveOverride_;\n'
        '\tconst PyModel* stockApprovedReflectionModel_ = NULL;')]
    for before, after in changes:
        if source.count(after) == 1:
            continue
        if source.count(before) != 1:
            raise ValueError('Unrecognized pinned DrawContext override seam: ' + str(path))
        source = source.replace(before, after)
    updates = [(path, source)]
    path = Path(root) / 'lib/duplo/pymodel.cpp'
    source = path.read_text(encoding='utf-8')
    start = source.index('void PyModel::draw( Moo::DrawContext& drawContext,')
    end = source.index('\n}\n', start)
    body = source[start:end]
    before = 'if (Moo::rc().reflectionScene())'
    after = 'if (Moo::rc().reflectionScene() &&\n\t\tdrawContext.stockApprovedReflectionModel() != this)'
    if body.count(after) != 1:
        if body.count(before) != 1:
            raise ValueError('Unrecognized pinned PyModel reflection budget seam: ' + str(path))
        source = source[:start] + body.replace(before, after) + source[end:]
    updates.append((path, source))
    path = Path(root) / 'client/script_app.cpp'
    source = path.read_text(encoding='utf-8')
    changes = [(
        '#include "pch.hpp"', '#include "pch.hpp"\n#include "vehicle_appearance.hpp"'), (
        '\tBigWorldClientScript::fini();',
        '\tfiniStockVehicleAppearanceResources();\n\tBigWorldClientScript::fini();')]
    for before, after in changes:
        if source.count(after) == 1:
            continue
        if source.count(before) != 1:
            raise ValueError('Unrecognized pinned ScriptApp teardown seam: ' + str(path))
        source = source.replace(before, after)
    updates.append((path, source))
    for path, source in updates:
        path.write_text(source, encoding='utf-8')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('engine_root', type=Path)
    patch_engine(parser.parse_args().engine_root)
