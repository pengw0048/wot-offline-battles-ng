// Host scenarios for the WGMinimap core; prints one JSON document.
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "stock_battle_fake_stage.hpp"
#include "wot/stock_battle_minimap.hpp"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace wot::stock_battle_minimap;
typedef Minimap<fake::Stage> FakeMinimap;

static fake::ProviderPtr rotated(float yawDegrees, float scale, float x, float z)
{
    const float yaw = yawDegrees * 0.017453292f;
    const float s = std::sin(yaw) * scale, c = std::cos(yaw) * scale;
    fake::ProviderPtr provider(new fake::Provider());
    const Matrix44 m = {{{c, 0.f, -s, 0.f}, {0.f, scale, 0.f, 0.f}, {s, 0.f, c, 0.f},
        {x, 7.f, z, 1.f}}};
    provider->world = m;
    return provider;
}

int main()
{
    fake::Stage stage;
    stage.symbols["VehicleEntry"] = std::vector<std::string>();
    stage.symbols["ViewPointEntry"] = std::vector<std::string>();
    FakeMinimap minimap(stage);
    fake::Clip root;
    root.node = stage.create("entriesContainer", 0);
    const int alive = stage.create("aliveVehicles", root.node);
    const int dead = stage.create("deadVehicles", root.node);
    stage.create("personal", root.node);

    // A missing container or symbol consumes no ID; IDs start at 1.
    unsigned id = 0;
    assert(minimap.addEntry(root, "VehicleEntry", "nowhere", fake::ProviderPtr(), true, 0xffffffffu,
        id) == FakeMinimap::NO_CONTAINER && id == 0);
    fake::Clip noRoot;
    assert(minimap.addEntry(noRoot, "VehicleEntry", "aliveVehicles", fake::ProviderPtr(), true,
        0xffffffffu, id) == FakeMinimap::NO_CONTAINER);
    assert(minimap.addEntry(root, "MissingEntry", "aliveVehicles", fake::ProviderPtr(), true,
        0xffffffffu, id) == FakeMinimap::ATTACH_FAILED && id == 0);
    unsigned ids[5];
    const unsigned props[5] = {0xffffffffu, 0xfffffffbu, 0xfffffffdu, 1u, 0xfffffffbu};
    fake::ProviderPtr providers[5] = {rotated(30.f, 2.f, 120.f, -45.f), rotated(-135.f, 1.f, 0.f, 0.f),
        rotated(90.f, 0.5f, -480.f, 480.f), rotated(10.f, 1.f, 250.f, 250.f), fake::ProviderPtr()};
    for (int i = 0; i < 5; ++i)
        assert(minimap.addEntry(root, i == 1 ? "ViewPointEntry" : "VehicleEntry",
            i == 1 ? "personal" : "aliveVehicles", providers[i], i != 3, props[i], ids[i]) ==
            FakeMinimap::ADDED);
    assert(ids[0] == 1 && ids[1] == 2 && ids[4] == 5);
    assert(stage.log[0] == "attach MissingEntry MissingEntry1 -1");
    assert(stage.log[1] == "attach VehicleEntry VehicleEntry1 -1");
    assert(stage.log[2] == "attach ViewPointEntry ViewPointEntry2 -1");
    assert(minimap.find(1)->rotate && minimap.find(1)->scale);
    assert(minimap.find(2)->rotate && !minimap.find(2)->scale);
    assert(!minimap.find(3)->rotate && minimap.find(3)->scale);
    assert(!minimap.find(4)->rotate && !minimap.find(4)->scale);

    // Removal recycles IDs last-in first-out; a stale ID is refused.
    assert(minimap.delEntry(4) && !minimap.delEntry(4) && minimap.find(4) == NULL);
    assert(stage.log.back() == "remove VehicleEntry4");
    assert(minimap.addEntry(root, "VehicleEntry", "aliveVehicles", providers[3], false, 1u, id) ==
        FakeMinimap::ADDED && id == 4);
    assert(stage.log.back() == "attach VehicleEntry VehicleEntry4 -1");

    // moveEntry re-parents; unknown IDs and containers are reported separately.
    assert(minimap.moveEntry(root, 3, "deadVehicles") == FakeMinimap::MOVED);
    assert(stage.childNames(dead) == (std::vector<std::string>{"VehicleEntry3"}));
    assert(minimap.moveEntry(root, 9, "deadVehicles") == FakeMinimap::NO_ENTRY);
    assert(minimap.moveEntry(root, 3, "nowhere") == FakeMinimap::MISSING_CONTAINER);
    assert(stage.childNames(alive) ==
        (std::vector<std::string>{"VehicleEntry1", "VehicleEntry5", "VehicleEntry4"}));

    // Before setArenaBB the original's 300/300 defaults apply.
    minimap.tick();
    const double unbounded = minimap.find(1) ? stage.nodes[minimap.find(1)->clip.node].x : 0.0;
    assert(unbounded == static_cast<double>(120.f * 300.f * 300.f));

    minimap.setArena(arenaTransform(-500.f, -400.f, 500.f, 600.f));
    minimap.mapSize(210.f, 210.f);
    minimap.tick();
    // Inactive entries are only hidden; an active entry without a matrix stays put.
    const fake::Node& inactive = stage.nodes[minimap.find(4)->clip.node];
    assert(!inactive.visible && inactive.x == 0 && inactive.displayCalls == 2);
    const fake::Node& unplaced = stage.nodes[minimap.find(5)->clip.node];
    assert(unplaced.visible && unplaced.x == 0 && unplaced.y == 0 && unplaced.displayCalls == 2);

    std::printf("{\"arena\":[%.9g,%.9g,%.9g,%.9g],\"mapSize\":[%.9g,%.9g],\"entries\":[",
        minimap.arena().inverseWidth, minimap.arena().inverseHeight, minimap.arena().centerX,
        minimap.arena().centerY, minimap.mapWidth(), minimap.mapHeight());
    for (unsigned entryID = 1; entryID <= 3; ++entryID)
    {
        FakeMinimap::Entry* entry = minimap.find(entryID);
        const fake::Node& node = stage.nodes[entry->clip.node];
        std::printf("%s{\"id\":%u,\"rotate\":%s,\"scale\":%s,\"world\":[", entryID > 1 ? "," : "",
            entryID, entry->rotate ? "true" : "false", entry->scale ? "true" : "false");
        for (int i = 0; i < 16; ++i)
            std::printf("%s%.9g", i ? "," : "", entry->provider->world.m[i / 4][i % 4]);
        std::printf("],\"x\":%.17g,\"y\":%.17g,\"rotation\":%.17g,\"xScale\":%.17g,"
            "\"yScale\":%.17g,\"visible\":%s}", node.x, node.y, node.rotation, node.xScale,
            node.yScale, node.visible ? "true" : "false");
    }
    std::printf("]}\n");

    // Entry 1 deactivated keeps its last position and only hides.
    const double lastX = stage.nodes[minimap.find(1)->clip.node].x;
    minimap.find(1)->active = false;
    minimap.tick();
    assert(stage.nodes[minimap.find(1)->clip.node].x == lastX);
    assert(!stage.nodes[minimap.find(1)->clip.node].visible);

    // Teardown releases every entry without touching the stage.
    const std::size_t logSize = stage.log.size();
    minimap.clear();
    assert(minimap.size() == 0 && stage.log.size() == logSize);
    return 0;
}
