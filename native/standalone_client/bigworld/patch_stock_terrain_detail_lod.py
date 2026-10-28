#!/usr/bin/env python3
"""Preserve authored terrain height LOD across detail-resource evaluation."""
import argparse
from pathlib import Path


FILES = ('lib/terrain/terrain2/terrain_block2.cpp',
         'lib/terrain/terrain2/terrain_block2.hpp')


def replace_once(source, before, after):
    if source.count(after) == 1:
        return source
    if source.count(before) != 1:
        raise ValueError('Stock terrain detail LOD seam changed: ' + before[:90])
    return source.replace(before, after, 1)


def patch(root):
    root = Path(root)
    pending = {name: (root / name).read_text(encoding='utf-8-sig') for name in FILES}
    name = FILES[0]
    edits = (
        ('forcedLod_( - 1 ),',
         'forcedLod_( - 1 ),\n\tforcedHeightMapLod_( -1 ),'),
        ('#if !defined(EDITOR_ENABLED)\n\tconst uint32 detailedHeightMapLod =',
         '// Keep the authored height-resource LOD separate from the clamped render LOD.\n'
         '\tforcedHeightMapLod_ = forcedLod;\n\n'
         '#if !defined(EDITOR_ENABLED)\n\tconst uint32 detailedHeightMapLod ='),
        ('distanceInfo_.minDistanceToCamera_,\n\t\t\t\t\t\t\t\t\tsettings_->topVertexLod());',
         'distanceInfo_.minDistanceToCamera_,\n'
         '#if !defined(EDITOR_ENABLED)\n'
         '\t\t\t\t\t\t\t\t\tforcedHeightMapLod_ < 0 ? settings_->topVertexLod() :\n'
         '\t\t\t\t\t\t\t\t\t\tstatic_cast<uint32>( forcedHeightMapLod_ ));\n'
         '#else\n'
         '\t\t\t\t\t\t\t\t\tsettings_->topVertexLod());\n'
         '#endif'),
    )
    for before, after in edits:
        pending[name] = replace_once(pending[name], before, after)
    name = FILES[1]
    pending[name] = replace_once(pending[name],
        'int\t\t\t\t\t\t\tforcedLod_;',
        'int\t\t\t\t\t\t\tforcedLod_;\n\t\tint\t\t\t\t\t\t\tforcedHeightMapLod_;')
    # Validate all seams before replacing either generated SDK source.
    for name, source in pending.items():
        if (root / name).read_text(encoding='utf-8-sig') != source:
            (root / name).write_text(source, encoding='utf-8')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('bigworld_root', type=Path)
    patch(parser.parse_args().bigworld_root)
