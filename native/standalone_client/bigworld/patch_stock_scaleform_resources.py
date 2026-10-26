#!/usr/bin/env python3
"""Restore the #1513 GFx file opener, stream selection and retained cache owner."""
import argparse
from pathlib import Path


def patch(root):
    root = Path(root)
    path = root / 'lib/scaleform/loader.cpp'
    source = path.read_text(encoding='utf-8-sig')
    start = source.find('\tGFile* FileOpener::OpenFile(')
    end = source.find('\n\tRender::Image *BWToGFxImageLoader::LoadProtocolImage', start)
    marker = '\t// Stock FileOpener methods are defined by stock_scaleform_resources.cpp.\n'
    if marker not in source:
        if start < 0 or end < 0:
            raise ValueError('Original Scaleform file opener seam changed')
        source = source[:start] + marker + source[end:]
    header_path = root / 'lib/scaleform/loader.hpp'
    header = header_path.read_text(encoding='utf-8-sig')
    edits = (
        ('#include "resmgr/binary_block.hpp"',
         '#include "resmgr/binary_block.hpp"\n#include <memory>'),
        ('namespace ScaleformBW\n{',
         'namespace ScaleformBW\n{\n\tstruct StockScaleformFileState;'),
        ('\tclass FileOpener : public GFx::FileOpener\n\t{\n\tpublic:',
         '\tclass FileOpener : public GFx::FileOpener\n\t{\n\tpublic:\n'
         '\t\tFileOpener();\n\t\tvirtual ~FileOpener();\n'
         '\t\tvoid stockAddStreamingFile(const BW::string& filename);\n'
         '\t\tvoid stockEnableStreaming();\n\t\tvoid stockDisableStreaming();\n'
         '\t\tbool stockPrecache(const BW::string& filename, bool forceReload);\n'
         '\t\tvoid stockEraseCached(const BW::string& filename);'),
        ('\t\t\tSInt mode = FileConstants::Mode_ReadWrite);\n\t};',
         '\t\t\tSInt mode = FileConstants::Mode_ReadWrite);\n'
         '\tprivate:\n\t\tstd::unique_ptr<StockScaleformFileState> stockFiles_;\n\t};'),
    )
    for before, after in edits:
        if after in header:
            continue
        if header.count(before) != 1:
            raise ValueError('Original Scaleform file opener header changed: ' + repr(before))
        header = header.replace(before, after, 1)
    helper = Path(__file__).with_name('stock_scaleform_resources.hpp').read_bytes()
    path.write_text(source, encoding='utf-8')
    header_path.write_text(header, encoding='utf-8')
    (root / 'lib/stock_scaleform_resources.hpp').write_bytes(helper)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('engine_root', type=Path)
    patch(parser.parse_args().engine_root)
