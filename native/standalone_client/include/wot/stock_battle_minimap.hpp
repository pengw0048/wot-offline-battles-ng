#pragma once

#include "wot/stock_battle_presentation.hpp"

#include <cmath>
#include <map>
#include <string>
#include <vector>

namespace wot
{
namespace stock_battle_minimap
{
using stock_battle_presentation::Matrix44;

// One SetDisplayInfo of a minimap entry (#1513 013ea600..013ea783).
struct EntryDisplay
{
    bool hasPosition, hasRotation, hasScale;
    double x, y, rotation, xScale, yScale;
    bool visible;
};

// 013e94c0: arena bounds become inverse sizes and a centre.
struct ArenaTransform
{
    float inverseWidth, inverseHeight, centerX, centerY;
};

// 013ea283: values before setArenaBB.
inline ArenaTransform defaultArenaTransform()
{
    ArenaTransform result = {300.f, 300.f, 0.f, 0.f};
    return result;
}

inline ArenaTransform arenaTransform(float bottomLeftX, float bottomLeftY, float topRightX,
    float topRightY)
{
    ArenaTransform result;
    result.inverseWidth = 1.f / (topRightX - bottomLeftX);
    result.inverseHeight = 1.f / (topRightY - bottomLeftY);
    result.centerX = (bottomLeftX + topRightX) * 0.5f;
    result.centerY = (topRightY + bottomLeftY) * 0.5f;
    return result;
}

// addEntry transformProps: bit 1 rotates, bit 2 scales; position is always set.
inline bool rotates(unsigned transformProps) { return ((transformProps >> 1) & 1) != 0; }
inline bool scales(unsigned transformProps) { return ((transformProps >> 2) & 1) != 0; }

// d3dx9 D3DXVec3Normalize: a zero vector stays zero.
inline void normalise(float& x, float& y, float& z)
{
    const float length = std::sqrt(x * x + y * y + z * z);
    if (length == 0.f)
    {
        x = y = z = 0.f;
        return;
    }
    x /= length;
    y /= length;
    z /= length;
}

// The display of an active entry with a matrix.
inline EntryDisplay entryDisplay(const Matrix44& world, const ArenaTransform& arena,
    float mapWidth, float mapHeight, bool rotate, bool scale)
{
    EntryDisplay result = {};
    result.hasPosition = true;
    result.x = (world.m[3][0] - arena.centerX) * arena.inverseWidth * mapWidth;
    result.y = (arena.centerY - world.m[3][2]) * arena.inverseHeight * mapHeight;
    if (rotate)
    {
        float x = world.m[2][0], y = world.m[2][1], z = world.m[2][2];
        normalise(x, y, z);
        // x87 fpatan of (x, z), stored as float before the 014b2548 degree
        // factor 0x42652ee0 (one ulp below 180/pi).
        const float radians = static_cast<float>(std::atan2(static_cast<double>(x),
            static_cast<double>(z)));
        result.hasRotation = true;
        result.rotation = radians * 57.2957763671875f;
    }
    if (scale)
    {
        const float(&m)[4][4] = world.m;
        result.hasScale = true;
        result.xScale = std::sqrt(m[0][0] * m[0][0] + m[0][1] * m[0][1] + m[0][2] * m[0][2]) *
            100.f;
        result.yScale = std::sqrt(m[2][0] * m[2][0] + m[2][1] * m[2][1] + m[2][2] * m[2][2]) *
            100.f;
    }
    return result;
}

// Backend provides Clip, Provider and:
//   bool isObject(clip); bool getMember(clip, name, out)
//   bool attachMovie(container, symbol, name, depth, out)
//   void removeFromParent(clip); void addChild(container, clip)
//   bool matrix(provider, Matrix44&); void setDisplay(clip, EntryDisplay)
template <class Backend>
class Minimap
{
public:
    typedef typename Backend::Clip Clip;
    typedef typename Backend::Provider Provider;

    // Map value at node+0x18 of 013e8490.
    struct Entry
    {
        Provider provider;   // +0x18
        Clip clip;           // +0x20
        bool active;         // +0x40
        bool rotate;         // +0x41
        bool scale;          // +0x42
    };

    enum AddResult { ADDED, NO_CONTAINER, ATTACH_FAILED };

    explicit Minimap(Backend& backend) :
        backend_(backend), arena_(defaultArenaTransform()), mapWidth_(300.f), mapHeight_(300.f)
    {
    }

    void setArena(const ArenaTransform& arena) { arena_ = arena; }
    const ArenaTransform& arena() const { return arena_; }
    float mapWidth() const { return mapWidth_; }
    float mapHeight() const { return mapHeight_; }
    void mapSize(float width, float height)
    {
        mapWidth_ = width;
        mapHeight_ = height;
    }

    // 013e96e0. IDs start at 1: a reused ID last-in first-out, else size + 1,
    // so a successful ID is never 0. The container is a member of the root.
    AddResult addEntry(Clip& root, const std::string& symbol, const std::string& containerName,
        const Provider& provider, bool active, unsigned transformProps, unsigned& id)
    {
        Clip container;
        if (!backend_.isObject(root) || !backend_.getMember(root, containerName.c_str(), container))
            return NO_CONTAINER;
        const unsigned entryID = freeIDs_.empty() ?
            static_cast<unsigned>(entries_.size()) + 1 : freeIDs_.back();
        Entry entry;
        if (!backend_.attachMovie(container, symbol.c_str(),
                stock_battle_presentation::instanceName(symbol, entryID).c_str(), -1, entry.clip))
            return ATTACH_FAILED;
        if (!freeIDs_.empty())
            freeIDs_.pop_back();
        entry.provider = provider;
        entry.active = active;
        entry.rotate = rotates(transformProps);
        entry.scale = scales(transformProps);
        entries_[entryID] = entry;
        id = entryID;
        return ADDED;
    }

    Entry* find(unsigned id)
    {
        typename Entries::iterator found = entries_.find(id);
        return found == entries_.end() ? NULL : &found->second;
    }

    // 013e9a40: remove the AS3 child, then recycle the ID.
    bool delEntry(unsigned id)
    {
        typename Entries::iterator found = entries_.find(id);
        if (found == entries_.end())
            return false;
        backend_.removeFromParent(found->second.clip);
        entries_.erase(found);
        freeIDs_.push_back(id);
        return true;
    }

    enum MoveResult { MOVED, NO_ENTRY, MISSING_CONTAINER };

    // 013e9ea0: re-parent the entry clip with container.addChild.
    MoveResult moveEntry(Clip& root, unsigned id, const std::string& containerName)
    {
        Entry* entry = find(id);
        if (entry == NULL)
            return NO_ENTRY;
        Clip container;
        if (!backend_.isObject(root) || !backend_.getMember(root, containerName.c_str(), container))
            return MISSING_CONTAINER;
        backend_.addChild(container, entry->clip);
        return MOVED;
    }

    // 013ea4c0.
    void tick()
    {
        for (typename Entries::iterator it = entries_.begin(); it != entries_.end(); ++it)
        {
            Entry& entry = it->second;
            EntryDisplay display = {};
            Matrix44 world;
            if (entry.active && entry.provider && backend_.matrix(entry.provider, world))
                display = entryDisplay(world, arena_, mapWidth_, mapHeight_, entry.rotate,
                    entry.scale);
            display.visible = entry.active;
            backend_.setDisplay(entry.clip, display);
        }
    }

    void clear() { entries_.clear(); }
    std::size_t size() const { return entries_.size(); }

private:
    typedef std::map<unsigned, Entry> Entries;

    Backend& backend_;
    Entries entries_;
    std::vector<unsigned> freeIDs_;
    ArenaTransform arena_;
    float mapWidth_, mapHeight_;
};
}
}
