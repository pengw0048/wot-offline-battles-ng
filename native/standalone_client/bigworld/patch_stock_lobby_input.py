#!/usr/bin/env python3
"""Restore the #1513 keyboard-mode names and key tables on the Scaleform key translator."""
import argparse
from pathlib import Path

# 00aaed30/00aaec70 and the 0150bae8.. name table: #1513 registers only wg_* names.
NAMES = (
    ('\tvoid setRedefineKeysMode(bool b)\n', '\tvoid wg_setRedefineKeysMode(bool b)\n'),
    ('PY_AUTO_MODULE_FUNCTION(RETVOID, setRedefineKeysMode, ARG(bool, END), BigWorld)',
     'PY_AUTO_MODULE_FUNCTION(RETVOID, wg_setRedefineKeysMode, ARG(bool, END), BigWorld)'),
    ('\tbool isCapsLockOn()\n', '\tbool wg_isCapsLockOn()\n'),
    ('PY_AUTO_MODULE_FUNCTION(RETDATA, isCapsLockOn, END, BigWorld)',
     'PY_AUTO_MODULE_FUNCTION(RETDATA, wg_isCapsLockOn, END, BigWorld)'),
    ('\tbool isNumLockOn()\n', '\tbool wg_isNumLockOn()\n'),
    ('PY_AUTO_MODULE_FUNCTION(RETDATA, isNumLockOn, END, BigWorld)',
     'PY_AUTO_MODULE_FUNCTION(RETDATA, wg_isNumLockOn, END, BigWorld)'),
    ('\tbool isScrollLockOn()\n', '\tbool wg_isScrollLockOn()\n'),
    ('PY_AUTO_MODULE_FUNCTION(RETDATA, isScrollLockOn, END, BigWorld)',
     'PY_AUTO_MODULE_FUNCTION(RETDATA, wg_isScrollLockOn, END, BigWorld)'),
)
# 01a1fa40 (default) and 01a1fb70 (redefine): right-hand modifiers keep their own codes.
TABLES = (
    ('\t\t\t{ KeyCode::KEY_RSHIFT,\t\t\tSF::Key::Shift },\n',
     '\t\t\t{ KeyCode::KEY_RSHIFT,\t\t\tSF::Key::RightShift },\n', 2),
    ('\t\t\t{ KeyCode::KEY_RCONTROL,\t\tSF::Key::Control },\n',
     '\t\t\t{ KeyCode::KEY_RCONTROL,\t\tSF::Key::RightControl },\n', 2),
    ('\t\t\t{ KeyCode::KEY_RALT,\t\t\tSF::Key::Alt },\n',
     '\t\t\t{ KeyCode::KEY_RALT,\t\t\tSF::Key::RightAlt },\n', 1),
)


def patch_source(source):
    if all(after in source for unused, after in NAMES):
        return source
    for before, after in NAMES:
        if source.count(before) != 1:
            raise ValueError('Pinned Scaleform key-state seam changed: ' + repr(before))
        source = source.replace(before, after, 1)
    for before, after, count in TABLES:
        if source.count(before) != count:
            raise ValueError('Pinned Scaleform key table changed: ' + repr(before))
        source = source.replace(before, after)
    return source


def patch(root):
    path = Path(root) / 'lib/scaleform/util.cpp'
    source = path.read_text(encoding='utf-8')
    patched = patch_source(source)
    if patched != source:
        path.write_text(patched, encoding='utf-8')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('engine_root', type=Path)
    patch(parser.parse_args().engine_root)
