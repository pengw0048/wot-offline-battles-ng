#!/usr/bin/env python3
"""Restore the original base-texture quality levels without disabling streaming."""
import argparse
from pathlib import Path

MANAGER = 'lib/moo/texture_manager.cpp'
HEADER = 'lib/moo/texture_manager.hpp'
DETAIL = 'lib/moo/texture_detail_level.cpp'
DETAIL_HEADER = 'lib/moo/texture_detail_level.hpp'
MANAGED = 'lib/moo/managed_texture.cpp'
MANAGED_HEADER = 'lib/moo/managed_texture.hpp'
STREAMING = 'lib/moo/texture_streaming_manager.cpp'
FILES = (MANAGER, HEADER, DETAIL, DETAIL_HEADER, MANAGED, MANAGED_HEADER, STREAMING)


def function(source, marker):
    start = source.index(marker)
    end = source.index('{', start)
    depth = 1
    while depth:
        end += 1
        depth += (source[end] == '{') - (source[end] == '}')
    return source[start:end + 1]


BASE_SKIP = '''uint32 TextureManager::stockBaseMipSkip(const BW::StringRef& resourceID,
    uint32 logicalSkip, bool split)
{
    if (logicalSkip == 0) return 0;
    const uint32 splitCount = split ? 1 : 0;
    if (logicalSkip <= splitCount) return 0;

    // 9b4a40 reserves the authored HD slot even when its file is absent.
    // 9b3f50 extends the logical mip count only for matching double dimensions.
    StreamingTextureLoader::HeaderInfo base;
    FileStreamerPtr stream = BWResource::instance().fileSystem()->streamFile(resourceID);
    uint32 logicalMips = 0;
    if (stream && StreamingTextureLoader::loadHeaderFromStream(stream.get(), base))
    {
        // Archive streams retain their file-system lock until destruction.
        stream = NULL;
        logicalMips = std::max(base.numMipLevels_, 1u);
        if (splitCount && logicalSkip >= logicalMips)
        {
            const BW::StringRef stem = BWResource::removeExtension(resourceID);
            const bool compressed = BWResource::getExtension(stem) == "c";
            const BW::string hd = removeTextureExtension(resourceID) +
                (compressed ? "_hd.c." : "_hd.") + BWResource::getExtension(resourceID);
            StreamingTextureLoader::HeaderInfo top;
            FileStreamerPtr topStream = BWResource::instance().fileSystem()->streamFile(hd);
            if (topStream && StreamingTextureLoader::loadHeaderFromStream(topStream.get(), top) &&
                top.width_ == base.width_ * 2 && top.height_ == base.height_ * 2)
                ++logicalMips;
        }
    }
    return wot::stockTextureBaseMipSkip(logicalSkip, splitCount, logicalMips);
}'''

SET_QUALITY = '''void TextureManager::setQualityOption(int optionIndex)
{
    BW_GUARD;
    int supported = std::max(0, std::min(optionIndex, 3));
    while (!wot::stockTextureQuality(supported, Moo::rc().maxAnisotropy()).supported)
        ++supported;
    if (supported != optionIndex)
    {
        // Saved values and authored presets use forced selection. Keep this
        // owner's real active option within its supported base-loader levels.
        qualitySettings_->selectOption(supported, true);
        return;
    }
    {
        RecursiveMutexHolder lock(texturesLock_);
        const wot::StockTextureQuality quality = wot::stockTextureQuality(
            optionIndex, Moo::rc().maxAnisotropy());
        for (TextureMap::iterator it = textures_.begin(); it != textures_.end(); ++it)
        {
            BaseTexture* texture = it->second;
            TextureDetailLevelPtr detail = pDetailLevels_->detailLevel(texture->resourceID());
            const uint32 skip = wot::stockTextureLogicalMipSkip(quality.mipSkip, detail->lodMode());
            // 9514f6 also reloads compressible textures when the skip is unchanged.
            if (((detail->lodMode() != 2 && skip != texture->mipSkip()) || detail->isCompressed()) &&
                !texture->isAnimated())
            {
                const BW::string name = removeTextureExtension(texture->resourceID()) +
                    (quality.compressed && detail->isCompressed() ? ".c.dds" : ".dds");
                texture->mipSkip(skip);
                // The cache owns the canonical name, while the disk name may be .c.dds.
                dirtyTextures_.push_back(std::make_pair(it->first.to_string(), name));
            }
        }
    }
    Moo::rc().clearTextureReuseList();
    reloadDirtyTextures();
}'''


def patch(root):
    root = Path(root)
    pending = {name: (root / name).read_text(encoding='utf-8-sig') for name in FILES}

    def edit(name, before, after):
        source = pending[name]
        if source.count(after) == 1:
            return
        if source.count(before) != 1:
            raise ValueError('Pinned texture quality owner changed: ' + name + ': ' + repr(before))
        pending[name] = source.replace(before, after, 1)

    def replace_function(name, marker, replacement):
        old = function(pending[name], marker)
        pending[name] = pending[name].replace(old, replacement, 1)

    edit(MANAGER, '#include "texture_manager.hpp"',
         '#include "texture_manager.hpp"\n#include "wot/stock_texture_quality.hpp"\n#include "resmgr/file_streamer.hpp"')
    edit(MANAGER, '\tthis->qualitySettings_->addOption("HIGH", "High", true);\n'
         '\tthis->qualitySettings_->addOption("MEDIUM", "Medium", true);\n'
         '\tthis->qualitySettings_->addOption("LOW", "Low", true);',
         '''    const unsigned anisotropy = Moo::rc().maxAnisotropy();
    this->qualitySettings_->addOption("MAX", "Max", false);
    this->qualitySettings_->addOption("HIGH", "High", wot::stockTextureQuality(1, anisotropy).supported);
    this->qualitySettings_->addOption("MEDIUM", "Medium", wot::stockTextureQuality(2, anisotropy).supported);
    this->qualitySettings_->addOption("LOW", "Low", true);''')
    for setting in ('compressionSettings_', 'filterSettings_'):
        edit(MANAGER, '\tMoo::GraphicsSetting::add(this->' + setting + ');',
             '\tthis->' + setting + '->stockImportant(false);\n'
             '\tMoo::GraphicsSetting::add(this->' + setting + ');')
    edit(MANAGER, '\t\tint qualitySetting = this->qualitySettings_->activeOption();',
         '\t\tint qualitySetting = wot::stockTextureQuality(this->qualitySettings_->activeOption(),\n'
         '\t\t\tMoo::rc().maxAnisotropy()).mipSkip;')
    edit(MANAGER, '\tpDetailLevels_->init();',
         '\tpDetailLevels_->init();\n'
         '\tif (!wot::stockTextureQuality(qualitySettings_->activeOption(), Moo::rc().maxAnisotropy()).supported)\n'
         '\t\tsetQualityOption(qualitySettings_->activeOption());')
    replace_function(MANAGER, 'bool TextureManager::isCompressed(', '''bool TextureManager::isCompressed(TextureDetailLevelPtr lod) const
{
    return wot::stockTextureQuality(qualitySettings_->activeOption(),
        Moo::rc().maxAnisotropy()).compressed && lod->isCompressed();
}''')
    replace_function(MANAGER, 'void TextureManager::setQualityOption(', SET_QUALITY)
    for method in ('configurableMipFilter', 'configurableMinMagFilter', 'configurableMaxAnisotropy'):
        before = function(pending[MANAGER], 'int TextureManager::' + method + '(')
        after = before.replace('this->filterSettings_->activeOption()',
                               'wot::stockTextureQuality(qualitySettings_->activeOption(), Moo::rc().maxAnisotropy()).filter')
        pending[MANAGER] = pending[MANAGER].replace(before, after, 1)
    if 'uint32 TextureManager::stockBaseMipSkip(' in pending[MANAGER]:
        replace_function(MANAGER, 'uint32 TextureManager::stockBaseMipSkip(', BASE_SKIP)
    else:
        edit(MANAGER, SET_QUALITY, BASE_SKIP + '\n\n' + SET_QUALITY)
    edit(HEADER, '\tint configurableMipFilter() const;',
         '\tstatic uint32 stockBaseMipSkip(const BW::StringRef& resourceID, uint32 logicalSkip, bool split);\n\n'
         '\tint configurableMipFilter() const;')
    edit(DETAIL_HEADER, '\tbool\t\tstreamable( ) const { return streamable_; }',
         '\tbool\t\tstreamable( ) const { return streamable_; }\n'
         '\tbool\t\tallowSplit() const { return allowSplit_; }')
    edit(DETAIL_HEADER, '\tbool\t\t\tstreamable_;', '\tbool\t\t\tstreamable_;\n\tbool\t\t\tallowSplit_;')
    edit(DETAIL, '\tstreamable_ = pSection->readBool( "streamable", streamable_ );\t',
         '\tstreamable_ = pSection->readBool( "streamable", streamable_ );\t\n'
         '\tallowSplit_ = pSection->readBool( "allowSplit", allowSplit_ );')
    edit(DETAIL, '\tpSection->writeBool( "streamable", streamable_ );',
         '\tpSection->writeBool( "streamable", streamable_ );\n'
         '\tpSection->writeBool( "allowSplit", allowSplit_ );')
    edit(DETAIL, '\tstreamable_( true )', '\tstreamable_( true ),\n\tallowSplit_( false )')
    edit(DETAIL, '\tstreamable_ = true;', '\tstreamable_ = true;\n\tallowSplit_ = false;')
    # Resolve resource policy at the existing detail owner. Background tasks
    # retain this value instead of entering the unsynchronised detail cache.
    edit(MANAGED_HEADER, '\tbool\t\t\t\tnoFilter_;',
         '\tbool\t\t\t\tnoFilter_;\n\tbool\t\t\t\tstockSplit_ = false;')
    edit(MANAGED_HEADER, '\t\tD3DFORMAT format = D3DFMT_UNKNOWN,\n'
         '\t\tint mipSkip = 0, bool noResize = false, bool noFilter = false,\n'
         '\t\tconst BW::StringRef& allocator = "texture/unknown managed texture" );',
         '\t\tD3DFORMAT format = D3DFMT_UNKNOWN,\n'
         '\t\tint mipSkip = 0, bool noResize = false, bool noFilter = false,\n'
         '\t\tconst BW::StringRef& allocator = "texture/unknown managed texture", bool stockSplit = false );')
    edit(MANAGED, '\tbool noResize, bool noFilter,\n\tconst BW::StringRef& allocator )',
         '\tbool noResize, bool noFilter,\n\tconst BW::StringRef& allocator, bool stockSplit )')
    edit(MANAGED, '  noFilter_( noFilter ),',
         '  noFilter_( noFilter ),\n  stockSplit_( stockSplit ),')
    edit(MANAGER, '\t\t\t\tlod->noResize(), lod->noFilter(), allocator );',
         '\t\t\t\tlod->noResize(), lod->noFilter(), allocator,\n'
         '\t\t\t\tlod->allowSplit() && lod->lodMode() != 0 );')
    edit(MANAGED, '\tnoFilter_( texture->noFilter_ ),',
         '\tnoFilter_( texture->noFilter_ ),\n\tstockSplit_( texture->stockSplit_ ),')
    edit(MANAGED, '\tbool\t\t\t\tnoFilter_;',
         '\tbool\t\t\t\tnoFilter_;\n\tbool\t\t\t\tstockSplit_;')
    edit(MANAGED, '\t\t\t, allocator_\n#endif\n\t\t\t);',
         '\t\t\t, allocator_\n#else\n\t\t\t, "texture/unknown managed texture"\n#endif\n\t\t\t, stockSplit_ );')
    edit(MANAGED, '\t\t\ttexture_->noFilter_\t\t\t= newTexture_->noFilter_ ;',
         '\t\t\ttexture_->noFilter_\t\t\t= newTexture_->noFilter_ ;\n'
         '\t\t\ttexture_->stockSplit_ = newTexture_->stockSplit_;')
    before = function(pending[MANAGED], 'void ManagedTexture::load( const FailurePolicy&')
    after = before.replace('input.mipSkip_ = mipSkip_;',
                           'input.mipSkip_ = TextureManager::stockBaseMipSkip(qualifiedResourceID_, mipSkip_, stockSplit_);')
    pending[MANAGED] = pending[MANAGED].replace(before, after, 1)
    edit(STREAMING, '\t\tMF_ASSERT( header.isFlatTexture_ );',
         '\t\t// Quality selection may open another stream from this archive.\n'
         '\t\tpStream = NULL;\n\n'
         '\t\tMF_ASSERT( header.isFlatTexture_ );')
    edit(STREAMING, '\tpEntry->maxAvailableStreamingMip = pEntry->maxAvailableMip - mipSkip;',
         '\tpEntry->maxAvailableStreamingMip = pEntry->maxAvailableMip -\n'
         '\t\tTextureManager::stockBaseMipSkip(pTex->resourceID(), mipSkip,\n'
         '\t\t\tpTex->detail()->allowSplit() && pTex->detail()->lodMode() != 0);')
    edit(STREAMING, '\t\t\trequestEntry_.maxAvailableMip - requestEntry_.pTexture->mipSkip();',
         '\t\t\trequestEntry_.maxAvailableMip - TextureManager::stockBaseMipSkip(\n'
         '\t\t\t\trequestEntry_.pTexture->resourceID(), requestEntry_.pTexture->mipSkip(),\n'
         '\t\t\t\trequestEntry_.pTexture->detail()->allowSplit() && requestEntry_.pTexture->detail()->lodMode() != 0);')

    for name, source in pending.items():
        (root / name).write_text(source, encoding='utf-8')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('engine_root', type=Path)
    patch(parser.parse_args().engine_root)
