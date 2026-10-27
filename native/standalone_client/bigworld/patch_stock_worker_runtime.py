#!/usr/bin/env python3
"""Keep the simulation worker hidden and frame-complete without scene/UI draw or Present."""
import argparse
from pathlib import Path

HEADER = 'lib/cstdmf/stock_worker_runtime.hpp'
INCLUDE = '#include "cstdmf/stock_worker_runtime.hpp"\n'
ROLE = 'wot::stockSimulationWorker()'
WORKER_RESIZE = ('wot::stockSimulationWorker() ?\n'
                 '{0}SWP_NOACTIVATE | SWP_NOZORDER : SWP_SHOWWINDOW );\n')

# (file, current text, final text). Every current text is unique in its file
# both after patch_stock_audio.py and after the complete engine patch chain.
EDITS = (
    ('client/bw_winmain.cpp', '#include "cstdmf/log_msg.hpp"\n',
     '#include "cstdmf/log_msg.hpp"\n' + INCLUDE),
    ('client/bw_winmain.cpp', '\t// Create the main window\n\ttimeBeginPeriod(1);\n',
     '\t// Cache the immutable process role before any window exists.\n'
     '\twot::stockSimulationWorker();\n\n'
     '\t// Create the main window\n\ttimeBeginPeriod(1);\n'),
    ('client/bw_winmain.cpp',
     '\t\t// Initialise and start a new game\n'
     '\t\tShowWindow( hWnd, nCmdShow );\n'
     '\t\tUpdateWindow( hWnd );\n',
     '\t\t// A simulation worker stays hidden from creation through teardown.\n'
     '\t\tif (wot::stockSimulationWorker())\n'
     '\t\t{\n'
     '\t\t\tif (!SetPropW( hWnd, wot::stockWorkerWindowProperty(), hWnd ))\n'
     '\t\t\t\tERROR_MSG( "Simulation worker window identity was not recorded\\n" );\n'
     '\t\t}\n'
     '\t\telse\n'
     '\t\t{\n'
     '\t\t\t// Initialise and start a new game\n'
     '\t\t\tShowWindow( hWnd, nCmdShow );\n'
     '\t\t\tUpdateWindow( hWnd );\n'
     '\t\t}\n'),
    ('client/app.cpp', '#include "world_app.hpp"\n',
     '#include "world_app.hpp"\n#include "stock_audio.hpp"\n' + INCLUDE),
    ('client/app.cpp',
     '\t\tif (active)\n\t\t{\n\t\t\t// Now tick (and input)\n',
     '\t\t// A hidden simulation worker always runs the complete active frame.\n'
     '\t\tif (active || wot::stockSimulationWorker())\n'
     '\t\t{\n\t\t\t// Now tick (and input)\n'),
    ('client/app.cpp',
     '\t\t\t\t// And draw\n'
     '\t\t\t\t{\n'
     '\t\t\t\t\tPROFILER_SCOPED( App_Draw );\n'
     '\t\t\t\t\tScopedDogWatch sdw( g_watchOutput );\n'
     '\t\t\t\t\tMainLoopTasks::root().draw();\n'
     '\t\t\t\t}\n',
     '\t\t\t\tif (wot::stockSimulationWorker())\n'
     '\t\t\t\t{\n'
     '\t\t\t\t\t// No scene, GUI or Present. Keep beginScene\'s resource preload\n'
     '\t\t\t\t\t// and DeviceApp::draw\'s audio service at this frame boundary.\n'
     '\t\t\t\t\tMoo::rc().preloadDeviceResources( 2 );\n'
     '\t\t\t\t\ttickStockAudio(\n'
     '\t\t\t\t\t\tCameraApp::instance().clientCamera().camera()->invView(),\n'
     '\t\t\t\t\t\tdRenderTime_ );\n'
     '\t\t\t\t}\n'
     '\t\t\t\telse\n'
     '\t\t\t\t{\n'
     '\t\t\t\t\t// And draw\n'
     '\t\t\t\t\tPROFILER_SCOPED( App_Draw );\n'
     '\t\t\t\t\tScopedDogWatch sdw( g_watchOutput );\n'
     '\t\t\t\t\tMainLoopTasks::root().draw();\n'
     '\t\t\t\t}\n'),
    ('lib/ashes/mouse_cursor.cpp', '#include "romp/py_texture_provider.hpp"\n',
     '#include "romp/py_texture_provider.hpp"\n' + INCLUDE),
    ('lib/ashes/mouse_cursor.cpp',
     '\t::SetCursorPos( pt.x, pt.y );\n'
     '\t\n'
     '\tif (Moo::rc().device()) \n'
     '\t{\n'
     '\t\tMoo::rc().device()->SetCursorPosition( pt.x, pt.y, 0 );\n'
     '\t}\n',
     '\t// A hidden simulation worker never moves the shared desktop cursor.\n'
     '\tif (!wot::stockSimulationWorker())\n'
     '\t{\n'
     '\t\t::SetCursorPos( pt.x, pt.y );\n'
     '\t\tif (Moo::rc().device())\n'
     '\t\t{\n'
     '\t\t\tMoo::rc().device()->SetCursorPosition( pt.x, pt.y, 0 );\n'
     '\t\t}\n'
     '\t}\n'),
    # The one owner of both ClipCursor(rect) and the desktop-wide ClipCursor(NULL)
    # used by updateMouseClipping, clipped(), staticSetFocus and device reset.
    ('lib/ashes/mouse_cursor.cpp',
     'void clipCursorToWindow( bool clip )\n{\n\tBW_GUARD;\n',
     'void clipCursorToWindow( bool clip )\n{\n\tBW_GUARD;\n'
     '\t// A hidden simulation worker never clips or releases the desktop cursor.\n'
     '\tif (wot::stockSimulationWorker())\n'
     '\t\treturn;\n'),
    ('lib/moo/render_context.cpp', '#include "moo/format_name_map.hpp"\n',
     '#include "moo/format_name_map.hpp"\n' + INCLUDE),
    ('lib/moo/render_context.cpp',
     '    BW_GUARD;\n    // Avoid changeMode to be called recursively from \n',
     '    BW_GUARD;\n'
     '    // A hidden simulation worker never requests an exclusive display mode.\n'
     '    if (wot::stockSimulationWorker())\n'
     '        windowed = true;\n'
     '    // Avoid changeMode to be called recursively from \n'),
    ('lib/moo/render_context.cpp',
     '                      SWP_SHOWWINDOW );\n\n        // finally, adjust',
     '                      ' + WORKER_RESIZE.format(' ' * 26) +
     '\n        // finally, adjust'),
    ('lib/moo/render_context.cpp',
     '                          SWP_SHOWWINDOW );\n        }\n    }\n\n    // Release the render device',
     '                          ' + WORKER_RESIZE.format(' ' * 30) +
     '        }\n    }\n\n    // Release the render device'),
    ('lib/moo/render_context.cpp',
     '    BW_GUARD;\n    // save windowed mode size\n    windowedSize_ = windowedSize;\n',
     '    BW_GUARD;\n'
     '    // A hidden simulation worker never requests an exclusive display mode.\n'
     '    if (wot::stockSimulationWorker())\n'
     '        windowed = true;\n'
     '    // save windowed mode size\n    windowedSize_ = windowedSize;\n'),
    ('lib/romp/progress.cpp', '#include "moo/material.hpp"\n',
     '#include "moo/material.hpp"\n' + INCLUDE),
    # After the callback and device check, before Clear/beginScene.
    ('lib/romp/progress.cpp',
     '\tif ( !ret || !Moo::rc().checkDevice())\n\t\treturn false;\n\n\t// just draw the scene\n',
     '\tif ( !ret || !Moo::rc().checkDevice())\n\t\treturn false;\n\n'
     '\tif (wot::stockSimulationWorker())\n'
     '\t{\n'
     '\t\t// No loading-screen frame; keep beginScene\'s preload and the frame counter.\n'
     '\t\tMoo::rc().preloadDeviceResources( 2 );\n'
     '\t\tMoo::rc().nextFrame();\n'
     '\t\treturn true;\n'
     '\t}\n\n'
     '\t// just draw the scene\n'),
    ('lib/ashes/gui_progress.cpp', '#include "ashes/clip_gui_shader.hpp"\n',
     '#include "ashes/clip_gui_shader.hpp"\n' + INCLUDE),
    # After the callback, progress script and GUI update, before Clear/beginScene.
    ('lib/ashes/gui_progress.cpp',
     '\t\t\tSimpleGUI::instance().update(dTime);\n\n\t\t\t// just draw the scene\n',
     '\t\t\tSimpleGUI::instance().update(dTime);\n\n'
     '\t\t\tif (wot::stockSimulationWorker())\n'
     '\t\t\t{\n'
     '\t\t\t\t// No loading-screen frame; keep beginScene\'s preload and the frame counter.\n'
     '\t\t\t\tMoo::rc().preloadDeviceResources( 2 );\n'
     '\t\t\t\tMoo::rc().nextFrame();\n'
     '\t\t\t\tcontinue;\n'
     '\t\t\t}\n\n'
     '\t\t\t// just draw the scene\n'),
)
FILES = tuple(sorted({path for path, unused, unused in EDITS}))
# Role queries each final file owns; the pristine count is zero.
ROLE_COUNTS = {'client/bw_winmain.cpp': 2, 'client/app.cpp': 2,
               'lib/ashes/mouse_cursor.cpp': 2, 'lib/moo/render_context.cpp': 4,
               'lib/romp/progress.cpp': 1, 'lib/ashes/gui_progress.cpp': 1}


# Pinned v0.10.1 WorkerPresentation.activate requires a positive hidden count.
# This EXE never shows the worker, so a zero count needs native proof instead.
PINNED_HIDE = '''        if not private_desktop:
            hidden_count = hide_windows()
            if (isinstance(hidden_count, bool) or
                    not isinstance(hidden_count, _INTEGER_TYPES) or
                    hidden_count < 1):
                raise WorkerPresentationError(
                    'native worker window was not found')
            self._window_hidden = True
'''
INDEPENDENT_HIDE = '''        if not private_desktop:
            hidden_count = hide_windows()
            if (isinstance(hidden_count, bool) or
                    not isinstance(hidden_count, _INTEGER_TYPES) or
                    hidden_count < 0):
                raise WorkerPresentationError(
                    'native worker window was not found')
            if hidden_count == 0:
                # The independent EXE never shows its simulation worker. Only
                # the native owner may prove that its own window stayed hidden.
                confirm = getattr(native, 'confirm_hidden_worker_window', None)
                confirmed = confirm() if callable(confirm) else 0
                if (isinstance(confirmed, bool) or
                        not isinstance(confirmed, _INTEGER_TYPES) or
                        confirmed < 1):
                    raise WorkerPresentationError(
                        'native worker window was not found')
            self._window_hidden = hidden_count > 0
'''


def adapt_worker_presentation(source):
    """Adapt the pinned worker_presentation.py for prepare_offline_fork.py."""
    if source.count(INDEPENDENT_HIDE) == 1 and PINNED_HIDE not in source:
        return source
    if source.count(PINNED_HIDE) != 1 or INDEPENDENT_HIDE in source:
        raise ValueError('The pinned worker presentation seam changed')
    return source.replace(PINNED_HIDE, INDEPENDENT_HIDE, 1)


def decode(data, path):
    """Return (text with LF newlines, BOM, newline) or fail closed."""
    bom = b'\xef\xbb\xbf' if data.startswith(b'\xef\xbb\xbf') else b''
    text = data[len(bom):].decode('utf-8')
    crlf = text.count('\r\n')
    if crlf and (crlf != text.count('\n') or crlf != text.count('\r')):
        raise ValueError('Mixed line endings in worker runtime target: ' + path)
    if not crlf and '\r' in text:
        raise ValueError('Unsupported line endings in worker runtime target: ' + path)
    return text.replace('\r\n', '\n'), bom, '\r\n' if crlf else '\n'


def encode(text, bom, newline):
    return bom + text.replace('\n', newline).encode('utf-8')


def state(sources, header, shared):
    """Return 'pristine', 'final' or None for a partial, duplicated or changed seam."""
    pristine = header is None
    final = header == shared
    for path, before, after in EDITS:
        text = sources[path]
        current, patched = text.count(before), text.count(after)
        pristine = pristine and current == 1 and patched == 0
        final = final and patched == 1 and current == (1 if before in after else 0)
    for path, count in ROLE_COUNTS.items():
        text = sources[path]
        pristine = pristine and ROLE not in text and INCLUDE not in text
        final = final and text.count(ROLE) == count and text.count(INCLUDE) == 1
    if pristine:
        return 'pristine'
    if final:
        return 'final'
    return None


def patch_sources(sources, header, shared):
    current = state(sources, header, shared)
    if current == 'final':
        return dict(sources)
    if current != 'pristine':
        raise ValueError('Worker runtime seam is partial, duplicated or changed')
    result = dict(sources)
    for path, before, after in EDITS:
        result[path] = result[path].replace(before, after, 1)
    if state(result, shared, shared) != 'final':
        raise ValueError('Worker runtime patch did not reach its final state')
    return result


def patch(root):
    root = Path(root)
    shared = Path(__file__).with_name('stock_worker_runtime.hpp').read_bytes()
    header_path = root / HEADER
    if not header_path.parent.is_dir():
        raise ValueError('The engine root has no lib/cstdmf directory')
    header = header_path.read_bytes() if header_path.exists() else None
    loaded = {path: decode((root / path).read_bytes(), path) for path in FILES}
    sources = {path: text for path, (text, unused, unused) in loaded.items()}
    # Every anchor is validated before the first write.
    patched = patch_sources(sources, header, shared)
    for path in FILES:
        if patched[path] != sources[path]:
            unused, bom, newline = loaded[path]
            (root / path).write_bytes(encode(patched[path], bom, newline))
    if header != shared:
        header_path.write_bytes(shared)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('engine_root', type=Path)
    patch(parser.parse_args().engine_root)
