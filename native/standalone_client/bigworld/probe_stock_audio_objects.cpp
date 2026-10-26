// Real CPython/Math and SDK owner probe. The volume audit forwards every
// production call to the linked Wwise SDK; it does not replace its result.
#if defined(_WIN32)
#undef NTDDI_VERSION
#define NTDDI_VERSION 0x0A000000
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#undef WINVER
#define WINVER 0x0A00
#endif
#include "pch.hpp"
#include <AK/SoundEngine/Common/AkSoundEngine.h>
#include <vector>
namespace AK { namespace SoundEngine {
struct AuditedVolume { AkGameObjectID source,listener;float value;AKRESULT result; };
std::vector<AuditedVolume> auditedVolumes;
AKRESULT auditedSetGameObjectOutputBusVolume(AkGameObjectID source,AkGameObjectID listener,float value)
{
    const AKRESULT result=SetGameObjectOutputBusVolume(source,listener,value);
    auditedVolumes.push_back({source,listener,value,result});
    return result;
}
} }
#define SetGameObjectOutputBusVolume auditedSetGameObjectOutputBusVolume
#include "stock_audio.cpp"
#undef SetGameObjectOutputBusVolume
#include "cstdmf/allocator.hpp"
#include "cstdmf/log_msg.hpp"
#include "resmgr/bwresource.hpp"
#include <AK/SoundEngine/Common/AkQueryParameters.h>
#include <functional>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace
{
void require(bool value,const char* detail) { if (!value) throw std::runtime_error(detail); }
void python(const char* code) { require(PyRun_SimpleString(code)==0,"Actual Python audio boundary failed"); }
void acknowledged(const std::function<bool()>& check)
{
    const ULONGLONG deadline=GetTickCount64()+5000;
    do {
        require(AK::SoundEngine::RenderAudio(true)==AK_Success,"SDK RenderAudio failed");
        if (check()) return;
        SwitchToThread();
    } while(GetTickCount64()<deadline);
    throw std::runtime_error("Actual SDK acknowledgement timed out");
}
BW::PyObjectPtr value(const char* name)
{
    return BW::PyObjectPtr(PyObject_GetAttrString(PyImport_AddModule("__main__"),name),BW::PyObjectPtr::STEAL_REFERENCE);
}
void publish(const char* name,PyObject* object)
{ require(PyObject_SetAttrString(PyImport_AddModule("__main__"),name,object)==0,"Cannot publish probe owner"); }
class ReentrantMatrix : public BW::MatrixProvider
{
public:
    ReentrantMatrix():MatrixProvider(false,&BW::MatrixProvider::s_type_) { pose.setIdentity(); }
    BW::Matrix pose;
    mutable std::function<void()> callback;
    virtual void matrix(BW::Matrix& output) const
    {
        output=pose;
        auto work=callback;
        callback={};
        if (work) work();
    }
};
void transforms(const char* path)
{
    std::ifstream input(path,std::ios::binary);
    unsigned count=0;input.read(reinterpret_cast<char*>(&count),4);
    require(input.good(),"Missing original provider-transform fixture");
    for (unsigned i=0;i<count;++i)
    {
        BW::Matrix matrix;BW::Vector3 offset;float expected[9];
        input.read(reinterpret_cast<char*>(&matrix),64);
        input.read(reinterpret_cast<char*>(&offset),12);
        input.read(reinterpret_cast<char*>(expected),36);
        require(input.good(),"Truncated original provider-transform fixture");
        const auto actual=BW::StockAudioDetail::providerTransform(matrix,offset);
        const float values[]={actual.OrientationFront().X,actual.OrientationFront().Y,actual.OrientationFront().Z,
            actual.OrientationTop().X,actual.OrientationTop().Y,actual.OrientationTop().Z,
            actual.Position().X,actual.Position().Y,actual.Position().Z};
        require(std::memcmp(values,expected,36)==0,"Original provider transform differs from production");
    }
    require(input.peek()==std::char_traits<char>::eof(),"Unexpected provider-transform tail");
}
}

int main(int argc,char** argv)
{
    BW_SYSTEMSTAGE_MAIN();
    BW::LogMsg::automatedTest(true);
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    if(argc!=3)return 2;
    HWND window=CreateWindowExW(0,L"STATIC",L"Audio object probe",0,0,0,1,1,NULL,NULL,GetModuleHandle(NULL),NULL);
    Py_NoSiteFlag=1;Py_Initialize();
    int result=1;
    try
    {
        BW::vector<BW::string> paths;paths.push_back(argv[1]);
        require(BW::BWResource::init(paths),"BWResource init failed");
        BW::PyFactoryMethodLink vectorFactory("Math","Vector3",&BW::PyVector<BW::Vector3>::s_type_);
        BW::PyFactoryMethodLink matrixFactory("Math","Matrix",&BW::PyMatrix::s_type_);
        vectorFactory.init();matrixFactory.init();
        BW::StockAudioDetail::objectTypeLink.init();
        BW::s_link_py_WW_getSoundObject.init();BW::s_link_py_WW_getSound.init();
        python("import _WWISE,Math\n"
            "invalidObject=_WWISE.WW_getSoundObject(None,None,Math.Vector3(),False)\n"
            "invalidSound=_WWISE.WW_getSound(None,None,None,Math.Vector3())\n"
            "assert isinstance(invalidObject,_WWISE.SoundObject)\n"
            "for instance in (invalidObject,invalidSound):\n"
            " assert instance.name=='' and type(instance.name) is str\n"
            " assert type(instance.volume) is float and instance.volume==1.0\n"
            " assert instance.matrixProvider is None\n"
            " assert instance.__members__==[] and instance.__members__ is not instance.__members__\n"
            " assert instance.__methods__==[] and instance.__methods__ is not instance.__methods__\n"
            " for attr in ('__members__','__methods__','name'):\n"
            "  for action in (setattr,delattr):\n"
            "   try:action(instance,attr,1) if action is setattr else action(instance,attr)\n"
            "   except TypeError:pass\n"
            "   else:raise AssertionError('Original read-only property accepted')\n"
            " for attr in ('volume','matrixProvider'):\n"
            "  try:delattr(instance,attr)\n"
            "  except TypeError:pass\n"
            "  else:raise AssertionError('Property deletion accepted')\n"
            " for bad in (None,'bad',(),object()):\n"
            "  try:instance.volume=bad\n"
            "  except TypeError:pass\n"
            "  else:raise AssertionError('Invalid volume accepted')\n"
            " for bad in (1,'bad',(),object()):\n"
            "  try:instance.matrixProvider=bad\n"
            "  except TypeError:pass\n"
            "  else:raise AssertionError('Invalid matrix provider accepted')\n"
            " assert instance.volume==1.0 and instance.matrixProvider is None\n"
            "for method in (invalidObject.stop,invalidObject.isPlaying):\n"
            " for text in ('', 'event', u'Chinese_\\u6811', 'embedded\\x00name'):\n"
            "  assert method(text) is None\n"
            " for args in ((),('a','b'),(None,),(1,),((),),(object(),)):\n"
            "  try:method(*args)\n"
            "  except TypeError:pass\n"
            "  else:raise AssertionError('Original one-string boundary accepted')\n");
        require(AK::SoundEngine::auditedVolumes.empty(),"Invalid handles submitted SDK volume writes");
        auto config=BW::BWResource::openSection("probe_audio.xml");
        require(config&&BW::initialiseStockAudio(config,window)&&BW::stockAudioReady(),"Actual SDK init failed");
        transforms(argv[2]);
        python("matrix=Math.Matrix()\n"
            "matrix.setTranslate((10,20,30))\n"
            "source=_WWISE.WW_getSoundObject('object_name',matrix,Math.Vector3(1,2,3),False)\n"
            "sound=_WWISE.WW_getSound('ambient_burn_static_small','sound_source',matrix,Math.Vector3(4,5,6))\n"
            "assert source.name=='object_name' and sound.name=='ambient_burn_static_small'\n"
            "assert source.matrixProvider is matrix and sound.matrixProvider is None\n");
        auto sourceObject=value("source"),soundObject=value("sound");
        const auto source=BW::StockAudioDetail::sourceOf(sourceObject.get(),false);
        const auto voice=*reinterpret_cast<BW::StockAudioDetail::SoundWrapper*>(soundObject.get())->voice;
        const auto soundSource=voice->source;
        for (const auto& pair:std::vector<std::pair<PyObject*,BW::StockAudioSource>>{
            {sourceObject.get(),source},{soundObject.get(),soundSource}})
        {
            for (const float volume:std::vector<float>{0.f,0.25f,1.f,2.f})
            {
                const auto before=AK::SoundEngine::auditedVolumes.size();
                BW::PyObjectPtr number(PyFloat_FromDouble(volume),BW::PyObjectPtr::STEAL_REFERENCE);
                require(PyObject_SetAttrString(pair.first,"volume",number.get())==0,"Volume setter conversion failed");
                require(AK::SoundEngine::auditedVolumes.size()==before+1,"Volume setter skipped actual SDK");
                const auto& write=AK::SoundEngine::auditedVolumes.back();
                require(write.source==pair.second->id&&write.listener==BW::StockAudioDetail::manager().listener&&
                    write.value==volume&&write.result==AK_Success,"Volume setter changed the original SDK arguments");
                BW::PyObjectPtr read(PyObject_GetAttrString(pair.first,"volume"),BW::PyObjectPtr::STEAL_REFERENCE);
                require(read&&PyFloat_Check(read.get())&&PyFloat_AsDouble(read.get())==1.0,"Volume getter fabricated cached state");
            }
        }
        // No audible output: the real source that will play is explicitly muted.
        python("sound.volume=0.0\nsource.volume=0.0\n"
               "assert source.play('ambient_burn_static_small') is True\n"
               "assert sound.play() is True\nassert sound.isPlaying is True\n"
               "assert source.isPlaying('ambient_burn_static_small') is None\n"
               "assert source.stop('ambient_burn_static_small') is None\n");
        acknowledged([&](){return AK::SoundEngine::Query::GetEventIDFromPlayingID(voice->playingID)!=AK_INVALID_UNIQUE_ID;});
        acknowledged([&](){AkUInt32 count=0;return AK::SoundEngine::Query::GetPlayingIDsFromGameObject(source->id,count,NULL)==AK_Success&&count==1;});
        const AkPlayingID playing=voice->playingID;
        python("assert sound.isPlaying is True\n");
        require(voice->playingID==playing,"Conversion-only object methods altered a voice");
        python("matrix2=Math.Matrix()\nmatrix2.setTranslate((40,50,60))\n"
               "source.matrixProvider=matrix2\nsound.matrixProvider=matrix2\n"
               "assert source.matrixProvider is matrix2 and sound.matrixProvider is None\n");
        acknowledged([&](){AkSoundPosition a,b;return AK::SoundEngine::Query::GetPosition(source->id,a)==AK_Success&&
            AK::SoundEngine::Query::GetPosition(soundSource->id,b)==AK_Success&&a.Position().X==41.f&&
            b.Position().X==44.f&&a.OrientationTop().X==1.f&&b.OrientationTop().X==1.f;});
        python("matrix2.setTranslate((70,80,90))\nsource.matrixProvider=None\nsound.matrixProvider=None\n");
        acknowledged([&](){AkSoundPosition p;return AK::SoundEngine::Query::GetPosition(soundSource->id,p)==AK_Success&&p.Position().X==74.f;});
        require(!source->matrix&&!soundSource->matrix,"Detaching retained provider ownership");
        python("sound.matrixProvider=matrix\nmatrix.setTranslate((90,100,110))\n"
               "assert sound.releaseMatrix() is None\n");
        require(!soundSource->matrix,"releaseMatrix retained its provider");
        acknowledged([&](){AkSoundPosition p;return AK::SoundEngine::Query::GetPosition(soundSource->id,p)==AK_Success&&p.Position().X==94.f;});
        // Production source setter and tick must not overwrite callback-created
        // bindings after sampling a provider that synchronously re-enters.
        BW::SmartPointer<ReentrantMatrix> old=new ReentrantMatrix,replacement=new ReentrantMatrix;
        old->pose.setTranslate(BW::Vector3(100,0,0));replacement->pose.setTranslate(BW::Vector3(200,0,0));
        old->callback=[&](){require(BW::setStockAudioSourceMatrix(source,replacement),"Reentrant replacement failed");};
        require(!BW::setStockAudioSourceMatrix(source,old),"Stale outer setter published a pose");
        require(source->matrix.get()==replacement.get()&&source->pose.applyToOrigin().x==200.f,"Outer setter replaced the new owner");
        require(BW::setStockAudioSourceMatrix(source,old),"Tick fixture setup failed");
        old->callback=[&](){require(BW::setStockAudioSourceMatrix(source,replacement),"Tick replacement failed");};
        BW::StockAudioDetail::updateSources();
        require(source->matrix.get()==replacement.get()&&source->pose.applyToOrigin().x==200.f,"Old tick overwrote reentrant provider");
        python("factory=_WWISE.WW_getSoundObject('factory_attachment',matrix,Math.Vector3(),False)\n"
               "factory.volume=0.0\nassert factory.play('ambient_burn_static_small') is True\n"
               "matrix.setTranslate((123,234,345))\n");
        auto factoryObject=value("factory");
        BW::StockAudioSource factorySource=BW::StockAudioDetail::sourceOf(factoryObject.get(),false);
        factoryObject=NULL;
        python("del factory\n");
        require(!factorySource->matrix,"Factory object destruction retained a voice's provider");
        acknowledged([&](){AkSoundPosition p;AkUInt32 count=0;
            return AK::SoundEngine::Query::GetPosition(factorySource->id,p)==AK_Success&&p.Position().X==123.f&&
                AK::SoundEngine::Query::GetPlayingIDsFromGameObject(factorySource->id,count,NULL)==AK_Success&&count==1;});
        auto matrixObject=value("matrix");BW::MatrixProviderPtr borrowedProvider;
        require(BW::Script::setData(matrixObject.get(),borrowedProvider)==0,"Real matrix converter failed");
        require(BW::setStockAudioSourceMatrix(factorySource,borrowedProvider),"Borrowed view setup failed");
        BW::PyObjectPtr borrowed(BW::wrapStockAudioSource(factorySource),BW::PyObjectPtr::STEAL_REFERENCE);
        require(borrowed,"Borrowed source wrapper failed");publish("borrowed",borrowed.get());borrowed=NULL;
        python("del borrowed\n");
        require(factorySource->matrix==borrowedProvider,"Borrowed source view detached the actual owner");
        BW::stopStockAudioSource(factorySource);
        python("ephemeral=_WWISE.WW_getSoundObject('ephemeral',None,Math.Vector3(),False)\n");
        auto ephemeral=value("ephemeral");
        const AkGameObjectID ephemeralID=BW::StockAudioDetail::sourceOf(ephemeral.get(),false)->id;
        acknowledged([&](){AkSoundPosition p;return AK::SoundEngine::Query::GetPosition(ephemeralID,p)==AK_Success;});
        ephemeral=NULL;python("del ephemeral\n");
        acknowledged([&](){AkSoundPosition p;return AK::SoundEngine::Query::GetPosition(ephemeralID,p)!=AK_Success;});
        python("dyingSound=_WWISE.WW_getSound('ambient_burn_static_small','dying_source',matrix,Math.Vector3())\n"
               "dyingSound.volume=0.0\nassert dyingSound.play() is True\n");
        auto dyingObject=value("dyingSound");
        const auto dyingVoice=*reinterpret_cast<BW::StockAudioDetail::SoundWrapper*>(dyingObject.get())->voice;
        const auto dyingSource=dyingVoice->source;
        acknowledged([&](){AkUInt32 count=0;return AK::SoundEngine::Query::GetPlayingIDsFromGameObject(dyingSource->id,count,NULL)==AK_Success&&count==1;});
        dyingObject=NULL;python("del dyingSound\n");
        require(!dyingVoice->playing&&!dyingVoice->notify,"Sound destruction left its native voice active");
        acknowledged([&](){AkUInt32 count=0;return AK::SoundEngine::Query::GetPlayingIDsFromGameObject(dyingSource->id,count,NULL)==AK_Success&&count==0;});
        python("sound.stop()\nsource.stopAll()\nassert sound.isPlaying is False\n");
        acknowledged([&](){AkUInt32 count=0;return AK::SoundEngine::Query::GetPlayingIDsFromGameObject(soundSource->id,count,NULL)==AK_Success&&count==0;});
        const auto volumeWrites=AK::SoundEngine::auditedVolumes.size();
        const auto epoch=source->epoch;
        old->callback=[&](){require(BW::StockAudioDetail::reinitialise(),"Actual SDK recreation failed");};
        require(!BW::setStockAudioSourceMatrix(source,old),"Old setter crossed a recreated SDK epoch");
        require(source->epoch!=epoch&&source->pose.applyToOrigin().x==200.f,
            "Old sample overwrote the recreated source pose");
        require(AK::SoundEngine::auditedVolumes.size()==volumeWrites,"Reinit invented a native volume cache");
        require(source->matrix.get()==old.get()&&!voice->playing,"Reinit lost provider or replayed explicit stop");
        BW::finiStockAudio();
        python("assert source.name=='' and sound.name==''\n"
               "assert source.matrixProvider is None and sound.matrixProvider is None\n"
               "assert source.volume==1.0 and sound.volume==1.0\n"
               "assert sound.isPlaying is False\n"
               "del source,sound,matrix,matrix2,invalidObject,invalidSound\n");
        sourceObject=NULL;soundObject=NULL;
        std::cout<<"Actual CPython audio members, SDK volume forwarding, immediate provider/detach, reentry, stop and reinit PASS"<<std::endl;
        result=0;
    }
    catch (const std::exception& error) { std::cerr<<error.what()<<std::endl; }
    BW::finiStockAudio();
    if(PyErr_Occurred())PyErr_Print();
    Py_Finalize();
    if(window)DestroyWindow(window);
    return result;
}
