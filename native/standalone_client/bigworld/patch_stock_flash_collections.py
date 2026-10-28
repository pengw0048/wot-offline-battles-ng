#!/usr/bin/env python3
"""Restore #1513 Python set conversion at the existing GFx value owner."""
import argparse
from pathlib import Path

SOURCE = 'lib/scaleform/py_gfx_value.cpp'
ANCHOR = '\telse if (PyDict_Check(pyValue))\n'
SET_BRANCH = '''\telse if (PySet_Check(pyValue))
\t{
\t\t// #1513 AB1A3D/AB1B63: set and its subclasses become AS arrays.
\t\tpMovie->CreateArray(&gfxValue);
\t\tPyObject *iterator = PyObject_GetIter(pyValue);
\t\tif (iterator != NULL)
\t\t{
\t\t\tuint index = 0;
\t\t\tPyObject *item;
\t\t\twhile ((item = PyIter_Next(iterator)) != NULL)
\t\t\t{
\t\t\t\tgfxValue.SetElement(index++, convertValueToGFx(item, pyMovieView));
\t\t\t\tPy_DECREF(item);
\t\t\t}
\t\t\tPy_DECREF(iterator);
\t\t}
\t}
'''


def patch_source(source):
    if SET_BRANCH + ANCHOR in source:
        if source.count(SET_BRANCH) != 1 or source.count(ANCHOR) != 1:
            raise ValueError('GFx set conversion owner is duplicated')
        return source
    if 'PySet_Check(pyValue)' in source or source.count(ANCHOR) != 1:
        raise ValueError('Pinned GFx set conversion owner anchor changed')
    begin = source.index('GFx::Value PyGFxValue::convertValueToGFx(')
    end = source.index('void PyGFxValue::collectChildValues()', begin)
    if not begin < source.index(ANCHOR) < end:
        raise ValueError('GFx collection anchor is outside its conversion owner')
    return source.replace(ANCHOR, SET_BRANCH + ANCHOR, 1)


def patch(root):
    path = Path(root) / SOURCE
    source = path.read_text(encoding='utf-8')
    patched = patch_source(source)
    if patched != source:
        path.write_text(patched, encoding='utf-8')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('engine_root', type=Path)
    patch(parser.parse_args().engine_root)
