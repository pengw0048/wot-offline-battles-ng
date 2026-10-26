"""Retire actual static-space voices before native space/script ownership ends."""
import argparse
from pathlib import Path


def once(source, before, after, marker=None):
    count=source.count(marker or after)
    if count==1:
        return source
    if count>1:
        raise ValueError('Duplicate static-sound lifecycle marker: '+repr(marker or after))
    if source.count(before) != 1:
        raise ValueError('Pinned static-sound lifecycle seam changed: '+repr(before))
    return source.replace(before, after)


def patch(root):
    root = Path(root)
    pending = []
    space_path = root/'lib/space/client_space.cpp'
    source = space_path.read_text(encoding='utf-8-sig')
    source = once(source,'#include "pch.hpp"','#include "pch.hpp"\n#include "stock_space_sounds.hpp"',
                  '#include "stock_space_sounds.hpp"')
    for signature in ('ClientSpace::~ClientSpace()', 'void ClientSpace::clear()'):
        begin=source.index(signature+'\n{')
        end=source.index('\n}',begin)
        body=source[begin:end]
        if 'retireStockSpaceSounds( this );' not in body:
            if '\tisCleared_ = true;' not in body:
                body=body.replace(signature+'\n{',signature+'\n{\n\tisCleared_ = true;',1)
            body=body.replace('\tisCleared_ = true;','\tisCleared_ = true;\n\tretireStockSpaceSounds( this );',1)
            source=source[:begin]+body+source[end:]
    pending.append((space_path,source))
    script_path = root/'client/script_bigworld.cpp'
    source = script_path.read_text(encoding='utf-8-sig')
    source = once(source,'#include "pch.hpp"','#include "pch.hpp"\n#include "stock_space_sounds.hpp"',
                  '#include "stock_space_sounds.hpp"')
    source = once(source,'\tMath_token |','\tStockSpaceSounds_token |\n\tMath_token |',
                  '\tStockSpaceSounds_token |')
    pending.append((script_path,source))
    app_path = root/'client/script_app.cpp'
    source = app_path.read_text(encoding='utf-8-sig')
    source = once(source,'#include "pch.hpp"','#include "pch.hpp"\n#include "stock_space_sounds.hpp"',
                  '#include "stock_space_sounds.hpp"')
    source = once(source,'\tBigWorldClientScript::fini();','\tfiniStockSpaceSounds();\n\tBigWorldClientScript::fini();','\tfiniStockSpaceSounds();')
    pending.append((app_path,source))
    particle_header_path=root/'lib/compiled_space/particle_system_loader.hpp'
    source=particle_header_path.read_text(encoding='utf-8-sig')
    source=once(source,'#include "loader.hpp"','#include "loader.hpp"\n#include "stock_space_sounds.hpp"',
                '#include "stock_space_sounds.hpp"')
    source=once(source,'\tDynamicSceneProvider& dynamicScene_;',
        '\tDynamicSceneProvider& dynamicScene_;\n'
        '\tClientSpace* soundSpace_ = NULL;\n'
        '\tStockSpaceSounds spaceSounds_;\n'
        '\tunsigned soundBindDepth_ = 0, soundUnloadDepth_ = 0;\n'
        '\tbool soundBound_ = false, soundUnloadPending_ = false;')
    source=once(source,'\t\tDynamicObjectHandle dynamicObjectHandle_;',
        '\t\tDynamicObjectHandle dynamicObjectHandle_;\n'
        '\t\tBW::string soundResource_;\n'
        '\t\tStockSpaceSoundHandle soundHandle_ = INVALID_STOCK_SPACE_SOUND;')
    pending.append((particle_header_path,source))
    particle_path=root/'lib/compiled_space/particle_system_loader.cpp'
    source=particle_path.read_text(encoding='utf-8-sig')
    source=once(source,'\tParticleSystemManager::fini();','\tthis->doUnload();\n\tParticleSystemManager::fini();')
    source=once(source,'\tMF_ASSERT( pReader_ == NULL );\n\tpReader_ = &reader;',
        '\tif (pReader_ || soundBindDepth_ || soundUnloadDepth_)\n'
        '\t{\n\t\tERROR_MSG("StaticSceneSoundFeature: particle mapping has not retired\\n");return false;\n\t}\n'
        '\tsoundSpace_ = pSpace;\n\tpReader_ = &reader;')
    source=once(source,'bool ParticleSystemLoader::doBind()\n{\n\taddToScene();\n\n\treturn true;\n}',
        'bool ParticleSystemLoader::doBind()\n{\n'
        '\tif (soundBindDepth_ || soundUnloadDepth_ || !pReader_) return false;\n'
        '\tif (soundBound_) return true;\n'
        '\tspaceSounds_ = acquireStockSpaceSounds( soundSpace_ );\n'
        '\tsoundBound_ = true;\n\t++soundBindDepth_;\n\taddToScene();\n\t--soundBindDepth_;\n'
        '\tif (soundUnloadPending_) { this->doUnload();return false; }\n\treturn true;\n}')
    source=once(source,'void ParticleSystemLoader::doUnload()\n{\n\tfreeParticleSystems();',
        'void ParticleSystemLoader::doUnload()\n{\n'
        '\tif (soundUnloadDepth_) return;\n'
        '\tif (soundBindDepth_) { soundUnloadPending_ = true;return; }\n'
        '\t++soundUnloadDepth_;soundBound_ = false;soundUnloadPending_ = false;\n'
        '\tfreeParticleSystems();\n\tspaceSounds_.reset();soundSpace_ = NULL;')
    source=once(source,'\tpReader_ = NULL;\n\tpStream_ = NULL;\n}',
        '\tpReader_ = NULL;\n\tpStream_ = NULL;\n\t--soundUnloadDepth_;\n}')
    source=once(source,'\tBW::string resourceID = strings.entry( def.resourceID_ );',
        '\tBW::string resourceID = strings.entry( def.resourceID_ );\n\tinstance.soundResource_ = resourceID;')
    source=once(source,'\t\tsceneObj.handle( &instance );',
        '\t\t// 6c3c50/6c3cd0 use the actual loaded particle resource name\n'
        '\t\t// and its authored world transform; no emitter-resource enumeration.\n'
        '\t\tinstance.soundHandle_ = addStockSpaceNamedSound( spaceSounds_,\n'
        '\t\t\tinstance.soundResource_, instance.pDef_->worldTransform_ );\n'
        '\t\tsceneObj.handle( &instance );')
    source=once(source,'\t\t\tbb, bb);',
        '\t\t\tbb, bb);\n'
        '\t\t// A synchronous scene observer can unload the mapping. Keep\n'
        '\t\t// its instance/mapped data alive until addObject has returned.\n'
        '\t\tif (soundUnloadPending_) break;')
    source=once(source,'\t\tdynamicScene_.removeObject( instance.dynamicObjectHandle_ );\n\t\tinstance.dynamicObjectHandle_ =\n\t\t\tDynamicObjectHandle::INVALID();',
        '\t\tconst auto sound = instance.soundHandle_;\n'
        '\t\tinstance.soundHandle_ = INVALID_STOCK_SPACE_SOUND;\n'
        '\t\tremoveStockSpaceSound( spaceSounds_, sound );\n'
        '\t\tconst auto handle = instance.dynamicObjectHandle_;\n'
        '\t\tinstance.dynamicObjectHandle_ = DynamicObjectHandle::INVALID();\n'
        '\t\tif (handle != DynamicObjectHandle::INVALID()) dynamicScene_.removeObject( handle );')
    pending.append((particle_path,source))
    header = Path(__file__).with_name('stock_space_sounds.hpp').read_bytes()
    for path,source in pending:
        path.write_text(source,encoding='utf-8')
    (root/'lib/stock_space_sounds.hpp').write_bytes(header)


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('engine_root',type=Path)
    patch(parser.parse_args().engine_root)
