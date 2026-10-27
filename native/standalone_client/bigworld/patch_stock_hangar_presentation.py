#!/usr/bin/env python3
"""Restore the #1513 MouseCursor.inFocus state, the GUIApp background and the EdgeDrawer pass."""
import argparse
from pathlib import Path

# (file, anchor, insertion, insert before the anchor). Each insertion is unique.
EDITS = (
    # 008b9760 stores the focus byte 01d3532c after the original clip release;
    # 008b8430/008b92b0 expose it as the read-only MouseCursor.inFocus.
    ('lib/ashes/mouse_cursor.hpp', '\tPY_RO_ATTRIBUTE_DECLARE( isActive_, active )\n',
     '\tPY_RO_ATTRIBUTE_DECLARE( s_inFocus_, inFocus )\n', False),
    ('lib/ashes/mouse_cursor.hpp', '\tstatic void staticSetFocus( bool focus );\n',
     '\tstatic bool s_inFocus_;\n', False),
    ('lib/ashes/mouse_cursor.cpp', '\tPY_ATTRIBUTE( hotSpot )\nPY_END_ATTRIBUTES()\n',
     '\tPY_ATTRIBUTE( inFocus )\n', True),
    ('lib/ashes/mouse_cursor.cpp', '/* static */void MouseCursor::staticSetFocus( bool focus )\n',
     'bool MouseCursor::s_inFocus_ = false;\n\n', True),
    ('lib/ashes/mouse_cursor.cpp',
     '\tif ( !focus )\n\t{\n\t\tclipCursorToWindow( false );\n\t}\n',
     '\ts_inFocus_ = focus;\n', False),
    # 00599b80 creates WGBackgroundImage; 006338e5 draws it before SimpleGUI.
    ('client/gui_app.cpp', '#include "ashes/simple_gui.hpp"\n',
     '#include "stock_hangar_presentation.hpp"\n', True),
    ('client/gui_app.cpp', '    SimpleGUI::instance().hwnd( DeviceApp::s_hWnd_ );\n',
     '    if (!initialiseStockHangarPresentation()) return false;\n', False),
    ('client/gui_app.cpp', '    DeviceApp::instance.deleteGUI();\n',
     '    finaliseStockHangarPresentation();\n', True),
    ('client/gui_app.cpp', '    SimpleGUI::instance().draw();\n',
     '    if (!drawStockGUIBackground())\n'
     '        ERROR_MSG("WGBackgroundImage: background draw failed\\n");\n', True),
    # 00630e2b/00630e77 draw the EdgeDrawer z-pass and transfer after the
    # semitransparent scene, before lens effects and the PPAA resolve.
    ('client/lens_app.cpp', '#include "romp/lens_effect_manager.hpp"\n',
     '#include "stock_hangar_presentation.hpp"\n', True),
    ('client/lens_app.cpp', '    rp->endSemitransparentDraw();\n',
     '    if (!drawStockEdgeDetection())\n'
     '        ERROR_MSG("EdgeDrawer: edge detection draw failed\\n");\n', False),
)
# Each final token occurs only in its own edit.
TOKENS = (('lib/ashes/mouse_cursor.hpp', 's_inFocus_', 2),
          ('lib/ashes/mouse_cursor.cpp', 's_inFocus_', 2),
          ('lib/ashes/mouse_cursor.cpp', 'inFocus', 3),
          ('client/gui_app.cpp', 'StockHangarPresentation', 2),
          ('client/gui_app.cpp', 'StockGUIBackground', 1),
          ('client/lens_app.cpp', 'stock_hangar_presentation', 1),
          ('client/lens_app.cpp', 'StockEdgeDetection', 1))
FILES = tuple(sorted({path for path, unused, unused, unused in EDITS}))


def joined(anchor, insertion, before):
    return insertion + anchor if before else anchor + insertion


def state(sources):
    """Return 'pristine', 'final' or None for a partial, duplicated or changed seam."""
    pristine = final = True
    for path, anchor, insertion, before in EDITS:
        text = sources[path]
        if text.count(anchor) != 1:
            return None
        applied = text.count(joined(anchor, insertion, before))
        present = text.count(insertion)
        pristine = pristine and applied == 0 and present == 0
        final = final and applied == 1 and present == 1
    counts = [sources[path].count(token) for path, token, unused in TOKENS]
    if pristine and not any(counts):
        return 'pristine'
    if final and counts == [count for unused, unused, count in TOKENS]:
        return 'final'
    return None


def patch_sources(sources):
    current = state(sources)
    if current == 'final':
        return dict(sources)
    if current != 'pristine':
        raise ValueError('Pinned hangar presentation seam is partial, duplicated or changed')
    result = dict(sources)
    for path, anchor, insertion, before in EDITS:
        result[path] = result[path].replace(anchor, joined(anchor, insertion, before), 1)
    if state(result) != 'final':
        raise ValueError('Hangar presentation patch did not reach its final state')
    return result


def patch(root):
    root = Path(root)
    sources = {path: (root / path).read_text(encoding='utf-8') for path in FILES}
    patched = patch_sources(sources)
    for path in FILES:
        if patched[path] != sources[path]:
            (root / path).write_text(patched[path], encoding='utf-8')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('engine_root', type=Path)
    patch(parser.parse_args().engine_root)
