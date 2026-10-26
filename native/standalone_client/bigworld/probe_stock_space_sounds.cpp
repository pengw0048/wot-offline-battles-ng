// Actual production owners, real CPython 2/Math and Wwise 2017 SDK. Silent
// bus gains bound this probe; it does not claim audible/map-render acceptance.
// Include the production implementation to inspect the existing opaque owners
// without adding test getters or alternate implementations to product APIs.
#include "stock_audio.cpp"
#include "stock_space_sounds.cpp"
#include "cstdmf/allocator.hpp"
#include "cstdmf/log_msg.hpp"
#include "resmgr/bwresource.hpp"
#include "pyscript/py_data_section.hpp"
#include "space/space_manager.hpp"
#include <AK/SoundEngine/Common/AkQueryParameters.h>
#include <iostream>
#include <stdexcept>
#include <functional>
#include <fstream>

namespace
{
void require(bool value,const char* detail)
{ if (!value) throw std::runtime_error(detail); }
void python(const char* code)
{ require(PyRun_SimpleString(code)==0,"Actual Python boundary failed"); }
void acknowledged(const std::function<bool()>& check)
{
    const ULONGLONG deadline=GetTickCount64()+5000;
    do
    {
        require(AK::SoundEngine::RenderAudio(true)==AK_Success,"SDK RenderAudio failed");
        if (check()) return;
        SwitchToThread();
    } while(GetTickCount64()<deadline);
    throw std::runtime_error("SDK state acknowledgement timed out");
}
class EmptyProbeSpace : public BW::ClientSpace
{
public:
    explicit EmptyProbeSpace( BW::SpaceID id ) : ClientSpace( id ) {}
    unsigned clears = 0;
protected:
    virtual void doClear() { ++clears; }
    virtual bool doAddMapping( GeometryMappingID, BW::Matrix &, const BW::string &, const BW::DataSectionPtr & )
        { throw std::runtime_error( "Geometry is outside the typed-space probe" ); }
    virtual void doDelMapping( GeometryMappingID ) { throw std::runtime_error( "Geometry is outside the typed-space probe" ); }
    virtual BW::EnviroMinder & doEnviro() { throw std::runtime_error( "Environment is outside the typed-space probe" ); }
    virtual float doGetLoadStatus( float ) const { throw std::runtime_error( "Geometry is outside the typed-space probe" ); }
    virtual void doTick( float ) { throw std::runtime_error( "Tick is outside the typed-space probe" ); }
    virtual void doUpdateAnimations( float ) { throw std::runtime_error( "Animation is outside the typed-space probe" ); }
    virtual BW::AABB doGetBounds() const { throw std::runtime_error( "Bounds are outside the typed-space probe" ); }
    virtual BW::Vector3 doClampToBounds( const BW::Vector3 & ) const { throw std::runtime_error( "Bounds are outside the typed-space probe" ); }
    virtual float doCollide( const BW::Vector3 &, const BW::Vector3 &, BW::CollisionCallback & ) const
        { throw std::runtime_error( "Collision is outside the typed-space probe" ); }
    virtual float doCollide( const BW::WorldTriangle &, const BW::Vector3 &, BW::CollisionCallback & ) const
        { throw std::runtime_error( "Collision is outside the typed-space probe" ); }
    virtual float doFindTerrainHeight( const BW::Vector3 & ) const { throw std::runtime_error( "Terrain is outside the typed-space probe" ); }
    virtual const BW::Moo::DirectionalLightPtr & doGetSunLight() const { throw std::runtime_error( "Lighting is outside the typed-space probe" ); }
    virtual const BW::Moo::Colour & doGetAmbientLight() const { throw std::runtime_error( "Lighting is outside the typed-space probe" ); }
    virtual const BW::Moo::LightContainerPtr & doGetLights() const { throw std::runtime_error( "Lighting is outside the typed-space probe" ); }
    virtual bool doGetLightsInArea( const BW::AABB &, BW::Moo::LightContainerPtr * )
        { throw std::runtime_error( "Lighting is outside the typed-space probe" ); }
};
}

namespace
{
void groupOrderFixture(const char* inputPath,const char* outputPath)
{
    std::ifstream input(inputPath,std::ios::binary);
    std::ofstream output(outputPath,std::ios::binary);
    auto word=[&](){std::uint32_t value=0;input.read(reinterpret_cast<char*>(&value),4);
        require(input.good(),"Truncated group-order fixture");return value;};
    auto write=[&](std::uint32_t value){output.write(reinterpret_cast<const char*>(&value),4);};
    BW::ClientSpacePtr space=new EmptyProbeSpace(11);
    BW::SpaceManager::instance().addSpace(space.get());
    auto owner=BW::acquireStockSpaceSounds(space.get());
    const auto cases=word();
    for(std::uint32_t c=0;c<cases;++c)
    {
        const auto count=word();
        for(std::uint32_t i=0;i<count;++i)
        {
            const auto length=word();BW::string name(length,'\0');
            if(length)input.read(&name[0],length);
            require(input.good(),"Truncated group name");
            auto definition=std::make_shared<BW::Definition>();
            definition->name=name;definition->events={"ambient_burn_static_small"};
            owner->add(definition,BW::Vector3());
        }
        write(static_cast<std::uint32_t>(owner->groups.size()));
        for(const auto& entry:owner->groups)
        {
            write(static_cast<std::uint32_t>(entry.first.size()));
            output.write(entry.first.data(),entry.first.size());
        }
        owner->clear();
    }
    require(output.good(),"Cannot write actual group order");
    space->clear();
}
}

int main(int argc,char** argv)
{
    BW_SYSTEMSTAGE_MAIN();
    BW::LogMsg::automatedTest(true);
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    if(argc!=2&&argc!=4)return 2;
    int result=1;
    HWND window=CreateWindowExW(0,L"STATIC",L"Static sound probe",0,0,0,1,1,NULL,NULL,GetModuleHandle(NULL),NULL);
    BW::PyFactoryMethodLink vectorFactory("Math","Vector3",&BW::PyVector<BW::Vector3>::s_type_);
    Py_NoSiteFlag=1;Py_Initialize();
    BW::ClientSpacePtr first,second;
    try
    {
        BW::vector<BW::string> paths;paths.push_back(argv[1]);
        require(BW::BWResource::init(paths),"BWResource init failed");
        // Full client jobs also construct PyIME and require the application
        // input owner. Exercise the actual relevant registration objects here.
        vectorFactory.init();
        require(PyType_Ready(&BW::PyDataSection::s_type_)==0,"Native DataSection type failed");
        BW::s_link_py_LSloadEventsDB.init();BW::s_link_py_LSstartAll.init();
        BW::s_link_py_stopAllLightSound.init();BW::s_link_py_WG_lightSoundRemove.init();
        python("import _WWISE, Math\n"
               "assert _WWISE.LSloadEventsDB([]) is False\n"
               "assert _WWISE.LSstartAll() is None\n"
               "assert _WWISE.stopAllLightSound() is False\n"
               "assert _WWISE.WG_lightSoundRemove(1,2,3) is False\n"
               "assert _WWISE.WG_lightSoundRemove(True,2.9,-1.5) is False\n"
               "assert _WWISE.WG_lightSoundRemove(0xffffffffL,2,3) is False\n"
               "for method,args in ((_WWISE.LSstartAll,(1,)),(_WWISE.stopAllLightSound,(1,)),"
               "(_WWISE.WG_lightSoundRemove,()),(_WWISE.WG_lightSoundRemove,(1,2)),"
               "(_WWISE.WG_lightSoundRemove,(1,2,3,4)),(_WWISE.WG_lightSoundRemove,(None,2,3))):\n"
               " try:method(*args)\n"
               " except TypeError:pass\n"
               " else:raise AssertionError('Invalid lightSound call shape')\n");
        BW::DataSectionPtr config=BW::BWResource::openSection("probe_audio.xml");
        require(config,"Probe sound configuration missing");
        config->writeBool("soundMgr/enabled",false);
        require(BW::initialiseStockAudio(config,window)&&!BW::stockAudioReady()&&BW::stockAudioApplicationAvailable(),
                "Configured disabled application owner differs");
        python("assert _WWISE.LSloadEventsDB(None) is True\nassert _WWISE.LSstartAll() is None\n");
        require(BW::database.names.empty(),"Disabled DB call performed a load");
        config->writeBool("soundMgr/enabled",true);
        require(BW::initialiseStockAudio(config,window)&&BW::stockAudioReady(),"Actual SDK initialization failed");
        python("rows=[['probe',Math.Vector3(1,2,3),('ambient_burn_static_small','ignored'),'hp'],"
               "['probe',Math.Vector3(9,8,7),('second',),'ignored'],[''],"
               "['empty_events',Math.Vector3(),('',''),'']]\n"
               "assert _WWISE.LSloadEventsDB(rows) is True\n"
               "for args in ((),(rows,rows)):\n"
               " try:_WWISE.LSloadEventsDB(*args)\n"
               " except TypeError:pass\n"
               " else:raise AssertionError('LSloadEventsDB count')\n");
        require(BW::database.names.size()==2&&BW::database.names[0]=="probe"&&BW::database.names[1]=="probe",
                "Duplicate ordered DB names differ");
        auto definition=BW::database.definitions.at("probe");
        require(definition->events.size()==2&&definition->events.front()=="ambient_burn_static_small"&&definition->offset.x==1,
                "First duplicate definition/event did not survive");
        // Original authored DB loaded through real ResMgr/DataSection and Math.
        BW::DataSectionPtr lightRoot=BW::BWResource::openSection("scripts/environment_effects.xml/lightSounds");
        require(lightRoot,"Original lightSounds XML missing");
        BW::PyObjectPtr nativeRoot(new BW::PyDataSection(lightRoot),BW::PyObjectPtr::STEAL_REFERENCE);
        require(PyObject_SetAttrString(PyImport_AddModule("__main__"),"_lightRoot",nativeRoot.get())==0,
                "Native DataSection publication failed");
        python("original=[]\n"
               "for key,row in _lightRoot.items():\n"
               " original.append([row.readString('modelName'),row.readVector3('offset'),row.readStrings('wwsound'),row.readString('hardPoint')])\n"
               "assert len(original)==452\n"
               "assert _WWISE.LSloadEventsDB(original) is True\n");
        require(BW::database.names.size()==452,"Original DB authored index count differs");
        first=new EmptyProbeSpace(9);second=new EmptyProbeSpace(10);
        BW::SpaceManager::instance().addSpace(first.get());BW::SpaceManager::instance().addSpace(second.get());
        auto owner=BW::acquireStockSpaceSounds(first.get());auto other=BW::acquireStockSpaceSounds(second.get());
        if(argc==4)groupOrderFixture(argv[2],argv[3]);
        // Exercise actual BinaryFormat, FNV StringTable and ILoader publication
        // using an untouched original space.bin, without a fabricated BWSS.
        {
            BW::CompiledSpace::BinaryFormat file;
            BW::CompiledSpace::StringTable strings;
            BW::CompiledSpace::StockSpaceSoundLoader loader;
            BW::Matrix identity;identity.setIdentity();
            require(file.open("original_space.bin")&&strings.read(file),"Original mapped space/StringTable failed");
            require(loader.loadFromSpace(first.get(),file,NULL,identity,strings)&&loader.bind(),"Actual WTau loader bind failed");
            require(!owner->groups.empty()&&owner->mappings.size()==1,"WTau bind did not register actual instances");
            for(const auto& entry:owner->groups)require(!entry.second->voice->playing,"WTau bind played a voice early");
            bool found=false;
            const auto data=owner->mappings.front().data;
            BW::DeprecatedSpaceHelpers::cameraSpace(first);
            for(const auto& chunk:data->chunks)
            {
                for(BW::uint32 item=0;item<chunk.count;++item)
                {
                    BW::uint32 packed=0;if(!data->lookup(chunk.id,item,packed))continue;
                    BW::PyObjectPtr args(Py_BuildValue("(III)",999u,chunk.id,item),BW::PyObjectPtr::STEAL_REFERENCE);
                    BW::PyObjectPtr answer(BW::py_WG_lightSoundRemove(args.get()),BW::PyObjectPtr::STEAL_REFERENCE);
                    require(answer.get()==Py_True,"Actual WGDE current-space lookup failed");
                    found=true;break;
                }
                if(found)break;
            }
            require(found,"Original WGDE fixture contains no native item");
            loader.unload();loader.unload();
            require(owner->groups.empty()&&owner->mappings.empty(),"WTau unload retained groups or mapping identities");
            BW::DeprecatedSpaceHelpers::cameraSpace(NULL);strings.close();file.close();
        }
        BW::Matrix identity;identity.setIdentity();
        require(BW::addStockSpaceNamedSound(owner,"not-an-authored-sound",identity)==BW::INVALID_STOCK_SPACE_SOUND,
                "Generic instance dispatch fabricated a direct-event fallback");
        require(BW::addStockSpaceModelSound(owner,24,identity)!=BW::INVALID_STOCK_SPACE_SOUND,
                "Original BSMO DB.names index did not register");
        require(BW::addStockSpaceNamedSound(owner,BW::database.names[0],identity)!=BW::INVALID_STOCK_SPACE_SOUND,
                "Original particle resource-name dispatch did not register");
        require(BW::addStockSpaceNamedSound(owner,BW::database.names[30],identity)!=BW::INVALID_STOCK_SPACE_SOUND,
                "Original tree resource-name dispatch did not register");
        owner->clear();
        auto direct=BW::database.event("ambient_burn_static_small");
        std::vector<BW::StockSpaceSoundHandle> handles;
        for(unsigned i=0;i<201;++i)handles.push_back(owner->add(direct,BW::Vector3(float(i),2,3)));
        auto group=owner->groups.begin()->second;
        auto source=BW::stockAudioVoiceSource(group->voice);
        require(group->positions.size()==200&&!group->voice->playing,"Bind played audio or bypassed native cap");
        require(AK::SoundEngine::SetGameObjectOutputBusVolume(source->id,BW::StockAudioDetail::manager().listener,0.f)==AK_Success,
                "Silent probe bus gain failed");
        BW::DeprecatedSpaceHelpers::cameraSpace(first);
        python("assert _WWISE.LSstartAll() is None\n");
        require(group->voice->playing&&source->multiplePositionsSet&&source->multiplePositions.size()==200,
                "Actual grouped SDK PostEvent/multiple positions failed");
        acknowledged([&](){return AK::SoundEngine::Query::GetEventIDFromPlayingID(group->voice->playingID)!=AK_INVALID_UNIQUE_ID;});
        // Original 124ecb0 admits a voice once while its started flag is set.
        const AkPlayingID startedID=group->voice->playingID;
        python("assert _WWISE.LSstartAll() is None\n");
        require(group->voice->playingID==startedID,"Repeated LSstartAll posted a second event");
        acknowledged([&](){AkUInt32 count=0;return AK::SoundEngine::Query::GetPlayingIDsFromGameObject(source->id,count,NULL)==AK_Success&&count==1;});
        BW::DeprecatedSpaceHelpers::cameraSpace(NULL);
        acknowledged([&](){AkSoundPosition p;return AK::SoundEngine::Query::GetPosition(source->id,p)==AK_Success&&
            p.Position().X==0.f&&p.Position().Y==2.f&&p.Position().Z==3.f&&p.OrientationFront().X==1.f;});
        owner->remove(handles.front());
        require(source->multiplePositions.size()==199&&source->multiplePositions.front().Position().X==1.f,"Position removal differs");
        BW::stopStockAudioVoice(group->voice);
        acknowledged([&](){AkUInt32 count=0;return AK::SoundEngine::Query::GetPlayingIDsFromGameObject(source->id,count,NULL)==AK_Success&&count==0;});
        require(BW::StockAudioDetail::reinitialise(),"Actual SDK device recreation failed");
        require(source->multiplePositionsSet&&source->multiplePositions.size()==199&&!group->voice->playing,
                "Reinitialization lost positions or restarted explicit stop");
        acknowledged([&](){AkSoundPosition p;return AK::SoundEngine::Query::GetPosition(source->id,p)==AK_Success&&p.Position().X==1.f;});
        other->add(direct,BW::Vector3());
        BW::DeprecatedSpaceHelpers::cameraSpace(first);
        python("assert _WWISE.stopAllLightSound() is True\n");
        require(owner->groups.empty()&&other->groups.size()==1,"Current-only preclear affected another space");
        // Failed conversion is terminal and does not fabricate a DB or voice.
        python("for value in (None,{},[()],[[1]],[[u'name',Math.Vector3(),('event',),'']],"
               "[['name',(1,2,3),('event',),'']],[['name',Math.Vector3(),['event'],'']],"
               "[['name',Math.Vector3(),(1,),'']],[['name',Math.Vector3(),('event',),None]]):\n"
               " try:_WWISE.LSloadEventsDB(value)\n"
               " except RuntimeError:pass\n"
               " else:raise AssertionError('Invalid native sound DB accepted')\n");
        first->clear();require(owner->retired&&owner->groups.empty(),"Native ClientSpace clear retained sound owner");
        second->clear();require(other->retired,"Second native ClientSpace did not retire");
        BW::DeprecatedSpaceHelpers::cameraSpace(NULL);first=NULL;second=NULL;
        std::cout<<"Actual CPython/Math DB, grouped 200 positions, silent SDK PostEvent, query acknowledgement, remove, reinit and native space retirement PASS"<<std::endl;
        result=0;
    }
    catch(const std::exception& error)
    {
        if(PyErr_Occurred())PyErr_Print();
        std::cerr<<"Static sound probe failed: "<<error.what()<<std::endl;
    }
    BW::DeprecatedSpaceHelpers::cameraSpace(NULL);
    if(first&&!first->cleared())first->clear();if(second&&!second->cleared())second->clear();
    first=NULL;second=NULL;
    BW::finiStockSpaceSounds();BW::finiStockAudio();
    vectorFactory.fini();
    Py_Finalize();BW::BWResource::fini();BW::LogMsg::fini();
    if(window)DestroyWindow(window);
    return result;
}
