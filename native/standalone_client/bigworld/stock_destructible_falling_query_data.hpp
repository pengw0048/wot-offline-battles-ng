#pragma once
#include <array>
#include <cmath>

namespace wot
{
using StockFallPoint=std::array<float,3>;
inline float stockFallAdd(float a,float b){volatile float r=a+b;return r;}
inline float stockFallSubtract(float a,float b){volatile float r=a-b;return r;}
inline float stockFallMultiply(float a,float b){volatile float r=a*b;return r;}
inline float stockFallDivide(float a,float b){volatile float r=a/b;return r;}
inline StockFallPoint stockFallOffset(const StockFallPoint& a,const StockFallPoint& b)
{return {{stockFallAdd(a[0],b[0]),stockFallAdd(a[1],b[1]),stockFallAdd(a[2],b[2])}};}

// 6b4ad0: the original probe/search control flow. Rotation and collision are
// the real D3DX and ClientSpace boundaries, supplied by the production owner.
// Height has already passed the original max(height,.2f) in 6b510d.
template<class Rotate,class Collide>
float stockDestructibleFallConstraint(const StockFallPoint& origin,float height,float yaw,
    const Rotate& rotate,const Collide& collide)
{
    const float length=stockFallMultiply(height,.95f);
    const StockFallPoint top={{0.f,length,0.f}};
    const auto first=stockFallOffset(origin,top);
    const auto tilted=stockFallOffset(origin,rotate(yaw,.0872664675116539f,top));
    float angle=1.4835299253463745f;
    if(collide(first,tilted)<0.f)
    {
        auto down=tilted;down[1]=stockFallAdd(down[1],stockFallMultiply(length,-2.f));
        const float hit=collide(tilted,down);
        if(hit<0.f)angle=-1.4835299253463745f;
        else
        {
            const float dx=stockFallSubtract(tilted[0],origin[0]);
            const float dy=stockFallSubtract(stockFallSubtract(tilted[1],hit),origin[1]);
            const float dz=stockFallSubtract(tilted[2],origin[2]);
            float furthest=static_cast<float>(std::sqrt(stockFallAdd(stockFallAdd(
                stockFallMultiply(dy,dy),stockFallMultiply(dx,dx)),stockFallMultiply(dz,dz))));
            angle=stockFallAdd(std::atan(stockFallMultiply(
                stockFallDivide(11.473721504211426f,length),dy)),.06283185631036758f);
            const float vertical=collide(origin,first);
            auto pivot=origin;pivot[1]=stockFallAdd(origin[1],vertical>0.f?stockFallAdd(vertical,.01f):.01f);
            float step=stockFallMultiply(stockFallSubtract(1.4835299253463745f,angle),.5f);
            float candidate=stockFallAdd(angle,step);step=stockFallMultiply(step,.5f);
            if(step>=.012271846644580364f)do
            {
                const auto end=stockFallOffset(pivot,rotate(yaw,-candidate,{{0.f,0.f,length}}));
                const float distance=collide(pivot,end);
                if(length>distance&&distance>0.f)
                {
                    if(distance>furthest){furthest=distance;angle=candidate;}
                    candidate=stockFallAdd(candidate,step);
                }
                else candidate=stockFallSubtract(candidate,step);
                step=stockFallMultiply(step,.5f);
            }while(step>=.012271846644580364f);
        }
    }
    return stockFallSubtract(1.5707963705062866f,angle);
}
}
