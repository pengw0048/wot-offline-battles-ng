#ifndef WOT_STOCK_SOUND_ZONES_PORTABLE
#include "pch.hpp"
#include "stock_sound_zones.hpp"
#include "stock_audio.hpp"
#include "cstdmf/timestamp.hpp"
#include "pyscript/script_math.hpp"
#include "resmgr/bwresource.hpp"
#include "space/space_manager.hpp"
#endif
#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

BW_BEGIN_NAMESPACE
int StockSoundZones_token = 1;
namespace
{
struct Zone
{
    Vector2 centre, halfSize;
    unsigned enter, exit, reverb;
    float sine, cosine, radiusSquared, gain;
    bool contains(const Vector3& camera) const
    {
        float x = centre.x - camera.x, z = centre.y - camera.z;
        if (!(radiusSquared >= x * x + z * z)) return false;
        // 8a3769 tests sine > 1e-5, not its absolute value. Keep the
        // original negative-angle and circular early-rejection semantics.
        if (sine > .00001f)
        {
            const float localX = cosine * x - sine * z;
            z = sine * x + cosine * z;
            x = localX;
        }
        return x >= -halfSize.x && x <= halfSize.x &&
               z >= -halfSize.y && z <= halfSize.y;
    }
};
struct Zones
{
    std::vector<Zone> entries;
    size_t current = (std::numeric_limits<size_t>::max)();
    double next = 0.;
    bool registered = false;
    unsigned defaultBus = 0;
    float defaultGain = 0.f;
    void clear()
    {
        // 8a3c70 cancels the native timer and deletes the complete registry.
        // It deliberately emits neither an exit event nor an aux reset.
        entries.clear(); current = (std::numeric_limits<size_t>::max)(); next = 0.;
    }
    void spaceDestroyed(SpaceManager*, ClientSpace*) { clear(); }
    void connect()
    {
        if (registered) return;
        SpaceManager::instance().spaceDestroyed().add<Zones, &Zones::spaceDestroyed>(this);
        registered = true;
    }
};
Zones& zones() { static Zones instance; return instance; }

void addSoundZoneTrigger(Vector2 centre, Vector2 size, float angle,
    BW::string enter, BW::string exit, BW::string reverb, float gain)
{
    auto& owner = zones();
    owner.connect();
    Zone zone;
    zone.centre = centre;
    zone.halfSize = Vector2(size.x * .5f, size.y * .5f);
    zone.enter = stockSoundZoneNameID(enter);
    zone.exit = stockSoundZoneNameID(exit);
    zone.reverb = stockSoundZoneNameID(reverb);
    zone.sine = sinf(angle); zone.cosine = cosf(angle);
    const float radius = (size.x > size.y ? size.x : size.y) * .5f;
    zone.radiusSquared = radius * radius; zone.gain = gain;
    if (owner.entries.empty()) owner.next = double(timestamp()) / stampsPerSecondD() + double(.1f);
    owner.entries.push_back(zone);
}
PY_AUTO_MODULE_FUNCTION(RETVOID, addSoundZoneTrigger,
    ARG(Vector2, ARG(Vector2, ARG(float, ARG(BW::string,
        ARG(BW::string, ARG(BW::string, ARG(float, END))))))), BigWorld)
}

void tickStockSoundZones(const Matrix& cameraWorld, double now)
{
    auto& owner = zones();
    if (owner.entries.empty() || now < owner.next) return;
    // The original callback returns float(.1) widened to double; missed
    // deadlines run once at the current camera, with no historical replay.
    owner.next = now + double(.1f);
    const Vector3 camera = cameraWorld.applyToOrigin();
    size_t selected = (std::numeric_limits<size_t>::max)();
    for (size_t i = 0; i != owner.entries.size(); ++i)
        if (owner.entries[i].contains(camera)) { selected = i; break; }
    if (selected == owner.current) return;
    if (owner.current < owner.entries.size()) postStockSoundZoneEvent(owner.entries[owner.current].exit);
    if (selected < owner.entries.size())
    {
        const auto& zone = owner.entries[selected];
        postStockSoundZoneEvent(zone.enter);
        setStockAudioAuxiliarySend(zone.reverb, zone.gain);
    }
    else setStockAudioAuxiliarySend(owner.defaultBus, owner.defaultGain);
    owner.current = selected;
}

void stockSoundZonesMappingLoaded(const BW::string& path)
{
    auto& owner = zones();
    // 1246a10 splits the native mapping path and selects component one.
    // It supplies scripts/arena_defs/<component>.xml to 1255a20. Missing
    // config or wwDefaultReverb resets both native defaults to zero.
    const size_t first = path.find('/');
    BW::string resource;
    if (first != BW::string::npos)
    {
        const size_t last = path.find('/', first + 1);
        resource = "scripts/arena_defs/" + path.substr(first + 1, last - first - 1) + ".xml";
    }
    DataSectionPtr config = resource.empty() ? NULL : BWResource::openSection(resource);
    DataSectionPtr reverb = config ? config->openSection("wwDefaultReverb") : NULL;
    owner.defaultBus = reverb ? stockSoundZoneNameID(reverb->readString("auxBus")) : 0;
    owner.defaultGain = reverb ? reverb->readFloat("controlValue", 0.f) : 0.f;
    // MappingEventInterface::onMappingLoaded (1247860) applies the default.
    setStockAudioAuxiliarySend(owner.defaultBus, owner.defaultGain);
}

void finiStockSoundZones()
{
    auto& owner = zones();
    owner.clear();
    if (owner.registered)
    {
        SpaceManager::instance().spaceDestroyed().remove<Zones, &Zones::spaceDestroyed>(&owner);
        owner.registered = false;
    }
    owner.defaultBus = 0; owner.defaultGain = 0.f;
}
BW_END_NAMESPACE
