#!/usr/bin/env python3
"""Keep optional profiler metadata from corrupting a real Python callback."""
import argparse
from pathlib import Path

SOURCE = 'lib/pyscript/script.cpp'
MARKER = '// Native callbacks can legitimately expose __module__ == None.'
EDITS = (
    ('\tPyObject * pModuleNameObj = NULL;\n',
     '\tPyObject * pModuleNameObj = NULL;\n'
     '\t' + MARKER + '\n'
     '\t// Keep dynamic metadata alive until StringBuilder has copied it.\n'
     '\tPyObjectPtr moduleNameOwner;\n'
     '\tPyObjectPtr functionNameOwner;\n'),
    ('''\t\tPyObject *modulePyStringName = PyObject_GetAttrString( pFunction, "__module__" );
\t\tif (modulePyStringName)
\t\t{
\t\t\tmoduleName = PyString_AsString( modulePyStringName );
\t\t\tPy_DECREF( modulePyStringName );
\t\t}
\t\telse''',
     '''\t\tmoduleNameOwner = PyObjectPtr(
\t\t\tPyObject_GetAttrString( pFunction, "__module__" ),
\t\t\tPyObjectPtr::STEAL_REFERENCE );
\t\tif (moduleNameOwner && PyString_Check( moduleNameOwner.get() ))
\t\t{
\t\t\tmoduleName = PyString_AsString( moduleNameOwner.get() );
\t\t}
\t\telse if (!moduleNameOwner)'''),
    ('''\t\tPyObject *functionPyStringName = PyObject_GetAttrString( pFunction, "__name__" );
\t\tif (functionPyStringName)
\t\t{
\t\t\tfunctionName = PyString_AsString( functionPyStringName );
\t\t\tPy_DECREF( functionPyStringName );
\t\t}
\t\telse''',
     '''\t\tfunctionNameOwner = PyObjectPtr(
\t\t\tPyObject_GetAttrString( pFunction, "__name__" ),
\t\t\tPyObjectPtr::STEAL_REFERENCE );
\t\tif (functionNameOwner && PyString_Check( functionNameOwner.get() ))
\t\t{
\t\t\tfunctionName = PyString_AsString( functionNameOwner.get() );
\t\t}
\t\telse if (!functionNameOwner)'''),
)


def patch_source(source):
    if MARKER in source:
        if source.count(MARKER) != 1 or any(source.count(after) != 1 for before, after in EDITS):
            raise ValueError('Incomplete pinned callback metadata patch')
        return source
    if any(source.count(before) != 1 for before, after in EDITS):
        raise ValueError('Pinned Script callback metadata seam changed')
    for before, after in EDITS:
        source = source.replace(before, after, 1)
    return source


def patch(root):
    path = Path(root) / SOURCE
    source = path.read_text(encoding='utf-8-sig')
    replacement = patch_source(source)
    if replacement != source:
        path.write_text(replacement, encoding='utf-8')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('engine_root', type=Path)
    patch(parser.parse_args().engine_root)
