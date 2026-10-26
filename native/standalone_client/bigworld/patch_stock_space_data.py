"""Install typed space-data scripting and its real native-space retirement hooks."""
import argparse
from pathlib import Path


def replace_once(source, before, after):
    if source.count(after) == 1:
        return source
    if source.count(before) != 1:
        raise ValueError('Unrecognized pinned typed-space lifecycle seam: '+repr(before))
    return source.replace(before, after)


def patch(root):
    root = Path(root)
    script_path = root / 'client/script_bigworld.cpp'
    script = script_path.read_text(encoding='utf-8-sig')
    script = replace_once(script, '#include "pch.hpp"', '#include "pch.hpp"\n#include "stock_space_data.hpp"')
    script = replace_once(script, '\t// Load all the standard entity scripts\n\tbool ret = EntityType::init();',
        '\t// Publish the real ordered space mapping before entity modules import it.\n'
        '\tif (!initialiseStockSpaceData()) return false;\n\n'
        '\t// Load all the standard entity scripts\n\tbool ret = EntityType::init();')
    script = replace_once(script, '\tMaterialKinds::fini();\n\tScript::fini();',
        '\tfinaliseStockSpaceData();\n\tMaterialKinds::fini();\n\tScript::fini();')
    space_path = root / 'lib/space/client_space.cpp'
    space = space_path.read_text(encoding='utf-8-sig')
    space = replace_once(space, '#include "pch.hpp"', '#include "pch.hpp"\n#include "stock_space_data.hpp"')
    space = replace_once(space, 'ClientSpace::~ClientSpace()\n{\n\tSpaceManager::instance().delSpace( this );',
        'ClientSpace::~ClientSpace()\n{\n'
        '\t// Block callback publication against a native owner already retiring.\n'
        '\tisCleared_ = true;\n'
        '\tretireStockSpaceData( id_, this );\n'
        '\tSpaceManager::instance().delSpace( this );')
    space = replace_once(space, '\tisCleared_ = true;\n\tdoClear();',
        '\tisCleared_ = true;\n'
        '\t// 684280 removes the typed object before clearing native geometry.\n'
        '\tretireStockSpaceData( id_, this );\n'
        '\tdoClear();')
    header = Path(__file__).with_name('stock_space_data.hpp').read_bytes()
    # Validate all seams before publishing either lifecycle mutation.
    script_path.write_text(script, encoding='utf-8')
    space_path.write_text(space, encoding='utf-8')
    (root / 'lib/stock_space_data.hpp').write_bytes(header)
    print('Typed native space scripting and clear/destructor ownership installed')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('engine_root', type=Path)
    patch(parser.parse_args().engine_root)
