#pragma once
#include "stock_ground_decals_data.hpp"

namespace wot { namespace GroundDecalMath
{
// 1179620: ordered BSP traversal of a box represented as a swept rectangle.
// Carry the clipped first-axis interval across partitions. Visiting every leaf
// and deduplicating triangles changes the original shared-triangle weighting.
template<class Node,class Plane,class Child,class Emit>
void visitBox(Node* root,const Point& center,const Point& first,const Point& second,const Point& third,
    Plane plane,Child child,Emit emit)
{
    struct Entry {Node* node;float low,high;};
    std::vector<Entry> pending;
    if(root)pending.push_back({root,-1.f,1.f});
    while(!pending.empty())
    {
        const Entry entry=pending.back();pending.pop_back();
        Node* front=child(entry.node,0);Node* back=child(entry.node,1);
        if(!front && !back){emit(entry.node);continue;}
        const auto equation=plane(entry.node);
        const Point normal={{equation[0],equation[1],equation[2]}};
        const float mid=mul(add(entry.high,entry.low),.5f);
        const float half=mul(sub(entry.high,entry.low),.5f);
        const float slope=dot(scale(first,half),normal);
        const float distance=sub(dot(plus(center,scale(first,mid)),normal),equation[3]);
        const float widthSecond=std::abs(dot(second,normal)),widthThird=std::abs(dot(third,normal));
        if(!(add(add(std::abs(slope),widthSecond),widthThird)>std::abs(distance)))
        {
            Node* next=distance>0.f?front:back;
            if(next)pending.push_back({next,entry.low,entry.high});
            continue;
        }
        emit(entry.node);
        const float width=add(widthSecond,widthThird),inverse=div(-1.f,slope);
        auto clamp=[](float value){const float upper=value<1.f?value:1.f;return upper>-1.f?upper:-1.f;};
        const float left=clamp(mul(add(width,distance),inverse));
        const float right=clamp(mul(sub(distance,width),inverse));
        if(slope<0.f)
        {
            if(front)pending.push_back({front,sub(mid,half),add(mul(left,half),mid)});
            if(back)pending.push_back({back,add(mul(right,half),mid),add(half,mid)});
        }
        else
        {
            if(front)pending.push_back({front,add(mul(left,half),mid),add(mid,half)});
            if(back)pending.push_back({back,sub(mid,half),add(mul(right,half),mid)});
        }
    }
}
}}
