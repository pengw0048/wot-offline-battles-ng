"""Select authored #1513 banks from real camera-space geometry mappings.

Run after the compiled mapping, space lifecycle, space sounds and zones patches.
"""
import argparse
from pathlib import Path

PATH='client/facade_app.cpp'
BEFORE='''        lastCameraSpace_ = pSpace;
        enviro.activate();'''
# Before activation and onCameraSpaceChange's music/ambient script producers.
AFTER='''        lastCameraSpace_ = pSpace;
        notifyStockAudioCameraSpace( pSpace.get() );
        enviro.activate();'''
INCLUDE='#include "stock_audio.hpp"'
MAPPING_ACCESS='''
\tconst BW::string & stockAudioGeometryPath() const { return path_; }
\tconst DataSectionPtr & stockAudioSettings() const { return pSettings_; }
'''
SPACE_QUERY='''
// Query the existing mapping owner whose complete settings are the current
// environment. No second path registry or stale space-ID cache is created.
BW::string CompiledSpace::stockAudioGeometryPath()
{
\tconst DataSectionPtr settings = this->enviro().pData();
\tfor (Mappings::reverse_iterator it = mappings_.rbegin(); it != mappings_.rend(); ++it)
\t{
\t\tif (it->pLoader_ && it->pLoader_->stockAudioSettings() == settings)
\t\t\treturn it->pLoader_->stockAudioGeometryPath();
\t}
\treturn BW::string();
}

'''


def patch(root):
    root=Path(root);pending={}
    def edit(name,before,after):
        path=root/name
        source=pending.get(path)
        if source is None:source=path.read_text(encoding='utf-8-sig')
        if after not in source:
            if source.count(before)!=1:raise ValueError('Pinned audio mapping seam changed: '+name+': '+before)
            source=source.replace(before,after,1)
        pending[path]=source
    edit(PATH,BEFORE,AFTER)
    edit(PATH,'#include "pch.hpp"','#include "pch.hpp"\n'+INCLUDE)
    mapping='lib/compiled_space/compiled_space_mapping.hpp'
    edit(mapping,'\tState state() const;','\tState state() const;\n'+MAPPING_ACCESS)
    header='lib/compiled_space/compiled_space.hpp'
    edit(header,'\tvoid mappingCreator( MappingCreator func );',
         '\tvoid mappingCreator( MappingCreator func );\n\tBW::string stockAudioGeometryPath();')
    cpp='lib/compiled_space/compiled_space.cpp'
    edit(cpp,'#include "compiled_space.hpp"','#include "compiled_space.hpp"\n'+INCLUDE)
    edit(cpp,'bool CompiledSpace::doAddMapping(',SPACE_QUERY+'bool CompiledSpace::doAddMapping(')
    # A camera may already refer to this space before its geometry is added.
    # Publish after the actual environment owner and before load completion can
    # spawn entities/UDOs/ambient producers. Inactive preload cannot switch banks.
    edit(cpp,'\t\tenviroMinderScene_.loadEnvironment( pLoadedSettings );',
         '\t\tenviroMinderScene_.loadEnvironment( pLoadedSettings );\n\t\tnotifyStockAudioCameraSpace( this );')
    edit(cpp,'\t\t\tmappings_.erase( mappings_.begin() + i );',
         '\t\t\tmappings_.erase( mappings_.begin() + i );\n\t\t\tnotifyStockAudioCameraSpace( this );')
    edit(cpp,'\tretired.swap( mappings_ );','\tretired.swap( mappings_ );\n\tnotifyStockAudioCameraSpace( this );')
    for path,source in pending.items():path.write_text(source,encoding='utf-8')


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('engine_root',type=Path)
    patch(parser.parse_args().engine_root)
