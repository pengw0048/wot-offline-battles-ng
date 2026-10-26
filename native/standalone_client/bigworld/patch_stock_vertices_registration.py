#!/usr/bin/env python3
"""Deregister only Vertices that the original #1513 manager registered."""
import argparse
from pathlib import Path


# #1513 0x0098e142 clears the flag, 0x0098fea2 stores the map-insert result
# and 0x0098e316 removes only flagged Vertices.
REPLACEMENTS = {
    'lib/moo/vertices.hpp': ((
        '''#if ENABLE_RELOAD_MODEL
private:
\tbool\tisInVerticesManger_;
public:
\tvoid isInVerticeManger( bool newVal ) { isInVerticesManger_ = newVal; }
#endif //ENABLE_RELOAD_MODEL
''',
        '''private:
\tbool\tisInVerticesManger_;
public:
\tvoid isInVerticeManger( bool newVal ) { isInVerticesManger_ = newVal; }
'''),),
    'lib/moo/vertices.cpp': ((
        '''#if ENABLE_RELOAD_MODEL
  isInVerticesManger_( true ),
#endif //ENABLE_RELOAD_MODEL
''',
        '''  isInVerticesManger_( false ),
'''), (
        '''#if ENABLE_RELOAD_MODEL
\tbool isInManager = isInVerticesManger_;
#else
\tbool isInManager = true;
#endif
''',
        '''\tbool isInManager = isInVerticesManger_;
'''),),
    'lib/moo/vertices_manager.cpp': ((
        '''\tvertices_.insert( std::make_pair(pVertices->resourceID(), pVertices) );
''',
        '''\t// A rejected duplicate stays unregistered and never removes the owner.
\tpVertices->isInVerticeManger(
\t\tvertices_.insert( std::make_pair(pVertices->resourceID(), pVertices) ).second );
'''),),
}


def patch(root):
    root = Path(root)
    updates = []
    for name, replacements in REPLACEMENTS.items():
        path = root / name
        source = path.read_text(encoding='utf-8-sig')
        for before, after in replacements:
            # Some replacements are suffixes of their pinned text; test it first.
            if source.count(before) == 1:
                source = source.replace(before, after)
            elif source.count(after) != 1:
                raise ValueError('Unrecognized pinned vertices registration seam: ' + name)
        updates.append((path, source))
    for path, source in updates:
        path.write_text(source, encoding='utf-8')
    print('Original #1513 vertices registration ownership installed')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('engine_root', type=Path)
    patch(parser.parse_args().engine_root)
