"""Attach original dynamic decals after compiled-scene decals in the real GBuffer."""
import argparse
from pathlib import Path


def patch(root):
    root = Path(root)
    pending = {}
    def edit(name, before, after):
        source = pending.get(name, (root / name).read_text(encoding='utf-8-sig'))
        if source.count(after) == 1: return
        if source.count(before) != 1:
            raise ValueError('Pinned dynamic decal seam changed: ' + name + ': ' + repr(before))
        pending[name] = source.replace(before, after, 1)
    manager = 'lib/moo/deferred_decals_manager.cpp'
    edit(manager, '#include "stock_space_decals.hpp"',
         '#include "stock_space_decals.hpp"\n#include "stock_dynamic_decals.hpp"')
    edit(manager, '    finaliseStockSpaceDecals();',
         '    finaliseStockDynamicDecals();\n    finaliseStockSpaceDecals();')
    current = pending.get(manager, (root / manager).read_text(encoding='utf-8-sig'))
    signature = 'bool DecalsManager::init()\n{'
    if current.count(signature) != 1:
        raise ValueError('Pinned dynamic decal init owner changed: ' + manager)
    body = current.split(signature, 1)[1].split('\n}', 1)[0]
    if body.count('initialiseStockDynamicDecals()') != 1:
        edit(manager, '    return initialiseStockSpaceDecals();',
             '    return initialiseStockSpaceDecals() && initialiseStockDynamicDecals();')
    edit(manager, '    setStockSpaceDecalsQuality(option);',
         '    setStockSpaceDecalsQuality(option);\n    setStockDynamicDecalQuality(option);')
    pipeline = 'lib/moo/deferred_pipeline.cpp'
    edit(pipeline, '#include "stock_space_decals.hpp"',
         '#include "stock_space_decals.hpp"\n#include "stock_dynamic_decals.hpp"')
    edit(pipeline, '    m_decalsManager->tick(dt);',
         '    m_decalsManager->tick(dt);\n    tickStockDynamicDecals(dt);')
    header = Path(__file__).with_name('stock_dynamic_decals.hpp').read_bytes()
    for name, source in pending.items(): (root / name).write_text(source, encoding='utf-8')
    (root / 'lib/stock_dynamic_decals.hpp').write_bytes(header)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('engine_root', type=Path)
    patch(parser.parse_args().engine_root)
