#include <cassert>
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include "wot/stock_static_model_binary.hpp"
#define BW_GUARD
#define IF_NOT_MF_ASSERT_DEV(x) if (!(x))
#define ARRAY_SIZE(x) (sizeof(x)/sizeof(*(x)))
#define ERROR_MSG(...) ((void)0)
#define ASSET_MSG(...) ((void)0)
#define NOTICE_MSG(...) ((void)0)
#define SUCCEEDED(x) ((x) == 0)
using BOOL = int;
using D3DXHANDLE = const char*;
template<class T> struct Ptr : std::shared_ptr<T> {
    Ptr() = default;
    Ptr(T* p):std::shared_ptr<T>(p) {}
    Ptr& operator=(T* p) { this->reset(p);return *this; }
    T* getObject() const { return this->get(); }
    bool hasObject() const { return bool(*this); }
};
template<class T> using SmartPointer = Ptr<T>;
namespace BW {
using string = std::string;
struct StringRef : std::string {
    using std::string::string;
    StringRef(const std::string& s):std::string(s) {}
    std::string to_string() const { return *this; }
};
template<class T> using vector = std::vector<T>;
struct StringBuilder {
    char* data;
    StringBuilder(char* p,std::size_t):data(p) { *p=0; }
    void append(const std::string& s) { std::strcpy(data,s.c_str()); }
    const char* string() const { return data; }
};
}
struct Vector3 { float x,y,z;Vector3(float a,float b,float c):x(a),y(b),z(c) {} };
struct Vector4 { float x=0,y=0,z=0,w=0;Vector4()=default;Vector4(float a,float b,float c,float d):x(a),y(b),z(c),w(d) {} };
struct Matrix {
    std::array<float,16> values{};
    void translation(Vector3 v) { values[12]=v.x;values[13]=v.y;values[14]=v.z; }
};
struct DataSection;
using DataSectionPtr = Ptr<DataSection>;
namespace Moo {
struct BaseTexture { std::string name;void* pTexture() { return this; }const std::string& resourceID() const { return name; } };
using BaseTexturePtr = Ptr<BaseTexture>;
struct TextureManager {
    static TextureManager* instance() { static TextureManager v;return &v; }
    BaseTexturePtr get(const BW::StringRef& name) { return new BaseTexture{ name }; }
};
struct EffectProperty {
    BW::StringRef name;
    explicit EffectProperty(const BW::StringRef& n):name(n) {}
    virtual ~EffectProperty() = default;
    const BW::StringRef& propName() const { return name; }
    void setParent(EffectProperty*) {}
    virtual EffectProperty* clone() const = 0;
    virtual bool be(const bool&) { return false; }
    virtual bool be(const int&) { return false; }
    virtual bool be(const float&) { return false; }
    virtual bool be(const Vector4&) { return false; }
    virtual bool be(const Matrix&) { return false; }
    virtual bool be(const BaseTexturePtr) { return false; }
    virtual bool be(const BW::StringRef&) { return false; }
    virtual bool getTexture(BaseTexturePtr&) { return false; }
    virtual bool getFloat(float&) const { return false; }
    virtual bool getInt(int&) const { return false; }
    virtual bool getBool(bool&) const { return false; }
    virtual bool getVector(Vector4&) const { return false; }
    virtual bool getMatrix(Matrix&) const { return false; }
};
using EffectPropertyPtr = Ptr<EffectProperty>;
using EffectPropertyMappings = std::map<BW::StringRef,EffectPropertyPtr>;
}
struct DataSection {
    std::string effect;
    Moo::EffectPropertyMappings authored;
    explicit DataSection(const char*) {}
    void writeString(const char* key,const BW::StringRef& v) { if(std::string(key)=="fx")effect=v; }
    void writeBool(const char*,bool) {}
    void writeFloat(const char*,float) {}
    void writeInt(const char*,int) {}
    void writeVector4(const char*,const Vector4&) {}
    void setString(const BW::StringRef&) {}
    DataSectionPtr newSection(const char*) { return new DataSection(""); }
};
using XMLSection = DataSection;
struct ID3DXEffect {
    std::map<std::string,int> names;
    D3DXHANDLE GetParameterByName(D3DXHANDLE,const char* name) {
        auto found=names.find(name);return found==names.end()?nullptr:found->first.c_str();
    }
    int SetTexture(D3DXHANDLE,void*) { return 0; }
    int SetVector(D3DXHANDLE,const Vector4*) { return 0; }
    int SetMatrix(D3DXHANDLE,const Matrix*) { return 0; }
    int SetBool(D3DXHANDLE,BOOL) { return 0; }
    int SetFloat(D3DXHANDLE,float) { return 0; }
    int SetInt(D3DXHANDLE,int) { return 0; }
};
namespace Moo {
@PROPERTY_CLASSES@
struct ManagedEffect {
    std::string resource;
    EffectPropertyMappings defaults;
    ID3DXEffect effect;
    ID3DXEffect* pEffect() { return &effect; }
    const EffectPropertyMappings& defaultProperties() const { return defaults; }
    const std::string& resourceID() const { return resource; }
};
using ManagedEffectPtr = Ptr<ManagedEffect>;
static std::map<std::string,ManagedEffectPtr> effects;
struct EffectPropertyFactory;
using EffectPropertyFactoryPtr = Ptr<EffectPropertyFactory>;
struct EffectPropertyFactory {
    static const EffectPropertyFactoryPtr& findFactory(const BW::StringRef&) {
        static EffectPropertyFactoryPtr absent;return absent;
    }
    EffectPropertyPtr create(const BW::StringRef&,D3DXHANDLE,ID3DXEffect*) { return {}; }
};
const std::size_t MAX_PARAM_NAME_SIZE = 1024;
struct EffectMaterial {
    using Properties = std::map<D3DXHANDLE,EffectPropertyPtr>;
    ManagedEffectPtr pManagedEffect_;
    Properties properties_;
    std::string identifier_;
    bool loadInternal(DataSectionPtr section,EffectPropertyMappings& mappings) {
        const auto found=effects.find(section->effect);
        if(found==effects.end())return false;
        pManagedEffect_=found->second;
        for(const auto& p:section->authored)mappings[p.first]=p.second->clone();
        return true;
    }
    bool load(DataSectionPtr,bool addDefault=true,bool suppressPropertyWarnings=false);
    bool initFromEffect(const BW::StringRef&,const BW::StringRef& diffuseMap="",int doubleSided=-1);
    void loadProperties(EffectPropertyMappings&,bool) const;
    void loadPropertyHandles(const EffectPropertyMappings&,bool);
    bool isDefault(EffectPropertyPtr);
    void replaceDefaults();
    Properties& properties() { return properties_; }
    EffectPropertyPtr getProperty(const BW::StringRef&);
    template<class T>bool setPropertyType(const BW::StringRef&,const T&,bool,const BW::StringRef&);
    template<class T>bool setProperty(const BW::StringRef& name,const T& value,bool create) { return setPropertyType(name,value,create,"Texture"); }
    const ManagedEffectPtr& pEffect() const { return pManagedEffect_; }
    void save(DataSectionPtr section) { section->authored.clear();for(const auto& p:properties_)section->authored[p.second->propName()]=p.second; }
};
using EffectMaterialPtr = Ptr<EffectMaterial>;
@SDK_METHODS@
struct ComplexEffectMaterial {
    EffectMaterialPtr base = new EffectMaterial;
    ComplexEffectMaterial() = default;
    ComplexEffectMaterial(const ComplexEffectMaterial& other):base(new EffectMaterial(*other.base)) {}
    bool initFromEffect(const char* name) { return base->initFromEffect(name); }
    bool load(DataSectionPtr section) { return base->load(section); }
    EffectMaterialPtr baseMaterial() { return base; }
    const std::string& identifier() const { return base->identifier_; }
};
using ComplexEffectMaterialPtr = Ptr<ComplexEffectMaterial>;
}
@CLONE@
@VARIANT@
struct StringTable {
    std::map<std::uint32_t,std::string> strings;
    const char* entry(std::uint32_t k) const { auto it=strings.find(k);return it==strings.end()?nullptr:it->second.c_str(); }
};
Matrix matrix(const wot::StockStaticModelBinary::Matrix& value) { Matrix result;result.values=value;return result; }
struct StockStaticModelSceneLoader {
    struct State {
        wot::StockStaticModelBinary binary;
        std::vector<SmartPointer<Moo::ComplexEffectMaterial>> materials;
        bool fail(const char*) { return false; }
        bool loadMaterials(const StringTable&);
    };
};
@STATIC_OWNER@
static Moo::ManagedEffectPtr makeEffect(const std::string& name) {
    auto effect=Moo::ManagedEffectPtr(new Moo::ManagedEffect);
    effect->resource=name;
    const auto add=[&](Moo::EffectProperty* p) { effect->effect.names[p->propName()]=1;effect->defaults[p->propName()]=p; };
    auto* texture=new Moo::TextureProperty("diffuseMap");texture->be(Moo::TextureManager::instance()->get("default.dds"));add(texture);
    auto* floating=new Moo::FloatProperty("amount");floating->be(.5f);add(floating);
    auto* integer=new Moo::IntProperty("srcBlend");integer->be(1);add(integer);
    auto* boolean=new Moo::BoolProperty("doubleSided");boolean->be(false);add(boolean);
    auto* vector=new Moo::Vector4Property("tint");vector->be(Vector4(1,1,1,1));add(vector);
    auto* transform=new Moo::MatrixProperty("uv");transform->be(Matrix{});add(transform);
    Moo::effects[name]=effect;return effect;
}
static std::string texture(Moo::EffectMaterialPtr material) {
    Moo::BaseTexturePtr result;assert(material->getProperty("diffuseMap")->getTexture(result));return result->name;
}
int main() {
    auto effect=makeEffect("rock.fx");
    StringTable strings;
    strings.strings={{1,"rock.fx"},{2,"diffuseMap"},{3,"rock.dds"},{4,"wood.dds"},{5,"amount"},{6,"srcBlend"},{7,"doubleSided"},{8,"tint"},{9,"uv"}};
    StockStaticModelSceneLoader::State state;
    auto& m=state.binary.materials;
    m.effects={1};
    m.records={{0,0,5},{0,6,11},{0,-1,-1}};
    float f=.25f,g=.75f;std::uint32_t bits,otherBits;std::memcpy(&bits,&f,4);std::memcpy(&otherBits,&g,4);
    m.properties={{2,6,3},{5,2,bits},{6,3,4},{7,1,1},{8,5,0},{9,4,0},
                  {2,6,4},{5,2,otherBits},{6,3,5},{7,1,0},{8,5,1},{9,4,1}};
    m.vectors={{{1,2,3,4}},{{5,6,7,8}}};
    wot::StockStaticModelBinary::Matrix first{},second{};first[12]=3;second[12]=8;m.matrices={first,second};
    assert(state.loadMaterials(strings));
    auto a=state.materials[0]->baseMaterial(),b=state.materials[1]->baseMaterial(),defaults=state.materials[2]->baseMaterial();
    if(texture(a)!="rock.dds"||texture(b)!="wood.dds"||texture(defaults)!="default.dds")return 2;
    assert(texture(defaults)=="default.dds");
    for(const auto& p:effect->defaults) {
        assert(a->getProperty(p.first)!=b->getProperty(p.first));
        assert(a->getProperty(p.first)!=p.second);
        assert(b->getProperty(p.first)!=p.second);
    }
    float amount=0;int blend=0;bool sided=false;Vector4 tint;Matrix uv;
    assert(a->getProperty("amount")->getFloat(amount)&&amount==.25f);
    assert(b->getProperty("amount")->getFloat(amount)&&amount==.75f);
    assert(a->getProperty("srcBlend")->getInt(blend)&&blend==4);
    assert(b->getProperty("srcBlend")->getInt(blend)&&blend==5);
    assert(a->getProperty("doubleSided")->getBool(sided)&&sided);
    assert(b->getProperty("doubleSided")->getBool(sided)&&!sided);
    assert(a->getProperty("tint")->getVector(tint)&&tint.x==1&&tint.w==4);
    assert(b->getProperty("tint")->getVector(tint)&&tint.x==5&&tint.w==8);
    assert(a->getProperty("uv")->getMatrix(uv)&&uv.values[12]==3);
    assert(b->getProperty("uv")->getMatrix(uv)&&uv.values[12]==8);
    assert(state.loadMaterials(strings)); // Same ManagedEffect survives mapping reload.
    assert(texture(state.materials[0]->baseMaterial())=="rock.dds");
    auto variantEffect=makeEffect("rock_modalpha.fx");
    Moo::ComplexEffectMaterialPtr source=new Moo::ComplexEffectMaterial;
    assert(source->initFromEffect("rock.fx"));
    // A shader-only variant introduces this default; it was not serialized.
    source->base->properties_.erase(effect->effect.GetParameterByName(nullptr,"srcBlend"));
    auto variantA=materialVariant(source,"_modalpha.fx");
    auto variantB=materialVariant(source,"_modalpha.fx");
    assert(variantA&&variantB);
    assert(variantA->base->setProperty("srcBlend",4,false));
    int other=0;
    assert(variantB->base->getProperty("srcBlend")->getInt(other));
    if(other!=1)return 3;
    assert(variantA->base->getProperty("srcBlend")!=variantEffect->defaults["srcBlend"]);
    // Missing effect and missing property preserve failure contracts.
    assert(!materialVariant(source,"_missing.fx"));
    assert(!a->setProperty("missing",true,false));
    return 0;
}
