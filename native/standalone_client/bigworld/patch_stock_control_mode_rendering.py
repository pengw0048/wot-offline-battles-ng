"""Restore the primary scene's #1513 low-detail decoration draw context."""
from pathlib import Path
import argparse


CONTEXT = '''\tbool stockLowDetailedMode() const { return stockLowDetailedMode_; }
\tvoid stockLowDetailedMode( bool enabled ) { stockLowDetailedMode_ = enabled; }
'''
SAMPLE = '\n        sceneDrawContext.stockLowDetailedMode( stockLowDetailedMode() );'


def replace_once(text, old, new, name):
    if new in text:
        return text
    if text.count(old) != 1:
        raise RuntimeError('Control mode rendering anchor is not unique: ' + name)
    return text.replace(old, new)


def patch(root):
    root = Path(root)
    updated = {}
    path = root / 'lib/scene/scene_draw_context.hpp'
    text = path.read_text()
    text = replace_once(text, '\tbool renderFlares() const;',
                        CONTEXT + '\n\tbool renderFlares() const;', 'draw context accessors')
    text = replace_once(text, '\tbool renderFlares_;',
                        '\tbool stockLowDetailedMode_ = false;\n\tbool renderFlares_;', 'default context mode')
    updated[path] = text
    path = root / 'client/world_app.cpp'
    text = path.read_text()
    text = replace_once(text, '#include "world_app.hpp"',
                        '#include "world_app.hpp"\n#include "stock_control_mode_rendering.hpp"',
                        'WorldApp mode owner')
    original = '        SceneDrawContext sceneDrawContext( scene, colourDrawContext );'
    text = replace_once(text, original, original + SAMPLE, 'primary draw context sample')
    updated[path] = text
    for path, text in updated.items():
        path.write_text(text)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('bigworld_root', type=Path)
    patch(parser.parse_args().bigworld_root)
