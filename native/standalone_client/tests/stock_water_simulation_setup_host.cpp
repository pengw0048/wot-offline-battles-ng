// Run SDK setupSim against the real #1513 ARIE uniform writer, including
// vector-width clamping, fresh material storage and caller-owned textures.
#include "wot/stock_effect_binding.hpp"
#include "wot/stock_effect_reflection.hpp"
#include <algorithm>
#include <cassert>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <string>
#define BW_GUARD
#define MF_ASSERT(x) assert(x)
struct Vector4 {float data[4];Vector4(float a,float b,float c,float d):data{a,b,c,d}{}};
template<class T>using ComObjectWrap=T*;
struct ID3DXEffect {
    wot::StockEffectReflection reflection;
    wot::StockEffectBinding binding;
    std::map<std::string,void*> textures;
    ID3DXEffect(std::shared_ptr<wot::StockEffectBinary> definition,wot::StockEffectUniformContext& context)
        :reflection(definition,{}),binding(definition,context){}
    int set(const char* name,const void* value,unsigned count,unsigned type){
        const auto* reference=reflection.parameter_reference(reflection.parameter_by_name(name));
        assert(reference);std::string error;
        assert(binding.set_uniform(reference->owner,reference->member,value,count,type,error));return 0;
    }
    int SetBool(const char* name,bool v){unsigned bits=v;return set(name,&bits,4,4);}
    int SetFloat(const char* name,float v){return set(name,&v,4,3);}
    int SetVector(const char* name,const Vector4* v){return set(name,v,16,3);}
    int SetTexture(const char* name,void* v){assert(reflection.parameter_by_name(name));textures[name]=v;return 0;}
    int CommitChanges(){assert(!"No simulation pass has begun yet");return -1;}
    float value(const char* name,unsigned component=0){
        auto* reference=reflection.parameter_reference(reflection.parameter_by_name(name));
        const auto& uniform=reflection.definition().uniform_buffers[reference->owner].uniforms[reference->member];
        auto bytes=binding.uniform_buffer(reference->owner);float value;
        std::memcpy(&value,bytes->bytes().data()+uniform.byte_offset+component*4,4);return value;
    }
};
struct Texture {void* pTexture(){return this;}};
struct Target {Texture texture;Texture* pTexture(){return &texture;}};
struct SimulationTextureBlock {
    Target targets[2];Target* previous(){return &targets[0];}Target* current(){return &targets[1];}
    int width(){return 256;}int height(){return 256;}
};
struct SimulationCell {static float s_landScaleX,s_landScaleY,s_waveSpeedSquared;};
float SimulationCell::s_landScaleX=50,SimulationCell::s_landScaleY=20,SimulationCell::s_waveSpeedSquared=200;
namespace Math {template<class T>T clamp(T lo,T v,T hi){return std::max(lo,std::min(v,hi));}}
struct Waters {static bool highQualityMode(){return true;}};
struct SimulationManager {
    Texture cookie;Texture* cookieTexture(){return &cookie;}
    void setupSim(SimulationTextureBlock*,ComObjectWrap<ID3DXEffect>&,float);
};
@SETUP@
int main(int argc,char**argv){
    assert(argc==2);std::ifstream input(argv[1],std::ios::binary);
    std::vector<char> bytes((std::istreambuf_iterator<char>(input)),{});
    auto definition=std::make_shared<wot::StockEffectBinary>();std::string error;
    assert(wot::decode_stock_dx9_effect(bytes.data(),bytes.size(),*definition,error));
    wot::StockEffectUniformPool pool;wot::StockEffectUniformContext context(pool);
    SimulationManager manager;SimulationTextureBlock target;
    for(int generation=0;generation!=2;++generation){
        ID3DXEffect effect(definition,context);auto* pointer=&effect;manager.setupSim(&target,pointer,1.f/30.f);
        assert(effect.value("psSimulationTexCoordDelta_x0y1")==-1.f/256.f);
        assert(effect.value("psSimulationTexCoordDelta_x2y1")==1.f/256.f);
        assert(effect.value("psSimulationTexCoordDelta_x1y0",1)==-1.f/256.f);
        assert(effect.value("psSimulationTexCoordDelta_x1y2",1)==1.f/256.f);
        assert(effect.value("psSimulationGridSize")==50.f/256.f);
        assert(effect.value("psSimulationGridSize",2)==20.f/256.f);
        assert(effect.value("dampening")==.9f&&effect.value("psSimulationWaveSpeedSquared")==200.f);
        assert(effect.value("psDeltaTime")==1.f/30.f);
        assert(effect.textures["heightMap"]==target.current()->pTexture());
        assert(effect.textures["previousHeightMap"]==target.previous()->pTexture());
        assert(effect.textures["cookieMap"]==manager.cookieTexture()->pTexture());
    }
    std::puts("SDK setupSim and original ARIE field writer passed for recreated materials");
}
