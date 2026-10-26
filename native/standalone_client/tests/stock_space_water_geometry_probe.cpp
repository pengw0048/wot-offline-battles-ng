#include <windows.h>
#pragma warning(push)
#pragma warning(disable:4458)
#include "stock_space_water_geometry.hpp"
#pragma warning(pop)
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
int main(int argc,char** argv)
{
    if(argc!=3) return 2;
    std::ifstream input(argv[1],std::ios::binary);
    float row[22];
    while(input.read(reinterpret_cast<char*>(row),sizeof(row)))
    {
        BW::BoundingBox local(BW::Vector3(row[0],row[1],row[2]),BW::Vector3(row[3],row[4],row[5]));
        BW::Matrix water;
        std::memcpy(&water,row+6,64);
        BW::Matrix cell=BW::stockWaterCellTransform(local,water);
        const bool enabled=BW::stockWaterCellEnabled(cell);
        float output[22]{};
        std::memcpy(output,&cell,64);
        if(enabled)
        {
            const auto box=BW::stockWaterCellBounds(cell);
            std::memcpy(output+16,&box.minBounds(),12);
            std::memcpy(output+19,&box.maxBounds(),12);
        }
        std::uint32_t words[22];std::memcpy(words,output,sizeof(words));
        std::cout<<(enabled?1:0)<<' ';
        for(auto word:words)std::cout<<word<<' ';
        std::cout<<'\n';
    }
    const auto dll=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"D3DX9_43.dll"));
    if(!dll)return 3;
    const auto dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(dll);
    const auto nt=reinterpret_cast<const IMAGE_NT_HEADERS*>(dll+dos->e_lfanew);
    std::ofstream table(argv[2],std::ios::binary);
    for(std::uintptr_t offset=0x1ca000;offset<0x1ca240;offset+=4)
    {
        auto value=*reinterpret_cast<const std::uintptr_t*>(dll+offset);
        if(value>=dll&&value<dll+nt->OptionalHeader.SizeOfImage)value=value-dll+0x10000000;
        table.write(reinterpret_cast<const char*>(&value),4);
    }
}
