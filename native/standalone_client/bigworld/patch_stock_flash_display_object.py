#!/usr/bin/env python3
"""Restore #1513 native descriptor precedence when assigning GFx proxy attributes."""
import argparse
from pathlib import Path

SOURCE = 'lib/scaleform/py_gfx_value.cpp'
BEFORE = '''	GFx::Value gfxValueNew = convertValueToGFx(value.get(), pyMovieView_);

	Children_t::iterator it = children_.find(attr);'''
MARKER = '// #1513 ab2ff3: native descriptors must consume Matrix writes'
AFTER = '''	// #1513 ab2ff3: native descriptors must consume Matrix writes
	// before dynamic ActionScript members can accept an undefined conversion.
	if (PyObjectPlus::pySetAttribute(attrObj, value))
		return true;
	PyErr_Clear();

	GFx::Value gfxValueNew = convertValueToGFx(value.get(), pyMovieView_);

	Children_t::iterator it = children_.find(attr);'''


def patch_sources(sources):
    text = sources[SOURCE]
    old, new = text.count(BEFORE), text.count(AFTER)
    if new == 1 and old == 1:  # AFTER contains the unmodified conversion suffix.
        if text.count(MARKER) != 1 or text.count('if (PyObjectPlus::pySetAttribute(attrObj, value))') != 1:
            raise ValueError('Duplicated GFx native attribute dispatch')
        return dict(sources)
    if old != 1 or new or MARKER in text or 'if (PyObjectPlus::pySetAttribute(attrObj, value))' in text:
        raise ValueError('Pinned GFx attribute dispatch seam is partial, duplicated or changed')
    start = text.index('bool PyGFxValue::pySetAttribute(')
    end = text.index('PyObject *PyGFxValue::pyCall(', start)
    if not start < text.index(BEFORE) < end:
        raise ValueError('GFx attribute dispatch seam is outside its setter owner')
    result = dict(sources)
    result[SOURCE] = text.replace(BEFORE, AFTER, 1)
    return result


def patch(root):
    path = Path(root) / SOURCE
    sources = {SOURCE: path.read_text(encoding='utf-8')}
    patched = patch_sources(sources)
    if patched[SOURCE] != sources[SOURCE]:
        path.write_text(patched[SOURCE], encoding='utf-8')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('engine_root', type=Path)
    patch(parser.parse_args().engine_root)
