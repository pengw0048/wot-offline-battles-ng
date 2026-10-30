#!/usr/bin/env python3
"""Hand the D3D9 device Moo creates to the render thread that owns it."""
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
}


def replace_once(text, old, new, label):
    if new in text:
        return text
    if text.count(old) != 1:
        raise RuntimeError("Unexpected public engine source for " + label)
    return text.replace(old, new, 1)


def patch(root):
    for relative, edits in EDITS.items():
        path = Path(root) / relative
        text = path.read_text()
        for old, new in edits:
            text = replace_once(text, old, new, relative)
        path.write_text(text)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("engine_root", type=Path)
    patch(parser.parse_args().engine_root)


if __name__ == "__main__":
    main()
