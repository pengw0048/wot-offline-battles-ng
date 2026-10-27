#!/usr/bin/env python3
"""Give the Scaleform manager the #1513 pending glyph-cache parameters and their frame consumer."""
import argparse
from pathlib import Path

# 00a930d9..00a93171: the Manager owns a pending flag and one GlyphCacheParams.
HEADER = (
    ('\t\tvoid needReloadResources() { needsReloadUnmanaged_ = true; };\n',
     '\t\tvoid needReloadResources() { needsReloadUnmanaged_ = true; };\n'
     '\n'
     '\t\t// Applied to the renderer glyph cache before the next frame.\n'
     '\t\tvoid setGlyphCacheParams( const Render::GlyphCacheParams & params );\n'),
    ('\t\tPtr<Render::Renderer2D> pRenderer_;\n',
     '\t\tPtr<Render::Renderer2D> pRenderer_;\n'
     '\t\tRender::GlyphCacheParams glyphCacheParams_;\n'
     '\t\tbool glyphCacheParamsPending_;\n'),
)
# 00a94404: the frame applies pending parameters once, before BeginFrame.
SOURCE = (
    ('\t\tneedsReloadUnmanaged_(false),\n',
     '\t\tneedsReloadUnmanaged_(false),\n'
     '\t\tglyphCacheParamsPending_(false),\n'),
    ('\t\tpRenderer_->BeginFrame();\n',
     '\t\tif (glyphCacheParamsPending_)\n'
     '\t\t{\n'
     '\t\t\tpRenderer_->GetGlyphCacheConfig()->SetParams( glyphCacheParams_ );\n'
     '\t\t\tglyphCacheParamsPending_ = false;\n'
     '\t\t}\n'
     '\n'
     '\t\tpRenderer_->BeginFrame();\n'),
    ('\tvoid Manager::setDrawTextManager(Ptr<GFx::DrawTextManager> dtm)\n',
     '\tvoid Manager::setGlyphCacheParams( const Render::GlyphCacheParams & params )\n'
     '\t{\n'
     '\t\tBW_GUARD;\n'
     '\t\tglyphCacheParams_ = params;\n'
     '\t\tglyphCacheParamsPending_ = true;\n'
     '\t}\n'
     '\n'
     '\n'
     '\tvoid Manager::setDrawTextManager(Ptr<GFx::DrawTextManager> dtm)\n'),
)
FILES = (('lib/scaleform/manager.hpp', HEADER), ('lib/scaleform/manager.cpp', SOURCE))
# Each token occurs only in the lines this patch adds.
TOKENS = {'lib/scaleform/manager.hpp': (('glyphCacheParams_', 1), ('glyphCacheParamsPending_', 1),
                                        ('setGlyphCacheParams', 1)),
          'lib/scaleform/manager.cpp': (('glyphCacheParams_', 2), ('glyphCacheParamsPending_', 4),
                                        ('setGlyphCacheParams', 1), ('GetGlyphCacheConfig', 1))}


def state(source, edits, tokens):
    old = [source.count(before) for before, unused in edits]
    new = [source.count(after) for unused, after in edits]
    found = [source.count(token) for token, unused in tokens]
    # Each insertion retains its anchor, so a final anchor also occurs exactly once.
    if old == new == [1] * len(edits) and found == [count for unused, count in tokens]:
        return 'final'
    if old == [1] * len(edits) and not any(found):
        return 'pristine'
    return None


def patch_source(relative, source):
    edits, tokens = dict(FILES)[relative], TOKENS[relative]
    current = state(source, edits, tokens)
    if current == 'final':
        return source
    if current is None:
        raise ValueError('Pinned %s glyph-cache seam is partial, duplicated or changed' % relative)
    for before, after in edits:
        source = source.replace(before, after, 1)
    if state(source, edits, tokens) != 'final':
        raise ValueError('%s glyph-cache patch did not reach its final state' % relative)
    return source


def patch(root):
    # Validate both files before writing either.
    changes = []
    for relative, unused in FILES:
        path = Path(root) / relative
        source = path.read_text(encoding='utf-8')
        patched = patch_source(relative, source)
        if patched != source:
            changes.append((path, patched))
    for path, patched in changes:
        path.write_text(patched, encoding='utf-8')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('engine_root', type=Path)
    patch(parser.parse_args().engine_root)
