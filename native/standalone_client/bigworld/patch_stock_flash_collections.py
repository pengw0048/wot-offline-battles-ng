#!/usr/bin/env python3
"""Restore #1513 Python collection and dictionary-key conversion at the GFx owner."""
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
DICT_BEFORE = '''\t\t\tconst char *objKey = PyString_AsString(pyKVKey);
\t\t\tGFx::Value objVal = convertValueToGFx(pyKVValue, pyMovieView);

\t\t\tgfxValue.SetMember(objKey, objVal);'''
DICT_AFTER = '''\t\t\tGFx::Value objVal = convertValueToGFx(pyKVValue, pyMovieView);
\t\t\tif (PyErr_Occurred()) break;

\t\t\t// #1513 AB1AD6..AB1B28: exact strings are borrowed; other keys
\t\t\t// use their Python string representation and release that new reference.
\t\t\tconst bool borrowedKey = PyString_CheckExact(pyKVKey);
\t\t\tPyObject *stringKey = borrowedKey ? pyKVKey : PyObject_Str(pyKVKey);
\t\t\tif (stringKey == NULL) break;
\t\t\tconst char *objKey = PyString_AsString(stringKey);
\t\t\tif (objKey != NULL) gfxValue.SetMember(objKey, objVal);
\t\t\tif (!borrowedKey) Py_DECREF(stringKey);
\t\t\tif (PyErr_Occurred()) break;'''
CALL_BEFORE = '''\t\tfor (uint i = 0; i < numArgs; ++i)
\t\t\tgfxArgs[i] = convertValueToGFx(PyTuple_GetItem(args, i), pyMovieView_);'''
CALL_AFTER = '''\t\tfor (uint i = 0; i < numArgs; ++i)
\t\t{
\t\t\tgfxArgs[i] = convertValueToGFx(PyTuple_GetItem(args, i), pyMovieView_);
\t\t\t// A failed key conversion must fail this call before invoking AS;
\t\t\t// a successful return would leak its exception into a later event.
\t\t\tif (PyErr_Occurred()) return NULL;
\t\t}'''


def patch_source(source):
    result = source
    for before, after in ((DICT_BEFORE, DICT_AFTER), (CALL_BEFORE, CALL_AFTER)):
        if after in result:
            if result.count(after) != 1:
                raise ValueError('GFx dictionary conversion owner is duplicated')
        elif result.count(before) == 1:
            result = result.replace(before, after, 1)
        else:
            raise ValueError('Pinned GFx dictionary conversion owner anchor changed')
    source = result
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
