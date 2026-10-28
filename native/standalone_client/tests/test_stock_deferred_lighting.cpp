// Execute production load/publication/constant lifetimes with recording owners.
// This host boundary fixture does not prove GPU texture loading or rendering.
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <iostream>
#define WOT_STOCK_DEFERRED_LIGHTING_PORTABLE
#define BW_BEGIN_NAMESPACE
#define BW_END_NAMESPACE
#define SUCCEEDED(value) ((value)>=0)
using D3DXHANDLE = const char*;
struct Vector3 {
    float v[3];
    Vector3(float x=0,float y=0,float z=0):v{x,y,z}{}
    float operator[](unsigned i)const{return v[i];}
};
namespace BW {using string=std::string;}
struct DataSection {
    std::map<std::string,std::string> strings;
    std::map<std::string,Vector3> vectors;
    std::map<std::string,float> scalars;
    unsigned luminanceReads=0;
    std::string readString(const char* name){return strings[name];}
    Vector3 readVector3(const char* name,const Vector3& fallback){auto at=vectors.find(name);return at==vectors.end()?fallback:at->second;}
    float readFloat(const char* name,float fallback){if(std::string(name)=="max_lum")++luminanceReads;auto at=scalars.find(name);return at==scalars.end()?fallback:at->second;}
};
using DataSectionPtr=std::shared_ptr<DataSection>;
namespace Moo {struct BaseTexture;using BaseTexturePtr=std::shared_ptr<BaseTexture>;}
struct StockDeferredLightingSettings {
    float sphericalHarmonics[28]={};bool loaded=false;
    std::string pmremResource;Moo::BaseTexturePtr pmrem;float pmremMips[4]={};
    float forwardMultiplier=1.f;bool forward=false;
};
bool loadStockDeferredLightingSettings(DataSectionPtr,StockDeferredLightingSettings&,bool forward=false);
bool initialiseStockForwardLightingConstants();
struct AutoConfigString {
    explicit AutoConfigString(const char* name){assert(std::string(name)=="system/PMREMTexture");}
    const std::string& value()const{static std::string resource="system/maps/PMREM.dds";return resource;}
};
enum {D3DFMT_A8R8G8B8=21};
namespace BWResource {
std::map<std::string,DataSectionPtr> resources;
bool textureExists=false;
bool pmremExists=true;
DataSectionPtr openSection(const std::string& name){return resources[name];}
bool fileExists(const std::string& name){
    if(name=="system/maps/env_brdf_lut.dds")return textureExists;
    assert(name=="actual/pmrem.dds"||name=="system/maps/PMREM.dds"||name=="missing/pmrem.dds");
    return name=="actual/pmrem.dds"||name=="system/maps/PMREM.dds"?pmremExists:false;
}
}
std::vector<std::string> errors;
void error(const char* format,...){char buffer[512];va_list args;va_start(args,format);std::vsnprintf(buffer,sizeof(buffer),format,args);va_end(args);errors.push_back(buffer);}
#define ERROR_MSG(...) error(__VA_ARGS__)
struct ID3DXEffect {
    std::vector<float> uniform;
    void* texture=NULL;
    bool fail=false;
    int SetValue(D3DXHANDLE,const void* data,unsigned bytes){
        if(fail)return -1;
        assert(bytes==112||bytes==16);
        uniform.resize(bytes/4);
        std::memcpy(uniform.data(),data,bytes);
        return 0;
    }
    int SetTexture(D3DXHANDLE,void* value){if(fail)return -1;texture=value;return 0;}
};
namespace Moo {
struct Colour {
    float r,g,b,a;
    Colour(float red=0,float green=0,float blue=0,float alpha=1):r(red),g(green),b(blue),a(alpha){}
};
struct BaseTexture {
    enum Status {STATUS_READY,STATUS_FAILED_TO_LOAD};
    Status state=STATUS_READY;
    void* gpu=reinterpret_cast<void*>(0x1513);
    void* pTexture(){return gpu;}
    Status status(){return state;}
    unsigned width(){return 1024;}
    unsigned format(){return D3DFMT_A8R8G8B8;}
};
struct EffectConstantValue {virtual ~EffectConstantValue(){};virtual bool operator()(ID3DXEffect*,D3DXHANDLE)=0;};
struct EffectVisualContext {
    enum Type {AUTO_CONSTANT_TYPE_PER_FRAME};
    std::map<std::string,std::shared_ptr<EffectConstantValue>> constants;
    unsigned registrations=0,removals=0;
    struct Sun {Colour m_ambient;};
    Sun sunlight;
    const Sun& sunLight() const {return sunlight;}
    void registerAutoConstant(Type,const char* name,EffectConstantValue* constant){assert(!constants.count(name));constants[name].reset(constant);++registrations;}
    void unregisterAutoConstant(Type,const char* name){assert(constants.erase(name)==1);++removals;}
};
struct RenderContext {EffectVisualContext context;EffectVisualContext& effectVisualContext(){return context;}};
RenderContext& rc(){static RenderContext context;return context;}
struct TextureManager {
    BaseTexturePtr source=std::make_shared<BaseTexture>();
    unsigned reads=0;
    static TextureManager* instance(){static TextureManager manager;return &manager;}
    BaseTexturePtr get(const std::string& resource,bool animation,bool placeholder,bool load,const char* allocator){
        assert((resource=="system/maps/env_brdf_lut.dds"||resource=="actual/pmrem.dds"||resource=="system/maps/PMREM.dds")&&animation&&!placeholder&&load);
        assert(std::string(allocator)=="Art/File Textures/");++reads;return source;
    }
};
}
#include "../bigworld/stock_deferred_lighting.cpp"
int main(){
    auto environment=std::make_shared<DataSection>();auto source=std::make_shared<DataSection>();
    environment->strings["PBS/DEFERRED/REM_SH"]="actual/one.xml";BWResource::resources["actual/one.xml"]=source;
    environment->strings["PBS/DEFERRED/PMREM"]="actual/pmrem.dds";
    for(unsigned i=0;i<9;++i)source->vectors["sh"+std::to_string(i)]=Vector3(float(i),float(i+20),float(i+40));
    StockDeferredLightingSettings first,second;
    assert(!loadStockDeferredLightingSettings(NULL,first)&&!first.loaded&&!errors.empty());
    assert(loadStockDeferredLightingSettings(environment,first)&&first.loaded&&source->luminanceReads==1);
    assert(!initialiseStockDeferredLightingConstants()&&Moo::rc().context.constants.empty());
    BWResource::textureExists=true;
    auto manager=Moo::TextureManager::instance();manager->source->state=Moo::BaseTexture::STATUS_FAILED_TO_LOAD;
    assert(!initialiseStockDeferredLightingConstants()&&Moo::rc().context.constants.empty());
    manager->source->state=Moo::BaseTexture::STATUS_READY;manager->source->gpu=NULL;
    assert(!initialiseStockDeferredLightingConstants()&&Moo::rc().context.constants.empty());
    manager->source->gpu=reinterpret_cast<void*>(0x1513);
    assert(initialiseStockDeferredLightingConstants()&&initialiseStockDeferredLightingConstants());
    auto& context=Moo::rc().context;
    assert(context.constants.size()==4&&context.registrations==4&&!context.constants.count("SpeedTreeMaterials"));
    ID3DXEffect effect;
    auto sh=context.constants["REM_SH9"],brdf=context.constants["EnvBRDFLut"];
    auto pmrem=context.constants["PMREM"],mips=context.constants["PMREMMipsNumber"];
    assert(!(*pmrem)(&effect,"PMREM")&&!(*mips)(&effect,"MIPS"));
    assert(!(*sh)(&effect,"SH"));
    activateStockDeferredLightingSettings(first);assert((*sh)(&effect,"SH"));
    assert(effect.uniform==std::vector<float>(first.sphericalHarmonics,first.sphericalHarmonics+28));
    assert((*brdf)(&effect,"BRDF")&&effect.texture==manager->source->gpu);
    assert((*pmrem)(&effect,"PMREM")&&effect.texture==first.pmrem->pTexture());
    assert((*mips)(&effect,"MIPS")&&effect.uniform==std::vector<float>({7,1,0,0}));
    BWResource::textureExists=false;
    assert(!initialiseStockDeferredLightingConstants()&&context.constants.size()==4&&!(*brdf)(&effect,"BRDF"));
    BWResource::textureExists=true;
    assert(initialiseStockDeferredLightingConstants()&&context.registrations==4);
    auto other=std::make_shared<DataSection>();other->vectors["sh0"]=Vector3(100,200,300);
    BWResource::resources["actual/two.xml"]=other;environment->strings["PBS/DEFERRED/REM_SH"]="actual/two.xml";
    assert(loadStockDeferredLightingSettings(environment,second)&&second.loaded);
    assert((*sh)(&effect,"SH")&&effect.uniform[0]==first.sphericalHarmonics[0]);
    activateStockDeferredLightingSettings(second);second.sphericalHarmonics[0]=999;
    assert((*sh)(&effect,"SH")&&effect.uniform[0]==100); // Publication owns its copy.
    effect.fail=true;assert(!(*sh)(&effect,"SH")&&!(*brdf)(&effect,"BRDF")&&!(*pmrem)(&effect,"PMREM")&&!(*mips)(&effect,"MIPS"));effect.fail=false;
    deactivateStockDeferredLightingSettings();assert(!(*sh)(&effect,"SH")&&!(*pmrem)(&effect,"PMREM")&&!(*mips)(&effect,"MIPS"));
    environment->strings["PBS/DEFERRED/PMREM"]="missing/pmrem.dds";
    assert(loadStockDeferredLightingSettings(environment,second)&&second.pmremResource=="system/maps/PMREM.dds");
    BWResource::pmremExists=false;
    assert(!loadStockDeferredLightingSettings(environment,second)&&!second.loaded&&!second.pmrem);
    BWResource::pmremExists=true;
    environment->strings.clear();assert(!loadStockDeferredLightingSettings(environment,second)&&!second.loaded);
    activateStockDeferredLightingSettings(second);assert(!(*sh)(&effect,"SH"));
    finaliseStockDeferredLightingConstants();finaliseStockDeferredLightingConstants();
    assert(context.constants.empty()&&context.removals==4&&!brdfLut);
    assert(!(*brdf)(&effect,"BRDF"));
    assert(initialiseStockDeferredLightingConstants());finaliseStockDeferredLightingConstants();
    assert(context.registrations==8&&context.removals==8);
    // A forward owner neither requires nor creates deferred-only BRDF/PMREM.
    BWResource::textureExists=false;BWResource::pmremExists=false;
    assert(initialiseStockForwardLightingConstants()&&initialiseStockForwardLightingConstants());
    assert(context.constants.size()==1&&context.constants.count("REM_SH9"));
    environment->strings["PBS/FORWARD/REM_SH"]="actual/forward.xml";
    environment->scalars["PBS/FORWARD/SH_multiplier"]=2.29f;
    BWResource::resources["actual/forward.xml"]=source;
    assert(loadStockDeferredLightingSettings(environment,first,true)&&first.loaded&&first.forward&&!first.pmrem);
    activateStockDeferredLightingSettings(first);
    context.sunlight.m_ambient=Moo::Colour(.2f,.3f,.4f);
    sh=context.constants["REM_SH9"];
    assert((*sh)(&effect,"SH"));
    const float expectedAmbient[]={.2f,.3f,.4f};
    for(unsigned colour=0;colour<3;++colour)
        for(unsigned component=0;component<4;++component)
            assert(effect.uniform[colour*4+component]==first.sphericalHarmonics[colour*4+component]*(expectedAmbient[colour]*first.forwardMultiplier));
    for(unsigned i=12;i<28;++i)assert(effect.uniform[i]==first.sphericalHarmonics[i]);
    const auto previous=effect.uniform;
    context.sunlight.m_ambient=Moo::Colour(.8f,.7f,.6f);
    assert((*sh)(&effect,"SH")&&effect.uniform!=previous);
    first.forwardMultiplier=999;assert((*sh)(&effect,"SH")&&effect.uniform[0]!=999);
    finaliseStockDeferredLightingConstants();finaliseStockDeferredLightingConstants();
    assert(context.constants.empty()&&context.registrations==9&&context.removals==9);
    std::cout<<"Production SH/BRDF load, publication, failures and repeated managed lifecycle passed"<<std::endl;
}
