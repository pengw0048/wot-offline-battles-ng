#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using uint64=uint64_t; using int64=int64_t; using int32=int32_t; using uint32=uint32_t;
using UINT64=uint64; using DWORD=uint32; using BOOL=int; using HRESULT=int;
constexpr HRESULT S_OK=0, S_FALSE=1, E_FAIL=-1, D3DERR_DEVICELOST=-2;
constexpr DWORD D3DISSUE_BEGIN=1, D3DISSUE_END=2, D3DGETDATA_FLUSH=1;
constexpr int D3DQUERYTYPE_TIMESTAMP=1, D3DQUERYTYPE_TIMESTAMPFREQ=2, D3DQUERYTYPE_TIMESTAMPDISJOINT=3;
#define FALSE 0
#define ENABLE_GPU_PROFILER 1
#define ENABLE_NVIDIA_PERFKIT 0
#define ENABLE_PER_CORE_PROFILER 0
#define MAX_GPU_SCOPE_COUNT 256
#define MAX_GPU_FRAME_COUNT 3
#define MAX_GPU_STACK_DEPTH 256
#define BW_GUARD
#define PROFILER_SCOPED(x)
#define MF_WATCH(...)
#define ERROR_MSG(...)
#define INLINE
#define SUCCEEDED(v) ((v)>=0)
#define MF_ASSERT(v) do { if (!(v)) throw std::runtime_error(#v); } while(0)
#define MF_MAX(a,b) std::max((a),(b))

static uint64 queryClock=1000000, stampFrequency=3071999488ULL, gpuFrequency=24000000;
static unsigned queryIssues=0, queryReads=0, liveQueries=0;
static bool deviceLost=false;
uint64 timestamp() { return 10000000000000ULL; }
uint64 stampsPerSecond() { return stampFrequency; }
double stampsToSeconds(uint64 value) { return double(value)/double(stampFrequency); }
struct IDirect3DQuery9 {
    IDirect3DQuery9() { ++liveQueries; }
    int type=0; uint64 value=0; bool fixed=false; HRESULT readResult=S_OK, issueResult=S_OK;
    unsigned pending=0;
    HRESULT Issue(DWORD) {
        ++queryIssues;
        if (!fixed) value = type==D3DQUERYTYPE_TIMESTAMPFREQ ? gpuFrequency :
            type==D3DQUERYTYPE_TIMESTAMPDISJOINT ? 0 : (queryClock+=1000);
        return issueResult;
    }
    HRESULT GetData(void* data,DWORD size,DWORD) {
        ++queryReads;
        if (pending) { --pending; return S_FALSE; }
        if (readResult==S_OK) std::memcpy(data,&value,size);
        return readResult;
    }
    void Release() { --liveQueries; delete this; }
};
using LPDIRECT3DQUERY9=IDirect3DQuery9*;
struct IDirect3DDevice9 {
    HRESULT CreateQuery(int type,IDirect3DQuery9** result) {
        *result=new IDirect3DQuery9; (*result)->type=type; return S_OK;
    }
    HRESULT TestCooperativeLevel() { return deviceLost ? D3DERR_DEVICELOST : S_OK; }
};
struct DeviceCallback {};
namespace Watcher { enum { WT_READ_WRITE=1 }; }
namespace Moo {
struct RC {
    struct Data { unsigned nDrawcalls_=0,nPrimitives_=0; } data;
    IDirect3DDevice9* device() { static IDirect3DDevice9 device; return &device; }
    Data& liveProfilingData() { return data; }
};
RC& rc() { static RC value; return value; }
}

struct Profiler {
    enum EventType{EVENT_START,EVENT_START_IDLE,EVENT_END,EVENT_COUNTER};
    enum EventCategory{CATEGORY_CPP=1,CATEGORY_GPU=2,CATEGORY_PYTHON=4,CATEGORY_ALL=7};
    enum{MAX_STACK_DEPTH=64,HISTORY_LENGTH=512};
    struct ProfileEntry{EventType event_;EventCategory category_;const char*name_;uint64 time_;int threadIdx_;int value_;int frameNumber_;};
    struct DisplayEntry{DisplayEntry*parent_;DisplayEntry*sibling_;DisplayEntry*child_;const char*name_;uint64 start_;uint64 numInclusiveCycles_;uint64 numChildCycles_;uint64 numInclusiveIdleCycles_;bool isIdle_;};
    struct EntryBuffer{ProfileEntry*entries_;int32 entryCount_;uint64 endOfFrameTimeStamp_;};
    struct Thread{DisplayEntry*head_;};
    bool doJsonDump_=false,forceJsonDump_=false,freezeProfiling_=false,isActive_=false,isPendingActive_=false;
    void*jsonOutputFile_=nullptr;float timeTakenForProfiling_=0;
    int maxNumThreads_=2,displayEntryCount_=0,maxEntryCount_=4096,maxDisplayEntryCount_=2048;
    int highWatermarkOfDisplayEntryCount_=0,frame_=0,profileFrameNumber_=1,gpuThreadID_=1;
    std::vector<std::vector<DisplayEntry*>>dStack_{2,std::vector<DisplayEntry*>(MAX_STACK_DEPTH)},dChildTails_{2,std::vector<DisplayEntry*>(MAX_STACK_DEPTH)};
    std::vector<DisplayEntry*>dHeads_{2,nullptr};std::vector<int>totalStackDepth_{0,0};std::vector<Thread>threads_{2};
    std::vector<std::vector<float>>perThreadHistory_{2,std::vector<float>(HISTORY_LENGTH)};
    std::vector<DisplayEntry>displayStorage_{2048};DisplayEntry*displayEntryBuffer_=displayStorage_.data();
    std::vector<ProfileEntry>entryStorage_{4096};EntryBuffer buffer{entryStorage_.data(),0,10000000};
    EntryBuffer*profileEntryBuffer_=&buffer;EntryBuffer*finalProfileEntryBuffer_=&buffer;
    std::pair<int,const char*>category_{7,"All"};std::pair<int,const char*>*finalEventCategory_=&category_;
    std::string profileErrorString_;
    void jsonDump(){}void stopJsonDump(){}void outputReport(){}void doRemoveThreads(){}void addGPUThread(){}
    void addEntry(const char*,EventType,int) {} // Counter producer is irrelevant to the CPU tree fixture.
    ProfileEntry*startNextEntry() { return &entryStorage_.at(buffer.entryCount_++); }
    void addGPUEntry(const char*,int64,EventType,int32=0);
    void processEntries();
    @WATCHER_ENABLED@
    void clear() { buffer.entryCount_=0;entryStorage_[0].time_=1000000; }
    unsigned gpuEntries() const { unsigned n=0;for(int i=0;i<buffer.entryCount_;++i)n+=entryStorage_[i].category_==CATEGORY_GPU;return n; }
    void cpu(EventType event,uint64 time) { *startNextEntry()={event,CATEGORY_CPP,"CPU",time,0,0,profileFrameNumber_}; }
};
using Moo::rc;
static Profiler g_profiler;
@PROCESS_ENTRIES@
@ADD_GPU_ENTRY@

@GPU_CLASS@
@GPU_SCOPE_CLASS@
@GPU_METHODS@

struct Fixture {
    GPUProfiler& gpu;
    Fixture():gpu(GPUProfiler::instance()) {
        gpu.fini();gpu.~GPUProfiler();std::memset(&gpu,0,sizeof(gpu));new(&gpu) GPUProfiler;
        gpu.init();g_profiler.clear();queryIssues=queryReads=0;deviceLost=false;stampFrequency=3071999488ULL;gpuFrequency=24000000;
    }
    ~Fixture() {
#ifdef OLD_CODE
        // Keep unrelated old resource leaks out of the two negative controls.
        gpu.setFrameDelay(MAX_GPU_FRAME_COUNT);
#endif
        gpu.fini();
    }
    void enable(bool value=true) { g_profiler.isActive_=value;g_profiler.isPendingActive_=false; }
    void delay(unsigned value) { gpu.setFrameDelay(value); }
    auto& frame() { return gpu.profilerFrames_[gpu.curFrame_]; }
    void frameWithScope() { gpu.beginFrame();auto id=gpu.beginScope("Scope");gpu.endScope(id);gpu.endFrame(); }
};
static void require(bool condition,const char*message) { if(!condition)throw std::runtime_error(message); }

static void validateCPU() {
    g_profiler.cpu(Profiler::EVENT_END,2000000);
    g_profiler.processEntries();
    require(g_profiler.threads_[0].head_ && g_profiler.threads_[0].head_->numInclusiveCycles_==1000000,
        "CPU hierarchy changed while containing a GPU batch");
}

static void off() {
    Fixture f;f.enable(false);f.delay(1);
    for(unsigned i=0;i<8;++i)f.frameWithScope();
    require(queryIssues==0 && queryReads==0 && g_profiler.gpuEntries()==0,"Disabled GPU timing still issues queries");
}

static void dumpFailure() {
    Fixture f;f.enable();f.delay(1);g_profiler.cpu(Profiler::EVENT_START,1000000);
    f.gpu.beginFrame();auto&frame=f.frame();const uint64 base=frame.frameBeginQuery_->value;
    // Exact 24 MHz timestamp deltas reconstructed from the failing f30 dump.
    const uint64 times[4][2]={{280,285},{329,331},{334,336},{342,344}};
    const char*names[]={"App_Tick","App_UpdateAnimations","AppDraw_Device","DebugApp_draw"};
    for(unsigned i=0;i<4;++i){auto id=f.gpu.beginScope(names[i]);f.gpu.endScope(id);frame.profilerRecords_[i].startQuery_->value=base+times[i][0];frame.profilerRecords_[i].endQuery_->value=base+times[i][1];}
    frame.frameEndQuery_->fixed=true;frame.frameEndQuery_->value=base+5;
    f.gpu.endFrame();
#ifdef OLD_CODE
    bool asserted=false;
    try { validateCPU(); } catch(const std::runtime_error& e) { asserted=std::string(e.what())=="openDisplayEntry->numInclusiveCycles_ >= openDisplayEntry->numChildCycles_"; }
    require(asserted,"Original GPU dump no longer reproduces the native assertion");
#else
    require(g_profiler.gpuEntries()==0,"Invalid GPU batch was partly published");validateCPU();
#endif
}

static void transitions() {
    Fixture f;f.delay(3);f.enable(false);f.gpu.beginFrame();require(!queryIssues,"Initial disabled frame queried GPU");
    f.enable();f.frameWithScope();f.frameWithScope();require(!queryReads && !g_profiler.gpuEntries(),"Unissued warm-up frame was consumed");
    f.frameWithScope();require(g_profiler.gpuEntries()==4,"Completed delayed GPU frame was not published");
    g_profiler.clear();f.gpu.beginFrame();auto id=f.gpu.beginScope("BeforeDisable");f.enable(false);f.gpu.endScope(id);
    unsigned reads=queryReads;f.gpu.endFrame();require(queryReads==reads && !g_profiler.gpuEntries(),"Disabled transition consumed stale frame");
    unsigned issues=queryIssues;f.frameWithScope();require(queryIssues==issues && queryReads==reads,"Disabled frame resumed query production");
    f.enable();f.frameWithScope();f.frameWithScope();require(queryReads==reads,"Re-enable consumed an old pending frame");
    f.frameWithScope();require(g_profiler.gpuEntries()==4,"Re-enabled query ring failed to resume");
}

static void emptyAndNested() {
    Fixture f;f.enable();f.delay(1);g_profiler.cpu(Profiler::EVENT_START,1000000);
    f.gpu.beginFrame();f.gpu.endFrame();require(g_profiler.gpuEntries()==2,"Valid zero-scope frame was lost");
    f.gpu.beginFrame();auto a=f.gpu.beginScope("A");auto b=f.gpu.beginScope("B");f.gpu.endScope(b);f.gpu.endScope(a);f.gpu.endFrame();
    require(g_profiler.gpuEntries()==8,"Valid nested GPU scopes were lost");validateCPU();
}

static void failures() {
    for(unsigned which=0;which<10;++which){
        Fixture f;f.enable();f.delay(1);g_profiler.cpu(Profiler::EVENT_START,1000000);
        f.gpu.beginFrame();auto id=f.gpu.beginScope("Scope");f.gpu.endScope(id);auto&frame=f.frame();
        IDirect3DQuery9*queries[]={frame.disjointQuery_,frame.freqQuery_,frame.frameBeginQuery_,frame.frameEndQuery_,frame.profilerRecords_[0].startQuery_,frame.profilerRecords_[0].endQuery_};
        if(which<6)queries[which]->readResult=E_FAIL;
        if(which==6){frame.disjointQuery_->fixed=true;frame.disjointQuery_->value=1;}
        if(which==7)gpuFrequency=0;
        if(which==8){frame.freqQuery_->pending=1;deviceLost=true;}
        if(which==9)frame.frameEndQuery_->issueResult=E_FAIL;
        f.gpu.endFrame();require(!g_profiler.gpuEntries(),"Failed query batch was published");validateCPU();
    }
    Fixture f;f.enable();f.delay(1);f.gpu.beginFrame();f.frame().frameBeginQuery_->pending=2;f.gpu.endFrame();
    require(g_profiler.gpuEntries()==2,"Eventually completed query was rejected");
}

static void scopeOwnership() {
    Fixture f;f.enable();f.delay(1);f.gpu.beginFrame();auto*old=new AutoGPUProfilerScope("Old");f.gpu.endFrame();
    require(!g_profiler.gpuEntries(),"Incomplete frame was published");
    f.gpu.beginFrame();auto*current=new AutoGPUProfilerScope("New");unsigned issues=queryIssues;delete old;
    require(queryIssues==issues,"Previous frame's RAII owner closed a current scope");
    delete current;auto once=f.gpu.beginScope("Once");f.gpu.endScope(once);
    issues=queryIssues;f.gpu.endScope(once);require(queryIssues==issues,"Scope closed twice");
    f.gpu.endFrame();require(g_profiler.gpuEntries()==6,"Current scope failed after a stale close");
    g_profiler.clear();f.gpu.beginFrame();auto a=f.gpu.beginScope("A");auto b=f.gpu.beginScope("B");f.gpu.endScope(a);f.gpu.endScope(b);f.gpu.endFrame();
    require(!g_profiler.gpuEntries(),"Crossing scopes entered the hierarchical profiler");
    f.gpu.beginFrame();for(unsigned i=0;i<MAX_GPU_SCOPE_COUNT;++i){auto token=f.gpu.beginScope("Many");f.gpu.endScope(token);}
    issues=queryIssues;f.gpu.endScope(f.gpu.beginScope("Overflow"));require(queryIssues==issues,"Full scope buffer admitted an extra query");
    f.gpu.endFrame();require(g_profiler.gpuEntries()==2+2*MAX_GPU_SCOPE_COUNT,"Full valid scope buffer was lost");
}

static void deviceLifecycle() {
    Fixture f;f.enable();f.delay(3);f.frameWithScope();f.gpu.beginFrame();auto old=f.gpu.beginScope("OldDevice");
    f.delay(1);f.gpu.deleteUnmanagedObjects();require(!liveQueries,"Changing query delay leaked ring resources");
    f.gpu.createUnmanagedObjects();f.delay(3);unsigned issues=queryIssues;f.gpu.endScope(old);
    require(queryIssues==issues,"Old device scope touched new query objects");
    f.frameWithScope();f.frameWithScope();require(!g_profiler.gpuEntries(),"Device reset consumed old queries");
    f.frameWithScope();require(g_profiler.gpuEntries()==4,"GPU timing failed after device reset");
    f.gpu.fini();f.gpu.fini();
}

static void longFrame() {
    Fixture f;f.enable();f.delay(1);gpuFrequency=1000000000;stampFrequency=3072000000;
    g_profiler.cpu(Profiler::EVENT_START,1000000);f.gpu.beginFrame();auto id=f.gpu.beginScope("Long");f.gpu.endScope(id);
    auto&frame=f.frame();uint64 base=frame.frameBeginQuery_->value;
    frame.profilerRecords_[0].startQuery_->value=base+1000000;
    frame.profilerRecords_[0].endQuery_->value=base+6000000000ULL;
    frame.frameEndQuery_->fixed=true;frame.frameEndQuery_->value=base+6010000000ULL;
    f.gpu.endFrame();require(g_profiler.gpuEntries()==4,"Long valid frame overflowed timestamp conversion");validateCPU();
}

int main(int argc,char**argv) {
    try {
        const std::string name=argc>1?argv[1]:"";
        if(name=="off")off();else if(name=="dump")dumpFailure();else if(name=="transitions")transitions();
        else if(name=="empty")emptyAndNested();else if(name=="failures")failures();else if(name=="ownership")scopeOwnership();
        else if(name=="device")deviceLifecycle();else if(name=="long")longFrame();else throw std::runtime_error("Unknown case");
#ifndef OLD_CODE
        require(liveQueries==0,"GPU query objects leaked after shutdown");
#endif
        std::cout<<"PASS "<<name<<"\n";return 0;
    } catch(const std::exception&e) { std::cerr<<e.what()<<"\n";return 1; }
}
