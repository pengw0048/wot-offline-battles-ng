#!/usr/bin/env python3
"""Restore exact #1513 terrain vertex bytes and stream ownership."""
import argparse
from pathlib import Path


FILES = ('lib/terrain/terrain2/terrain_vertex_buffer.cpp',
         'lib/terrain/terrain2/grid_vertex_buffer.cpp',
         'lib/terrain/terrain2/vertex_lod_entry.cpp')


def replace_once(source, before, after):
    if source.count(after) == 1:
        return source
    if source.count(before) != 1:
        raise ValueError('Stock terrain vertex seam changed: ' + before[:90])
    return source.replace(before, after, 1)


def patch(root):
    root = Path(root)
    pending = {name: (root / name).read_text(encoding='utf-8-sig') for name in FILES}
    for name in FILES:
        pending[name] = replace_once(pending[name], '#include "pch.hpp"',
            '#include "pch.hpp"\n#include "stock_terrain_vertices.hpp"')
    name = FILES[0]
    edits = (
        ('sizeof( Vector2 ) * resolutionX * resolutionZ',
         'sizeof( StockTerrainHeightVertex ) * resolutionX * resolutionZ'),
        ('Moo::VertexLock<Vector2> vl( pVertexBuffer_ );',
         'Moo::VertexLock<StockTerrainHeightVertex> vl( pVertexBuffer_ );'),
        ('memcpy( vl, vertices, bufferSize );',
         'for (uint32 index = 0; index < resolutionX * resolutionZ; ++index)\n'
         '\t\t\t\t\tpackStockTerrainHeightVertex( vl[index], vertices[index] );'),
        ('pVertexBuffer_.set( 0, 0, sizeof( Vector2 ) );\n'
         '\t\tpGridBuffer_->pBuffer().set( 1, 0, sizeof( Vector2 ) );',
         'pGridBuffer_->pBuffer().set( 0, 0, sizeof( StockTerrainGridVertex ) );\n'
         '\t\tpVertexBuffer_.set( 1, 0, sizeof( StockTerrainHeightVertex ) );'),
    )
    for before, after in edits:
        pending[name] = replace_once(pending[name], before, after)
    name = FILES[1]
    edits = (
        ('uint32 nVertices = uint32(resolutionX) * uint32(resolutionZ);',
         'uint32 nVertices = uint32(resolutionX) * uint32(resolutionZ);\n'
         '\t\tconst bool morph = stockTerrainGridMorphEnabled( resolutionX, resolutionZ );'),
        ('sizeof( Vector2 ) * nVertices', 'sizeof( StockTerrainGridVertex ) * nVertices'),
        ('Moo::VertexLock<Vector2> vl( pVertexBuffer_ );',
         'Moo::VertexLock<StockTerrainGridVertex> vl( pVertexBuffer_ );'),
        ('vl[index].x = x;\n\t\t\t\t\t\tvl[index].y = z;',
         'packStockTerrainGridVertex( vl[index], x, z, xind, zind, morph );'),
    )
    for before, after in edits:
        pending[name] = replace_once(pending[name], before, after)
    name = FILES[2]
    pending[name] = replace_once(pending[name],
        'sizeof( Vector2 ) * vGridSize * vGridSize;',
        'sizeof( StockTerrainHeightVertex ) * vGridSize * vGridSize;')
    # Validate every seam before writing any generated SDK source.
    for name, source in pending.items():
        if (root / name).read_text(encoding='utf-8-sig') != source:
            (root / name).write_text(source, encoding='utf-8')
    helper = Path(__file__).with_name('stock_terrain_vertices.hpp').read_bytes()
    destination = root / 'lib/terrain/terrain2/stock_terrain_vertices.hpp'
    if not destination.exists() or destination.read_bytes() != helper:
        destination.write_bytes(helper)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('bigworld_root', type=Path)
    patch(parser.parse_args().bigworld_root)
