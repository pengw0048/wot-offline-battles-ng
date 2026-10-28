#include "stock_ground_decals_data.hpp"
#include <cstdio>
int main()
{
    using namespace wot::GroundDecalMath;
    unsigned count;
    while(std::fread(&count,4,1,stdin)==1)
    {
        Point start,direction;std::fread(start.data(),4,3,stdin);std::fread(direction.data(),4,3,stdin);
        std::vector<SurfaceContact> values(count);
        for(auto& value:values){std::fread(value.point.data(),4,3,stdin);std::fread(value.normal.data(),4,3,stdin);std::fread(&value.flags,4,1,stdin);}
        Projection result; if(!project(values,start,direction,result,[](const Point& value){const float length=std::sqrt(dot(value,value));return length?Point{{div(value[0],length),div(value[1],length),div(value[2],length)}}:Point{{0,0,0}};}))return 2;
        std::fwrite(result.axis.data(),4,3,stdout);std::fwrite(&result.distance,4,1,stdout);std::fwrite(&result.depth,4,1,stdout);
        unsigned object=result.object;std::fwrite(&object,4,1,stdout);
    }
}
