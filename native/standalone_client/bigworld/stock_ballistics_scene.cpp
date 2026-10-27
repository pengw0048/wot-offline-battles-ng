#include "pch.hpp"
#include "stock_ballistics_scene.hpp"

#include "stock_battle_scene_queries.hpp"
#include "wot/stock_ballistics.hpp"

BW_BEGIN_NAMESPACE

namespace
{
namespace ballistics = wot::stock_ballistics;

Vector3 native( const ballistics::Vec3 & value ) { return Vector3( value.x, value.y, value.z ); }

// Stateless: every call queries the current space through the BQ owner, which
// returns false for a missing or cleared space and retires geometry itself.
class StockBallisticsScene : public ballistics::Scene
{
public:
    bool collideSegment( std::int32_t spaceID, const ballistics::Vec3 & start,
        const ballistics::Vec3 & end, std::uint16_t flags,
        const ballistics::SceneFilter & filter, ballistics::SceneHit & result ) override
    {
        // 014ef060/014ef07c read only the identity; empty stays accept-all.
        StockNativeSegmentFilter nativeFilter;
        if (filter)
            nativeFilter = [&filter]( uint32, uint32, uint64 identity )
                { return filter( identity ); };
        StockNativeSegmentHit hit;
        if (!collideStockSegmentNative( SpaceID( spaceID ), native( start ), native( end ),
                WorldTriangle::Flags( flags ), nativeFilter, hit ))
            return false;
        result.point = ballistics::Vec3{ hit.point.x, hit.point.y, hit.point.z };
        result.materialKind = hit.materialKind;
        result.identity = hit.identity;
        return true;
    }

    float collideWater( const ballistics::Vec3 & start, const ballistics::Vec3 & end ) override
    {
        return collideStockWater( native( start ), native( end ), false );
    }
};
} // namespace

wot::stock_ballistics::Scene & stockBallisticsScene()
{
    static StockBallisticsScene s_scene;
    return s_scene;
}

BW_END_NAMESPACE
