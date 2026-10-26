#include "pch.hpp"
#include "stock_tps_visual.hpp"
#include "moo/visual.hpp"
#include "moo/visual_manager.hpp"
#include "moo/complex_effect_material.hpp"
#include "moo/effect_material.hpp"
#include "moo/effect_property.hpp"
#include "moo/draw_context.hpp"
#include "moo/render_context.hpp"
#include "space/client_space.hpp"
#include "space/deprecated_space_helpers.hpp"
#include "scene/scene.hpp"
#include "terrain/terrain_scene_view.hpp"
#include <limits>
#include <set>
#include <vector>

BW_BEGIN_NAMESPACE
namespace
{
class VisualResource : public StockTPSVisualResource
{
public:
    explicit VisualResource( const BW::string & name ) : name_(name) {}
    bool load( std::string & error ) override
    {
        if (visual_) return true;
        visual_ = Moo::VisualManager::instance()->get( name_ );
        if (!visual_)
        {
            error = "Cannot load original tPS Visual: " + std::string(name_.c_str(), name_.size());
            return false;
        }
        return true;
    }
    bool draw( const Matrix & world, const Vector4 & colour, const Vector4 & lighting,
        std::string & error ) override
    {
        if (!load( error )) return false;
        Moo::Visual::ReadGuard guard( visual_.getObject() );
        struct Property
        {
            Moo::EffectPropertyPtr owner;
            Vector4 previous;
            Property( Moo::EffectPropertyPtr value, const Vector4 & replacement ) : owner(value)
            { owner->getVector( previous );owner->be( replacement ); }
            Property( Property && other ) : owner(other.owner), previous(other.previous) { other.owner = NULL; }
            ~Property() { if (owner) owner->be( previous ); }
        };
        std::vector<Property> properties;
        std::set<Moo::EffectProperty *> bound;
        for (auto & render : visual_->renderSets())
            for (auto & geometry : render.geometry_)
                for (auto & group : geometry.primitiveGroups_)
                {
                    if (!group.material_ || !group.material_->baseMaterial())
                    { error = "Original tPS Visual has no material";return false; }
                    auto material = group.material_->baseMaterial();
                    // facba0 binds these two original properties. Preserve the
                    // shared Visual material after this immediate draw owner.
                    const auto bind = [&]( const char * name, const Vector4 & value )
                    {
                        auto property = material->getProperty( name );
                        if (!property)
                        {
                            if (!material->setProperty( name, Vector4(1.f,1.f,1.f,1.f), true )) return false;
                            property = material->getProperty( name );
                        }
                        Vector4 old;
                        if (!property || !property->getVector( old )) return false;
                        if (!bound.insert( property.getObject() ).second) return true;
                        properties.emplace_back( property, value );return true;
                    };
                    if (!bind( "g_mul_color", colour ) || !bind( "g_lighting_params", lighting ))
                    { error = "Cannot bind original tPS Visual colour/lighting property";return false; }
                }
        // A private immediate context keeps the shared material values valid
        // until all of this Visual's real geometry passes have completed.
        struct WorldGuard
        {
            WorldGuard( const Matrix & value ) { Moo::rc().push();Moo::rc().world(value); }
            ~WorldGuard() { Moo::rc().pop(); }
        } worldGuard( world );
        Moo::DrawContext context( Moo::RENDERING_PASS_COLOR );
        context.begin( Moo::DrawContext::ALL_CHANNELS_MASK );
        const HRESULT result = visual_->draw( context, true, true );
        context.end( Moo::DrawContext::ALL_CHANNELS_MASK );
        context.flush( Moo::DrawContext::ALL_CHANNELS_MASK );
        if (FAILED(result)) { error = "Cannot draw original tPS Visual geometry";return false; }
        return true;
    }
private:
    BW::string name_;
    Moo::VisualPtr visual_;
};
}
std::shared_ptr<StockTPSVisualResource> createStockTPSVisualResource( const BW::string & resource )
{ return std::make_shared<VisualResource>( resource ); }
float stockTPSGroundHeight( float x, float z )
{
    const ClientSpacePtr space = DeprecatedSpaceHelpers::cameraSpace();
    if (space && !space->cleared())
    {
        const Vector3 position(x,0.f,z);
        auto * view = space->scene().getView<Terrain::TerrainSceneView>();
        Terrain::TerrainFinder::Details block;
        if (view && view->findTerrainBlock( position, block ))
        {
            const float height = space->findTerrainHeight( position );
            if (height != std::numeric_limits<float>::min()) return height;
        }
    }
    // aeb310's original no-space/FLT_MIN result, rather than a fabricated hit.
    return 20.f;
}
BW_END_NAMESPACE
