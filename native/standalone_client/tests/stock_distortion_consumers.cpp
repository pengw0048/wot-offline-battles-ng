#include "wot/stock_shader_binary.hpp"
#include <fstream>
#include <iterator>
#include <iostream>
#include <stdexcept>
#include <string>
// Counts DX9 shaders that sample a named global SRV; selector-resolved slots count as readers.
static int readers(const wot::StockEffectBinary& effect, const std::string& name) {
    int target=-1, count=0;
    for(std::size_t index=0;index<effect.resource_views.size();++index)
        if(effect.resource_views[index].name==name) target=int(index);
    if(target<0) return 0;
    const auto& table=effect.resource_indirections[1];
    for(const auto& shader:effect.shaders)
        for(const auto& resource:shader.resources)
            if(resource.kind==2 && resource.resource_index<table.size() &&
               (table[resource.resource_index]==std::uint32_t(target) || table[resource.resource_index]>=0x10000)) ++count;
    return count;
}
int main(int argc,char** argv) {
    int distortion=0, gbuffer=0;
    for(int index=1;index<argc;++index) {
        std::ifstream in(argv[index],std::ios::binary);
        std::vector<char> bytes((std::istreambuf_iterator<char>(in)),{});
        wot::StockEffectBinary effect;std::string error;
        if(!wot::decode_stock_dx9_effect(bytes.data(),bytes.size(),effect,error)) throw std::runtime_error(error);
        distortion+=readers(effect,"g_distortionMap");
        gbuffer+=readers(effect,"g_GBufferChannel0");
    }
    if(distortion!=0) return 3;
    if(gbuffer==0) return 4;
    std::cout<<"No original DX9 shader samples g_distortionMap\n";
}
