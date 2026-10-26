"""Load the complete original #1513 BWT2 terrain through TerrainBlock2."""
import argparse
from pathlib import Path


LOAD = '''bool Terrain2SceneProvider::doLoadFromSpace( ClientSpace * pSpace,
    BinaryFormat& reader, const DataSectionPtr& pSpaceSettings,
    const Matrix& transform, const StringTable& strings )
{
    PROFILER_SCOPED( Terrain2SceneProvider_doLoadFromSpace );
    MF_ASSERT( pReader_ == NULL );
    pReader_ = &reader;
    pStream_ = pReader_->findAndOpenSection(Terrain2Types::FORMAT_MAGIC,
        Terrain2Types::FORMAT_VERSION, "Terrain2SceneProvider");
    if (!stockData_.read(pStream_, strings))
    {
        ASSET_MSG("Terrain2SceneProvider: invalid original BWT2 in '%s'.\\n", reader.resourceID());
        this->unload();
        return false;
    }
    stockData_.publish(blockData_, blockGridLookup_, sceneOctree_,
        sceneOctreeContents_.storage(), sceneContents_);
    pHeader_ = &stockData_.header;
    pTerrainSettings_ = new Terrain::TerrainSettings();
    if (!pTerrainSettings_->initStockCompiled(stockData_.source,
        stockData_.resources, stockData_.mappings))
    {
        this->unload();
        return false;
    }
    const BW::string spaceDir = BWUtil::getFilePath(reader.resourceID());
    if (!this->loadBlockInstances(spaceDir.c_str(), transform, strings))
    {
        this->unload();
        return false;
    }
    if (!pTerrainSettings_->loadStockCompiledResources(spaceDir))
    {
        this->unload();
        return false;
    }
    return true;
}
'''

BIND = '''bool Terrain2SceneProvider::doBind()
{
    if (isValid() && !stockBound_)
    {
        // #1513 006c91b0 activates the terrain renderer before publishing blocks.
        pTerrainSettings_->setActiveRenderer();
        space().scene().addProvider(this);
        stockBound_ = true;
    }
    return true;
}
'''

PROGRESS = '''float Terrain2SceneProvider::percentLoaded() const
{
    // #1513 006ca490: block loading is synchronous and each record reaches
    // one terminal outcome, including a missing resource or failed block.
    return blockInstances_.empty() ? 1.f :
        float(numBlocksLoaded_.load(std::memory_order_relaxed)) / float(blockInstances_.size());
}
'''

UNLOAD = '''void Terrain2SceneProvider::doUnload()
{
    // A failed/background load has never been registered with the scene.
    if (stockBound_)
    {
        space().scene().removeProvider(this);
        stockBound_ = false;
    }
    this->freeBlockInstances();
    pHeader_ = NULL;
    blockData_.reset();
    blockGridLookup_.reset();
    sceneContents_.reset();
    sceneOctreeContents_.storage().reset();
    sceneOctree_.storage().bounds_.reset();
    sceneOctree_.storage().centers_.reset();
    sceneOctree_.storage().nodes_.reset();
    sceneOctree_.storage().dataReferences_.reset();
    sceneOctree_.storage().parents_.reset();
    pTerrainSettings_ = NULL;
    stockData_.clear();
    resourcesPath_.clear();
    if (pStream_ && pReader_) pReader_->closeSection(pStream_);
    pStream_ = NULL;
    pReader_ = NULL;
}
'''

SETTINGS = '''// #1513 006c93e0 -> ac68b0 copies the embedded settings directly.
bool TerrainSettings::initStockCompiled(
    const std::shared_ptr<wot::StockTerrainBinary>& data,
    const BW::vector<BW::string>& resources,
    const BW::map<BW::string, uint32>& mappings)
{
    BW_GUARD;
    if (!data || data->settings[0] != TERRAIN2_VERSION ||
        resources.size() != 3 || !TerrainRenderer2::init()) return false;
    TerrainGraphicsOptions& graphics = *TerrainGraphicsOptions::instance();
    graphics.delSettings(this);
    stockBorderMap_ = NULL;
    stockCompiledData_ = data;
    stockCompiledResources_ = resources;
    stockCompiledMappings_ = mappings;
    version_ = data->settings[0];
    blockSize_ = data->header.blockSize;
    vertexLod_.setBlockSize(blockSize_);
    pRenderer_ = TerrainRenderer2::instance();
    uvProjections_ = true;
    lodMapSize_ = data->settings[2];
    aoMapSize_ = data->settings[3];
    heightMapEditorSize_ = data->settings[4];
    heightMapSize_ = data->settings[5];
    normalMapSize_ = data->settings[6];
    holeMapSize_ = data->settings[7];
    shadowMapSize_ = data->settings[8];
    blendMapSize_ = data->settings[9];
    // Original ad19b0/ae3300 use the authored count, not log2(editor size).
    numVertexLods_ = data->settings[27];
    savedVertexLodValues_.assign(data->lodDistances.begin(), data->lodDistances.end());
    vertexLod_.startBias(data->settingFloat(28));
    vertexLod_.endBias(data->settingFloat(29));
    defaultHeightMapLod_ = data->settings[30];
    detailHeightMapDistance_ = data->settingFloat(31);
    m_savedBumpParams.m_enabled = graphics.bumpMapping();
    m_savedBumpParams.m_fadingStart = data->settingFloat(14);
    m_savedBumpParams.m_fadingDistance = data->settingFloat(15);
    m_savedLodTextureParams.m_start = data->settingFloat(16);
    m_savedLodTextureParams.m_distance = data->settingFloat(17);
    m_savedLodTextureParams.m_blendPreloadDistance = data->settingFloat(18);
    // The original version-200 path has one LOD blend transition (ac9de0).
    // Its streaming/draw masks below bypass the public separate-normal path.
    m_savedLodNormalParams.m_start = 0.f;
    m_savedLodNormalParams.m_distance = 0.f;
    m_savedLodNormalParams.m_normalPreloadDistance = 0.f;
    applyLodModifier(graphics.lodModifier());
    applyLodTextureModifier(graphics.lodTextureModifier());
    applyLodNormalModifier(graphics.lodNormalModifier());
    applyBumpMappingModifier(graphics.lodBumpModifier());
    graphics.addSettings(this);
    return true;
}

bool TerrainSettings::loadStockCompiledResources(const BW::string& spaceDir)
{
    BW_GUARD;
    // #1513 ac6430 removes the leading "spaces/" component; 5cb5e0 owns
    // the resources.xml system/whiteBmp AutoConfigString fallback.
    const size_t separator = spaceDir.find('/');
    BW::string name = "maps/landscape/" +
        (separator == BW::string::npos ? spaceDir : spaceDir.substr(separator + 1));
    if (name.empty() || name[name.size() - 1] != '/') name += '/';
    name += "border.dds";
    if (BWResource::fileExists(name))
        stockBorderMap_ = Moo::TextureManager::instance()->get(name, false, false, true,
            "Terrain/File Textures/Terrain/");
    if (!stockBorderMap_)
        stockBorderMap_ = Moo::TextureManager::instance()->get(stockTerrainWhiteMap.value(), true, true, true,
            "Art/File Textures/");
    return stockBorderMap_ && stockBorderMap_->pTexture();
}

uint8 TerrainSettings::stockTextureMask(float minimum, float maximum) const
{
    const unsigned flags = wot::StockTerrainBinary::textureFlags(minimum, maximum,
        useLodTexture(), bumpMapping(), lodTextureStart(), lodTextureDistance(),
        bumpFadingStart(), bumpFadingDistance());
    uint8 result = (flags & 1) ? TerrainRenderer2::RTM_DrawLOD : 0;
    if (flags & 4) result |= TerrainRenderer2::RTM_DrawBlend |
        TerrainRenderer2::RTM_PreLoadBlend | TerrainRenderer2::RTM_PreloadNormals;
    if (flags & 8) result |= TerrainRenderer2::RTM_DrawBump;
    return result;
}

'''

SETTINGS_DECLARATIONS = '''
#ifndef MF_SERVER
    bool initStockCompiled(const std::shared_ptr<wot::StockTerrainBinary>& data,
        const BW::vector<BW::string>& resources, const BW::map<BW::string, uint32>& mappings);
    const wot::StockTerrainBinary* stockCompiledData() const { return stockCompiledData_.get(); }
    uint8 stockTextureMask(float minimum, float maximum) const;
    bool loadStockCompiledResources(const BW::string& spaceDir);
    const SmartPointer<Moo::BaseTexture>& stockBorderMap() const { return stockBorderMap_; }
#endif
'''

SETTINGS_MEMBERS = '''
#ifndef MF_SERVER
    std::shared_ptr<const wot::StockTerrainBinary> stockCompiledData_;
    BW::vector<BW::string> stockCompiledResources_;
    BW::map<BW::string, uint32> stockCompiledMappings_;
    SmartPointer<Moo::BaseTexture> stockBorderMap_;
#endif
'''

RESET_COMPILED = '''
#ifndef MF_SERVER
    if (stockCompiledData_)
    {
        stockCompiledData_.reset();
        stockCompiledResources_.clear();
        stockCompiledMappings_.clear();
        stockBorderMap_ = NULL;
    }
#endif
'''

MATERIAL_MEMBERS = '''
        D3DXHANDLE stockUseBump_;
        D3DXHANDLE stockMultipassBranch_;
        D3DXHANDLE stockUseHoles_;
        D3DXHANDLE stockOuterBorder_;
        D3DXHANDLE stockInverseBorder_;
        D3DXHANDLE stockBorderBlend_;
        D3DXHANDLE stockBorderMap_;
        D3DXHANDLE stockUseDetailNormal_;
        D3DXHANDLE stockDetailNormalMap_;
        D3DXHANDLE stockEnablePuddles_;
        D3DXHANDLE stockPuddlesColor_;
'''

MATERIAL_HANDLES = '''
    stockUseBump_ = pEffect->GetParameterByName(NULL, "g_useBump");
    stockMultipassBranch_ = pEffect->GetParameterByName(NULL, "useMultipassBlendingBranch");
    stockUseHoles_ = pEffect->GetParameterByName(NULL, "g_useHoles");
    stockOuterBorder_ = pEffect->GetParameterByName(NULL, "outerBorder");
    stockInverseBorder_ = pEffect->GetParameterByName(NULL, "invBorderDiffMul2Blend");
    stockBorderBlend_ = pEffect->GetParameterByName(NULL, "borderBlend");
    stockBorderMap_ = pEffect->GetParameterByName(NULL, "borderBlendMap");
    stockUseDetailNormal_ = pEffect->GetParameterByName(NULL, "useDetailNormal");
    stockDetailNormalMap_ = pEffect->GetParameterByName(NULL, "detailNormalMap");
    stockEnablePuddles_ = pEffect->GetParameterByName(NULL, "enablePuddles");
    stockPuddlesColor_ = pEffect->GetParameterByName(NULL, "puddlesColor");
'''

MATERIAL_CONSTANTS = '''
    if (const wot::StockTerrainBinary* data = pBlock->settings()->stockCompiledData())
    {
        const auto border = data->borderParameters();
        const Vector4 outer(border.outer[0], border.outer[1], border.outer[2], border.outer[3]);
        const Vector4 inverse(border.inverseDifference[0], border.inverseDifference[1],
            border.inverseDifference[2], border.inverseDifference[3]);
        pMaterial->SetParam(pMaterial->stockOuterBorder_, &outer);
        pMaterial->SetParam(pMaterial->stockInverseBorder_, &inverse);
        pMaterial->SetParam(pMaterial->stockBorderBlend_, border.blend);
        const SmartPointer<Moo::BaseTexture>& map = pBlock->settings()->stockBorderMap();
        pMaterial->SetParam(pMaterial->stockBorderMap_, map ? map->pTexture() : NULL);
        // Original BWT2 ctor/load/bind leave detail/global/noise texture slots
        // empty. acbaf0 explicitly disables detail normals in this state.
        pMaterial->SetParam(pMaterial->stockDetailNormalMap_, static_cast<DX::BaseTexture*>(NULL));
        pMaterial->SetParam(pMaterial->stockUseDetailNormal_, false);
        // Original acd5d0 tests SettingsData bit 4 and forwards its RGBA.
        const bool puddles = (data->settings[1] & 4) != 0;
        pMaterial->SetParam(pMaterial->stockEnablePuddles_, puddles);
        if (puddles)
        {
            const Vector4 colour(data->settingFloat(10), data->settingFloat(11),
                data->settingFloat(12), data->settingFloat(13));
            pMaterial->SetParam(pMaterial->stockPuddlesColor_, &colour);
        }
    }
'''

TECHNIQUE_HELPER = '''
    bool selectStockTerrainTechnique(Moo::EffectMaterial& material,
        const char* legacy, const char* original)
    {
        if (!material.pEffect() || !material.pEffect()->pEffect()) return false;
        const bool stock = wot::is_stock_effect(material.pEffect()->pEffect());
        return stock ? material.setTechnique(original) : (!legacy || material.setTechnique(legacy));
    }

'''


def replace_once(text, before, after):
    if text.count(after) == 1:
        return text
    if text.count(before) != 1:
        raise ValueError('Unrecognized pinned terrain seam: ' + before[:90])
    return text.replace(before, after)


def function(text, signature, replacement):
    start = text.index(signature)
    body = text.index('{', start)
    depth = 1
    end = body + 1
    while depth:
        if text[end] == '{':
            depth += 1
        elif text[end] == '}':
            depth -= 1
        end += 1
    return text[:start] + replacement.rstrip() + text[end:]


def patch(root):
    root = Path(root)
    directory = root / 'lib/compiled_space'
    paths = [directory / 'terrain2_types.hpp', directory / 'terrain2_scene_provider.hpp',
             directory / 'terrain2_scene_provider.cpp']
    types, header, source = [path.read_text(encoding='utf-8-sig') for path in paths]
    types = replace_once(types, 'FORMAT_VERSION = 0x00000001;', 'FORMAT_VERSION = 0x00000002;')
    types = replace_once(types, '\t\t\tint32 gridMaxZ_;',
                         '\t\t\tint32 gridMaxZ_;\n\t\t\tuint32 stockResourceKeys_[3];')
    header = replace_once(header, '#include "compiled_space/terrain2_types.hpp"',
                          '#include "compiled_space/terrain2_types.hpp"\n#include "compiled_space/stock_space_terrain.hpp"\n#include <atomic>')
    header = replace_once(header, '\t\tBinaryFormat* pReader_;',
                          '\t\tStockTerrain2Data stockData_;\n\t\tbool stockBound_;\n\t\tBinaryFormat* pReader_;')
    header = replace_once(header, '\t\tsize_t numBlocksLoaded_;',
        '\t\tstd::atomic<size_t> numBlocksLoaded_;')
    source = replace_once(source, '\tpReader_( NULL ),', '\tstockBound_( false ),\n\tpReader_( NULL ),')
    source = function(source, 'bool Terrain2SceneProvider::doLoadFromSpace(', LOAD)
    source = function(source, 'bool Terrain2SceneProvider::doBind()', BIND)
    source = function(source, 'void Terrain2SceneProvider::doUnload()', UNLOAD)
    source = function(source, 'float Terrain2SceneProvider::percentLoaded() const', PROGRESS)
    source = replace_once(source, 'return blockData_.valid() && pHeader_ != NULL;',
                          'return pHeader_ != NULL && stockData_.source && pTerrainSettings_;')
    source = replace_once(source, '\t\tuint32 gridIndex = ',
                          '''        if (!provider.isValid() || !std::isfinite(position.x) || !std::isfinite(position.z)) return NULL;
        const float gridX = floorf(position.x / provider.pHeader_->blockSize_);
        const float gridZ = floorf(position.z / provider.pHeader_->blockSize_);
        if (gridX < provider.pHeader_->gridMinX_ || gridX > provider.pHeader_->gridMaxX_ ||
            gridZ < provider.pHeader_->gridMinZ_ || gridZ > provider.pHeader_->gridMaxZ_) return NULL;
\t\tuint32 gridIndex = ''')
    source = replace_once(source, 'if (index == Terrain2Types::INVALID_BLOCK_REFERENCE)',
                          'if (index == Terrain2Types::INVALID_BLOCK_REFERENCE || index >= provider.blockInstances_.size())')
    source = replace_once(source, '\t\t\tcontinue;\n\t\t}\n\n\t\tchar buffer[BW_MAX_PATH];',
                          '\t\t\t++numBlocksLoaded_;\n\t\t\tcontinue;\n\t\t}\n\n\t\tchar buffer[BW_MAX_PATH];')
    source = replace_once(source, 'invalid string table index %d.',
        'unknown terrain resource key %08x.')
    source = replace_once(source, '\n\t\t\tintersection.insert( instance.sceneObject_ );',
                          '\n\t\t\tif (!instance.pBlock_) continue;\n\t\t\tintersection.insert( instance.sceneObject_ );')
    source = replace_once(source, 'if (hull.intersects( instance.worldBB_ ))',
                          'if (instance.pBlock_ && hull.intersects( instance.worldBB_ ))')
    source = replace_once(source, '\t\t\tTerrain::TerrainHoleMap &thm = pInst->pBlock_->holeMap();',
                          '\t\t\tif (!pInst || !pInst->pBlock_) continue;\n\t\t\tTerrain::TerrainHoleMap &thm = pInst->pBlock_->holeMap();')
    settings_header_path = root / 'lib/terrain/terrain_settings.hpp'
    settings_source_path = root / 'lib/terrain/terrain_settings.cpp'
    settings_header = settings_header_path.read_text(encoding='utf-8-sig')
    settings_source = settings_source_path.read_text(encoding='utf-8-sig')
    settings_header = replace_once(settings_header, '#include "resmgr/datasection.hpp"',
        '#include "resmgr/datasection.hpp"\n#include "cstdmf/bw_map.hpp"\n#include <memory>\n'
        'namespace wot { struct StockTerrainBinary; }')
    settings_header = replace_once(settings_header, '\t~TerrainSettings();',
        '\t~TerrainSettings();\n' + SETTINGS_DECLARATIONS)
    settings_header = replace_once(settings_header, 'namespace Terrain\n{',
        'namespace Moo { class BaseTexture; }\n\nnamespace Terrain\n{')
    settings_header = replace_once(settings_header, '\tuint32 version_;',
        SETTINGS_MEMBERS + '\n\tuint32 version_;')
    settings_source = replace_once(settings_source, '#include "terrain_settings.hpp"',
        '#include "terrain_settings.hpp"\n#include "wot/stock_terrain_binary.hpp"\n'
        '#include "moo/texture_manager.hpp"\n#include "resmgr/bwresource.hpp"')
    settings_source = replace_once(settings_source, 'namespace\n{\n\t// This function returns',
        'namespace\n{\n#ifndef MF_SERVER\n'
        '    AutoConfigString stockTerrainWhiteMap("system/whiteBmp");\n#endif\n'
        '\t// This function returns')
    if SETTINGS not in settings_source:
        settings_source = replace_once(settings_source, 'void TerrainSettings::setActiveRenderer()',
            SETTINGS + 'void TerrainSettings::setActiveRenderer()')
    if RESET_COMPILED not in settings_source:
        start = settings_source.index('bool TerrainSettings::init(')
        guard = settings_source.index('BW_GUARD;', start) + len('BW_GUARD;')
        settings_source = settings_source[:guard] + RESET_COMPILED + settings_source[guard:]
    block_path = root / 'lib/terrain/terrain2/terrain_block2.cpp'
    renderer_path = root / 'lib/terrain/terrain2/terrain_renderer2.cpp'
    renderer_header_path = root / 'lib/terrain/terrain2/terrain_renderer2.hpp'
    block = block_path.read_text(encoding='utf-8-sig')
    renderer = renderer_path.read_text(encoding='utf-8-sig')
    renderer_header = renderer_header_path.read_text(encoding='utf-8-sig')
    if 'lodRenderInfo_.renderTextureMask_ = settings_->stockCompiledData() ?' not in block:
        load_start = block.index('\t\tlodRenderInfo_.renderTextureMask_ = TerrainRenderer2::getLoadFlag(')
        load_end = block.index(';', load_start) + 1
        original_load = block[load_start:load_end]
        block = block[:load_start] + original_load.replace('= TerrainRenderer2::getLoadFlag(',
            '= settings_->stockCompiledData() ?\n'
            '\t\t\tsettings_->stockTextureMask(distanceInfo_.minDistanceToCamera_, distanceInfo_.maxDistanceToCamera_) :\n'
            '\t\t\tTerrainRenderer2::getLoadFlag(') + block[load_end:]
    renderer = replace_once(renderer, '    MF_ASSERT_DEBUG( partialResult != 0 );',
        '    if (pSettings->stockCompiledData()) return pSettings->stockTextureMask(minDistance, maxDistance);\n'
        '    MF_ASSERT_DEBUG( partialResult != 0 );')
    renderer = replace_once(renderer, '#include "terrain_renderer2.hpp"',
        '#include "terrain_renderer2.hpp"\n#include "stock_effect_adapter.hpp"\n'
        '#include "wot/stock_terrain_binary.hpp"\n#include "moo/base_texture.hpp"')
    renderer = replace_once(renderer, 'namespace\n{', 'namespace\n{' + TECHNIQUE_HELPER)
    for name, legacy, original in (
            ('texturedMaterial_', 'MAIN', 'Color'),
            ('texturedReflectionMaterial_', 'REFLECTION', 'Reflection'),
            ('lodTextureMaterial_', 'MAIN', 'Color'),
            ('lodTextureReflectionMaterial_', 'REFLECTION', 'Reflection')):
        before = name + '.pEffect_->hTechnique("' + legacy + '");'
        after = '(void)selectStockTerrainTechnique(*' + name + '.pEffect_, "' + legacy + '", "' + original + '");'
        begin = renderer.index('bool TerrainRenderer2::initInternal(')
        end = renderer.index('TerrainRenderer2::~TerrainRenderer2()', begin)
        checked = 'success &= selectStockTerrainTechnique(*' + name + '.pEffect_, "' + legacy + '", "' + original + '");'
        block_initial = replace_once(renderer[begin:end], before, checked)
        block_reload = replace_once(renderer[end:], before, after)
        renderer = renderer[:begin] + block_initial + block_reload
    for name, original in (('zPassMaterial_', 'ZPass'), ('shadowCastMaterial_', 'ShadowCast')):
        begin = renderer.index('bool TerrainRenderer2::initInternal(')
        end = renderer.index('TerrainRenderer2::~TerrainRenderer2()', begin)
        before = '        ' + name + '.getHandles();'
        call = 'selectStockTerrainTechnique(*' + name + '.pEffect_, NULL, "' + original + '");'
        block_initial = replace_once(renderer[begin:end], before,
            '        success &= ' + call + '\n' + before)
        block_reload = replace_once(renderer[end:], before,
            '        (void)' + call + '\n' + before)
        renderer = renderer[:begin] + block_initial + block_reload
    renderer_header = replace_once(renderer_header, '        D3DXHANDLE              bumpFading_;',
        '        D3DXHANDLE              bumpFading_;\n' + MATERIAL_MEMBERS)
    renderer = replace_once(renderer,
        '    bumpShaderMask_         = pEffect->GetParameterByName( NULL, "bumpShaderMask" );',
        '    bumpShaderMask_         = pEffect->GetParameterByName( NULL,\n'
        '        wot::is_stock_effect(pEffect) ? "g_bumpMaskIndex" : "bumpShaderMask" );\n' + MATERIAL_HANDLES)
    renderer = replace_once(renderer,
        '    pMaterial->SetParam( pMaterial->bumpShaderMask_, bumpShaderMask );',
        '    pMaterial->SetParam( pMaterial->bumpShaderMask_, bumpShaderMask );\n'
        '    pMaterial->SetParam( pMaterial->stockUseBump_, bumped && bumpShaderMask != 0 );')
    renderer = replace_once(renderer,
        '    pMaterial->SetParam( pMaterial->useMultipassBlending_, blended );',
        '    pMaterial->SetParam( pMaterial->useMultipassBlending_, blended );\n'
        '    pMaterial->SetParam( pMaterial->stockMultipassBranch_, blended );')
    renderer = replace_once(renderer,
        '        pMaterial->SetParam( pMaterial->useMultipassBlending_, doBlends );',
        '        pMaterial->SetParam( pMaterial->useMultipassBlending_, doBlends );\n'
        '        pMaterial->SetParam( pMaterial->stockMultipassBranch_, doBlends );')
    renderer = replace_once(renderer,
        '    setTransformConstants( pBlock, pMaterial );',
        '    setTransformConstants( pBlock, pMaterial );\n' + MATERIAL_CONSTANTS)
    renderer = replace_once(renderer,
        '    pMaterial->SetParam( pMaterial->holesSize_, (float)pBlock->holesSize() );',
        '    pMaterial->SetParam( pMaterial->holesSize_, (float)pBlock->holesSize() );\n'
        '    pMaterial->SetParam( pMaterial->stockUseHoles_, pBlock->holesSize() != 0 );')
    renderer = replace_once(renderer,
        '            setHolesMapConstants( pBlock, pMaterial );\n        }',
        '            setHolesMapConstants( pBlock, pMaterial );\n        }\n'
        '        else pMaterial->SetParam(pMaterial->stockUseHoles_, false);')
    renderer = replace_once(renderer, '    // Update constants for this draw method',
        '    // Select the actual Properties owner before any mutable ARIE write.\n'
        '    if (!pMaterial->pEffect_->properties().select(*pMaterial->pEffect_->pEffect())) return false;\n\n'
        '    // Update constants for this draw method')
    renderer = replace_once(renderer,
        '                else if (DM_SHADOW_CAST != drawMethod && DM_Z_ONLY != drawMethod)',
        '                else if (DM_SHADOW_CAST != drawMethod && DM_Z_ONLY != drawMethod &&\n'
        '                    (!wot::is_stock_effect(pEffectMaterial->pEffect()->pEffect()) || !Moo::rc().reflectionScene()))')
    graphics_path = root / 'lib/terrain/terrain_graphics_options.cpp'
    graphics = graphics_path.read_text(encoding='utf-8-sig')
    # #1513 adaed0, Holder<GraphicsOption> value+8. Texture/bump scales and
    # mesh biases already match; the public vertex scales do not.
    for old, new in (
        ('GraphicsOption(true,  0, 1.0f, 0.8f', 'GraphicsOption(true,  0, 0.8f, 0.8f'),
        ('GraphicsOption(true,  0, 1.0f, 0.6f', 'GraphicsOption(true,  0, 0.7f, 0.6f'),
        ('GraphicsOption(false, 0, 1.0f, 0.4f', 'GraphicsOption(false, 0, 0.6f, 0.4f'),
        ('GraphicsOption(false, 1, 1.0f, 0.2f', 'GraphicsOption(false, 1, 0.6f, 0.2f')):
        graphics = replace_once(graphics, old, new)
    index_path = root / 'lib/terrain/terrain2/terrain_index_buffer.cpp'
    index = index_path.read_text(encoding='utf-8-sig')
    index = replace_once(index, '#include "pch.hpp"',
        '#include "pch.hpp"\n#include "stock_effect_adapter.hpp"\n#include "wot/stock_terrain_binary.hpp"')
    index = replace_once(index,
        '\t\tSAFE_SET( pEffect, Float, "lodStart",\tmorphValue.x );\n'
        '\t\tSAFE_SET( pEffect, Float, "lodEnd",\t\tmorphValue.y );',
        '''        if (wot::is_stock_effect(pEffect))
        {
            const auto values = wot::StockTerrainBinary::lodParameters(morphValue.x, morphValue.y);
            const Vector4 parameters(values[0], values[1], values[2], values[3]);
            const D3DXHANDLE handle = pEffect->GetParameterByName(NULL, "lodParams");
            if (handle) pEffect->SetVector(handle, &parameters);
        }
        else
        {
            SAFE_SET( pEffect, Float, "lodStart", morphValue.x );
            SAFE_SET( pEffect, Float, "lodEnd", morphValue.y );
        }''')
    # Validate every seam before writing the patched public sources.
    for path, text in zip(paths, (types, header, source)):
        path.write_text(text, encoding='utf-8')
    (directory / 'stock_space_terrain.hpp').write_bytes(
        Path(__file__).with_name('stock_space_terrain.hpp').read_bytes())
    settings_header_path.write_text(settings_header, encoding='utf-8')
    settings_source_path.write_text(settings_source, encoding='utf-8')
    block_path.write_text(block, encoding='utf-8')
    renderer_path.write_text(renderer, encoding='utf-8')
    renderer_header_path.write_text(renderer_header, encoding='utf-8')
    graphics_path.write_text(graphics, encoding='utf-8')
    index_path.write_text(index, encoding='utf-8')
    binary_header = root / 'lib/wot/stock_terrain_binary.hpp'
    binary_header.parent.mkdir(parents=True, exist_ok=True)
    binary_header.write_bytes((Path(__file__).resolve().parents[1] /
        'include/wot/stock_terrain_binary.hpp').read_bytes())
    print('Original BWT2 terrain scene owner installed')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('engine_root', type=Path)
    patch(parser.parse_args().engine_root)
