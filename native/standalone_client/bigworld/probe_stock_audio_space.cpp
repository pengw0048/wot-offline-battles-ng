// Reuse the existing real SDK/CPython audio probe's translation unit and link
// dependencies. Its original main is compiled but never executed here.
#define main unusedStockAudioObjectsMain
#include "probe_stock_audio_objects.cpp"
#undef main
#include "stock_audio_space_path.hpp"

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
        // Exercise the real geometry -> arena_defs producer. Complete native
        // space.settings deliberately has no gameplayTypes on these maps.
        const char* maps[]={"33_fjord", "01_karelia", "33_fjord"};
        for (unsigned index=0;index<sizeof(maps)/sizeof(maps[0]);++index)
        {
            const char* name=maps[index];
            const BW::string geometry=BW::string("spaces/")+name;
            const auto settings=BW::BWResource::openSection(geometry+"/space.settings");
            require(settings&&!settings->openSection("gameplayTypes"),
                "Exact map environment must not impersonate the arena definition");
            const auto definition=BW::BWResource::openSection(
                BW::string(wot::stockAudioArenaResource(geometry.c_str()).c_str()));
            require(definition&&definition->openSection("gameplayTypes"),
                "Actual authored arena definition is missing");
            const BW::string music=definition->readString("wwmusicSetup/wwmusicLoading");
            require(!music.empty(),"Actual initial music event is missing");
            BW::setStockAudioSpaceBankSet(geometry);
            // The authored initial producer runs after scene-bank publication.
            // Acceptance is a real PostEvent receipt, not speaker/listening proof.
            eventReceipt(name,music.c_str(),true);
            phase(name,true);
            BW::setStockAudioSpaceBankSet("spaces/hangar_v2");
            phase("hangar-after",false);
        }
        require(BW::selectStockAudioBankSet("SFX_soundbanks_login"),"Return to login banks failed");
        phase("login-after",false);
        std::cout<<"Actual SDK real arena_defs producer / initial music / unload / same-and-other-arena PASS"<<std::endl;
        result=0;
    }
    catch(const std::exception& error){std::cerr<<error.what()<<std::endl;}
    BW::finiStockAudio();
    if(PyErr_Occurred()) PyErr_Print();
    Py_Finalize();
    DestroyWindow(window);
    return result;
}
