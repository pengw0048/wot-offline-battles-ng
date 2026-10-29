#!/usr/bin/env python3
"""Split GUI component updates and Flash frame submission in the native profiler."""
import argparse
from pathlib import Path


SIMPLE_GUI = 'lib/ashes/simple_gui.cpp'
FLASH = 'lib/scaleform/flash_gui_component.cpp'
MANAGER = 'lib/scaleform/manager.cpp'
UPDATE = '''\twhile (it != end)
\t{
\t\tSimpleGUIComponent* c = (*it);

\t\tc->update( dTime_, newResolution.x, newResolution.y );
\t\tc->applyShaders( dTime_ );

\t\tit++;
\t}
'''
SEAMS = (
    *((name, '#include "pch.hpp"\n',
       '#include "pch.hpp"\n#include "cstdmf/profiler.hpp"\n')
      for name in (SIMPLE_GUI, FLASH, MANAGER)),
    (SIMPLE_GUI, UPDATE,
     '\t{\n\t\tPROFILER_SCOPED( GUI_UpdateComponents );\n' +
     ''.join('\t' + line if line.strip() else line for line in UPDATE.splitlines(True)) +
     '\t}\n'),
    (FLASH, '\tpMovieView_->pMovieView()->Advance( dTime, 0, false );\n',
     '\t{\n'
     '\t\tPROFILER_SCOPED( GUI_FlashAdvance );\n'
     '\t\tpMovieView_->pMovieView()->Advance( dTime, 0, false );\n'
     '\t}\n'),
    (FLASH, '\t//This scoped viewport/scissor setter will remember the original settings,\n',
     '\tPROFILER_SCOPED( GUI_FlashSubmit );\n\n'
     '\t//This scoped viewport/scissor setter will remember the original settings,\n'),
    (MANAGER, '\t\tGFx::Viewport gvFullScreen;\n\t\tfullScreenViewport( gvFullScreen );\n',
     '\t\tPROFILER_SCOPED( GUI_GlobalMovies );\n\n'
     '\t\tGFx::Viewport gvFullScreen;\n\t\tfullScreenViewport( gvFullScreen );\n'),
)
TOKENS = {
    SIMPLE_GUI: ('GUI_UpdateComponents',),
    FLASH: ('GUI_FlashAdvance', 'GUI_FlashSubmit'),
    MANAGER: ('GUI_GlobalMovies',),
}


def patch_source(name, source):
    edits = [(before, after) for relative, before, after in SEAMS if relative == name]
    tokens = TOKENS[name]
    final = all(source.count(after) == 1 for before, after in edits)
    if final and all(source.count(token) == 1 for token in tokens):
        restored = source
        for before, after in edits:
            restored = restored.replace(after, before, 1)
        if all(restored.count(before) == 1 for before, after in edits):
            return source
    if (any(token in source for token in tokens) or
            any(after in source for before, after in edits) or
            any(source.count(before) != 1 for before, after in edits)):
        raise ValueError('Pinned %s GUI-profiler seam is partial, duplicated or changed' % name)
    for before, after in edits:
        source = source.replace(before, after, 1)
    return source


def patch(root):
    # Validate every file before writing any of them.
    changes = []
    for name in TOKENS:
        path = Path(root) / name
        source = path.read_text(encoding='utf-8')
        patched = patch_source(name, source)
        if patched != source:
            changes.append((path, patched))
    for path, source in changes:
        path.write_text(source, encoding='utf-8')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('engine_root', type=Path)
    patch(parser.parse_args().engine_root)
