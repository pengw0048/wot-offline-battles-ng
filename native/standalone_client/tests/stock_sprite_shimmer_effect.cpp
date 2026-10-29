#include "wot/stock_shader_binary.hpp"
#include "wot/stock_effect_binding.hpp"
#include "wot/stock_shader_runtime.hpp"
#include <fstream>
#include <iterator>
#include <iostream>
#include <stdexcept>
#include <set>
int main(int argc,char** argv) {
    if(argc!=3) return 2;
    for(int index=1;index<3;++index) {
        std::ifstream in(argv[index],std::ios::binary);
        std::vector<char> bytes((std::istreambuf_iterator<char>(in)),{});
        wot::StockEffectBinary effect;std::string error;
        if(!wot::decode_stock_dx9_effect(bytes.data(),bytes.size(),effect,error)) throw std::runtime_error(error);
        wot::StockEffectUniformPool pool;wot::StockEffectUniformContext context(pool);
        wot::StockEffectBinding binding(std::make_shared<wot::StockEffectBinary>(effect),context);
        wot::StockDx9PassSelection selected;
        if(!binding.select_pass(0,0,selected,error) || !selected.render_state || !selected.pixel_shader) throw std::runtime_error(error);
        std::vector<wot::StockDx9StateValue> states;
        if(!wot::stock_dx9_render_states(effect.render_states[*selected.render_state],states,error)) throw std::runtime_error(error);
        bool found=false;
        for(const auto& state:states) if(state.state==168) {
            // The ordinary sprite writes RGBA; original shimmer first writes
            // alpha only. The normal shader cannot substitute for this owner.
            if(state.value!=(index==1?15u:8u)) return 3;
            found=true;
        }
        if(!found || effect.techniques[0].name!=(index==1?"SpriteParticles":"pointDraw")) return 4;
        std::set<std::string> fields;
        for(const auto& buffer:effect.uniform_buffers)
            for(const auto& uniform:buffer.uniforms) fields.insert(uniform.name);
        if(fields.count("fogToFogColour") || fields.count("alphaTestEnable")) return 5;
        for(const auto* field:{"softDepthRange","softFalloffPower","softDepthOffset",
                              "nearFadeCutoff","nearFadeStart","nearFadeFalloffPower"})
            if(fields.count(field)!=(index==1?1u:0u)) return 6;
    }
    std::cout<<"Original ordinary RGBA / shimmer alpha-mask render contracts passed\n";
}
