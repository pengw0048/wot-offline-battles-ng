#include "wot/stock_effect_reflection.hpp"
#include <fstream>
#include <iterator>
#include <iostream>
#include <stdexcept>
int main(int argc,char** argv) {
    if(argc!=2)return 2;
    std::ifstream stream(argv[1],std::ios::binary);
    std::vector<char> bytes((std::istreambuf_iterator<char>(stream)),{});
    auto definition=std::make_shared<wot::StockEffectBinary>();std::string error;
    if(!wot::decode_stock_dx9_effect(bytes.data(),bytes.size(),*definition,error))
        throw std::runtime_error(error);
    wot::StockEffectReflection reflection(definition,{});
    if(!reflection.technique_by_name("standard"))throw std::runtime_error("Missing original standard technique");
    const std::pair<const char*,unsigned> parameters[]={
        {"world",3},{"diffuseColor",3},{"zTestDisable",4},{"zWriteEnable",4},
        {"blendMode",1},{"doubleSided",4},{"useWireframe",4},{"colorTexture",5},
        {"colorWriteAlpha",1},{"outputAlphaMultiplier",3}};
    for(const auto& parameter:parameters) {
        const auto handle=reflection.parameter_by_name(parameter.first);
        const auto type=reflection.parameter_type(handle);
        if(!handle||!type||type->basic_type!=parameter.second||!reflection.parameter_used(handle))
            throw std::runtime_error(std::string("Incorrect original parameter: ")+parameter.first);
    }
    std::cout<<"Original TrajectoryDrawer geometrics shader contract passed\n";
}
