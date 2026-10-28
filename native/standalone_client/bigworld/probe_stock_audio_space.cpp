// Reuse the existing real SDK/CPython audio probe's translation unit and link
// dependencies. Its original main is compiled but never executed here.
#define main unusedStockAudioObjectsMain
#include "probe_stock_audio_objects.cpp"
#undef main
#include "resmgr/xml_section.hpp"

namespace
{
void eventReceipt(const char* phase,const char* event,bool expected)
{
    auto voice=BW::createStockAudioVoice(event,"Scene bank probe");
    require(BW::setStockAudioSourceVolume(BW::stockAudioVoiceSource(voice),0.f),
        "Cannot mute the actual probe emitter");
    const bool accepted=BW::playStockAudioVoice(voice);
    std::cout<<"phase="<<phase<<" event="<<event<<" accepted="<<accepted
        <<" playingID="<<voice->playingID<<std::endl;
    require(accepted==expected,"Actual SDK scene-bank event result differs");
    if(accepted) BW::stopStockAudioVoice(voice);
    require(AK::SoundEngine::RenderAudio(true)==AK_Success,"SDK command processing failed");
}
void phase(const char* name,bool accepted)
{
    eventReceipt(name,"wpn_small_PC",accepted);
    eventReceipt(name,"gun_rld_20_45mm",accepted);
    eventReceipt(name,"eng_gaz_aa_pc",accepted);
}
}

int main(int argc,char**argv)
{
    BW_SYSTEMSTAGE_MAIN();
    BW::LogMsg::automatedTest(true);
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    if(argc<2) return 2;
    // Message-only HWND: no visible window, activation, input or focus change.
    HWND window=CreateWindowExW(0,L"STATIC",L"Audio scene bank probe",0,
        0,0,0,0,HWND_MESSAGE,NULL,GetModuleHandle(NULL),NULL);
    if(!window) return 3;
    Py_NoSiteFlag=1;
    Py_Initialize();
    int result=1;
    try
    {
        BW::vector<BW::string> paths;
        for(int index=1;index<argc;++index) paths.push_back(argv[index]);
        require(BW::BWResource::init(paths),"Original resource mounts failed");
        const auto config=BW::BWResource::openSection("engine_config.xml");
        require(config&&BW::initialiseStockAudio(config,window)&&BW::stockAudioReady(),
            "Actual SDK initialization failed");
        require(BW::selectStockAudioBankSet("SFX_soundbanks_login"),"Login banks failed");
        phase("login-before",false);
        BW::DataSectionPtr arena=new BW::XMLSection("probe_arena");
        require(arena->newSection("gameplayTypes")!=NULL,"Cannot create authored-section fixture");
        BW::setStockAudioSpaceBankSet(arena);
        phase("arena-first",true);
        BW::DataSectionPtr hangar=new BW::XMLSection("probe_hangar");
        BW::setStockAudioSpaceBankSet(hangar);
        phase("hangar-after",false);
        BW::setStockAudioSpaceBankSet(arena);
        phase("arena-second",true);
        require(BW::selectStockAudioBankSet("SFX_soundbanks_login"),"Return to login banks failed");
        phase("login-after",false);
        std::cout<<"Actual SDK scene-bank negative/positive/unload/second-arena PASS"<<std::endl;
        result=0;
    }
    catch(const std::exception& error){std::cerr<<error.what()<<std::endl;}
    BW::finiStockAudio();
    if(PyErr_Occurred()) PyErr_Print();
    Py_Finalize();
    DestroyWindow(window);
    return result;
}
