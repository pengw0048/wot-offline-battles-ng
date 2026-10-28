#include "wot/stock_shader_binary.hpp"
#include "wot/stock_effect_binding.hpp"
#include "wot/stock_shader_runtime.hpp"
#include <algorithm>
#include <array>
#include <cassert>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

namespace BW { using string=std::string; }
@RESOURCE_STRING@
int productionRenderKind(const wot::StockTPSEmitterResource& source)
{
    int renderKind=0;
    @RENDER_KIND@
    return renderKind;
}

using Register=std::array<float,4>;
// Only the arithmetic present in the original particle vertex programs is
// interpreted. The production decoder, selector and register planner run intact.
float runAlpha(const wot::StockShaderBinary& shader,const std::vector<std::uint32_t>& floats,float distance)
{
    Register r[32]={},c[256]={},o[12]={};
    Register v[16]={};
    v[0]={0.f,0.f,distance,1.f};v[1]={1.f,1.f,1.f,.75f};
    // Zero radius keeps the ordinary and long-particle vertices at the requested
    // clip distance, isolating the shared near-distance fade from shape offsets.
    v[2]={0.f,1.f,0.f,0.f};v[3]={0.f,0.f,0.f,1.f};v[5]={1.f,0.f,1.f,0.f};
    for(std::size_t i=0;i<floats.size();++i)std::memcpy(&c[i/4][i%4],&floats[i],4);
    auto bank=[&](std::uint32_t token)->Register& {
        assert(!(token&0x2000));const unsigned type=((token>>28)&7)|((token>>8)&0x18),index=token&0x7ff;
        if(type==0)return r[index];if(type==1)return v[index];if(type==2)return c[index];if(type==6)return o[index];
        throw std::runtime_error("Unexpected particle register type");
    };
    auto source=[&](std::uint32_t token) {
        const auto& value=bank(token);Register result{};
        for(unsigned k=0;k<4;++k)result[k]=value[(token>>(16+k*2))&3];
        const unsigned modifier=(token>>24)&15;
        if(modifier==1)for(auto& value:result)value=-value;
        else if(modifier==11)for(auto& value:result)value=std::fabs(value);
        else if(modifier)throw std::runtime_error("Unexpected particle source modifier "+std::to_string(modifier));
        return result;
    };
    unsigned colour=12;
    const auto& code=shader.bytecode;
    assert(code.at(0)==0xfffe0300u);
    for(std::size_t i=1;i<code.size();) {
        const unsigned token=code[i],op=token&0xffff,length=(token>>24)&15;
        if(op==0xfffe){i+=1+((token>>16)&0x7fff);continue;}
        if(op==0xffff){assert(colour<12);return o[colour][3];}
        const auto* a=&code[i+1];
        if(op==81)std::memcpy(c[a[0]&0x7ff].data(),a+1,16);
        else if(op==31) {
            const auto type=((a[1]>>28)&7)|((a[1]>>8)&0x18);
            if(type==6&&(a[0]&15)==10&&((a[0]>>16)&15)==0)colour=a[1]&0x7ff;
        } else {
            const Register x=source(a[1]),y=length>2?source(a[2]):Register{},z=length>3?source(a[3]):Register{};
            Register out{};
            for(unsigned k=0;k<4;++k)switch(op) {
                case 1:out[k]=x[k];break;
                case 2:out[k]=x[k]+y[k];break;
                case 3:out[k]=x[k]-y[k];break;
                case 4:out[k]=x[k]*y[k]+z[k];break;
                case 5:out[k]=x[k]*y[k];break;
                case 6:out[k]=1.f/x[0];break;
                case 7:out[k]=1.f/std::sqrt(std::fabs(x[0]));break;
                case 8:out[k]=x[0]*y[0]+x[1]*y[1]+x[2]*y[2];break;
                case 9:out[k]=x[0]*y[0]+x[1]*y[1]+x[2]*y[2]+x[3]*y[3];break;
                case 10:out[k]=std::min(x[k],y[k]);break;
                case 11:out[k]=std::max(x[k],y[k]);break;
                case 12:out[k]=x[k]<y[k]?1.f:0.f;break;
                case 13:out[k]=x[k]>=y[k]?1.f:0.f;break;
                case 18:out[k]=x[k]*y[k]+(1.f-x[k])*z[k];break;
                case 32:out[k]=std::pow(std::fabs(x[0]),y[0]);break;
                case 35:out[k]=std::fabs(x[k]);break;
                case 36:out[k]=x[k]/std::sqrt(x[0]*x[0]+x[1]*x[1]+x[2]*x[2]);break;
                default:throw std::runtime_error("Unexpected particle opcode "+std::to_string(op));
            }
            const unsigned modifier=(a[0]>>20)&15;
            if(modifier==1)for(auto& value:out)value=std::max(0.f,std::min(1.f,value));
            else if(modifier)throw std::runtime_error("Unexpected particle destination modifier");
            auto& destination=bank(a[0]);
            for(unsigned k=0;k<4;++k)if((a[0]>>(16+k))&1)destination[k]=out[k];
        }
        i+=1+length;
    }
    throw std::runtime_error("Missing original particle shader end");
}

std::vector<char> read(const char* path)
{
    std::ifstream input(path,std::ios::binary);assert(input);
    return std::vector<char>((std::istreambuf_iterator<char>(input)),{});
}
float drawAlpha(const std::shared_ptr<wot::StockEffectBinary>& definition,int quality,int mode,float distance,bool restored)
{
    wot::StockEffectUniformPool pool;wot::StockEffectUniformContext context(pool);
    wot::StockEffectBinding binding(definition,context);std::string error;
    auto write=[&](const char* name,const void* data,unsigned size) {
        bool wrote=false;
        for(unsigned b=0;b<definition->uniform_buffers.size();++b)
            for(unsigned u=0;u<definition->uniform_buffers[b].uniforms.size();++u) {
                auto field=definition->uniform_buffers[b].uniforms[u];
                if(field.name!=name&&field.semantic!=name)continue;
                if(definition->uniform_buffers[b].flags&1) {
                    field.flags&=~2u;
                    assert(binding.uniform_buffer(b)->write_uniform(field,definition->types[field.type_index],data,size,error));
                }else assert(binding.set_uniform(b,u,data,size,{},error));
                wrote=true;
            }
        assert(wrote);
    };
    write("particleQuality",&quality,4);write("particleTransformMode",&mode,4);
    const float projection[16]={1,0,0,0,0,1,0,0,0,0,1,1,0,0,0,0};
    const float camera[4]={0,0,0,1},direction[3]={0,0,1};
    write("ViewProjection",projection,sizeof(projection));write("CameraPos",camera,sizeof(camera));write("CameraDir",direction,sizeof(direction));
    if(restored) {
        const auto value=wot::stockTPSParticleParameters(1.0471975f,false,quality,false);
        write("ParticlesParams",value.data(),sizeof(value));
    }
    unsigned technique=unsigned(definition->techniques.size());
    for(unsigned i=0;i<definition->techniques.size();++i)if(definition->techniques[i].name=="Render")technique=i;
    wot::StockDx9PassSelection pass;
    assert(binding.select_pass(technique,0,pass,error)&&pass.vertex_shader);
    const auto& shader=definition->shaders[*pass.vertex_shader];
    std::vector<wot::StockShaderUniformView> views;
    for(const auto& resource:shader.resources)if(resource.kind==3) {
        std::optional<unsigned> buffer;
        assert(wot::resolve_stock_effect_reference(*definition,binding.selection(),wot::StockEffectResourceTable::uniform_buffer,resource.resource_index,buffer,error)&&buffer);
        const auto& bytes=binding.uniform_buffer(*buffer)->bytes();
        if(resource.shader_slot>=views.size())views.resize(resource.shader_slot+1);
        views[resource.shader_slot]={bytes.data(),bytes.size()};
    }
    wot::StockShaderRegisters registers;assert(wot::stage_stock_shader_registers(shader,views,registers,error));
    return runAlpha(shader,registers[2].words,distance);
}

int main(int argc,char** argv)
{
    assert(argc==5);unsigned draws=0;
    for(unsigned pipeline=0;pipeline<2;++pipeline) {
        auto bytes=read(argv[1+pipeline]);auto effect=std::make_shared<wot::StockEffectBinary>();std::string error;
        assert(wot::decode_stock_dx9_effect(bytes.data(),bytes.size(),*effect,error));
        bytes=read(argv[3+pipeline]);wot::StockTPSResource resource;
        assert(resource.read(bytes.data(),bytes.size(),error));
        unsigned sources=0;
        for(const auto& lod:resource.effects())for(const auto& emitter:lod.emitters) {
            const int kind=productionRenderKind(emitter);
            assert(kind==3||kind==4); // Authored smoke/fire and their long variants.
            const int mode=kind==3?0:2;
            for(int quality=0;quality<=4;++quality)for(float distance:{1.f,2.f,3.5f,5.f,20.f}) {
                const float oldAlpha=drawAlpha(effect,quality,mode,distance,false);
                const float alpha=drawAlpha(effect,quality,mode,distance,true);
                const float expected=.75f*std::max(0.f,std::min(1.f,(distance-2.f)/3.f));
                if (oldAlpha!=0.f||!std::isfinite(alpha)||std::fabs(alpha-expected)>=.00001f)
                    throw std::runtime_error("Particle alpha mismatch: pipeline="+std::to_string(pipeline)+
                        " quality="+std::to_string(quality)+" mode="+std::to_string(mode)+
                        " distance="+std::to_string(distance)+" absent="+std::to_string(oldAlpha)+
                        " restored="+std::to_string(alpha)+" expected="+std::to_string(expected));
                ++draws;
            }
            ++sources;
        }
        assert(sources==(pipeline==0?10u:7u));
    }
    std::cout<<"Authored shot particle alpha: "<<draws<<" original shader draws, zero-default negative passed\n";
}
