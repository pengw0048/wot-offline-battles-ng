#ifndef WOT_STOCK_SPACE_SOUNDS_PORTABLE
#include "pch.hpp"
#include "stock_space_sounds.hpp"
#include "stock_audio.hpp"
#include "stock_audio_source.hpp"
#include "compiled_space/binary_format.hpp"
#include "compiled_space/string_table.hpp"
#include "space/deprecated_space_helpers.hpp"
#include "pyscript/script_math.hpp"
#endif
#include "stock_space_sounds_data.hpp"
#include <algorithm>
#include <cfloat>
#include <map>
#include <unordered_map>
#include <vector>

BW_BEGIN_NAMESPACE
int StockSpaceSounds_token = 1;
namespace
{
// a669e0 hashes the raw object-name bytes with the original x86 FNV-1a.
// The supported target's MSVC hash-list also supplies LSstartAll iteration.
struct SoundNameHash
{
    std::size_t operator()(const BW::string& name) const
    {
        std::uint32_t hash=0x811c9dc5u;
        for (const unsigned char byte:name) hash=(hash^byte)*0x1000193u;
        return hash;
    }
};
struct Definition
{
    BW::string name, hardPoint;
    Vector3 offset = Vector3::zero();
    std::vector<BW::string> events;
};
struct Database
{
    std::vector<BW::string> names;
    std::map<BW::string,std::shared_ptr<Definition>> definitions;
    std::shared_ptr<Definition> event(const BW::string& name)
    {
        auto found = definitions.find(name);
        if (found != definitions.end()) return found->second;
        // 125a510 creates this exact direct-event definition on a cache miss.
        auto value = std::make_shared<Definition>();
        value->name = "static_" + name;
        value->events.push_back(name);
        definitions[name] = value;
        return value;
    }
};
Database database;
std::map<ClientSpace*,std::weak_ptr<StockSpaceSoundOwner>> owners;
bool fail(const char* message)
{
    ERROR_MSG("StaticSceneSoundFeature: %s\n",message);
    return false;
}
}

struct StockSpaceSoundOwner
{
    struct Position { StockSpaceSoundHandle handle; Vector3 position; };
    struct Group
    {
        std::shared_ptr<Definition> definition;
        StockAudioVoice voice;
        std::vector<Position> positions;
        bool retired = false;
    };
    struct Mapping
    {
        const void* identity;
        std::shared_ptr<const wot::StockSpaceSoundIdentities> data;
        std::map<std::uint32_t,StockSpaceSoundHandle> handles;
    };
    std::vector<Mapping> mappings;
    Mapping& mapping(const void* identity)
    {
        for (auto& value:mappings) if (value.identity==identity) return value;
        mappings.push_back({identity,{},{}});return mappings.back();
    }
    ClientSpace* space;
    struct Groups : std::unordered_map<BW::string,std::shared_ptr<Group>,SoundNameHash>
    {
        Groups() { this->max_load_factor(FLT_MAX);this->rehash(8); }
    };
    Groups groups;
    std::uint32_t groupBuckets=8;
    StockSpaceSoundHandle next = 0;
    bool retired = false;
    explicit StockSpaceSoundOwner(ClientSpace* value):space(value) {}
    ~StockSpaceSoundOwner() { retired=true;space=NULL;clear(); }
    bool usable() const { return !retired && space && !space->cleared(); }
    static bool update(const std::shared_ptr<Group>& group)
    {
        std::vector<Vector3> values;
        values.reserve(group->positions.size());
        for (const auto& position:group->positions) values.push_back(position.position);
        return setStockAudioSourcePositions(stockAudioVoiceSource(group->voice),values);
    }
    static void stop(const std::shared_ptr<Group>& group)
    {
        // Keep the source alive through both operations, including synchronous
        // observers. Retired groups are detached before any backend call.
        const auto voice=group->voice;
        const auto source=stockAudioVoiceSource(voice);
        stopStockAudioVoice(voice);
        stopStockAudioSource(source);
    }
    StockSpaceSoundHandle add(const std::shared_ptr<Definition>& definition,const Vector3& position)
    {
        if (!usable() || !stockAudioReady() || !definition || definition->events.empty() ||
            next == INVALID_STOCK_SPACE_SOUND) return INVALID_STOCK_SPACE_SOUND;
        // 124a700 consumes the identity even when 124a450's group is full.
        const auto handle=next++;
        auto found=groups.find(definition->name);
        std::shared_ptr<Group> group;
        if (found==groups.end())
        {
            group=std::make_shared<Group>();group->definition=definition;
            groups.emplace(definition->name,group);
            // The original inserts the node into its current bucket before
            // checking load and rehashing (12497d0). v142 otherwise rehashes
            // first, changing the exact LSstartAll order at 9/65/513 groups.
            if (static_cast<float>(groups.size())/static_cast<float>(groupBuckets)>1.f)
            {
                if (groupBuckets<512) groupBuckets*=8;
                else if (groupBuckets<0x1fffffffu) groupBuckets*=2;
                groups.rehash(groupBuckets);
            }
            // 124a450 creates, but does not play, the first ordered event.
            // Preserve its positions even if the actual SDK rejects the voice.
            group->voice=createStockAudioVoice(definition->events.front(),definition->name);
            if (group->retired || !usable()) { stop(group);return handle; }
        }
        else group=found->second;
        if (group->positions.size()<200) group->positions.push_back({handle,position});
        return handle;
    }
    void remove(StockSpaceSoundHandle handle)
    {
        for (auto& value:mappings)
            for (auto it=value.handles.begin();it!=value.handles.end();)
                if (it->second==handle) it=value.handles.erase(it);else ++it;
        for (auto it=groups.begin();it!=groups.end();++it)
        {
            const auto group=it->second;
            auto found=std::find_if(group->positions.begin(),group->positions.end(),
                [handle](const Position& position){return position.handle==handle;});
            if (found==group->positions.end()) continue;
            group->positions.erase(found);
            if (!group->positions.empty()) { update(group);return; }
            group->retired=true;groups.erase(it);
            stop(group);return;
        }
    }
    void start()
    {
        if (!usable() || !stockAudioReady()) return;
        std::vector<std::shared_ptr<Group>> snapshot;
        for (const auto& entry:groups) snapshot.push_back(entry.second);
        for (const auto& group:snapshot)
        {
            if (group->retired || !usable()) continue;
            if (update(group) && !group->retired && usable())
            {
                playStockAudioVoice(group->voice);
                if (group->retired || !usable()) stop(group);
            }
        }
    }
    void clear()
    {
        Groups old;
        old.swap(groups);groupBuckets=8;
        for (auto& value:mappings) value.handles.clear();
        // Internal handles remain unique across preclear so a delayed mapping
        // removal cannot address a replacement group. Original generic scene
        // IDs supply that identity boundary around its resetting counter.
        for (const auto& entry:old) entry.second->retired=true;
        for (const auto& entry:old) stop(entry.second);
    }
};

StockSpaceSounds acquireStockSpaceSounds(ClientSpace* space)
{
    if (!space || space->cleared()) return {};
    auto current=owners[space].lock();
    if (current) return current;
    current=std::make_shared<StockSpaceSoundOwner>(space);
    owners[space]=current;
    return current;
}
StockSpaceSoundHandle addStockSpaceModelSound(const StockSpaceSounds& owner,
    uint32 index,const Matrix& world)
{
    if (!owner || !owner->usable() || index>=database.names.size())
        return INVALID_STOCK_SPACE_SOUND;
    const auto found=database.definitions.find(database.names[index]);
    if (found==database.definitions.end()) return INVALID_STOCK_SPACE_SOUND;
    Vector3 offset;
    D3DXVec3TransformNormal(&offset,&found->second->offset,&world);
    return owner->add(found->second,offset+world.applyToOrigin());
}
StockSpaceSoundHandle addStockSpaceNamedSound(const StockSpaceSounds& owner,
    const BW::string& name,const Matrix& world)
{
    if (!owner || !owner->usable()) return INVALID_STOCK_SPACE_SOUND;
    const auto found=database.definitions.find(name);
    // Generic model/tree/particle dispatch (124a240) uses lookup-only; the
    // direct-event fallback belongs to WTau/WTCP's 124a700 path.
    if (found==database.definitions.end()) return INVALID_STOCK_SPACE_SOUND;
    Vector3 offset;
    D3DXVec3TransformNormal(&offset,&found->second->offset,&world);
    return owner->add(found->second,offset+world.applyToOrigin());
}
StockSpaceSoundHandle addStockSpaceControlPointSound(const StockSpaceSounds& owner,
    const BW::string& event, const Vector3& position)
{
    if (!owner || !owner->usable() || event.empty()) return INVALID_STOCK_SPACE_SOUND;
    return owner->add(database.event(event),position);
}

void removeStockSpaceSound(const StockSpaceSounds& owner,StockSpaceSoundHandle handle)
{
    if (owner && handle!=INVALID_STOCK_SPACE_SOUND) owner->remove(handle);
}
void clearStockCurrentSpaceSounds(const ClientSpacePtr& expectedSpace)
{
    if (!expectedSpace || !stockAudioReady()) return;
    const auto found=owners.find(expectedSpace.get());
    if (found!=owners.end()) if (auto owner=found->second.lock()) owner->clear();
}

void registerStockSpaceObjectSound(const StockSpaceSounds& owner,const void* mapping,
    bool tree,uint32 index,StockSpaceSoundHandle handle)
{
    if (!owner || !owner->usable() || index>0x7fffffffu ||
        handle==INVALID_STOCK_SPACE_SOUND) return;
    owner->mapping(mapping).handles[index|(tree?0x80000000u:0u)]=handle;
}
bool removeStockSpaceObjectSound(const StockSpaceSounds& owner,uint32 chunk,uint32 item)
{
    if (!owner || !owner->usable()) return false;
    for (const auto& mapping:owner->mappings)
    {
        if (!mapping.data) continue;
        uint32 object=0;
        const auto& chunks=mapping.data->chunks;
        if (std::find_if(chunks.begin(),chunks.end(),[chunk](const wot::StockSpaceSoundIdentities::Chunk& value)
            { return value.id==chunk; })==chunks.end()) continue;
        if (!mapping.data->lookup(chunk,item,object)) return false;
        const auto found=mapping.handles.find(object);
        if (found!=mapping.handles.end())
        {
            const auto handle=found->second;
            if (stockAudioReady()) owner->remove(handle);
        }
        // 125a310 returns lookup success even if no sound was registered.
        return true;
    }
    return false;
}
void removeStockSpaceSoundMapping(const StockSpaceSounds& owner,const void* identity)
{
    if (!owner) return;
    for (auto it=owner->mappings.begin();it!=owner->mappings.end();++it)
    {
        if (it->identity!=identity) continue;
        std::vector<StockSpaceSoundHandle> handles;
        for (const auto& value:it->handles) handles.push_back(value.second);
        owner->mappings.erase(it);
        for (const auto handle:handles) owner->remove(handle);
        return;
    }
}

void retireStockSpaceSounds(ClientSpace* space)
{
    const auto found=owners.find(space);
    if (found==owners.end()) return;
    auto owner=found->second.lock();
    owners.erase(found);
    if (owner) { owner->retired=true;owner->space=NULL;owner->clear(); }
}
void finiStockSpaceSounds()
{
    std::map<ClientSpace*,std::weak_ptr<StockSpaceSoundOwner>> old;
    old.swap(owners);
    std::vector<StockSpaceSounds> snapshot;
    for (const auto& entry:old)
        if (auto owner=entry.second.lock())
        { owner->retired=true;owner->space=NULL;snapshot.push_back(owner); }
    database=Database();
    for (const auto& owner:snapshot) owner->clear();
}
#ifndef WOT_STOCK_SPACE_SOUNDS_PORTABLE

namespace
{
PyObject* invalidSoundDatabase(const char* detail)
{
    // 125a780 sets PyExc_RuntimeError on malformed data. Return NULL at the
    // C boundary so the exception cannot escape alongside a non-NULL bool.
    PyErr_Format(PyExc_RuntimeError,"StaticSceneSoundDB::loadEventsDB: %s",detail);
    return NULL;
}
PyObject* py_LSloadEventsDB(PyObject* args)
{
    PyObject* data;
    if (!PyArg_ParseTuple(args,"O:LSloadEventsDB",&data)) return NULL;
    if (!stockAudioReady())
    {
        // 125ac2e returns true without reading the DB when the configured
        // AudioApp exists but is disabled; only an absent owner returns false.
        if (stockAudioApplicationAvailable()) Py_RETURN_TRUE;
        Py_RETURN_FALSE;
    }
    // 125a780 clears the DB before checking the original list schema.
    database=Database();
    if (!PyList_Check(data))
    {
        return invalidSoundDatabase("lightSound DB invalid format");
    }
    for (Py_ssize_t i=0;i<PyList_GET_SIZE(data);++i)
    {
        PyObject* row=PyList_GET_ITEM(data,i);
        if (!PyList_Check(row) || PyList_GET_SIZE(row)<1 || !PyString_Check(PyList_GET_ITEM(row,0)))
            return invalidSoundDatabase("lightSound DB invalid item/model name");
        const char* name=PyString_AsString(PyList_GET_ITEM(row,0));
        if (!*name) continue;
        if (PyList_GET_SIZE(row)<4) return invalidSoundDatabase("lightSound DB invalid item length");
        auto value=std::make_shared<Definition>();value->name=name;
        if (!PyVector<Vector3>::Check(PyList_GET_ITEM(row,1)))
            return invalidSoundDatabase("lightSound DB invalid offset");
        if (Script::setData(PyList_GET_ITEM(row,1),value->offset)) return NULL;
        PyObject* events=PyList_GET_ITEM(row,2);
        if (!PyTuple_Check(events)) return invalidSoundDatabase("lightSound DB invalid events list");
        for (Py_ssize_t j=0;j<PyTuple_GET_SIZE(events);++j)
        {
            PyObject* event=PyTuple_GET_ITEM(events,j);
            if (!PyString_Check(event)) return invalidSoundDatabase("lightSound DB invalid event");
            const char* text=PyString_AsString(event);
            if (*text) value->events.push_back(text);
        }
        if (!PyString_Check(PyList_GET_ITEM(row,3)))
            return invalidSoundDatabase("lightSound DB invalid hardpoint");
        value->hardPoint=PyString_AsString(PyList_GET_ITEM(row,3));
        if (value->events.empty()) continue;
        // Preserve authored order: compiled model sound indices address this
        // vector directly, while duplicate map keys retain the first record.
        database.names.push_back(value->name);
        database.definitions.emplace(value->name,value);
    }
    Py_RETURN_TRUE;
}
PY_MODULE_FUNCTION(LSloadEventsDB,_WWISE)
PyObject* py_LSstartAll(PyObject* args)
{
    if (!PyArg_ParseTuple(args,":LSstartAll")) return NULL;
    ClientSpacePtr space=DeprecatedSpaceHelpers::cameraSpace();
    if (space && stockAudioReady())
    {
        const auto found=owners.find(space.get());
        if (found!=owners.end()) if (auto owner=found->second.lock()) owner->start();
    }
    Py_RETURN_NONE;
}
PY_MODULE_FUNCTION(LSstartAll,_WWISE)
PyObject* py_stopAllLightSound(PyObject* args)
{
    if (!PyArg_ParseTuple(args,":stopAllLightSound")) return NULL;
    const ClientSpacePtr space=DeprecatedSpaceHelpers::cameraSpace();
    if (!space) Py_RETURN_FALSE;
    clearStockCurrentSpaceSounds(space);
    Py_RETURN_TRUE;
}
PY_MODULE_FUNCTION(stopAllLightSound,_WWISE)
PyObject* py_WG_lightSoundRemove(PyObject* args)
{
    PyObject *first,*second,*third;
    if (!PyArg_ParseTuple(args,"OOO:WG_lightSoundRemove",&first,&second,&third)) return NULL;
    uint32 spaceID=0,chunk=0,item=0;
    if (Script::setData(first,spaceID,"() argument 1") ||
        Script::setData(second,chunk,"() argument 2") ||
        Script::setData(third,item,"() argument 3")) return NULL;
    // 125a310 validates the supplied spaceID but looks up the current space.
    (void)spaceID;
    const auto space=DeprecatedSpaceHelpers::cameraSpace();
    if (!space) Py_RETURN_FALSE;
    const auto found=owners.find(space.get());
    if (found!=owners.end())
        if (auto owner=found->second.lock())
            if (removeStockSpaceObjectSound(owner,chunk,item)) Py_RETURN_TRUE;
    Py_RETURN_FALSE;
}
PY_MODULE_FUNCTION(WG_lightSoundRemove,_WWISE)

}

namespace CompiledSpace
{
struct StockSpaceSoundLoader::State
{
    ClientSpace* space=NULL;
    const void* mapping=NULL;
    std::shared_ptr<wot::StockSpaceSoundIdentities> identities;
    wot::StockSpaceSoundData binary;
    std::vector<BW::string> events;
    StockSpaceSounds owner;
    std::vector<StockSpaceSoundHandle> handles;
    bool bound=false;
};
StockSpaceSoundLoader::StockSpaceSoundLoader() {}
StockSpaceSoundLoader::~StockSpaceSoundLoader() { doUnload(); }
bool StockSpaceSoundLoader::doLoadFromSpace(ClientSpace* space,BinaryFormat& file,
    const DataSectionPtr&,const Matrix&,const StringTable& strings)
{
    if (state_ || unloadDepth_) return fail("Previous sound mapping has not unloaded");
    const size_t section=file.findSection(FourCC("WTau"));
    if (section==BinaryFormat::NOT_FOUND || file.sectionVersion(section)!=2)
        return fail("Missing or incompatible original WTau section");
    auto* stream=file.openSection(section,true);
    if (!stream) return fail("Cannot map original WTau section");
    auto next=std::make_shared<State>();next->space=space;next->mapping=&file;
    const size_t length=stream->size();const void* bytes=stream->readRaw(length);
    std::string error;const bool read=bytes&&next->binary.read(bytes,length,error);
    file.closeSection(stream);
    if (!read) return fail(error.empty()?"Cannot read original WTau data":error.c_str());
    const size_t identitySection=file.findSection(FourCC("WGDE"));
    if (identitySection==BinaryFormat::NOT_FOUND || file.sectionVersion(identitySection)!=1)
        return fail("Missing or incompatible original WGDE sound identities");
    auto* identityStream=file.openSection(identitySection,true);
    if (!identityStream) return fail("Cannot map original WGDE sound identities");
    next->identities=std::make_shared<wot::StockSpaceSoundIdentities>();
    const size_t identityLength=identityStream->size();
    const void* identityBytes=identityStream->readRaw(identityLength);
    const bool identityRead=next->identities->read(identityBytes,identityLength,error);
    file.closeSection(identityStream);
    if (!identityRead) return fail(error.c_str());
    for (const auto& value:next->binary.definitions)
    {
        // Exact 6cce90 reads only entry+4 and entry+c. The legacy wwiseName
        // and authored maxDistance fields are not runtime selection gates.
        const char* event=strings.entry(value.eventName);
        if (!event) return fail("WTau event key is absent from the original BWST");
        next->events.push_back(event);
    }
    state_=next;loaded_.store(true,std::memory_order_release);return true;
}
bool StockSpaceSoundLoader::doBind()
{
    auto live=state_;
    if (!live) return false;
    if (live->bound) return true;
    live->owner=acquireStockSpaceSounds(live->space);
    if (!live->owner) return false;
    live->bound=true;
    live->owner->mapping(live->mapping).data=live->identities;
    for (size_t i=0;i<live->events.size();++i)
    {
        const auto& p=live->binary.definitions[i].position;
        const auto handle=live->events[i].empty()?INVALID_STOCK_SPACE_SOUND:
            live->owner->add(database.event(live->events[i]),Vector3(p[0],p[1],p[2]));
        if (state_!=live) { removeStockSpaceSound(live->owner,handle);return false; }
        live->handles.push_back(handle);
    }
    return true;
}
void StockSpaceSoundLoader::doUnload()
{
    loaded_.store(false,std::memory_order_release);
    auto live=state_;state_.reset();
    if (!live) return;
    ++unloadDepth_;live->bound=false;
    std::vector<StockSpaceSoundHandle> handles;handles.swap(live->handles);
    for (auto handle:handles) removeStockSpaceSound(live->owner,handle);
    removeStockSpaceSoundMapping(live->owner,live->mapping);
    live->owner.reset();--unloadDepth_;
}
float StockSpaceSoundLoader::percentLoaded() const
{ return loaded_.load(std::memory_order_acquire)?1.f:0.f; }
}
#endif
BW_END_NAMESPACE
