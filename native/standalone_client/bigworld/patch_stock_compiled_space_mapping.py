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
    # Only the background producer owns completion. Reading progress must not
    # inspect containers that it is still constructing or advance its state.
    loader = 'lib/compiled_space/loader.hpp'
    edit(loader, '#include "compiled_space/forward_declarations.hpp"',
         '#include "compiled_space/forward_declarations.hpp"\n#include <atomic>')
    edit(loader, 'mutable LoadStatus loadStatus_;', 'std::atomic<LoadStatus> loadStatus_;')
    loader = 'lib/compiled_space/loader.cpp'
    edit(loader, '\tloadStatus_ = ILoader::LOADING;',
         '\tloadStatus_.store( ILoader::LOADING, std::memory_order_release );')
    edit(loader, '\tloadStatus_ = success ? ILoader::LOADED : ILoader::FAILED;',
         '\tloadStatus_.store( success ? ILoader::LOADED : ILoader::FAILED, std::memory_order_release );')
    edit(loader, '\tloadStatus_ = UNLOADED;',
         '\tloadStatus_.store( UNLOADED, std::memory_order_release );')
    edit(loader, '\tswitch (loadStatus_)', '\tswitch (loadStatus_.load( std::memory_order_acquire ))')
    edit(loader, '''\t\t\tfloat progress = percentLoaded();
\t\t\tif (almostEqual(progress, 1.0f, FLT_EPSILON))
\t\t\t{
\t\t\t\tloadStatus_ = ILoader::LOADED;
\t\t\t}
\t\t\treturn progress;''', '''            // The synchronous load function has not returned yet.
            return 0.f;''')
    edit(loader, '''\tif (loadStatus_ == ILoader::FAILED)
\t{
\t\treturn false;
\t}

\tMF_ASSERT( loadStatus_ == ILoader::LOADED );''',
         '''    if (loadStatus_.load( std::memory_order_acquire ) != ILoader::LOADED)
    {
        return false;
    }''')
    mapping = 'lib/compiled_space/compiled_space_mapping.hpp'
    edit(mapping, '#include "compiled_space/forward_declarations.hpp"',
         '#include "compiled_space/forward_declarations.hpp"\n#include <atomic>')
    edit(mapping, '\tState state_;', '\tState state_;\n\tstd::atomic<size_t> completedLoadStages_{0};')
    mapping = 'lib/compiled_space/compiled_space_mapping.cpp'
    edit(mapping, '\tstate_ = STATE_LOADING;',
         '\tcompletedLoadStages_.store( 0, std::memory_order_relaxed );\n\tstate_ = STATE_LOADING;')
    edit(mapping, '\tif (!assetList_.read( reader_ ))',
         '\tcompletedLoadStages_.fetch_add( 1, std::memory_order_release );\n\n\tif (!assetList_.read( reader_ ))')
    edit(mapping, '\tassetList_.preloadAssets( stringTable_ );',
         '\tassetList_.preloadAssets( stringTable_ );\n\tcompletedLoadStages_.fetch_add( 1, std::memory_order_release );')
    edit(mapping, '''\t\t\t++numFailures_;
\t\t}
\t}


\t//
\t// Entities/UDOs''', '''\t\t\t++numFailures_;
\t\t}
        completedLoadStages_.fetch_add( 1, std::memory_order_release );
\t}


\t//
\t// Entities/UDOs''')
    edit(mapping, '''\tfloat sum = 0.f;
\tint numSubsystems = 0;

\tsum += staticGeometry_.percentLoaded();
\t++numSubsystems;

\tsum += assetList_.percentLoaded();
\t++numSubsystems;

\tfor (Loaders::const_iterator it = loaders_.begin();
\t\tit != loaders_.end(); ++it)
\t{
\t\tILoader* pLoader = *it;
\t\tsum += pLoader->loadStatus();
\t\t++numSubsystems;
\t}

\tif (numSubsystems > 0)
\t{
\t\treturn sum / (float)numSubsystems;
\t}
\telse
\t{
\t\treturn 0.f;
\t}''', '''    // Geometry, preloads and each section publish only after returning.
    // Reserve the last stage for main-thread binding and mapping publication.
    // No progress observer touches background-owned vectors or scene state.
    return float(completedLoadStages_.load( std::memory_order_acquire )) /
        float(loaders_.size() + 3);''')
    shared = Path(__file__).with_name('stock_compiled_space_mapping.hpp').read_bytes()
    for relative, source in updates.items():
        (root / relative).write_text(source, encoding='utf-8')
    (root / 'lib/stock_compiled_space_mapping.hpp').write_bytes(shared)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('engine_root', type=Path)
    patch(parser.parse_args().engine_root)
