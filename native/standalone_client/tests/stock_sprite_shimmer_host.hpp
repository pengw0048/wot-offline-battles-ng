#include <any>
#include <array>
#include <cassert>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <type_traits>
#define BW_GUARD
#define ERROR_MSG(...) throw std::runtime_error("Unexpected material mode")
#define BW_BEGIN_NAMESPACE namespace BW {
#define BW_END_NAMESPACE }
namespace BW {
using StringRef = std::string;
using string = std::string;
template<class T> using SmartPointer = std::shared_ptr<T>;
class DataSection {
public:
    std::map<string,std::any> values;
    template<class T> T read(const StringRef& name, const T& fallback) {
        const auto found=values.find(name);
        return found==values.end() ? fallback : std::any_cast<T>(found->second);
    }
    template<class T> void write(const StringRef& name, const T& value) { values[name]=value; }
};
}
#include "particle_serialisation.hpp"
namespace BW {
namespace Moo {
struct Material { enum { ZERO=1, ONE=2, SRC_COLOUR=3, INV_SRC_COLOUR=4, SRC_ALPHA=5, INV_SRC_ALPHA=6 }; };
struct EffectMaterial {
    string shader;
    std::map<string,std::any> values;
    unsigned writes=0;
    struct Properties { unsigned clears=0; void clear(){++clears;} } properties_;
    inline static unsigned loads=0;
    inline static std::set<string> resources;
    void initFromEffect(const string& name) {
        if (!resources.count(name)) throw std::runtime_error("Undeclared shader resource");
        shader=name; ++loads;
    }
    bool pEffect() const { return !shader.empty(); }
    Properties& properties(){return properties_;}
    template<class T> void setProperty(const char* name, const T& value, bool create) {
        assert(create);
        const string key(name);
        const bool ordinary=shader.find("shimmer")==string::npos;
        const std::set<string> fields={"srcBlend","destBlend","alphaReference",
            "softDepthRange","softFalloffPower","softDepthOffset",
            "nearFadeCutoff","nearFadeStart","nearFadeFalloffPower"};
        // Exact .10/.11 effect tables are independently checked by the shader
        // consumer. An unknown property is a rejected production call.
        if (key!="diffuseMap" && (!ordinary || !fields.count(key)))
            throw std::runtime_error("Unknown shader property: "+key);
        if constexpr(std::is_enum_v<T>) values[key]=int(value);
        else values[key]=value;
        ++writes;
    }
};
using EffectMaterialPtr=std::shared_ptr<EffectMaterial>;
}
namespace SpriteParticleRendererDefaults {
struct Config { string name; const string& value()const{return name;} };
Config s_defaultEffectFileName{"configured/default.fx"};
Config s_shimmerEffectFileName{"configured/shimmer.fx"};
}
using namespace SpriteParticleRendererDefaults;
class ParticleSystemRenderer {
public:
    void clone(ParticleSystemRenderer*)const {}
};
class SpriteParticleRenderer : public ParticleSystemRenderer {
public:
    enum MaterialFX { FX_ADDITIVE, FX_ADDITIVE_ALPHA, FX_BLENDED,
        FX_BLENDED_COLOUR, FX_BLENDED_INVERSE_COLOUR, FX_SOLID, FX_SHIMMER,
        FX_SOURCE_ALPHA, FX_MAX };
    MaterialFX materialFX_=FX_BLENDED;
    string effectName_=s_defaultEffectFileName.value(), textureName_;
    bool materialSettingsChanged_=false, rotated_=false;
    int frameCount_=0;
    float frameRate_=0;
    std::array<float,3> explicitOrientation_{};
    float softDepthRange_=0,softFalloffPower_=1,softDepthOffset_=0;
    float nearFadeCutoff_=1,nearFadeStart_=1,nearFadeFalloffPower_=1;
    Moo::EffectMaterialPtr effectMaterial_;
    std::shared_ptr<int> diffuseMap_=std::make_shared<int>(1);
    unsigned propertyUpdates=0, textureUpdates=0;
    explicit SpriteParticleRenderer(const StringRef& name) : textureName_(name) { updateMaterial(); }
    MaterialFX materialFX()const {return materialFX_;}
    void materialFX(MaterialFX);
    const string& effectName()const{return effectName_;}
    void effectName(const string&);
    void updateMaterial();
    void updateTexture(){++textureUpdates;}
    void setMaterial(Moo::EffectMaterialPtr&);
    void setMaterialProperties(bool force=false);
    void setMaterialFX();
    void loadInternal(DataSectionPtr);
    void saveInternal(DataSectionPtr)const;
    SpriteParticleRenderer* clone()const;
    template<class S> void serialise(const S&)const;
    void drawPrefix();
#define ACCESSOR(T,N) T N()const{return N##_;} void N(T v){N##_=v;}
    ACCESSOR(int,frameCount)
    ACCESSOR(float,frameRate)
    ACCESSOR(float,softDepthRange)
    ACCESSOR(float,softFalloffPower)
    ACCESSOR(float,softDepthOffset)
    ACCESSOR(float,nearFadeCutoff)
    ACCESSOR(float,nearFadeStart)
    ACCESSOR(float,nearFadeFalloffPower)
#undef ACCESSOR
    const string& textureName()const{return textureName_;}
    void textureName(const string& v){textureName_=v;}
    const std::array<float,3>& explicitOrientation()const{return explicitOrientation_;}
    void explicitOrientation(const std::array<float,3>& v){explicitOrientation_=v;}
};
@METHODS@
}
int main(){
    using namespace BW;
    Moo::EffectMaterial::resources={s_defaultEffectFileName.name,s_shimmerEffectFileName.name,"configured/explicit.fx"};
    SpriteParticleRenderer owner("authored.dds");
    assert(owner.effectMaterial_->shader==s_defaultEffectFileName.name);
    for(int mode:{6,6,2,0,1,3,4,5,7,6,2}){
        auto previous=owner.effectMaterial_;
        unsigned loads=Moo::EffectMaterial::loads;
        owner.materialFX(static_cast<SpriteParticleRenderer::MaterialFX>(mode));
        const string expected=mode==6?s_shimmerEffectFileName.name:s_defaultEffectFileName.name;
        assert(owner.effectName_==expected && owner.materialSettingsChanged_);
        assert(owner.effectMaterial_==previous && Moo::EffectMaterial::loads==loads);
        owner.drawPrefix();
        assert(owner.effectMaterial_!=previous && owner.effectMaterial_->shader==expected);
        assert(!owner.materialSettingsChanged_ && Moo::EffectMaterial::loads==loads+1);
        owner.drawPrefix();assert(Moo::EffectMaterial::loads==loads+1);
        const auto& values=owner.effectMaterial_->values;
        assert(owner.effectMaterial_->writes==values.size());
        if (mode==6) assert(values.size()==1 && values.count("diffuseMap"));
        else {
            const int source[]={5,2,5,3,3,2,0,2}, destination[]={2,6,6,4,4,1,0,1};
            assert(std::any_cast<int>(values.at("srcBlend"))==source[mode]);
            assert(std::any_cast<int>(values.at("destBlend"))==destination[mode]);
            assert(values.count("alphaReference")==unsigned(mode==7));
            assert(std::any_cast<float>(values.at("softDepthRange"))==owner.softDepthRange_);
        }
        const unsigned writes=owner.effectMaterial_->writes;
        owner.setMaterialProperties();assert(owner.effectMaterial_->writes==writes);
        owner.setMaterialProperties(true);
        assert(owner.effectMaterial_->writes-writes==unsigned(mode==6?1:7));
    }
    owner.effectName("configured/explicit.fx");
    assert(owner.effectMaterial_->shader=="configured/explicit.fx");
    owner.materialFX(SpriteParticleRenderer::FX_SHIMMER);owner.drawPrefix();
    assert(owner.effectMaterial_->shader==s_shimmerEffectFileName.name);
    owner.effectName("configured/explicit.fx");
    auto section=std::make_shared<DataSection>();owner.saveInternal(section);
    assert(std::any_cast<int>(section->values.at("materialFX_"))==6);
    assert(!section->values.count("effectName_"));
    SpriteParticleRenderer loaded("other.dds");loaded.loadInternal(section);
    assert(loaded.materialFX()==SpriteParticleRenderer::FX_SHIMMER);
    assert(loaded.effectMaterial_->shader==s_shimmerEffectFileName.name);
    assert(loaded.textureName()=="authored.dds");
    owner.rotated_=true;
    std::unique_ptr<SpriteParticleRenderer> cloned(owner.clone());
    assert(cloned->materialFX()==SpriteParticleRenderer::FX_SHIMMER && cloned->rotated_);
    assert(cloned->effectMaterial_!=owner.effectMaterial_);
    assert(cloned->effectMaterial_->shader==s_shimmerEffectFileName.name);
    owner.materialFX(SpriteParticleRenderer::FX_BLENDED);owner.drawPrefix();
    assert(cloned->effectMaterial_->shader==s_shimmerEffectFileName.name);
    section->values["materialFX_"]=2;loaded.loadInternal(section);
    assert(loaded.effectMaterial_->shader==s_defaultEffectFileName.name);
    section->values.erase("materialFX_");loaded.effectName("configured/explicit.fx");loaded.loadInternal(section);
    assert(loaded.effectMaterial_->shader==s_defaultEffectFileName.name);
    // Configured resource identities, not hardcoded shader paths, own selection.
    s_defaultEffectFileName.name="alternative/default.fx";
    s_shimmerEffectFileName.name="alternative/shimmer.fx";
    Moo::EffectMaterial::resources.insert(s_defaultEffectFileName.name);
    Moo::EffectMaterial::resources.insert(s_shimmerEffectFileName.name);
    for(auto mode:{SpriteParticleRenderer::FX_SHIMMER,SpriteParticleRenderer::FX_BLENDED}){
        loaded.materialFX(mode);loaded.drawPrefix();
        assert(loaded.effectMaterial_->shader==(mode==6?s_shimmerEffectFileName.name:s_defaultEffectFileName.name));
    }
}
