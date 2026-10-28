#!/usr/bin/env python3
"""Run BigWorld.WGFlockManager sweeps where #1513 WorldApp::tick services its WG timer queue.

The camera space setter also counts assignments that change the space, so a
run can be fenced by the exact assignment it started under.
"""
import argparse
from pathlib import Path

FILES = (
    ('client/world_app.cpp', 'tickStockFlockManagers', (
        ('#include "world_app.hpp"\n',
         '#include "stock_flock_manager.hpp"\n#include "world_app.hpp"\n'),
        ('        tickStockVehicleTraces();\n',
         '        tickStockVehicleTraces();\n        tickStockFlockManagers();\n'),
    )),
    ('lib/space/deprecated_space_helpers.hpp', 'cameraSpaceChanges', (
        ('\tClientSpacePtr cameraSpace();\n',
         '\tClientSpacePtr cameraSpace();\n'
         '\t// Counts assignments that replaced the camera space.\n'
         '\tuint32 cameraSpaceChanges();\n'),
    )),
    ('lib/space/deprecated_space_helpers.cpp', 'cameraSpaceChanges', (
        ('static ClientSpacePtr s_cameraSpace( NULL );\n',
         'static ClientSpacePtr s_cameraSpace( NULL );\n'
         'static uint32 s_cameraSpaceChanges = 0;\n'),
        ('{\n\ts_cameraSpace = space;\n}\n',
         '{\n\tif (space.get() != s_cameraSpace.get())\n\t{\n\t\t++s_cameraSpaceChanges;\n\t}\n'
         '\ts_cameraSpace = space;\n}\n'),
        ('{\n\treturn s_cameraSpace;\n}\n',
         '{\n\treturn s_cameraSpace;\n}\n\n\n'
         'uint32 DeprecatedSpaceHelpers::cameraSpaceChanges()\n{\n\treturn s_cameraSpaceChanges;\n}\n'),
    )),
)


def patch_text(text, token, edits):
    patched = [after in text for unused, after in edits]
    if all(patched) and all(text.count(after) == 1 for unused, after in edits) and \
            text.count(token) == sum(after.count(token) for unused, after in edits):
        return text
    if any(patched) or token in text:
        raise ValueError('WGFlockManager seam is partial or duplicated: ' + token)
    for before, after in edits:
        if text.count(before) != 1:
            raise ValueError('WGFlockManager anchor changed: ' + repr(before))
        text = text.replace(before, after, 1)
    return text


def patch_file(path, token, edits):
    data = path.read_bytes()
    if b'\r\n' in data and data.count(b'\r\n') != data.count(b'\n'):
        raise ValueError('Mixed line endings in ' + str(path))
    newline = '\r\n' if b'\r\n' in data else '\n'
    text = data.decode('utf-8-sig').replace('\r\n', '\n')
    if '\r' in text:
        raise ValueError('Unsupported line endings in ' + str(path))
    patched = patch_text(text, token, edits)
    if patched != text:
        bom = b'\xef\xbb\xbf' if data.startswith(b'\xef\xbb\xbf') else b''
        return bom + patched.replace('\n', newline).encode('utf-8')
    return None


def patch(root):
    # Check every file before writing any of them.
    pending = [(Path(root) / name, patch_file(Path(root) / name, token, edits))
               for name, token, edits in FILES]
    for path, data in pending:
        if data is not None:
            path.write_bytes(data)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('engine_root', type=Path)
    patch(parser.parse_args().engine_root)
