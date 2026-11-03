#!/usr/bin/env python3
"""Install the reviewed primary compiled-model GPU visibility filter."""
import argparse
from pathlib import Path


def replace(source, before, after, label):
    if after in source:
        if source.count(after) != 1:
            raise ValueError('Duplicated scene occlusion seam: ' + label)
        return source
    if source.count(before) != 1:
        raise ValueError('Unrecognized scene occlusion seam: ' + label)
    return source.replace(before, after, 1)


def patch(root):
    root = Path(root)
    header = Path(__file__).with_name('stock_scene_occlusion.hpp').read_bytes()
    path = root / 'client/world_app.cpp'
    source = path.read_text(encoding='utf-8-sig')
    original = source
    source = replace(source, '#include "world_app.hpp"',
        '#include "world_app.hpp"\n#include "stock_scene_occlusion.hpp"', 'primary draw include')
    source = replace(source,
        '        DrawHelpers::drawSceneVisibilitySet( sceneDrawContext,',
        '        filterStockSceneOcclusion(scene, visibleObjects, viewProjectionMat);\n'
        '        DrawHelpers::drawSceneVisibilitySet( sceneDrawContext,', 'frustum candidate consumer')
    boundary = '    rp->endOpaqueDraw();'
    issue = '    if (pSpace && ClientChunkSpaceAdapter::getChunkSpace(pSpace) == NULL) issueStockSceneOcclusion(pSpace->scene());'
    if source.count(boundary) != 1 or source.count(issue) > 1:
        raise ValueError('Unrecognized scene occlusion seam: populated primary depth boundary')
    if issue not in source:
        source = source.replace(boundary,
            '    // Keep the populated opaque/terrain depth bound while issuing bounds queries.\n' +
            issue + '\n' + boundary, 1)
    elif source.index(issue) > source.index(boundary):
        raise ValueError('Scene occlusion query follows its depth boundary')
    # Resolve and validate every source seam before changing either SDK file.
    target = root / 'lib/stock_scene_occlusion.hpp'
    if source != original:
        path.write_text(source, encoding='utf-8')
    if not target.exists() or target.read_bytes() != header:
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(header)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('root', type=Path)
    patch(parser.parse_args().root)
