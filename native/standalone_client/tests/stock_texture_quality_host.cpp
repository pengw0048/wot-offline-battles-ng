// The test runner inserts the actual patched SDK methods below the declarations.
#include "wot/stock_texture_quality.hpp"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <vector>
using uint32 = unsigned;
using uint = unsigned;
using DWORD = unsigned;
using D3DFORMAT = unsigned;
using D3DTEXTUREFILTERTYPE = unsigned;
using std::max;
#ifndef ENABLE_RESOURCE_COUNTERS
#define ENABLE_RESOURCE_COUNTERS 1
#endif
#define TRACE_MSG(...)
#define WARNING_MSG(...)
#define BW_GUARD
#define MEMTRACKER_SCOPED(x)
#define MF_ASSERT(x) assert(x)
#define MAKEFOURCC(a,b,c,d) (unsigned(a)|(unsigned(b)<<8)|(unsigned(c)<<16)|(unsigned(d)<<24))
enum { D3DFMT_UNKNOWN=0, D3DFMT_A8R8G8B8=21, D3DFMT_DXT1=MAKEFOURCC('D','X','T','1'),
       D3DX_FILTER_POINT=1, D3DX_FILTER_LINEAR=2, D3DTEXF_NONE=0, D3DTEXF_POINT=1,
       D3DTEXF_LINEAR=2, D3DTEXF_ANISOTROPIC=3 };
namespace BW {
struct StringRef : std::string {
    using std::string::string;
    StringRef(const std::string& value) : std::string(value) {}
    std::string to_string() const { return *this; }
};
using string = std::string;
}
static const DWORD DDS_MAGIC=0x20534444;
struct DDS_PIXELFORMAT { DWORD dwSize,dwFlags,dwFourCC,dwRGBBitCount,dwRBitMask,dwGBitMask,dwBBitMask,dwABitMask; };
struct DDS_HEADER {
    DWORD dwSize,dwHeaderFlags,dwHeight,dwWidth,dwPitchOrLinearSize,dwDepth,dwMipMapCount;
    DWORD reserved1[11]; DDS_PIXELFORMAT ddspf; DWORD dwCaps,dwCaps2,dwCaps3,dwCaps4,reserved2;
};
static_assert(sizeof(DDS_HEADER)==124,"DDS header layout");
@FILESYSTEM@
struct BWResource {
    static BWResource& instance(){static BWResource v;return v;}
    FileSystem* fileSystem(){static FileSystem v;return &v;}
    static BW::StringRef removeExtension(const BW::StringRef& s){return s.substr(0,s.find_last_of('.'));}
    static BW::StringRef getExtension(const BW::StringRef& s){return s.substr(s.find_last_of('.')+1);}
};
// The production helper below is extracted from base_texture.cpp as well.
BW::StringRef removeTextureExtension(const BW::StringRef&);
namespace DDSHelper {
bool isDXTFormat(unsigned format){return format==D3DFMT_DXT1;}
unsigned getFormat(const DDS_PIXELFORMAT& p){return p.dwFourCC?p.dwFourCC:D3DFMT_A8R8G8B8;}
bool isFlatTexture(const DDS_HEADER& h){return h.dwCaps2==0;}
}
namespace Math { template<class T>T clamp(T a,T b,T c){return std::max(a,std::min(b,c));} }
namespace StreamingTextureLoader {
struct HeaderInfo {bool valid_=false;unsigned width_=0,height_=0,numMipLevels_=0,format_=0;bool isFlatTexture_=false;};
struct LoadOptions {unsigned mipSkip_=0,numMips_=0;};
struct LoadResult {unsigned texture2D_=0;};
static unsigned lastSkip=999,lastMips=999;
bool loadHeaderFromStream(IFileStreamer*,HeaderInfo&);
bool createTexture(const HeaderInfo&,const LoadOptions& o,LoadResult& r) {
    lastSkip=o.mipSkip_;lastMips=o.numMips_;r.texture2D_=1;return true;
}
}
struct DataSection {
    std::map<std::string,int> numbers;
    void readStrings(const char*,std::vector<std::string>&) {}
    int readInt(const char* key,int fallback){auto i=numbers.find(key);return i==numbers.end()?fallback:i->second;}
    bool readBool(const char* key,bool fallback){return readInt(key,fallback)!=0;}
    std::string readString(const char*,const std::string& fallback){return fallback;}
};
using DataSectionPtr=std::shared_ptr<DataSection>;
struct FormatNameMap {
    static std::string formatString(unsigned){return "A8R8G8B8";}
    static unsigned formatType(const std::string& value){return value=="UNKNOWN"?0:21;}
};
struct TextureDetailLevel {
    std::vector<std::string> prefixes_,postfixes_,contains_;
    unsigned format_=21,formatCompressed_=0,lodMode_=0,mipCount_=0,mipSize_=0,streamingPriority_=0,maxExtent_=0,sourceMipType_=0;
    bool noResize_=false,noFilter_=false,normalMap_=false,streamable_=true,allowSplit_=false;
    enum {SOURCE_NO_MIPMAPS,SOURCE_HORIZONTAL_MIPMAPS,SOURCE_VERTICAL_MIPMAPS};
    void init(DataSectionPtr);
    bool allowSplit() const{return allowSplit_;} unsigned lodMode() const{return lodMode_;}
    bool isCompressed() const{return formatCompressed_!=0;} unsigned format()const{return format_;}
    unsigned formatCompressed()const{return formatCompressed_;}
    bool noResize()const{return noResize_;}bool noFilter()const{return noFilter_;}
};
using TextureDetailLevelPtr=std::shared_ptr<TextureDetailLevel>;
struct TextureDetails {
    bool readable=true;
    std::map<std::string,TextureDetailLevelPtr> values;
    TextureDetailLevelPtr detailLevel(const BW::StringRef& id) {
        assert(readable);auto p=values.find(removeTextureExtension(id));assert(p!=values.end());return p->second;
    }
};
struct Setting {
    struct Option {std::string label;bool supported;};
    std::string name;bool important=true;std::vector<Option> options;
    unsigned value=1;std::function<void(int)> selected;
    unsigned activeOption() const{return value;}
    void selectOption(int index,bool force){assert(force);value=index;selected(index);}
    void addOption(const char* label,const char*,bool supported){options.push_back({label,supported});}
    void stockImportant(bool value){important=value;}bool stockImportant()const{return important;}
    const std::string& label()const{return name;}
};
static unsigned mapLockDepth=0,streamingLocks=0;
struct StreamingMutex {};
struct RecursiveMutexHolder {explicit RecursiveMutexHolder(int&) {++mapLockDepth;}~RecursiveMutexHolder(){--mapLockDepth;}};
struct SimpleMutexHolder {
    explicit SimpleMutexHolder(int&) {}
    explicit SimpleMutexHolder(StreamingMutex&) {assert(mapLockDepth==0);++streamingLocks;}
};
namespace Moo {
struct GraphicsSetting {
    using GraphicsSettingPtr=Setting*;
    using GraphicsSettingVector=std::vector<Setting*>;
    static GraphicsSettingVector& registry(){static GraphicsSettingVector value;return value;}
    static GraphicsSettingVector settings(){return registry();}
    static void add(Setting* setting){registry().push_back(setting);}
};
template<class T> Setting* makeCallbackGraphicsSetting(const char* name,const char*,T& owner,
        void(T::*callback)(int),int active,bool,bool){
    static std::vector<std::unique_ptr<Setting>> owned;
    owned.emplace_back(new Setting);auto* setting=owned.back().get();setting->name=name;setting->value=active;
    setting->selected=[&owner,callback](int option){(owner.*callback)(option);};return setting;
}
struct Context {unsigned anisotropy=8,reuseClears=0;unsigned maxAnisotropy()const{return anisotropy;}void clearTextureReuseList(){++reuseClears;}};
Context& rc(){static Context v;return v;}
}
struct BaseTexture {
    enum {FAIL_ATTEMPT_LOAD_PLACEHOLDER=1,FAIL_RETURN_NULL=0,STATUS_NOT_READY,STATUS_LOADING,STATUS_READY,STATUS_FAILED_TO_LOAD};
    using FailurePolicy=int;
    std::string resource;unsigned skip=0,reloads=0;bool animated=false;
    std::string allocator_;
    BaseTexture():BaseTexture("",0){}
    explicit BaseTexture(const BW::StringRef& allocator):BaseTexture("",0){allocator_=allocator;}
    BaseTexture(std::string name,unsigned mip):resource(name),skip(mip){}
    virtual ~BaseTexture()=default;
    const std::string& resourceID()const{return resource;}
    virtual unsigned mipSkip()const{return skip;}
    virtual void mipSkip(unsigned v){skip=v;}
    bool isAnimated()const{return animated;}
    virtual void reload(const BW::StringRef& id){resource=id;++reloads;}
};
struct TextureManager {
    using TextureMap=std::map<BW::StringRef,BaseTexture*>;
    using TextureStringVector=std::vector<std::pair<std::string,std::string>>;
    TextureMap textures_;int texturesLock_=0;TextureDetails details;TextureDetails* pDetailLevels_=&details;
    Setting quality,compression,filter;Setting* qualitySettings_=&quality;Setting* compressionSettings_=&compression;Setting* filterSettings_=&filter;
    TextureStringVector dirtyTextures_;
    static TextureManager* current;static TextureManager* instance(){return current;}
    bool isCompressed(TextureDetailLevelPtr)const;
    static uint32 stockBaseMipSkip(const BW::StringRef&,uint32,bool);
    void setQualityOption(int);
    void setCompressionOption(int){}void setFilterOption(int){}
    void registerSettings();
    void reloadDirtyTextures();
    BaseTexture* find(const BW::StringRef& name){auto i=textures_.find(name);return i==textures_.end()?nullptr:i->second;}
    int configurableMipFilter()const;int configurableMinMagFilter()const;int configurableMaxAnisotropy()const;
};
TextureManager* TextureManager::current=nullptr;
// The real dirty-reload body uses a retaining pointer. The fixture owns each
// texture for the complete call, while preserving its nullable lookup contract.
using BaseTexturePtr=BaseTexture*;
struct TextureLoader {
    struct InputOptions {unsigned mipSkip_=0;bool upscaleToNextPow2_=false;unsigned format_=0,mipFilter_=0;};
    static unsigned lastSkip;
    void loadTextureFromFile(const std::string&,const InputOptions& o,const std::string&){lastSkip=o.mipSkip_;}
};
unsigned TextureLoader::lastSkip=999;
struct ManagedTexture:BaseTexture {
    class ReloadTextureTask;
    ManagedTexture(const BW::StringRef&, const FailurePolicy&, D3DFORMAT, int,
                   bool, bool, const BW::StringRef&, bool stockSplit=false);
    unsigned width_=0,height_=0,textureMemoryUsed_=0,localFrameTimestamp_=0,frameTimestamp_=0;
    bool loadedFromDisk_=true,reuseOnRelease_=false,cubemap_=false,stockSplit_=false;
    void* texture_=nullptr;unsigned tex_=0,cubeTex_=0;
    std::string resourceID_;
    int refCount()const{return 1;}
    void release(){state=STATUS_NOT_READY;}
    unsigned mipSkip_=0,format_=21;bool noResize_=false,noFilter_=false;int loadingLock_=0,state=STATUS_NOT_READY;
    std::string qualifiedResourceID_;
    void load(const FailurePolicy&);
    int status() const{return state;}void status(int value){state=value;}
    void processLoaderOutput(const TextureLoader&,const FailurePolicy&){state=STATUS_READY;}
};
using ManagedTexturePtr=std::shared_ptr<ManagedTexture>;
struct TaskManager;
struct BackgroundTask {
    explicit BackgroundTask(const char*){}
    virtual ~BackgroundTask()=default;
    virtual void doBackgroundTask(TaskManager&)=0;
    virtual void doMainThreadTask(TaskManager&)=0;
};
struct TaskManager {BackgroundTask* pending=nullptr;void addMainThreadTask(BackgroundTask* task){pending=task;}};
static BW::StringRef canonicalTextureName(const BW::StringRef& name){return name;}
struct TextureStreamingManager;
struct StreamingTextureHandle {bool isValid()const{return true;}operator std::size_t()const{return 0;}};
struct StreamingTexture:BaseTexture {
    using BaseTexture::BaseTexture;TextureDetailLevelPtr detail_;
    TextureDetailLevelPtr detail()const{return detail_;}
    TextureStreamingManager* owner=nullptr;
    TextureStreamingManager* streamingManager()const{return owner;}
    StreamingTextureHandle streamingTextureHandle()const{return {};}
    std::string& resourceID_=resource;
    void reload();void reload(const BW::StringRef&)override;
};
struct StreamingTextureEntry {
    StreamingTexture* pTexture=nullptr;bool streamingActive=false;
    unsigned currentMip=~0u,width=0,height=0,maxAvailableMip=0,minAvailableMip=0,
        minAvailableStreamingMip=0,maxAvailableStreamingMip=0,requestedMip=0,format=0;
};
struct TextureStreamingManager {
    static const unsigned INVALID_MIP=~0u;
    StreamingMutex textureLock_;std::vector<StreamingTextureEntry> streamingTextures_{1};
    void reloadTexture(StreamingTexture*);
    TextureManager* pTextureManager_=nullptr;StreamingTextureEntry entry;
    StreamingTextureEntry* fetchEntry(StreamingTexture* t){return entry.pTexture==t?&entry:nullptr;}
    void updateTextureMipSkip(StreamingTexture*,uint32);
    unsigned initialLoadMipSkip()const{return 2;}
    struct StreamingTextureLoadTask {
        TextureStreamingManager* pManager_=nullptr;StreamingTextureEntry requestEntry_;
        unsigned pTargetTexture_=0;
        bool createTargetTexture();
    };
};
static unsigned calculateMaxMips(unsigned w,unsigned h){unsigned v=std::max(w,h),n=0;while(v>1){v>>=1;++n;}return n;}

// Minimal Python list ownership; the real publication loop below selects which
// registered owners reach the original UI. Per-setting serialization is separate.
using Py_ssize_t=std::size_t;
struct PyObject {
    int refs=1;std::string label;std::vector<PyObject*> values;
    ~PyObject(){for(auto* value:values)if(value && --value->refs==0)delete value;}
};
struct PyObjectPtr {
    enum {STEAL_REFERENCE};PyObject* value;
    PyObjectPtr(PyObject* object,int):value(object){}
    ~PyObjectPtr(){if(value && --value->refs==0)delete value;}
    PyObject* get()const{return value;}explicit operator bool()const{return value;}
};
static PyObject* PyList_New(std::size_t size){auto* out=new PyObject;out->values.resize(size);return out;}
static void PyList_SET_ITEM(PyObject* list,std::size_t index,PyObject* value){list->values[index]=value;}
static void Py_INCREF(PyObject* value){++value->refs;}
static PyObject* settingView(Setting* setting){auto* out=new PyObject;out->label=setting->label();return out;}

@OWNERS@

static void dds(const std::string& name,unsigned width,unsigned height,unsigned mips,unsigned format=D3DFMT_A8R8G8B8) {
    DDS_HEADER h={};h.dwSize=124;h.dwWidth=width;h.dwHeight=height;h.dwMipMapCount=mips;h.ddspf.dwFourCC=format==21?0:format;
    std::vector<unsigned char> bytes(128);std::memcpy(bytes.data(),&DDS_MAGIC,4);std::memcpy(bytes.data()+4,&h,124);
    BWResource::instance().fileSystem()->add(name,bytes);
}
static TextureDetailLevelPtr detail(TextureManager& m,const std::string& name,unsigned lod,bool split,bool compressed=false){
    auto value=std::make_shared<TextureDetailLevel>();auto section=std::make_shared<DataSection>();
    section->numbers["lodMode"]=lod;section->numbers["allowSplit"]=split;value->init(section);
    value->formatCompressed_=compressed?D3DFMT_DXT1:0;m.details.values[removeTextureExtension(name)]=value;return value;
}
int main(int argc,char** argv){
    if(argc==2 && std::string(argv[1])=="oracle"){
        unsigned cap,option,lod,split,mips,logical;
        while(std::cin>>cap>>option>>lod>>split>>mips>>logical){
            auto q=wot::stockTextureQuality(option,cap);
            std::cout<<q.mipSkip<<' '<<q.filter<<' '<<q.compressed<<' '<<q.supported<<' '
                     <<wot::stockTextureLogicalMipSkip(q.mipSkip,lod)<<' '
                     <<wot::stockTextureBaseMipSkip(logical,split,mips)<<'\n';
        }return 0;
    }
    TextureManager registered;registered.registerSettings();
    assert(Moo::GraphicsSetting::registry().size()==3);
    assert(registered.qualitySettings_->options.size()==4);
    assert(registered.qualitySettings_->options[0].label=="MAX" && !registered.qualitySettings_->options[0].supported);
    assert(registered.qualitySettings_->options[3].label=="LOW" && registered.qualitySettings_->options[3].supported);
    {
        PyObjectPtr view(stockGraphicsSettingsView(),PyObjectPtr::STEAL_REFERENCE);
        assert(view.get()->values.size()==1 && view.get()->values[0]->label=="TEXTURE_QUALITY");
    }
    registered.qualitySettings_->selectOption(0,true);
    assert(registered.qualitySettings_->activeOption()==1);
    TextureManager m;TextureManager::current=&m;
    m.quality.selected=[&](int option){m.setQualityOption(option);};
    for(unsigned cap:{1u,2u,4u,8u,16u}){
        Moo::rc().anisotropy=cap;m.quality.value=0;m.setQualityOption(0);
        assert(m.quality.value==(cap>=4?1u:cap>=2?2u:3u));
    }
    Moo::rc().anisotropy=8;
    auto defaultDetail=std::make_shared<TextureDetailLevel>();defaultDetail->init(std::make_shared<DataSection>());assert(!defaultDetail->allowSplit());
    for(unsigned lod=0;lod<4;++lod)for(bool split:{false,true}){
        const std::string name="resource"+std::to_string(lod)+std::to_string(split)+".dds";
        auto d=detail(m,name,lod,split);dds(name,512,512,10);
        for(unsigned option=1;option<4;++option){
            m.quality.value=option;
            const unsigned logical=calcMipSkip(wot::stockTextureQuality(option,8).mipSkip,lod);
            const unsigned expected=logical>(split&&lod!=0)?logical-(split&&lod!=0):0;
            m.details.readable=false;
            ManagedTexture managed(name,0,21,logical,false,false,"fixture",split&&lod!=0);
            if(TextureLoader::lastSkip!=expected)return 11;
            assert(managed.mipSkip_==logical);
            StreamingTexture texture(name,logical);texture.detail_=d;TextureStreamingManager streaming;streaming.pTextureManager_=&m;
            TextureStreamingManager::StreamingTextureLoadTask task;task.pManager_=&streaming;task.requestEntry_.pTexture=&texture;
            assert(task.createTargetTexture());assert(task.requestEntry_.maxAvailableStreamingMip==9-expected);
            // Preserve the existing initial two-mip streaming budget, then
            // exercise the real already-loaded request path at its quality cap.
            assert(StreamingTextureLoader::lastSkip==expected+2);
            task.requestEntry_.currentMip=task.requestEntry_.requestedMip;
            task.requestEntry_.requestedMip=task.requestEntry_.maxAvailableStreamingMip;
            assert(task.createTargetTexture());assert(StreamingTextureLoader::lastSkip==expected);
            streaming.entry=task.requestEntry_;streaming.updateTextureMipSkip(&texture,logical);
            assert(streaming.entry.maxAvailableStreamingMip==9-expected);
            StreamingTexture other("unused.dds",0);streaming.updateTextureMipSkip(&other,logical);
            // The real task captures authored policy before background loading.
            // Change the source afterward to prove it uses the captured value.
            auto owner=std::make_shared<ManagedTexture>(name,0,21,logical,false,false,"fixture",split&&lod!=0);
            std::unique_ptr<BackgroundTask> reload(new ManagedTexture::ReloadTextureTask(owner,name));
            owner->stockSplit_=!owner->stockSplit_;
            TaskManager tasks;reload->doBackgroundTask(tasks);
            assert(TextureLoader::lastSkip==expected && tasks.pending==reload.get());
            tasks.pending->doMainThreadTask(tasks);
            assert(owner->stockSplit_==(split&&lod!=0));
            assert(owner->mipSkip_==logical && owner->status()==BaseTexture::STATUS_READY);
            m.details.readable=true;
        }
    }
    // Short chains: clamp before subtracting the authored split slot. The HD
    // file changes logical extent but is never uploaded by the base loader.
    detail(m,"short.dds",1,true);dds("short.dds",2,2,2);
    assert(m.stockBaseMipSkip("short.dds",2,true)==0);
    dds("short_hd.dds",4,4,1);assert(m.stockBaseMipSkip("short.dds",2,true)==1);
    dds("short_hd.dds",8,4,1);assert(m.stockBaseMipSkip("short.dds",2,true)==0);
    detail(m,"single.dds",1,true);dds("single.dds",1,1,1);assert(m.stockBaseMipSkip("single.dds",2,true)==0);
    detail(m,"plain.dds",1,false);dds("plain.dds",2,2,2);assert(m.stockBaseMipSkip("plain.dds",2,false)==1);
    detail(m,"absent.dds",1,true);assert(m.stockBaseMipSkip("absent.dds",2,true)==1);
    detail(m,"short.c.dds",1,true);dds("short.c.dds",2,2,2,D3DFMT_DXT1);dds("short_hd.c.dds",4,4,1,D3DFMT_DXT1);
    assert(m.stockBaseMipSkip("short.c.dds",2,true)==1);
    // A failed header read also relinquishes its archive stream, so later
    // texture requests in the same package can still open and read normally.
    BWResource::instance().fileSystem()->add("broken.dds",std::vector<unsigned char>(16));
    assert(m.stockBaseMipSkip("broken.dds",2,false)==2);
    StreamingTexture broken("broken.dds",2);broken.detail_=detail(m,"broken.dds",1,false);
    TextureStreamingManager brokenManager;
    TextureStreamingManager::StreamingTextureLoadTask brokenTask;
    brokenTask.pManager_=&brokenManager;brokenTask.requestEntry_.pTexture=&broken;
    assert(!brokenTask.createTargetTexture());
    assert(m.stockBaseMipSkip("plain.dds",2,false)==1);
    // Medium/low share mipSkip=2. Compression still requires a reload, with
    // lookup through the canonical cache key in both directions.
    auto compressed=detail(m,"compressed.dds",1,false,true);detail(m,"ordinary.dds",1,false);
    detail(m,"fixed.dds",2,false);detail(m,"animated.dds",1,false);
    BaseTexture texture("compressed.dds",2),ordinary("ordinary.dds",2),fixed("fixed.dds",0),animated("animated.dds",0);
    animated.animated=true;m.textures_={{"compressed.dds",&texture},{"ordinary.dds",&ordinary},{"fixed.dds",&fixed},{"animated.dds",&animated}};
    m.quality.value=3;m.setQualityOption(3);assert(texture.resource=="compressed.c.dds"&&texture.reloads==1&&ordinary.reloads==0&&fixed.skip==0&&animated.skip==0);
    assert(m.isCompressed(compressed));assert(!m.isCompressed(m.details.values["ordinary"]));
    assert(m.configurableMaxAnisotropy()==1&&m.configurableMinMagFilter()==2&&m.configurableMipFilter()==2);
    m.quality.value=2;m.setQualityOption(2);assert(texture.resource=="compressed.dds"&&texture.reloads==2);assert(!m.isCompressed(compressed));
    m.quality.value=1;m.setQualityOption(1);assert(ordinary.skip==1&&ordinary.reloads==1&&texture.skip==1);
    assert(m.configurableMaxAnisotropy()==8&&m.configurableMinMagFilter()==3);
    assert(m.dirtyTextures_.empty());
    // Execute both real reload owners while tracking their native lock order.
    StreamingTexture loaded("ordinary.dds",2);loaded.detail_=m.details.values["ordinary"];
    TextureStreamingManager liveStreaming;loaded.owner=&liveStreaming;
    m.textures_={{"ordinary.dds",&loaded}};m.quality.value=1;m.setQualityOption(1);
    assert(streamingLocks==1 && mapLockDepth==0 && loaded.skip==1);
    std::cout<<"Actual SDK texture quality consumers passed\n";
}
