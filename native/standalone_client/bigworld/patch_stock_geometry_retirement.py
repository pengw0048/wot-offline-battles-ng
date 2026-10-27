#!/usr/bin/env python3
"""Retire space geometry cleared or unmapped by a collision callback after the query.

The public BSP node loop keeps reading its triangles after a visitor stops, so
geometry released by a Python filter must outlive the outermost collide. Run
after patch_stock_space_data.py and patch_stock_space_sounds.py: they edit the
same ClientSpace::clear body.
"""
import argparse
from pathlib import Path

HEADER_PUBLIC = (
    '\tbool cleared() const;\n',
    '\tbool cleared() const;\n\n'
    '\t// Collision callbacks may clear or unmap geometry. Its physical\n'
    '\t// retirement waits until the outermost collide on any space returns.\n'
    '\tstatic bool geometryQueryActive();\n'
    '\tstatic void retireDeferredGeometry();\n')

HELPERS = '''
namespace
{
// #1513 scene queries run Python filters inside traversal, and the BSP node
// loop keeps reading after a visitor stops. Retire geometry a callback clears
// or unmaps once the outermost collide returns; logical clearing is immediate.
int s_geometryQueries = 0;

struct DeferredGeometry
{
	ClientSpacePtr pSpace;
	bool clear;
	ClientSpace::GeometryMappingID mappingID;
};
BW::vector< DeferredGeometry > s_deferredGeometry;

bool deferGeometry( ClientSpace * pSpace, bool clear,
	ClientSpace::GeometryMappingID mappingID )
{
	if (s_geometryQueries == 0)
	{
		return false;
	}
	for (size_t i = 0; clear && i < s_deferredGeometry.size(); ++i)
	{
		if (s_deferredGeometry[i].clear && s_deferredGeometry[i].pSpace == pSpace)
		{
			return true;
		}
	}
	DeferredGeometry entry = { pSpace, clear, mappingID };
	s_deferredGeometry.push_back( entry );
	return true;
}

class GeometryQueryScope
{
public:
	GeometryQueryScope() { ++s_geometryQueries; }
	~GeometryQueryScope()
	{
		--s_geometryQueries;
		ClientSpace::retireDeferredGeometry();
	}
};
} // namespace


bool ClientSpace::geometryQueryActive()
{
	return s_geometryQueries != 0;
}


void ClientSpace::retireDeferredGeometry()
{
	// Entries leave the queue before they run, so a retirement may re-enter.
	while (s_geometryQueries == 0 && !s_deferredGeometry.empty())
	{
		DeferredGeometry entry = s_deferredGeometry.front();
		s_deferredGeometry.erase( s_deferredGeometry.begin() );
		if (entry.clear)
		{
			entry.pSpace->doClear();
		}
		else
		{
			entry.pSpace->doDelMapping( entry.mappingID );
		}
	}
}

'''

COLLIDES = (
    ('\tCollisionCallback & cc ) const\n{\n\treturn doCollide( start, end, cc );\n}',
     '\tCollisionCallback & cc ) const\n{\n\tGeometryQueryScope scope;\n'
     '\treturn doCollide( start, end, cc );\n}'),
    ('\tCollisionCallback & cc ) const\n{\n\treturn doCollide( triangle, translation, cc );\n}',
     '\tCollisionCallback & cc ) const\n{\n\tGeometryQueryScope scope;\n'
     '\treturn doCollide( triangle, translation, cc );\n}'))

DEL_MAPPING = (
    '\tdoDelMapping( mappingID );',
    '\tif (!deferGeometry( this, false, mappingID ))\n\t{\n'
    '\t\tthis->doDelMapping( mappingID );\n\t}')

CLEAR = (
    '\tdoClear();',
    '\tif (!deferGeometry( this, true, 0 ))\n\t{\n\t\tthis->doClear();\n\t}')


def once(source, before, after, what):
    if after in source:
        return source
    if source.count(before) != 1:
        raise ValueError('ClientSpace geometry retirement anchor changed: ' + what)
    return source.replace(before, after, 1)


def body(source, signature):
    begin = source.index(signature)
    return begin, source.index('\n}', begin)


def apply(root):
    root = Path(root)
    header_path = root / 'lib/space/client_space.hpp'
    space_path = root / 'lib/space/client_space.cpp'
    header = once(header_path.read_text(encoding='utf-8-sig'), *HEADER_PUBLIC, 'header')
    space = space_path.read_text(encoding='utf-8-sig')
    space = once(space, 'namespace BW\n{\n', 'namespace BW\n{\n' + HELPERS, 'namespace')
    for before, after in COLLIDES:
        space = once(space, before, after, 'collide')
    for signature, (before, after) in (
            ('void ClientSpace::delMapping( GeometryMappingID mappingID )', DEL_MAPPING),
            ('void ClientSpace::clear()', CLEAR)):
        begin, end = body(space, signature)
        space = space[:begin] + once(space[begin:end], before, after, signature) + space[end:]
    header_path.write_text(header, encoding='utf-8')
    space_path.write_text(space, encoding='utf-8')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('engine_root', type=Path)
    apply(parser.parse_args().engine_root)
