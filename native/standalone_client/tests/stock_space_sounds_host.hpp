// Bounded voice SDK boundary; the included group/retirement owner is production.
// ClientSpace / ClientSpacePtr are supplied by the surrounding owner fixture.
#include <array>
#include <cstdint>
#include <cstring>
#include <memory>
#define WOT_STOCK_SPACE_SOUNDS_PORTABLE
namespace BW
{
using string=std::string;
using uint32=std::uint32_t;
struct Vector3
{
    float x=0,y=0,z=0;
    Vector3(){} Vector3(float a,float b,float c):x(a),y(b),z(c){}
    static Vector3 zero(){return {};}
    Vector3 operator+(const Vector3& other)const{return {x+other.x,y+other.y,z+other.z};}
};
struct Matrix
{
    float v[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    Vector3 applyToOrigin()const{return {v[12],v[13],v[14]};}
};
static void D3DXVec3TransformNormal(Vector3* out,const Vector3* p,const Matrix* m)
{
    *out={p->x*m->v[0]+p->y*m->v[4]+p->z*m->v[8],
          p->x*m->v[1]+p->y*m->v[5]+p->z*m->v[9],
          p->x*m->v[2]+p->y*m->v[6]+p->z*m->v[10]};
}
struct TestAudioSource {unsigned id;std::vector<Vector3> positions;};
struct TestAudioVoice
{
    string event,name;std::shared_ptr<TestAudioSource> source;
    unsigned starts=0,stops=0;bool playing=false;
};
using StockAudioSource=std::shared_ptr<TestAudioSource>;
using StockAudioVoice=std::shared_ptr<TestAudioVoice>;
using StockSpaceSoundHandle=std::uint64_t;
constexpr StockSpaceSoundHandle INVALID_STOCK_SPACE_SOUND=~StockSpaceSoundHandle(0);
struct StockSpaceSoundOwner;
using StockSpaceSounds=std::shared_ptr<StockSpaceSoundOwner>;
static bool audioReady=true,updateAccepted=true,creationAccepted=true;
static unsigned nextSource=0;
static std::vector<StockAudioVoice> audioVoices;
static std::function<void()> onAudioCreate,onAudioUpdate,onAudioPlay,onAudioStop;
static bool stockAudioReady(){return audioReady;}
static StockAudioVoice createStockAudioVoice(const string& event,const string& name)
{
    auto voice=std::make_shared<TestAudioVoice>();voice->event=event;voice->name=name;
    if(creationAccepted)voice->source=std::make_shared<TestAudioSource>(TestAudioSource{++nextSource,{}});
    audioVoices.push_back(voice);if(auto callback=onAudioCreate)callback();return voice;
}
static StockAudioSource stockAudioVoiceSource(const StockAudioVoice& voice)
{return voice?voice->source:StockAudioSource();}
static bool setStockAudioSourcePositions(const StockAudioSource& source,const std::vector<Vector3>& positions)
{
    if(!audioReady||!source||!updateAccepted)return false;
    source->positions=positions;if(auto callback=onAudioUpdate)callback();return true;
}
static bool playStockAudioVoice(StockAudioVoice& voice)
{
    if(!audioReady||!voice||!voice->source||voice->playing)return false;
    voice->playing=true;++voice->starts;if(auto callback=onAudioPlay)callback();return true;
}
static void stopStockAudioVoice(const StockAudioVoice& voice)
{if(voice){voice->playing=false;++voice->stops;if(auto callback=onAudioStop)callback();}}
static void stopStockAudioSource(const StockAudioSource& source)
{if(source)for(const auto& voice:audioVoices)if(voice->source==source)voice->playing=false;}
}
#include WOT_SOUND_PRODUCTION_FILE
