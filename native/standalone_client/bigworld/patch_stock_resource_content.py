#!/usr/bin/env python3
"""Restore the physical application-content snapshot owned by BWResourceImpl."""
import argparse
from pathlib import Path


def patch(root):
    root = Path(root)
    pending = {}
    def edit(path, before, after, marker=None):
        source = pending.get(path,(root/path).read_text(encoding='utf-8-sig'))
        if (marker or after) in source:return
        if source.count(before)!=1:
            raise ValueError('Original resource-content seam changed: '+path+': '+repr(before))
        pending[path]=source.replace(before,after,1)
    cpp='lib/resmgr/bwresource.cpp'
    edit(cpp,'#include "bwresource.hpp"','#include "bwresource.hpp"\n#include "stock_resource_content.hpp"',
         '#include "stock_resource_content.hpp"')
    edit(cpp,'class BWResourceImpl\n{\npublic:',
         'class BWResourceImpl\n{\npublic:\n\tint stockContentType_ = 0;')
    edit(cpp,'\t\tnativeFileSystem_ = NativeFileSystem::create( "" );',
         '\t\tnativeFileSystem_ = NativeFileSystem::create( "" );\n'
         '#ifdef _WIN32\n'
         '\t\tstockContentType_ = loadStockResourceContentType(*nativeFileSystem_, getAppDirectory());\n'
         '#endif')
    edit(cpp,'DataSectionPtr BWResource::rootSection()\n{',
         'int BWResource::stockActiveContentType() const\n{\n\treturn pimpl_->stockContentType_;\n}\n\n'
         'DataSectionPtr BWResource::rootSection()\n{')
    header='lib/resmgr/bwresource.hpp'
    edit(header,'\tDataSectionPtr \t\t\t\trootSection();',
         '\tDataSectionPtr \t\t\t\trootSection();\n\tint stockActiveContentType() const;')
    copied=(root/'lib/stock_resource_content.hpp',Path(__file__).with_name('stock_resource_content.hpp').read_bytes())
    for path,source in pending.items():(root/path).write_text(source,encoding='utf-8')
    copied[0].write_bytes(copied[1])


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('engine_root',type=Path)
    patch(parser.parse_args().engine_root)
