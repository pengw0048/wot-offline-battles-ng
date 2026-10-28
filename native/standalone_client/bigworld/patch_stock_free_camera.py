#!/usr/bin/env python3
"""Restore the #1513 FreeCamera inheritance, tree target and key reset owner."""
import argparse
from pathlib import Path

HEADER = 'lib/camera/free_camera.hpp'
SOURCE = 'lib/camera/free_camera.cpp'


def patch(root):
    root = Path(root)
    pending = {name: (root / name).read_text() for name in (HEADER, SOURCE)}

    def edit(name, before, after):
        source = pending[name]
        if after in source:
            return
        if source.count(before) != 1:
            raise ValueError('FreeCamera owner anchor changed: ' + name + ': ' + repr(before))
        pending[name] = source.replace(before, after, 1)

    edit(HEADER, '#include "base_camera.hpp"',
         '#include "stock_speedtree_targeted_camera.hpp"')
    edit(HEADER, 'class FreeCamera : public BaseCamera',
         'class FreeCamera : public SpeedTreeTargetedCamera')
    edit(HEADER, 'Py_Header( FreeCamera, BaseCamera )',
         'Py_Header( FreeCamera, SpeedTreeTargetedCamera )')
    edit(HEADER, '\tPY_FACTORY_DECLARE()',
         '\tvoid resetKeys();\n'
         '\tPY_AUTO_METHOD_DECLARE( RETVOID, resetKeys, END )\n\n'
         '\tPY_FACTORY_DECLARE()')
    edit(SOURCE, '\tBaseCamera( pType ),', '\tSpeedTreeTargetedCamera( pType ),')
    edit(SOURCE, 'void FreeCamera::update( float dTime )\n{\n\tBW_GUARD;',
         'void FreeCamera::update( float dTime )\n{\n\tBW_GUARD;\n'
         '\t// #1513 0079f297 publishes before fixed/provider camera branches.\n'
         '\tSpeedTreeTargetedCamera::update( dTime );')
    edit(SOURCE, 'PY_BEGIN_METHODS( FreeCamera )\nPY_END_METHODS()',
         '// #1513 0079f150 jumps to CameraControl::clearVel (007994d0).\n'
         'void FreeCamera::resetKeys()\n{\n\tCameraControl::clearVel();\n}\n\n'
         'PY_BEGIN_METHODS( FreeCamera )\n\tPY_METHOD( resetKeys )\nPY_END_METHODS()')

    shared = (Path(__file__).resolve().parent / 'stock_speedtree_targeted_camera.hpp').read_text()
    # Validate the complete patch before changing the SDK sources.
    for name, content in pending.items():
        if (root / name).read_text() != content:
            (root / name).write_text(content)
    destination = root / 'lib/stock_speedtree_targeted_camera.hpp'
    if not destination.exists() or destination.read_text() != shared:
        destination.write_text(shared)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('bigworld_root', type=Path)
    patch(parser.parse_args().bigworld_root)
