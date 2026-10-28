#!/usr/bin/env python3
"""Restore the inherited #1513 GUI restricted-viewport rendering contract."""
import argparse
from pathlib import Path


def patch(root):
    root = Path(root)
    pending = {}

    def edit(name, old, new, marker=None):
        text = pending.get(name)
        if text is None:
            text = (root / name).read_text(encoding='utf-8-sig')
        if (marker or new) in text:
            return
        if text.count(old) != 1:
            raise ValueError('Restricted viewport owner anchor changed: ' + name)
        pending[name] = text.replace(old, new, 1)

    base = 'lib/ashes/simple_gui_component'
    edit(base + '.hpp', '\tPY_RW_ACCESSOR_ATTRIBUTE_DECLARE( bool, visible, visible )',
         '\tPY_RW_ACCESSOR_ATTRIBUTE_DECLARE( bool, visible, visible )\n'
         '\tbool drawWithRestrictedViewPort() const { return drawWithRestrictedViewPort_; }\n'
         '\tPY_RW_ATTRIBUTE_DECLARE( drawWithRestrictedViewPort_, drawWithRestrictedViewPort )')
    edit(base + '.hpp', '\tbool\t\t\t\tvisible_;',
         '\tbool\t\t\t\tvisible_;\n\tbool drawWithRestrictedViewPort_;')
    edit(base + '.cpp', '\tPY_ATTRIBUTE( parent )',
         '\tPY_ATTRIBUTE( parent )\n\tPY_ATTRIBUTE( drawWithRestrictedViewPort )')
    # aa8a1a passes true to 8bf700; 8bf7e0 stores the inherited byte +0x41.
    edit(base + '.cpp', ' visible_( true ),',
         ' visible_( true ),\n drawWithRestrictedViewPort_( true ),')

    context = 'lib/moo/render_context'
    edit(context + '.hpp', '#include "fog_helper.hpp"',
         '#include "fog_helper.hpp"\n#include "stock_flash_restricted_viewport.hpp"')
    edit(context + '.hpp', '\t\tbool\t\t\t\t\t\tpopRenderTarget();',
         '\t\tbool\t\t\t\t\t\tpopRenderTarget();\n'
         '        StockFlashRestrictedViewport& stockFlashRestrictedViewport()\n'
         '        {\n'
         '            if (!stockFlashRestrictedViewport_)\n'
         '                stockFlashRestrictedViewport_.reset(new StockFlashRestrictedViewport());\n'
         '            return *stockFlashRestrictedViewport_;\n'
         '        }')
    edit(context + '.hpp', '\t\tFogHelper\t\t\t\t\tfogHelper_;',
         '\t\tFogHelper\t\t\t\t\tfogHelper_;\n'
         '        std::unique_ptr<StockFlashRestrictedViewport> stockFlashRestrictedViewport_;')
    edit(context + '.cpp', '    DeviceCallback::fini();',
         '    stockFlashRestrictedViewport_.reset();\n    DeviceCallback::fini();')

    util = 'lib/scaleform/util'
    signature = 'void runtimeGUIViewport( const Vector2* '
    edit(util + '.hpp', signature + 'corners, GFx::Viewport& gv, GRectF& rect );',
         signature + 'corners, GFx::Viewport& gv, GRectF& rect, bool restricted = true );')
    edit(util + '.cpp', signature + 'lcorners, GFx::Viewport& gv, GRectF& rect )',
         signature + 'lcorners, GFx::Viewport& gv, GRectF& rect, bool restricted )')
    edit(util + '.hpp', '\tvoid fullScreenViewport( GFx::Viewport &outGv  );',
         '\tvoid fullScreenViewport( GFx::Viewport &outGv  );\n'
         '\tvoid fullScreenViewport( GFx::Viewport &outGv, bool restricted );')
    full_viewport = '''void fullScreenViewport( GFx::Viewport &outGv, bool restricted )
    {
        fullScreenViewport(outGv);
        if (!restricted) return;
        const auto region = Moo::rc().stockFlashRestrictedViewport().region(
            Moo::rc().backBufferDesc().Width, Moo::rc().backBufferDesc().Height,
            Moo::rc().windowed());
        if (region.restricted)
            outGv = GFx::Viewport(outGv.BufferWidth, outGv.BufferHeight,
                region.left, region.top, region.width, region.height);
    }

    '''
    edit(util + '.cpp', 'void viewportFromClipBounds( const Vector2* corners, GFx::Viewport& outGv )',
         full_viewport + 'void viewportFromClipBounds( const Vector2* corners, GFx::Viewport& outGv )',
         'void fullScreenViewport( GFx::Viewport &outGv, bool restricted )')
    edit(util + '.cpp', '\t\tfullScreenViewport( gv );',
         '\t\tfullScreenViewport( gv, restricted );')
    for name, corners in (('flash_gui_component', 'corners'), ('flash_text_gui_component', 'corners_')):
        edit('lib/scaleform/' + name + '.cpp',
             'runtimeGUIViewport( ' + corners + ', gv, rect );',
             'runtimeGUIViewport( ' + corners + ', gv, rect, drawWithRestrictedViewPort() );')
    edit('lib/scaleform/flash_text_gui_component.cpp', 'fullScreenViewport( gv );',
         'fullScreenViewport( gv, drawWithRestrictedViewPort() );')

    helper = Path(__file__).with_name('stock_flash_restricted_viewport.hpp').read_bytes()
    for name, text in pending.items():
        (root / name).write_text(text, encoding='utf-8')
    (root / 'lib/stock_flash_restricted_viewport.hpp').write_bytes(helper)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('engine_root', type=Path)
    patch(parser.parse_args().engine_root)
