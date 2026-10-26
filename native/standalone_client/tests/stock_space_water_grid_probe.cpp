#include "../bigworld/stock_space_water.hpp"
#include <cstring>
#include <iostream>

int main()
{
    float x,z,tessellation;
    while(std::cin>>x>>z>>tessellation)
    {
        wot::StockSpaceWaterData::Definition state{};
        std::memcpy(state.state.data()+3,&x,4);
        std::memcpy(state.state.data()+4,&z,4);
        std::memcpy(state.state.data()+6,&tessellation,4);
        wot::StockSpaceWaterData::Grid grid{};
        const bool valid=wot::StockSpaceWaterData::grid(state,grid);
        std::cout<<valid<<' '<<grid.x<<' '<<grid.z<<' '<<grid.vertices<<'\n';
    }
}
