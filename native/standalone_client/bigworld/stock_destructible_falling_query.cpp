#include "pch.hpp"
#include "stock_destructible_falling_query.hpp"
#include "math/matrix.hpp"
#include "stock_destructible_falling_query_data.hpp"
#include "physics2/collision_callback.hpp"
#include "physics2/collision_obstacle.hpp"
#include "space/client_space.hpp"
#include <algorithm>
#include <limits>

BW_BEGIN_NAMESPACE
namespace
{
// 6b47f0 ignores water/nonphysical flags and all destructible material kinds.
// It uses the ordinary native terrain LOD, not useHighestAvailableLod().
class StockFallCollision : public CollisionCallback
{
public:
    float closest=std::numeric_limits<float>::max();
    uint32 flags=0;
    int operator()(const CollisionObstacle&,const WorldTriangle& triangle,float distance)override
    {
        const unsigned value=triangle.flags(),material=value>>8;
        if((value&0xd0u)||(material>=0x47u&&material<=0x64u))return COLLIDE_ALL;
        if(closest>distance){closest=distance;flags=value&255u;}
        return COLLIDE_BEFORE;
    }
};
}
float queryStockDestructibleFallPitch(ClientSpace& space,const Vector3& origin,float height,float yaw)
{
    auto rotate=[](float yaw,float pitch,const wot::StockFallPoint& point)
    {
        Matrix rotation;D3DXMatrixRotationYawPitchRoll(&rotation,yaw,pitch,0.f);
        Vector3 source(point[0],point[1],point[2]),result;
        D3DXVec3TransformCoord(&result,&source,&rotation);
        return wot::StockFallPoint{{result.x,result.y,result.z}};
    };
    auto collide=[&space](const wot::StockFallPoint& start,const wot::StockFallPoint& end)
    {
        StockFallCollision callback;
        return space.collide(Vector3(start[0],start[1],start[2]),Vector3(end[0],end[1],end[2]),callback);
    };
    return wot::stockDestructibleFallConstraint({{origin.x,origin.y,origin.z}},height>.2f?height:.2f,yaw,rotate,collide);
}
BW_END_NAMESPACE
