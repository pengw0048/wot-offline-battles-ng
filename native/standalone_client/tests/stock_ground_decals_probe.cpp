#include "stock_ground_decals_data.hpp"
#include <cstdio>
int main()
{
    using namespace wot::GroundDecalMath;
    static_assert(sizeof(Contact)==24,"Consumed native contact fields must occupy six floats");
    Triangle triangle;Point center;float radius;
    while(std::fread(&triangle,1,36,stdin)==36)
    {
        if(std::fread(&center,1,12,stdin)!=12 || std::fread(&radius,1,4,stdin)!=4)return 2;
        const auto values=contacts(triangle,center,radius);
        const unsigned count=static_cast<unsigned>(values.size());
        std::fwrite(&count,1,4,stdout);
        for(const auto& contact:values)std::fwrite(&contact,1,24,stdout);
    }
}
