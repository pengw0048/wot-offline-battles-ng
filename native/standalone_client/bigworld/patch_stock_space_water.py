"""Connect BWWa2 state fields to the real public Water resource/render owner."""
import argparse
from pathlib import Path


def replace(source, before, after):
    if after in source:
        return source
    if source.count(before) != 1:
        raise RuntimeError('The reviewed Water seam changed: ' + repr(before))
    return source.replace(before, after)


def patch(root):
    root = Path(root)
    pending = []
    path = root / 'lib/romp/water.hpp'
    source = replace(path.read_text(), '\t\tbool\t\tuseShadows_;', '''\t\tbool\t\tuseShadows_;

        // Original a2eb80 consumes these additional BWWa2 fields. The stock
        // mapping converts fields explicitly; this is not the 244-byte wire ABI.
        Vector4 forwardWaterParameters_ = Vector4(0,0,0,0);
        float shallowWaterBiasForward_ = 0.f;
        float sunScaleDeferred_ = 0.f;
        float deepColourScale_ = 1.f;''')
    source = replace(source, '\tvoid addVisibleChunk( Chunk* pChunk ) { visibleChunks_.push_back( pChunk ); }', '''\tvoid addVisibleChunk( Chunk* pChunk ) { visibleChunks_.push_back( pChunk ); }
    void stockBeginVisibleCells();
    void stockAddVisibleCell(const BoundingBox& bounds) { stockVisibleCells_.push_back(bounds); }
    const BW::vector<BoundingBox>& stockVisibleCells() const { return stockVisibleCells_; }''')
    source = replace(source, '\tBW::vector<Chunk*>\t\t\t\t\tvisibleChunks_;',
                     '\tBW::vector<Chunk*>\t\t\t\t\tvisibleChunks_;\n    BW::vector<BoundingBox> stockVisibleCells_;')
    source = replace(source, '\tbool\t\t\t\t\t\t\t\tinitialised_;',
                     '\tbool\t\t\t\t\t\t\t\tinitialised_;\n    bool stockQueryDataReady_;\n    uint64 stockQueryGeneration_;')
    pending.append((path, source))
    path = root / 'lib/romp/water.cpp'
    source = replace(path.read_text(), '    initialised_( false ),',
                     '    initialised_( false ),\n    stockQueryDataReady_( false ),\n    stockQueryGeneration_( ++s_stockWaterQueryGeneration ),')
    source = replace(source, '''void Water::rebuild( const WaterState& state, const WaterResources& resources )
{
    BW_GUARD;''', '''void Water::rebuild( const WaterState& state, const WaterResources& resources )
{
    BW_GUARD;
    {
        // Wait for an old table producer before invalidating its queued completion.
        SimpleMutexHolder guard(*deletionLock_);
        stockQueryDataReady_ = false;
        stockQueryGeneration_ = ++s_stockWaterQueryGeneration;
    }''')
    source = replace(source, '''    w3.stop();
    static DogWatch w4( "Finalisation" );''', '''    // Main-thread completion follows doCreateTables; the alpha grid and
    // actual bounds are now complete independently of GPU draw readiness.
    stockQueryDataReady_ = true;

    w3.stop();
    static DogWatch w4( "Finalisation" );''')
    source = replace(source, '#define BANK_DIST_DEFAULT_VALUE std::make_pair(Vector2::zero(), 100000000.0f)',
                     '#define BANK_DIST_DEFAULT_VALUE std::make_pair(Vector2::zero(), 10000.0f)')
    source = replace(source, '            minDist = 100000000.0f;', '            minDist = 10000.0f;')
    source = replace(source, """    transform_.setRotateY( state_.orientation_ );
    transform_.postTranslateBy( state_.position_ );
    invTransform_.invert( transform_ );

    if (Water::backgroundLoad())""", """    D3DXMatrixRotationY(reinterpret_cast<D3DXMATRIX*>(&transform_), state_.orientation_);
    transform_.postTranslateBy( state_.position_ );
    D3DXMatrixInverse(reinterpret_cast<D3DXMATRIX*>(&invTransform_), NULL,
        reinterpret_cast<const D3DXMATRIX*>(&transform_));

    if (Water::backgroundLoad())""")
    source = replace(source, """    value = p1 & 0xff;
    value |= ((p2 << 8  ) & 0xff00);
    value |= ((p3 << 16 ) & 0xff0000);
    value |= ((p4 << 24 ) & 0xff000000);""", """    // 00a2d270 assembles unsigned octets before shifting. A signed-char
    // shift in the public reader has undefined behavior for authored alpha.
    value = static_cast<uint8>(p1);
    value |= uint32(static_cast<uint8>(p2)) << 8;
    value |= uint32(static_cast<uint8>(p3)) << 16;
    value |= uint32(static_cast<uint8>(p4)) << 24;""")
    source = replace(source, '                    dir += delta/(dist*dist);',
                     '                    const float weight = 1.f/(dist*dist);\n                    dir += delta*weight;')
    source = replace(source, '                dir.normalise();', '                D3DXVec2Normalize(&dir, &dir);')
    source = replace(source, '    paramCache_->setVector( "deepColour", &state_.deepColour_ );', '''    // 00a2eb80: deferred shader colours are linear; forward consumes authored
    // tint directly. Deep-water brightness is independent of its alpha.
    const bool deferred = Renderer::instance().pipelineType() ==
        IRendererPipeline::TYPE_DEFERRED_SHADING;
    Vector4 deepColour = state_.deepColour_;
    Vector4 reflectionTint = state_.reflectionTint_;
    Vector4 refractionTint = state_.refractionTint_;
    if (deferred)
    {
        for (uint channel = 0; channel != 3; ++channel)
        {
            deepColour[channel] = powf(deepColour[channel], 2.2f) * state_.deepColourScale_;
            reflectionTint[channel] = powf(reflectionTint[channel], 2.2f);
            refractionTint[channel] = powf(refractionTint[channel], 2.2f);
        }
    }
    paramCache_->setVector( "deepColour", &deepColour );''')
    source = replace(source, '''    paramCache_->setFloat( "fresnelExp", state_.fresnelExponent_ );
    paramCache_->setFloat( "fresnelConstant", state_.fresnelConstant_ );

    paramCache_->setFloat( "fresnelExp", state_.fresnelExponent_ );
    paramCache_->setFloat( "fresnelConstant", state_.fresnelConstant_ );''', '''    // #1513 removed the two per-water Fresnel fields. Their replacement is
    // a four-component forward shader parameter plus a shallow-water bias.
    Vector4 forward = state_.forwardWaterParameters_;
    forward.y = 100.f - forward.y;
    if (forward.y < 0.001f) forward.y = 0.001f;
    paramCache_->setVector( "forwardWaterParameters", &forward );
    paramCache_->setFloat( "shallowWaterBiasForward", state_.shallowWaterBiasForward_ );''')
    source = replace(source, '''    paramCache_->setVector( "reflectionTint", &state_.reflectionTint_ );
    paramCache_->setVector( "refractionTint", &state_.refractionTint_ );''', '''    paramCache_->setVector( "reflectionTint", &reflectionTint );
    paramCache_->setVector( "refractionTint", &refractionTint );''')
    source = replace(source, '''    paramCache_->setFloat( "sunScale", Waters::instance().insideVolume() ? 0.f : state_.sunScale_ );''', '''    paramCache_->setFloat( "sunScale", Waters::instance().insideVolume() ? 0.f :
        (deferred ? state_.sunScaleDeferred_ : state_.sunScale_) );''')
    source = replace(source, '''    paramCache_->setBool( "enableWaterShadows", state_.useShadows_ );''', '''    paramCache_->setBool( "enableWaterShadows", state_.useShadows_ );
    paramCache_->setBool( "g_useHighQuality", Waters::s_highQuality_ );''')
    source = replace(source, 'Water::Water( const WaterState& state, const WaterResources& resources, RompColliderPtr pCollider )', '''namespace
{
uint64 s_stockWaterQueryGeneration = 0;
struct StockWaterTablesContext
{
    Water* water;
    uint64 generation;
    StockWaterTablesContext(Water* owner,uint64 identity):water(owner),generation(identity){}
};
class StockWaterTablesTask : public CStyleBackgroundTask
{
    // The actual queued task owns the context through completion or cancellation.
    StockWaterTablesContext context_;
public:
    StockWaterTablesTask(Water* owner,uint64 generation,FUNC_PTR background,FUNC_PTR completion):
        CStyleBackgroundTask("Water Load Task",background,&context_,completion,&context_),
        context_(owner,generation){}
};
}

void Water::stockBeginVisibleCells()
{
    // 00a29740 resets once before this view's selected cells are appended.
    if (drawMark_ != Waters::nextDrawMark()) stockVisibleCells_.clear();
}

Water::Water( const WaterState& state, const WaterResources& resources, RompColliderPtr pCollider )''')
    source = replace(source, '''            new CStyleBackgroundTask( "Water Load Task",
                &Water::doCreateTables, this,
                &Water::onCreateTablesComplete, this ) );''', '''            new StockWaterTablesTask(this,stockQueryGeneration_,
                &Water::doCreateTables,&Water::onCreateTablesComplete) );''')
    source = replace(source, '''        Water::doCreateTables( this );
        Water::onCreateTablesComplete( this );''', '''        StockWaterTablesContext context(this,stockQueryGeneration_);
        Water::doCreateTables( &context );
        Water::onCreateTablesComplete( &context );''')
    for signature in ('void Water::doCreateTables( void* self )',
                      'void Water::onCreateTablesComplete( void * self )'):
        source = replace(source, signature + '''
{
    BW_GUARD;

    Water * selfWater = static_cast< Water * >( self );
    if (Water::stillValid(selfWater))
    {''', signature + '''
{
    BW_GUARD;

    const StockWaterTablesContext* context = static_cast<StockWaterTablesContext*>(self);
    Water* selfWater = context->water;
    if (Water::stillValid(selfWater))
    {
        // stillValid holds deletionLock_: an old task may find a newer Water at
        // the same address, but must never build or publish that object's data.
        if (selfWater->stockQueryGeneration_ != context->generation)
        {
            if (Water::backgroundLoad()) deletionLock_->give();
            return;
        }''')
    source = replace(source, '''void Water::deleteWater(Water* water)
{\x20\x20\x20
    BW_GUARD_PROFILER( Water_deleteWater );''', '''static void removeStockWaterFromDrawList(Water* water);

void Water::deleteWater(Water* water)
{\x20\x20\x20
    BW_GUARD_PROFILER( Water_deleteWater );
    if (!water) return;
    // Scene observers may retire a mapping after enqueue and before draw.
    // The global render queue must release that pointer before Water deletion.
    removeStockWaterFromDrawList(water);''')
    source = replace(source, 'static VectorNoDestructor< Water * > s_waterDrawList;', '''static VectorNoDestructor< Water * > s_waterDrawList;
static void removeStockWaterFromDrawList(Water* water)
{
    for (size_t index = s_waterDrawList.size(); index != 0; --index)
        if (s_waterDrawList[index-1] == water) s_waterDrawList.erase(s_waterDrawList.begin()+index-1);
}''')
    pending.append((path, source))
    for path, source in pending:
        path.write_text(source)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('root', type=Path)
    patch(parser.parse_args().root)
