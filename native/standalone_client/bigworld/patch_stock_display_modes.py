"""Keep the Python display contract on the real public render-context owner."""
from pathlib import Path
import argparse


def replace_once(text, before, after):
    if after in text:
        return text
    if text.count(before) != 1:
        raise RuntimeError('The reviewed display owner anchor changed: ' + repr(before))
    return text.replace(before, after)


def patch(root):
    root = Path(root)
    paths = [root / 'client/script_bigworld.cpp', root / 'lib/moo/render_context.cpp',
             root / 'client/bw_winmain.cpp']
    script, render, window = [path.read_text() for path in paths]
    if '#include "stock_display_modes.hpp"' not in script:
        script = replace_once(script, '#include "pch.hpp"',
                              '#include "pch.hpp"\n#include "stock_display_modes.hpp"')
    before = '''static bool changeVideoMode( int modeIndex, bool windowed )
{
\tBW_GUARD;
\treturn (
\t\tMoo::rc().device() != NULL &&
\t\tMoo::rc().changeMode( modeIndex, windowed ) );
}
PY_AUTO_MODULE_FUNCTION( RETDATA, changeVideoMode, ARG( int, ARG( bool, END ) ), BigWorld )'''
    after = '''static bool changeVideoMode( int modeIndex, int windowMode )
{
\tBW_GUARD;
\treturn stockChangeVideoMode( modeIndex, windowMode );
}
PY_AUTO_MODULE_FUNCTION( RETDATA, changeVideoMode, ARG( int, ARG( int, END ) ), BigWorld )'''
    script = replace_once(script, before, after)
    script = replace_once(script, 'App::instance().resizeWindow(width, height);',
                          'stockResizeWindow(width, height);')
    if '#include "stock_display_modes.hpp"' not in render:
        render = replace_once(render, '#include "pch.hpp"',
                              '#include "pch.hpp"\n#include "stock_display_modes.hpp"')
    render = replace_once(render, '    stencilWanted_ = requireStencil;',
                          '    stencilWanted_ = requireStencil;\n    stockRememberDisplayWindow();')
    render = replace_once(render, '                                mode.RefreshRate = 0;',
                          '                                // Preserve the hardware refresh rate in the canonical mode vector.')
    render = replace_once(render,
        '        presentParameters_.BackBufferHeight = mode.Height;',
        '        presentParameters_.BackBufferHeight = mode.Height;\n'
        '        presentParameters_.FullScreen_RefreshRateInHz = mode.RefreshRate;')
    render = replace_once(render, 'namespace Moo\n{',
        'bool stockDisplayDeviceLost() { return s_lost; }\n\nnamespace Moo\n{')
    render = replace_once(render,
        '            hr = device_->Present( NULL, NULL, NULL, NULL );',
        '            hr = device_->Present( NULL, NULL, NULL, NULL );\n'
        '            // #1513 publishes device loss immediately after Present.\n'
        '            if (hr == D3DERR_DEVICELOST || hr == D3DERR_DEVICENOTRESET)\n'
        '                s_lost = true;')
    window = replace_once(window, '#include "bw_winmain.hpp"',
                          '#include "bw_winmain.hpp"\n#include "stock_display_modes.hpp"')
    window = replace_once(window, '\tstatic bool s_inSizeMove = false;',
        '\tstockDisplayWindowMessage( msg, wParam, lParam );\n\n'
        '\tstatic bool s_inSizeMove = false;')
    # Validate every anchor before mutating any production translation unit.
    for path, contents in zip(paths, (script, render, window)):
        if path.read_text() != contents:
            path.write_text(contents)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('bigworld_root', type=Path)
    patch(parser.parse_args().bigworld_root)
