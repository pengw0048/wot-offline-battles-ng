#!/usr/bin/env python3
"""Replace the SDK sun splodge with the exact #1513 forward attachment contract."""
import argparse
from pathlib import Path

HEADER = 'lib/duplo/py_splodge.hpp'
SOURCE = 'lib/duplo/py_splodge.cpp'
OLD_CLASS = '''PySplodge( Vector3 bbSize, PyTypeObject * pType = &s_type_ );'''
NEW_CLASS = '''PySplodge( const Matrix & transform, float maxLod, float heightOffset,
        PyTypeObject * pType = &s_type_ );
    ~PySplodge();
    void detach() override;
    void leaveWorld() override;
    void worldBoundingBox( BoundingBox & bounds, const Matrix & world, bool skinny = false ) override;
    void worldVisibilityBox( BoundingBox & bounds, const Matrix & world, bool skinny = false ) override;'''
OLD_FACTORY = '''static PySplodge * New( Vector3 bbSize );
\tPY_AUTO_FACTORY_DECLARE( PySplodge, OPTARG( Vector3, Vector3(0,0,0), END ) )'''
NEW_FACTORY = '''static PyObject * New( const Matrix & transform, float maxLod, float heightOffset );
    PY_AUTO_FACTORY_DECLARE( PySplodge, ARG( Matrix,
        OPTARG( float, 50.f, OPTARG( float, 1.f, END ) ) ) )'''
NEW_SOURCE = '''#include "pch.hpp"
#include "py_splodge.hpp"
#include "stock_py_splodge.hpp"
'''


def patch(root):
    root = Path(root)
    header = (root / HEADER).read_text(encoding='utf-8')
    source = (root / SOURCE).read_text(encoding='utf-8')
    helper = Path(__file__).with_name('stock_py_splodge.hpp').read_bytes()
    if NEW_CLASS in header:
        if NEW_FACTORY not in header or source != NEW_SOURCE:
            raise ValueError('Partial #1513 Splodge owner patch')
        (root/'lib/stock_py_splodge.hpp').write_bytes(helper)
        return
    seams = [('#include "py_attachment.hpp"', '#include "py_attachment.hpp"\n#include <memory>'),
             (OLD_CLASS, NEW_CLASS),
             ('\tPY_RW_ATTRIBUTE_REF_DECLARE( bbSize_, size )\n', ''),
             (OLD_FACTORY, NEW_FACTORY),
             ('\tVector3\t\t\tbbSize_;', '\tMatrix transform_;\n    float heightOffset_;\n    std::shared_ptr<unsigned> generation_;')]
    for before, after in seams:
        if header.count(before) != 1:
            raise ValueError('Pinned Splodge header anchor changed: '+repr(before))
        header = header.replace(before, after, 1)
    if source.count('PySplodge::PySplodge( Vector3 bbSize,') != 1 or 'static SplodgeRenderer s_splodges;' not in source:
        raise ValueError('Pinned Splodge renderer owner changed')
    (root/HEADER).write_text(header, encoding='utf-8')
    (root/SOURCE).write_text(NEW_SOURCE, encoding='utf-8')
    (root/'lib/stock_py_splodge.hpp').write_bytes(helper)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('engine_root', type=Path)
    patch(parser.parse_args().engine_root)
