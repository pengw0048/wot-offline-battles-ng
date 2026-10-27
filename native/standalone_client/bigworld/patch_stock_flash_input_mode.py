#!/usr/bin/env python3
"""Expose the #1513 GUI.Flash key-routing mode as wg_inputKeyMode on FlashGUIComponent."""
import argparse
from pathlib import Path

HEADER = 'lib/scaleform/flash_gui_component.hpp'
SOURCE = 'lib/scaleform/flash_gui_component.cpp'
# 005ca4ab registers only wg_inputKeyMode (getter 00aa86d0, setter 00aa8ba0 on +0x170).
SEAMS = (
    (HEADER, '\tPY_RW_ATTRIBUTE_DECLARE( inputKeyMode_, inputKeyMode )\n',
     '\tPY_RW_ATTRIBUTE_DECLARE( inputKeyMode_, wg_inputKeyMode )\n'),
    (SOURCE, '\t/*~ attribute FlashGUIComponent.inputKeyMode\n',
     '\t/*~ attribute FlashGUIComponent.wg_inputKeyMode\n'),
    (SOURCE, '\tPY_ATTRIBUTE(inputKeyMode)\n', '\tPY_ATTRIBUTE(wg_inputKeyMode)\n'),
)
# The member, its 00aa806f default and the 00aa8700 readers stay as they are.
ANCHORS = (
    (HEADER, '\tuint inputKeyMode_;\n'),
    (SOURCE, ' inputKeyMode_(0)\n'),
    (SOURCE, '\tif (!handled && !script() && pMovieView_ && 2 != inputKeyMode_)\n'),
    (SOURCE, '\t\tif (1 == inputKeyMode_ && handled) {\n'),
)
TOKEN = 'wg_inputKeyMode'
TOKEN_COUNTS = {HEADER: 1, SOURCE: 2}


def counts(sources):
    old = [sources[name].count(before) for name, before, unused in SEAMS]
    new = [sources[name].count(after) for name, unused, after in SEAMS]
    anchors = [sources[name].count(text) for name, text in ANCHORS]
    tokens = {name: sources[name].count(TOKEN) for name in TOKEN_COUNTS}
    return old, new, anchors, tokens


def patched_state(sources):
    old, new, anchors, tokens = counts(sources)
    return not any(old) and new == [1] * len(SEAMS) and anchors == [1] * len(ANCHORS) and tokens == TOKEN_COUNTS


def patch_sources(sources):
    if patched_state(sources):
        return dict(sources)
    old, new, anchors, tokens = counts(sources)
    if old != [1] * len(SEAMS) or any(new) or anchors != [1] * len(ANCHORS) or any(tokens.values()):
        raise ValueError('Pinned FlashGUIComponent input-mode seam is partial, duplicated or changed: '
                         'old %r new %r anchors %r tokens %r' % (old, new, anchors, tokens))
    result = dict(sources)
    for name, before, after in SEAMS:
        result[name] = result[name].replace(before, after, 1)
    if not patched_state(result):
        raise ValueError('FlashGUIComponent input-mode patch did not reach its final state')
    return result


def patch(root):
    paths = {name: Path(root) / name for name in TOKEN_COUNTS}
    sources = {name: path.read_text(encoding='utf-8') for name, path in paths.items()}
    patched = patch_sources(sources)
    for name, path in paths.items():
        if patched[name] != sources[name]:
            path.write_text(patched[name], encoding='utf-8')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('engine_root', type=Path)
    patch(parser.parse_args().engine_root)
