#!/usr/bin/env python3
"""Resolve original #1513 vertex formats from their packing-set directory."""
import argparse
from pathlib import Path


PATH = 'lib/moo/vertex_format_cache.cpp'

MEMBER_BEFORE = '''\tFormatMap formats_;
\tSimpleMutex formatsLock_;
'''
MEMBER_AFTER = '''\tFormatMap formats_;
\tBW::StringRefSet<> missing_;
\tSimpleMutex formatsLock_;
'''

GET_BEFORE = '''const VertexFormat * VertexFormatCacheInternal::get( const BW::StringRef & formatName )
{
\tconst VertexFormat* foundFormat = find( formatName );
\tif (foundFormat)
\t{
\t\treturn foundFormat;
\t}

\t// This name doesn't exist; try to insert if the\x20
\t// vertex format can be loaded successfully.
\tDataSectionPtr pSection =\x20
\t\tBWResource::instance().openSection( getResourcePath( formatName ) );
\tVertexFormat vf;
\tif (pSection && VertexFormat::load( vf, pSection, formatName ))
\t{
\t\treturn insert( formatName, vf );
\t}
\treturn NULL;
}
'''
GET_AFTER = '''const VertexFormat * VertexFormatCacheInternal::get( const BW::StringRef & formatName )
{
\t// #1513 0x009a8000 tries "set3/<name>" before "<name>" and remembers
\t// candidates whose definition is absent or rejected.
\tconst BW::StringRef packingSet( "set3/", 5 );
\tBW::string prefixed;
\tBW::StringRef candidates[2] = { formatName, BW::StringRef() };
\tsize_t count = 1;
\tif (formatName.find( packingSet ) != 0)
\t{
\t\tprefixed = packingSet + formatName;
\t\tcandidates[0] = prefixed;
\t\tcandidates[1] = formatName;
\t\tcount = 2;
\t}

\tfor (size_t i = 0; i < count; ++i)
\t{
\t\tconst BW::StringRef & candidate = candidates[i];
\t\tconst VertexFormat* foundFormat = find( candidate );
\t\tif (foundFormat)
\t\t{
\t\t\treturn foundFormat;
\t\t}
\t\t{
\t\t\tSimpleMutexHolder smh( formatsLock_ );
\t\t\tif (missing_.count( candidate ))
\t\t\t{
\t\t\t\tcontinue;
\t\t\t}
\t\t}

\t\tDataSectionPtr pSection =
\t\t\tBWResource::instance().openSection( getResourcePath( candidate ) );
\t\tVertexFormat vf;
\t\tif (pSection && VertexFormat::load( vf, pSection, candidate ))
\t\t{
\t\t\t// Like #1513 0x009a87d0, a concurrent loader's entry wins.
\t\t\tconst VertexFormat * inserted = insert( candidate, vf );
\t\t\treturn inserted ? inserted : find( candidate );
\t\t}

\t\tSimpleMutexHolder smh( formatsLock_ );
\t\tmissing_.insert( candidate );
\t}
\treturn NULL;
}
'''

REPLACEMENTS = ((MEMBER_BEFORE, MEMBER_AFTER), (GET_BEFORE, GET_AFTER))


def patch(root):
    path = Path(root) / PATH
    source = path.read_text(encoding='utf-8-sig')
    for before, after in REPLACEMENTS:
        if source.count(before) == 1:
            source = source.replace(before, after)
        elif source.count(after) != 1:
            raise ValueError('Unrecognized pinned vertex format cache seam: ' + PATH)
    path.write_text(source, encoding='utf-8')
    print('Original #1513 packing-set vertex format lookup installed')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('engine_root', type=Path)
    patch(parser.parse_args().engine_root)
