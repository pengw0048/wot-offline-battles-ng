#!/usr/bin/env python3
"""Install the D3D9 render owner and bounded coarse frame-stage diagnostics."""
import argparse
from pathlib import Path


EDITS = {
    'lib/moo/render_context.cpp': (
        (r'''#include "stock_display_modes.hpp"
#include "render_context.hpp"
#include "stock_screenshot_callback.hpp"
#include "renderer.hpp"
''',
         r'''#include "stock_display_modes.hpp"
#include "render_context.hpp"
#include "stock_d3d9_render_thread.hpp"
#include "stock_screenshot_callback.hpp"
#include "renderer.hpp"
'''),
        (r'''        return false;
    }

    // succeeded
''',
         r'''        return false;
    }

    // One render thread owns the real device; Moo records calls on its wrapper.
    device_ = ::wot::d3d9rt::wrap_device( device_, usingD3DDeviceEx_ );

    // succeeded
'''),
    ),
    'client/app.cpp': (
        ('#include "cstdmf/main_loop_task.hpp"\n',
         '#include "cstdmf/main_loop_task.hpp"\n#include "stock_d3d9_render_thread.hpp"\n'),
        ('\t\t\t\tGPU_PROFILER_SCOPE( App_Tick );\n',
         '\t\t\t\tGPU_PROFILER_SCOPE( App_Tick );\n'
         '\t\t\t\t::wot::d3d9rt::FrameStage stockFrameStage( 0 );\n'),
        ('\t\t\t\t\tGPU_PROFILER_SCOPE( App_UpdateAnimations );\n',
         '\t\t\t\t\tGPU_PROFILER_SCOPE( App_UpdateAnimations );\n'
         '\t\t\t\t\t::wot::d3d9rt::FrameStage stockFrameStage( 1 );\n'),
        # Stay outside the worker's draw branch so its complete cached patch
        # remains intact. This scope ends at the enclosing checkDevice block.
        ('\t\t\t\t\tMainLoopTasks::root().updateAnimations( dGameTime_ );\n\t\t\t\t}\n',
         '\t\t\t\t\tMainLoopTasks::root().updateAnimations( dGameTime_ );\n\t\t\t\t}\n'
         '\t\t\t\t::wot::d3d9rt::FrameStage stockDrawStage( 2 );\n'),
    ),
    'client/world_app.cpp': (
        ('#include "cstdmf/main_loop_task.hpp"\n',
         '#include "cstdmf/main_loop_task.hpp"\n#include "stock_d3d9_render_thread.hpp"\n'),
        ('    PROFILER_SCOPED( MainScene_Draw );\n',
         '    PROFILER_SCOPED( MainScene_Draw );\n'
         '    ::wot::d3d9rt::FrameStage stockFrameStage( 3 );\n'),
        ('    PROFILER_SCOPED( Terrain_Draw );\n',
         '    PROFILER_SCOPED( Terrain_Draw );\n'
         '    ::wot::d3d9rt::FrameStage stockFrameStage( 4 );\n'),
    ),
}


def replace_once(text, old, new, label):
    if text.count(new) == 1 and old not in text.replace(new, '', 1):
        return text
    if new in text or text.count(old) != 1:
        raise RuntimeError("Unexpected public engine source for " + label)
    return text.replace(old, new, 1)


def patch(root):
    updated = {}
    for relative, edits in EDITS.items():
        path = Path(root) / relative
        text = path.read_text()
        for old, new in edits:
            text = replace_once(text, old, new, relative)
        updated[path] = text
    # Validate every owner before changing any source file.
    for path, text in updated.items():
        if path.read_text() != text:
            path.write_text(text)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("engine_root", type=Path)
    patch(parser.parse_args().engine_root)


if __name__ == "__main__":
    main()
