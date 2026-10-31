#pragma once
// World internals shared by its translation units.
#include "stock_native_battle_world.hpp"
#include "offline_destructible_geometry.h"
#include <array>

namespace native_battle {
struct FallenFoliage {
    std::unique_ptr<stock_native_bot_foliage::Trees> trees;
};

// The validated baked destructible catalog of this map (destructibles/<map>.json, format version 9).
struct DestructibleCatalog {
    using Key = std::pair<std::uint32_t, std::uint32_t>;
    using Signature = std::array<std::int64_t, 12>;
    // Values are the DestructiblesCache DESTR_TYPE_* the descriptor must carry.
    enum class Kind { Tree = 0, Falling = 1, Fragile = 2, Structure = 3 };
    struct Resource {
        std::string filename;                         // authored name for the descriptor cache
        Kind kind = Kind::Fragile;
        std::vector<std::array<double, 6>> boxes;     // local min xyz, max xyz
        std::vector<int> materials;                   // per box; -1 outside structures
        std::set<std::size_t> retained;               // retained_collision_boxes
    };
    struct Placement {
        Kind kind = Kind::Fragile;
        std::string resource;                         // normalized resource name (trees: normalized .spt)
        std::string descriptor;                       // authored descriptor filename
        std::optional<std::size_t> box_index;
        Signature signature{};
        double item_scale = 0., margin = 0.;
        std::vector<offline_destructible::Box> boxes; // world OBBs; empty for trees and degenerate transforms
    };
    static constexpr std::int64_t quantization = 1000;
    std::string map;
    std::map<std::string, Resource> resources;        // by normalized filename
    std::map<Key, Placement> placements;              // baked instances and trees by wire
    std::set<Signature> ambiguous;
    std::set<Key> excluded;
};

namespace world {
std::string shortMapName(std::string map);
bool exists(const std::string& path);
// One JSON document through the stock interpreter's json module; startup-only.
p::Ref readJson(const std::string& path);
std::string normalizedFilename(const std::string& filename);

// Baked static and fallen-tree foliage of this map (foliage/<map>.json beside config.json).
void loadFoliage(Battle& b, const std::string& config_dir);
// destructibles/<map>.json validated as the pinned prebaked loader and sensor catalog did; null when the map has none.
std::unique_ptr<DestructibleCatalog> loadDestructibleCatalog(const std::string& config_dir, const std::string& map);
// Accepted fallen trees follow their native matrices on the visible client; repeated calls are gated by cadence.
void refreshFallenFoliage(Battle& b, double now, bool force);
// Visible detached-turret presentation over World::turrets.
void presentTurretRows(Battle& b);
void advanceTurretPresentation(Battle& b, double now);
void retireTurretPresentation(Battle& b);
// The space owner's catalog-dependent state after a round-wide destructible disable.
void withdrawCatalog(Battle& b);
// One canonical destructible event through the space's native events owner (both peers).
bool ingressDestructible(Battle& b, const Value& event);
void closeSpace(Battle& b);
// Stream remapping and native destructible bodies, once per frame before drive and Bot steps.
void spaceFrame(Battle& b, double now);
// Native emit / stop_lifetime / remove_light_sound for NativeBackend's presentation seam.
std::array<Object, 3> presentationCallables();
}
}
