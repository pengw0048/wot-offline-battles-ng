#include "wot/stock_effect_reflection.hpp"
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
int main(int argc,char** argv) {
    if(argc!=3)return 2;
    std::ifstream input(argv[1],std::ios::binary);
    std::vector<char> bytes((std::istreambuf_iterator<char>(input)),{});
    auto definition=std::make_shared<wot::StockEffectBinary>();std::string error;
    if(!wot::decode_stock_dx9_effect(bytes.data(),bytes.size(),*definition,error))throw std::runtime_error(error);
    wot::StockEffectReflection reflection(definition,{});
    if(std::string(argv[2])=="simulation") {
        for(const char* technique:{"WaterSimulation","WaterSimulationPerturb","simulation_edging"})
            if(!reflection.technique_by_name(technique))throw std::runtime_error(technique);
        if(reflection.technique_by_name("water_simulation")||reflection.parameter_by_name("perturb")||reflection.parameter_by_name("edge"))
            throw std::runtime_error("Retired SDK selectors must not be used");
        const std::pair<const char*,unsigned> parameters[]={
            {"WorldViewProj",3},{"impactStrength",3},{"g_highQuality",4},{"g_cellInfo",3},
            {"heightMap",5},{"previousHeightMap",5},{"cookieMap",5},
            {"Y0_enabled",4},{"X0_enabled",4},{"Y2_enabled",4},{"X2_enabled",4}};
        for(auto p:parameters){auto handle=reflection.parameter_by_name(p.first);auto type=reflection.parameter_type(handle);
            if(!type||type->basic_type!=p.second||!reflection.parameter_used(handle))throw std::runtime_error(p.first);}
        for(auto p:parameters){auto handle=reflection.parameter_by_name(p.first);
            if(reflection.annotation_by_name(handle,"artistEditable")||reflection.annotation_by_name(handle,"UIName"))
                throw std::runtime_error("Manual simulation parameters must survive EffectMaterial::setProperties");}
    }else if(std::string(argv[2])=="water_deferred"){
        for(const char* name:{"simulationMap","useSimulation","combineSimulation","simulationPower"}) {
            auto handle=reflection.parameter_by_name(name);
            if(!handle||!reflection.parameter_used(handle))throw std::runtime_error(name);
            if(reflection.annotation_by_name(handle,"artistEditable")||reflection.annotation_by_name(handle,"UIName"))
                throw std::runtime_error("Manual water surface parameters must survive EffectMaterial::setProperties");
        }
        float power=0;
        if(!reflection.default_value(reflection.parameter_by_name("simulationPower"),&power,sizeof(power))||power!=1.f)
            throw std::runtime_error("Water surface uses its original nonzero simulationPower default");
    }else{
        if(reflection.parameter_by_name("simulationMap")||reflection.parameter_by_name("useSimulation"))
            throw std::runtime_error("Forward water has no simulation texture consumer");
    }
    std::cout<<"Original water ripple shader consumer passed\n";
}
