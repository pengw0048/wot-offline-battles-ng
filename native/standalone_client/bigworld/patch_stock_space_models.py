#!/usr/bin/env python3
"""Install the original compiled static-model owner and animation read seam."""
import argparse
from pathlib import Path


TOUCH = '''void Animation::touchStockCompiledFrame( float frame ) const
{
    BW_GUARD;
    // #1513 966a90 touches the actual frame and both interpolation windows.
    if (!pStreamer_) return;
    pStreamer_->touch( frame );
    pStreamer_->touch( std::min( totalTime_, frame + 8.f ) );
    pStreamer_->touch( std::max( 0.f, frame - 8.f ) );
}

'''


COMPILED_LOD = r'''void LodSettings::initStockCompiled(DataSectionPtr config)
{
    // #1513 a36d40 reads lod/default/staticModel/lod0..4 from AppConfig.
    for (unsigned lod = 0; lod < 5; ++lod)
    {
        BW::string path = "lod/default/staticModel/lod";
        path.push_back(static_cast<char>('0' + lod));
        stockCompiledLods_[lod] = config ? config->readFloat(path, 1.f) : 1.f;
    }
}

float LodSettings::stockCompiledLodScale() const
{
#ifdef EDITOR_ENABLED
    if (ignoreLods_) return 0.f;
#endif
    if (!lodSettings_) return 1.f;
    const int option = lodSettings_->activeOption();
    MF_ASSERT(option >= 0 && static_cast<size_t>(option) < lodOptions_.size());
    return lodOptions_[option];
}

float LodSettings::stockCompiledLodDefault(unsigned lod) const
{
    MF_ASSERT(lod < 5);
    return stockCompiledLods_[lod];
}

'''


PREVIOUS_CONSTANTS = r'''class StockPreviousWorldConstant : public EffectConstantValue
{
public:
    bool operator()(ID3DXEffect* effect, D3DXHANDLE handle)
    {
        const Matrix* previous = rc().effectVisualContext().stockPreviousWorld();
        return previous && SUCCEEDED(effect->SetMatrix(handle, previous));
    }
};

class StockPreviousWorldOffsetConstant : public EffectConstantValue
{
public:
    bool operator()(ID3DXEffect* effect, D3DXHANDLE handle)
    {
        // #1513 6d4090 appends previous bone rows to the actual WorldPalette.
        const float rows = static_cast<float>(rc().effectVisualContext().stockPreviousPaletteEntries() * 3);
        return SUCCEEDED(effect->SetFloat(handle, rows));
    }
};

'''

STOCK_INSTANCE_ALLOCATOR = r'''DrawContext::InstanceData* DrawContext::allocStockInstanceData( uint32 paletteEntries )
{
    if (paletteEntries)
    {
        // The original packet contains current rows followed by previous rows.
        InstanceData* data = allocInstanceData(paletteEntries * 2);
        data->stockPreviousPaletteEntries_ = paletteEntries;
        return data;
    }
    InstanceData* data = static_cast<InstanceData*>(instanceDataAllocator_.allocate(
        sizeof(InstanceData) + sizeof(Matrix) * 2));
    data->paletteSize_ = 0;
    data->stockPreviousPaletteEntries_ = 0;
    data->stockPreviousWorld_ = true;
    data->stockAnimatedWorld_ = false;
    return data;
}

'''


LEGACY_SORT_DISTANCE = '''\t\tsortOp.cameraDistance_ =
\t\t\trc().view().applyPoint( worldBB.centre() +\x20
\t\t\tprimitives->origin( primitiveGroupIndex )).z;'''
NATIVE_SORT_DISTANCE = '''        if (instanceData->stockPreviousWorld_ || instanceData->stockPreviousPaletteEntries_)
        {
            // #1513 93d4a0 uses the authored world visibility-bound centre
            // and camera inverse-view translation. Compiled primitives do not
            // have the public Visual loader's groupOrigins_ side product.
            const Vector3 difference = rc().invView().applyToOrigin() - worldBB.centre();
            sortOp.cameraDistance_ = difference.length();
        }
        else
        {
\t\tsortOp.cameraDistance_ =
\t\t\trc().view().applyPoint( worldBB.centre() +\x20
\t\t\tprimitives->origin( primitiveGroupIndex )).z;
        }'''


def previous_poses(root):
    paths = [root / 'lib/moo' / name for name in (
        'draw_context.hpp', 'draw_context.cpp', 'effect_visual_context.hpp', 'effect_visual_context.cpp')]
    dh, dc, vh, vc = [path.read_text(encoding='utf-8-sig') for path in paths]
    dh = replace(dh, '\t\tuint32\t\t\tpaletteSize_;',
        '\t\tuint32 stockPreviousPaletteEntries_;\n'
        '\t\tbool stockPreviousWorld_;\n'
        '\t\tbool stockAnimatedWorld_;\n'
        '\t\tuint32\t\t\tpaletteSize_;', 'draw packet previous-pose ownership')
    dh = replace(dh, '\tInstanceData*\tallocInstanceData( uint32 numPalletteEntries );',
        '\tInstanceData* allocStockInstanceData( uint32 paletteEntries );\n'
        '\tInstanceData*\tallocInstanceData( uint32 numPalletteEntries );', 'complete current/previous packet allocator')
    dc = replace(dc, '\tdata->paletteSize_ = numPalletteEntries;',
        '\tdata->stockPreviousPaletteEntries_ = 0;\n'
        '\tdata->stockPreviousWorld_ = false;\n'
        '\tdata->stockAnimatedWorld_ = false;\n'
        '\tdata->paletteSize_ = numPalletteEntries;', 'initialise every legacy packet history guard')
    dc = replace(dc, 'bool isInstanced = canUseHWInstancing && channelType == OPAQUE_CHANNEL && !isSkinned;',
        'bool isInstanced = canUseHWInstancing && channelType == OPAQUE_CHANNEL && !isSkinned && !instanceData->stockAnimatedWorld_;',
        'animated rigid packets retain their individual previous transforms')
    dc = replace(dc, LEGACY_SORT_DISTANCE, NATIVE_SORT_DISTANCE, 'original compiled transparent camera distance')
    allocator = 'DrawContext::InstanceData*\tDrawContext::allocInstanceData( uint32 numPalletteEntries )'
    if STOCK_INSTANCE_ALLOCATOR not in dc:
        dc = replace(dc, allocator, STOCK_INSTANCE_ALLOCATOR + allocator, 'native packet allocation')
    dc = replace(dc, '\t\t\teffectVisualContext_.worldMatrix( instanceData->matrix() );',
        '\t\t\teffectVisualContext_.worldMatrix( instanceData->matrix() );\n'
        '\t\t\tif (instanceData->stockPreviousWorld_)\n'
        '\t\t\t\teffectVisualContext_.stockPreviousWorld(instanceData->matrix() + 1);', 'actual previous rigid world producer')
    anchor = '\t\t\t\tinstanceData->paletteSize_ );'
    dc = replace(dc, anchor, anchor + '\n'
        '\t\t\teffectVisualContext_.stockPreviousPaletteEntries(instanceData->stockPreviousPaletteEntries_);',
        'actual previous palette offset producer')
    history_api = ('\tvoid stockPreviousWorld(const Matrix* matrix) { stockPreviousWorld_ = matrix; }\n'
        '\tconst Matrix* stockPreviousWorld() const { return stockPreviousWorld_; }\n'
        '\tvoid stockPreviousPaletteEntries(uint32 entries) { stockPreviousPaletteEntries_ = entries; }\n'
        '\tuint32 stockPreviousPaletteEntries() const { return stockPreviousPaletteEntries_; }\n')
    if history_api not in vh:
        anchors = ('\tconst Matrix*        worldMatrix() const { return worldMatrix_; }',
                   '\tconst Matrix*\t\tworldMatrix() const { return worldMatrix_; }')
        anchor = next((value for value in anchors if value in vh), None)
        if anchor is None:
            raise ValueError('Unrecognised actual world matrix publication API')
        vh = replace(vh, anchor, history_api + anchor, 'actual previous-pose semantic input API')
    vh = replace(vh, '\tconst Matrix*\t\t\tworldMatrix_;',
        '\tconst Matrix* stockPreviousWorld_ = NULL;\n'
        '\tuint32 stockPreviousPaletteEntries_ = 0;\n'
        '\tconst Matrix*\t\t\tworldMatrix_;', 'borrowed current packet history fields')
    for signature in ('void EffectVisualContext::worldMatrix( const Matrix* worldMatrix )',
                      'void\tEffectVisualContext::worldMatrixPalette( const Vector4* worldPalette, uint32 worldPaletteSize )'):
        vc = replace(vc, signature + '\n{', signature + '\n{\n'
            '\tstockPreviousWorld_ = NULL;\n\tstockPreviousPaletteEntries_ = 0;', 'clear history when switching current object ' + signature)
    if PREVIOUS_CONSTANTS not in vc:
        vc = replace(vc, 'class WorldPaletteConstant : public EffectConstantValue',
            PREVIOUS_CONSTANTS + 'class WorldPaletteConstant : public EffectConstantValue', 'real previous world semantic providers')
    registrations = ('    DEFINE_AUTO_CONST(PER_OBJECT, "PrevWorld") = new StockPreviousWorldConstant;\n'
                     '    DEFINE_AUTO_CONST(PER_OBJECT, "PrevWorldOffset") = new StockPreviousWorldOffsetConstant;\n')
    if registrations not in vc:
        anchor = vc.index('= new StockInstancingStreamConstant;') if '= new StockInstancingStreamConstant;' in vc else vc.index('= new WorldConstant;')
        begin = vc.rfind('\n', 0, anchor) + 1
        vc = vc[:begin] + registrations + vc[begin:]
    return list(zip(paths, (dh, dc, vh, vc)))


def replace(source, before, after, label):
    if after in source:
        if source.count(after) != 1:
            raise ValueError('Duplicate original static-model seam: ' + label)
        return source
    if source.count(before) != 1:
        raise ValueError('Unrecognised original static-model seam: ' + label)
    return source.replace(before, after, 1)


def patch(root):
    root = Path(root)
    header = root / 'lib/moo/animation.hpp'
    implementation = root / 'lib/moo/animation.cpp'
    h = header.read_text(encoding='utf-8-sig')
    c = implementation.read_text(encoding='utf-8-sig')
    h = replace(h, '\tvoid\t\t\t\t\tanimate( float time ) const;',
                '\tvoid\t\t\t\t\tanimate( float time ) const;\n\tvoid touchStockCompiledFrame( float frame ) const;',
                'streamed interpolation window declaration')
    if TOUCH not in c:
        c = replace(c, 'uint32 Animation::nChannelBinders( ) const',
                    TOUCH + 'uint32 Animation::nChannelBinders( ) const',
                    'streamed interpolation window implementation')
    lod_header = root / 'lib/moo/lod_settings.hpp'
    lod_implementation = root / 'lib/moo/lod_settings.cpp'
    finale = root / 'client/finale_app.cpp'
    lh = lod_header.read_text(encoding='utf-8-sig')
    lc = lod_implementation.read_text(encoding='utf-8-sig')
    f = finale.read_text(encoding='utf-8-sig')
    lh = replace(lh, '\tfloat applyLodBias(float distance);',
        '\tvoid initStockCompiled(DataSectionPtr config);\n'
        '\tfloat stockCompiledLodScale() const;\n'
        '\tfloat stockCompiledLodDefault(unsigned lod) const;\n'
        '\tfloat applyLodBias(float distance);', 'compiled LOD consumer API')
    lh = replace(lh, '\tBW::vector<float> lodOptions_;',
        '\tfloat stockCompiledLods_[5] = {1.f, 1.f, 1.f, 1.f, 1.f};\n'
        '\tBW::vector<float> lodOptions_;', 'authored five native LOD multipliers')
    if COMPILED_LOD not in lc:
        lc = replace(lc, 'float LodSettings::applyLodBias(float distance)',
                     COMPILED_LOD + 'float LodSettings::applyLodBias(float distance)',
                     'compiled LOD values and option payload')
    lc = replace(lc, 'void LodSettings::setLodOption(int optionIdx)\n{}',
        '''void LodSettings::setLodOption(int optionIdx)
{
    // #1513 a374f0: the OBJECT_LOD callback selects the shared decor owner.
    auto decor = Moo::GraphicsSetting::getFromLabel("DECOR_LEVEL");
    if (decor) decor->selectOption(std::min(2, 3 - optionIdx));
}''', 'original shared decor callback')
    # Insert before the stable original line, preserving the independent decor
    # patch's complete after-line hook in either order and on repeated runs.
    lod_hook = '    LodSettings::instance().initStockCompiled(configSection);'
    if lod_hook not in f:
        f = replace(f, '    LodSettings::instance().init(lodOptionsSec);',
            lod_hook + '\n    LodSettings::instance().init(lodOptionsSec);', 'native LOD configuration producer')
    history = previous_poses(root)
    base = Path(__file__).resolve().parent
    copies = [(base / 'stock_static_model_scene.hpp', root / 'lib/compiled_space/stock_static_model_scene.hpp'),
              (base / 'stock_static_model_scene.cpp', root / 'lib/compiled_space/stock_static_model_scene.cpp'),
              (base.parent / 'include/wot/stock_static_model_binary.hpp', root / 'lib/wot/stock_static_model_binary.hpp')]
    # Check every seam before writing any public source.
    contents = [(source.read_bytes(), destination) for source, destination in copies]
    for path, text in history:
        path.write_text(text, encoding='utf-8')
    lod_header.write_text(lh, encoding='utf-8')
    lod_implementation.write_text(lc, encoding='utf-8')
    finale.write_text(f, encoding='utf-8')
    header.write_text(h, encoding='utf-8')
    implementation.write_text(c, encoding='utf-8')
    for data, destination in contents:
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_bytes(data)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('engine_root', type=Path)
    patch(parser.parse_args().engine_root)
