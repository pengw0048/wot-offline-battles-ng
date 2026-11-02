"""Restore #1513 mode-1/2 edge colour deferral and World mask preparation."""
import argparse
from pathlib import Path


def replace(source, before, after):
    if source.count(after) == 1:
        return source
    if source.count(before) != 1:
        raise ValueError('The pinned edge-detection seam changed: ' + repr(before))
    return source.replace(before, after, 1)


def patch(root):
    root = Path(root)
    pending = []
    for name, anchor, insertion in (
            ('client/world_app.cpp', '    rp->endOpaqueDraw();',
             '    // #1513 00637193 prepares edges before completing opaque geometry.\n'
             '    if (!prepareStockEdgeDetection())\n'
             '        ERROR_MSG("EdgeDrawer: edge mask preparation failed\\n");\n\n'),
            ('lib/duplo/chunk_attachment.cpp',
             'void ChunkAttachment::draw( Moo::DrawContext& drawContext )\n{\n',
             '    if (stockDeferEdgeAttachment( this->pAttachment(), drawContext )) return;\n'),
            ('lib/compiled_space/py_attachment_entity_embodiment.cpp',
             'void PyAttachmentEntityEmbodiment::doDraw(Moo::DrawContext& drawContext )\n{\n',
             '    if (stockDeferEdgeAttachment( pAttachment_.get(), drawContext )) return;\n')):
        path = root / name
        source = path.read_text(encoding='utf-8')
        include = '#include "stock_hangar_presentation.hpp"'
        count = source.splitlines().count(include)
        if count > 1:
            raise ValueError('Duplicate edge-detection include: ' + name)
        if not count:
            source = replace(source, '#include "pch.hpp"', '#include "pch.hpp"\n' + include)
        if source.count(anchor) != 1 or source.count(insertion) > 1:
            raise ValueError('Duplicate or changed edge-detection owner: ' + name)
        if insertion not in source:
            if name.startswith('client/'):
                source = replace(source, anchor, insertion + anchor)
            else:
                source = replace(source, anchor, anchor + insertion)
        pending.append((path, source))
    for path, source in pending:
        if path.read_text(encoding='utf-8') != source:
            path.write_text(source, encoding='utf-8')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('engine_root', type=Path)
    patch(parser.parse_args().engine_root)
