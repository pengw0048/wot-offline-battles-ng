#!/usr/bin/env python3
"""Restore the #1513 AvatarFilter Vector3 sub-filter channels and methods."""
import argparse
from pathlib import Path


def replace_one(source, before, after, path):
    if source.count(after) == 1:
        return source
    if source.count(before) != 1:
        raise ValueError('Unrecognized AvatarFilter vector seam: ' + str(path))
    return source.replace(before, after)


def patch_engine(root):
    root = Path(root)
    updates = []
    path = root / 'client/avatar_filter.hpp'
    source = path.read_text(encoding='utf-8')
    for before, after in [(
            '#include "network/basictypes.hpp"\n',
            '#include "network/basictypes.hpp"\n#include "stock_avatar_vector_filter.hpp"\n'), (
            '\tvoid addCallback( AvatarFilterCallback * pCallback );\n',
            '\tvoid addCallback( AvatarFilterCallback * pCallback );\n\n'
            '\t// #1513 AvatarFilter+0x2f8: the Vector3 channels live with this filter.\n'
            '\tStockAvatarVectorChannels & stockVectorChannels() { return stockVectorChannels_; }\n'), (
            '\tListNode callbackListHead_;\n',
            '\tListNode callbackListHead_;\n\n\tStockAvatarVectorChannels stockVectorChannels_;\n')]:
        source = replace_one(source, before, after, path)
    updates.append((path, source))
    path = root / 'client/py_avatar_filter.hpp'
    source = path.read_text(encoding='utf-8')
    source = replace_one(source, '\tPY_METHOD_DECLARE( py_debugMatrixes );\n',
                         '\tPY_METHOD_DECLARE( py_debugMatrixes );\n\n'
                         '\t// #1513 PySubFilterCollection<PyAvatarFilter> (00626ff0..00626ee0).\n'
                         '\tvoid syncVector3( int id, const Vector3 & value, double time );\n'
                         '\tVector3 getVector3( int id, double time );\n'
                         '\tvoid resetVector3( int id );\n'
                         '\tvoid setInterpolationType( int id, int type );\n'
                         '\tPY_AUTO_METHOD_DECLARE( RETVOID, syncVector3,\n'
                         '\t\tARG( int, ARG( BW::Vector3, OPTARG( double, 0.0, END ) ) ) );\n'
                         '\tPY_AUTO_METHOD_DECLARE( RETDATA, getVector3, ARG( int, OPTARG( double, 0.0, END ) ) );\n'
                         '\tPY_AUTO_METHOD_DECLARE( RETVOID, resetVector3, ARG( int, END ) );\n'
                         '\tPY_AUTO_METHOD_DECLARE( RETVOID, setInterpolationType, ARG( int, ARG( int, END ) ) );\n',
                         path)
    updates.append((path, source))
    path = root / 'client/py_avatar_filter.cpp'
    source = path.read_text(encoding='utf-8')
    source = replace_one(source, '\tPY_METHOD( debugMatrixes )\nPY_END_METHODS()',
                         '\tPY_METHOD( debugMatrixes )\n\tPY_METHOD( syncVector3 )\n\tPY_METHOD( getVector3 )\n'
                         '\tPY_METHOD( resetVector3 )\n\tPY_METHOD( setInterpolationType )\nPY_END_METHODS()', path)
    updates.append((path, source))
    for path, source in updates:
        path.write_text(source, encoding='utf-8')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('engine_root', type=Path)
    patch_engine(parser.parse_args().engine_root)
