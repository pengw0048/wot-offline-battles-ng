"""Dispatch original compiled sections with an explicit mapping visibility mask."""
import argparse
from pathlib import Path


def patch(root):
    root = Path(root)
    updates = {}

    def edit(relative, before, after):
        source = updates.get(relative)
        if source is None:
            source = (root / relative).read_text(encoding='utf-8-sig')
        if source.count(after) == 1:
            return
        if source.count(before) != 1:
            raise ValueError('Unrecognized compiled mapping seam: ' + repr(before))
        updates[relative] = source.replace(before, after, 1)

    header = 'lib/compiled_space/compiled_space.hpp'
    edit(header, '\t\tAssetClient* pAssetClient );\n\npublic:',
         '\t\tAssetClient* pAssetClient, uint32 visibilityMask );\n\npublic:')
    edit(header, '\tvoid mappingCreator( MappingCreator func );',
         '\tvoid mappingCreator( MappingCreator func );\n'
         '\tbool addMappingWithVisibilityMask( GeometryMappingID mappingID,\n'
         '\t\tMatrix & transform, const BW::string & path, uint32 visibilityMask,\n'
         '\t\tconst DataSectionPtr & settings = DataSectionPtr() );')
    cpp = 'lib/compiled_space/compiled_space.cpp'
    edit(cpp, '#include "compiled_space.hpp"',
         '#include "compiled_space.hpp"\n#include "stock_compiled_space_mapping.hpp"')
    edit(cpp, 'mappingCreator_( &DefaultCompiledSpaceMapping::create )',
         'mappingCreator_( &createStockCompiledSpaceMapping )')
    edit(cpp, '\tStaticSceneProvider::registerHandlers( this->scene() );',
         '\tStaticSceneProvider::registerHandlers( this->scene() );\n'
         '\tregisterStockCompiledSpaceHandlers( this->scene() );')
    edit(cpp, '''bool CompiledSpace::doAddMapping( GeometryMappingID mappingID,
\t\tMatrix& transform, const BW::string & path,\x20
\t\tconst SmartPointer< DataSection >& pSettings )
{
\tMF_ASSERT( this->mappingIDToIndex( mappingID ) == size_t(-1) );''',
         '''bool CompiledSpace::doAddMapping( GeometryMappingID mappingID,
        Matrix& transform, const BW::string & path,
        const SmartPointer< DataSection >& pSettings )
{
    // 00654d90: local addSpaceGeometryMapping explicitly requests all bits.
    return addMappingWithVisibilityMask( mappingID, transform, path,
        0xffffffffU, pSettings );
}

bool CompiledSpace::addMappingWithVisibilityMask( GeometryMappingID mappingID,
        Matrix & transform, const BW::string & path, uint32 visibilityMask,
        const DataSectionPtr & pSettings )
{
\tMF_ASSERT( this->mappingIDToIndex( mappingID ) == size_t(-1) );''')
    edit(cpp, '\t\tpath, transform, pLoadedSettings, *this, pAssetClient_ );',
         '\t\tpath, transform, pLoadedSettings, *this, pAssetClient_, visibilityMask );')
    # std::remove alone leaves pointers to already destroyed derived loaders.
    edit('lib/compiled_space/compiled_space_mapping.cpp',
         '\tstd::remove( loaders_.begin(), loaders_.end(), pLoader );',
         '\tloaders_.erase( std::remove( loaders_.begin(), loaders_.end(), pLoader ),\n'
         '\t\tloaders_.end() );')
    shared = Path(__file__).with_name('stock_compiled_space_mapping.hpp').read_bytes()
    for relative, source in updates.items():
        (root / relative).write_text(source, encoding='utf-8')
    (root / 'lib/stock_compiled_space_mapping.hpp').write_bytes(shared)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('engine_root', type=Path)
    patch(parser.parse_args().engine_root)
