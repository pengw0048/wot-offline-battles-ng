#include "wot/stock_effect_binding.hpp"
#include "wot/stock_effect_reflection.hpp"
#include <cassert>
#include <fstream>
#include <iterator>
#include <iostream>

int main(int argc,char**argv)
{
    assert(argc==2);
    std::ifstream input(argv[1],std::ios::binary);
    std::vector<char> bytes((std::istreambuf_iterator<char>(input)),{});
    auto definition=std::make_shared<wot::StockEffectBinary>();std::string error;
    assert(wot::decode_stock_dx9_effect(bytes.data(),bytes.size(),*definition,error));
    wot::StockEffectReflection reflection(definition,{});
    wot::StockEffectUniformPool pool;wot::StockEffectUniformContext context(pool);
    wot::StockEffectBinding binding(definition,context);
    assert(reflection.annotation_by_name(reflection.parameter_by_name("doubleSided"),"artistEditable"));
    for(const char* name:{"blendMode","colorTexture","diffuseColor","zTestDisable","zWriteEnable",
            "outputAlphaType","outputColourType","textureIsPointSampled","outputAlphaMultiplier","colorWriteAlpha","useWireframe"})
        assert(!reflection.annotation_by_name(reflection.parameter_by_name(name),"artistEditable")&&
            !reflection.annotation_by_name(reflection.parameter_by_name(name),"UIName"));
    const auto set=[&](const char* name,const void* data,unsigned size,unsigned type){
        auto handle=reflection.parameter_by_name(name);assert(handle);
        auto* target=reflection.parameter_reference(handle);assert(target);
        assert(binding.set_uniform(target->owner,target->member,data,size,type,error));
    };
    for(bool history:{false,true})
    {
        const auto technique=reflection.technique_by_name(history?"textured2D":"standard2D");assert(technique);
        const float white[]={1,1,1,1};set("diffuseColor",white,16,3);
        unsigned yes=1,no=0;int mode=2,alpha=1;float multiplier=1;
        set("zTestDisable",&yes,4,4);set("zWriteEnable",&no,4,4);
        set("doubleSided",&yes,4,4);set("useWireframe",&no,4,4);
        set("blendMode",&mode,4,1);set("colorWriteAlpha",&no,4,1);
        if(history){set("outputAlphaType",&alpha,4,1);set("outputColourType",&alpha,4,1);}
        set("textureIsPointSampled",&no,4,4);set("outputAlphaMultiplier",&multiplier,4,3);
        const auto index=reflection.technique_source_index(technique);assert(index);
        wot::StockDx9PassSelection selected;assert(binding.select_pass(*index,0,selected,error));
        assert(selected.pixel_shader&&selected.vertex_shader&&selected.render_state);
        if(history)
        {
            const auto* texture=reflection.parameter_reference(reflection.parameter_by_name("colorTexture"));
            assert(texture&&texture->kind==wot::StockEffectParameterKind::resource_view);
            bool found=false;
            for(const auto& resource:definition->shaders[*selected.pixel_shader].resources)
                if(resource.kind==2)
                {
                    std::optional<std::uint32_t> view;
                    assert(wot::resolve_stock_effect_reference(*definition,binding.selection(),
                        wot::StockEffectResourceTable::resource_view,resource.resource_index,view,error));
                    if(view&&*view==texture->owner){assert(resource.shader_slot==0);found=true;}
                }
            assert(found);
        }
    }
    std::cout<<"Original FlashBang 2D shader passes and manual fields passed\n";
}
