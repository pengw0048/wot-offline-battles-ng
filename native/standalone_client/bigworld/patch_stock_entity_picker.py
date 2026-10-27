#!/usr/bin/env python3
"""Use the Python entity wrapper at the public picker's skeleton boundary."""
import argparse
from pathlib import Path


RELATIVE = 'client/entity_picker.cpp'
ORIGINAL = '''\t//use nasty c-style cast to avoid constness
\tPyObject* pyEnt = (PyObject*)(&entity);
\tPyObjectPtr attr = PyObjectPtr(
\t\tPyObject_GetAttrString( pyEnt, "skeletonCollider" ),
\t\tPyObjectPtr::STEAL_REFERENCE );\x20
'''
REPLACEMENT = '''\t// Entity and PyEntity have separate object layouts. Pin the script wrapper
\t// while attribute lookup may re-enter Python and destroy the entity.
\tPyEntityPtr pPyEntity = entity.pPyEntity();
\tif (!pPyEntity || pPyEntity->isDestroyed())
\t{
\t\treturn false;
\t}
\tPyObjectPtr attr = PyObjectPtr(
\t\tPyObject_GetAttrString( pPyEntity.get(), "skeletonCollider" ),
\t\tPyObjectPtr::STEAL_REFERENCE );
'''
ABSENT = '''\tif ( !attr )
\t{
\t\tPyErr_Clear();
\t\treturn true;
\t}

\tif (SkeletonCollider::Check( attr.get() ))'''
LIVE = '''\tif ( !attr )
\t{
\t\tPyErr_Clear();
\t\treturn !pPyEntity->isDestroyed();
\t}

\tif (pPyEntity->isDestroyed())
\t{
\t\treturn false;
\t}

\tif (SkeletonCollider::Check( attr.get() ))'''


def patch(root):
    path = Path(root) / RELATIVE
    source = path.read_text(encoding='utf-8-sig')
    if source.count(REPLACEMENT) == 1 and source.count(LIVE) == 1:
        if ORIGINAL in source or ABSENT in source:
            raise ValueError('Entity picker contains mixed wrapper ownership')
        return
    if source.count(ORIGINAL) != 1 or source.count(ABSENT) != 1:
        raise ValueError('Pinned entity picker skeleton boundary changed')
    source = source.replace(ORIGINAL, REPLACEMENT, 1).replace(ABSENT, LIVE, 1)
    path.write_text(source, encoding='utf-8')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('engine_root', type=Path)
    patch(parser.parse_args().engine_root)
