// BQ's production native f642b0/f645e0 owner, BP's scene adapter and BP's
// presentation core together over a geometric space fixture.
#include "stock_ballistics_scene_host.hpp"
#include "stock_scene_identity.hpp"

#include "stock_ground_water_query.inc"
#include "stock_battle_scene_queries.cpp"
#include "stock_ballistics_scene.cpp"

#include <cstdio>
#include <string>

// The static-item identity owner: readStockObstacleItem since BQ b351782e,
// readStockObstacleIdentity before it.
namespace BW {
bool readStockObstacleItem(const CollisionObstacle & obstacle, uint32 & chunkID, uint32 & itemIndex) {
    if (!obstacle.identity.known) return false;
    chunkID = obstacle.identity.chunkID;
    itemIndex = obstacle.identity.itemIndex;
    return true;
}
bool readStockObstacleIdentity(const CollisionObstacle & obstacle, StockObstacleIdentity & output) {
    return readStockObstacleItem(obstacle, output.chunkID, output.itemIndex);
}
}

using namespace wot::stock_ballistics;

namespace
{
int failures = 0;
#define CHECK( condition ) do { if (!(condition)) { \
    std::printf( "FAIL %s:%d %s\n", __FILE__, __LINE__, #condition ); ++failures; } } while (0)

bool near( float a, float b, float tolerance ) { return std::fabs( a - b ) <= tolerance; }
Vec3 position( const Projectile & p ) { return { p.matrix.m[3][0], p.matrix.m[3][1], p.matrix.m[3][2] }; }

std::uint64_t identity( std::uint32_t chunk, std::uint16_t index )
{
    return std::uint64_t( chunk ) | (std::uint64_t( index ) << 32) | (std::uint64_t( 1 ) << 48);
}

BW::ClientSpace * makeSpace( BW::SpaceID id )
{
    BW::ClientSpace * space = new BW::ClientSpace( id );
    BW::SpaceManager::instance().spaces[id] = BW::ClientSpacePtr( space );
    parameters().spaceID = id;
    return space;
}

void plane( BW::ClientSpace * space, float z, std::uint32_t material, bool known = false,
    std::uint32_t chunk = 0, std::uint32_t index = 0 )
{
    BW::HostPlane p;
    p.z = z;
    p.triangle.v[0] = BW::Vector3( -1, 0, z );
    p.triangle.v[1] = BW::Vector3( 1, 0, z );
    p.triangle.v[2] = BW::Vector3( 0, 1, z );
    p.triangle.flags_ = BW::WorldTriangle::Flags( material << 8 );
    p.obstacle.identity.known = known;
    p.obstacle.identity.chunkID = chunk;
    p.obstacle.identity.itemIndex = index;
    space->planes.push_back( p );
}

struct Harness
{
    float now = 10.f;
    std::vector<std::string> events;
    std::function<void( std::int32_t )> onKill;
    Simulator make()
    {
        Callbacks callbacks;
        callbacks.boundary = []( Vec3 &, const Vec3 &, const Vec3 & ) { return false; };
        callbacks.kill = [this]( std::int32_t id, const Vec3 &, const Vec3 & ) {
            events.push_back( "kill " + std::to_string( id ) );
            if (onKill) onKill( id );
        };
        callbacks.erase = [this]( std::int32_t id ) { events.push_back( "delete " + std::to_string( id ) ); };
        return Simulator( callbacks, [this] { return now; }, [] { return Vec3{ 0.f, 2.f, -5.f }; },
            BW::stockBallisticsScene() );
    }
};

AddResult fire( Simulator & s, std::uint32_t & handle, std::int32_t id, float maxDistance = 1000.f )
{
    const Vec3 start = { 0.f, 1.f, 0.f };
    return s.add( handle, id, 9.81f, start, Vec3{ 0.f, 0.f, 500.f }, start, maxDistance, false,
        start, Vec3{ 0.f, 2.f, -5.f } );
}

void reset()
{
    BW::SpaceManager::instance().spaces.clear();
    BW::Waters::instance().clear();
    parameters() = Parameters();
    parameters().serverTickLength = 0.1f;
}

void testWallAndDestructible()
{
    reset();
    BW::ClientSpace * space = makeSpace( 7 );
    plane( space, 20.f, 0x49u, true, 7u, 3u );
    plane( space, 40.f, 3u );
    Harness h;
    Simulator s = h.make();
    std::uint32_t handle = 0;
    CHECK( fire( s, handle, 1 ) == AddResult::ADDED );
    const Projectile * p = s.lookup( handle );
    // The fence is recorded with its native identity, then rejected by the
    // seen-set filter so the farther wall is found in the same chord.
    CHECK( p->points.size() == 2 );
    CHECK( p->points.size() == 2 && !p->points[0].final && p->points[0].identity == identity( 7u, 3u ) &&
        p->points[0].materialKind == 0x49u );
    CHECK( p->points.size() == 2 && p->points[1].final && near( p->points[1].position.z, 40.f, 1e-3f ) &&
        p->points[1].identity == 0u );
    bool rejected = false;
    for (const BW::HostVisit & visit : space->visits)
        rejected = rejected || (visit.z == 20.f && visit.verdict == BW::COLLIDE_ALL);
    CHECK( rejected );
    for (bool highest : space->lods) CHECK( highest );
    // The recheck at the standing fence is zero-length: BQ answers false
    // without traversing, as #1513 01172190 does, so the tracer reaches the wall.
    const int before = space->collides;
    h.now = 10.05f; s.tick();
    CHECK( h.events.empty() && space->collides == before );
    h.now = 10.1f; s.tick();
    CHECK( h.events.size() == 1 && near( position( *s.lookup( handle ) ).z, 39.99f, 0.02f ) );
    // The same outcome once the fence has been removed.
    std::uint32_t second = 0;
    CHECK( fire( s, second, 2 ) == AddResult::ADDED );
    space->planes.erase( space->planes.begin() );
    h.now = 10.2f; s.tick();
    CHECK( h.events.size() == 2 && near( position( *s.lookup( second ) ).z, 39.99f, 0.02f ) );
}

void testIdentities()
{
    reset();
    BW::ClientSpace * space = makeSpace( 8 );
    plane( space, 10.f, 0x49u, true, 0u, 0u );
    plane( space, 11.f, 0x4au, true, 0u, 1u );
    plane( space, 12.f, 0x4bu, true, 1u, 0u );
    plane( space, 13.f, 0x56u, true, 0x80000000u, 0u );
    plane( space, 14.f, 0x50u, true, 1u, 0u );   // repeats (1, 0): filtered as seen
    plane( space, 15.f, 0x48u );                 // material outside 0x49..0x56: final
    Harness h;
    Simulator s = h.make();
    std::uint32_t handle = 0;
    CHECK( fire( s, handle, 1 ) == AddResult::ADDED );
    const Projectile * p = s.lookup( handle );
    const std::uint64_t expected[] = { identity( 0u, 0u ), identity( 0u, 1u ), identity( 1u, 0u ),
        identity( 0x80000000u, 0u ) };
    CHECK( p->points.size() == 5 );
    for (int i = 0; i < 4 && p->points.size() == 5; ++i)
        CHECK( p->points[i].identity == expected[i] && !p->points[i].final );
    CHECK( identity( 0u, 0u ) == (std::uint64_t( 1 ) << 48) );
    CHECK( p->points.size() == 5 && p->points[4].final && near( p->points[4].position.z, 15.f, 1e-3f ) );
}

void testWater()
{
    reset();
    BW::ClientSpace * space = makeSpace( 9 );
    plane( space, 40.f, 3u );
    BW::Water water;
    water.transform_.m[3][1] = 5.f;
    water.invTransform_.m[3][1] = -5.f;
    water.bbActual_.minimum = BW::Vector3( -1000.f, -1.f, -1000.f );
    water.bbActual_.maximum = BW::Vector3( 1000.f, 1.f, 1000.f );
    BW::Waters::instance().push_back( &water );
    Harness h;
    Simulator s = h.make();
    const Vec3 start = { 0.f, 10.f, 0.f }, velocity = { 0.f, -100.f, 500.f };
    // Segment path: water at y = 5 lies before the wall at z = 40.
    std::uint32_t wet = 0;
    CHECK( s.add( wet, 1, 9.81f, start, velocity, start, 1000.f, false, start, start ) == AddResult::ADDED );
    const Projectile * p = s.lookup( wet );
    CHECK( p->points.size() == 1 && p->points[0].final && near( p->points[0].position.y, 5.f, 0.05f ) &&
        p->points[0].position.z > 20.f && p->points[0].position.z < 30.f );
    // Tracer-camera path (attributed shot, camera above the gun).
    std::uint32_t seen = 0;
    CHECK( s.add( seen, 2, 9.81f, start, velocity, start, 1000.f, true, start, Vec3{ 0.f, 20.f, -5.f } ) ==
        AddResult::ADDED );
    p = s.lookup( seen );
    CHECK( p->points.size() == 1 && p->points[0].final && near( p->points[0].position.y, 5.f, 0.05f ) );
    // Water below the wall's height is behind it: the wall is final.
    water.transform_.m[3][1] = -5.f;
    water.invTransform_.m[3][1] = 5.f;
    std::uint32_t dry = 0;
    CHECK( s.add( dry, 3, 9.81f, start, velocity, start, 1000.f, false, start, start ) == AddResult::ADDED );
    p = s.lookup( dry );
    CHECK( p->points.size() == 1 && near( p->points[0].position.z, 40.f, 1e-3f ) );
    BW::Waters::instance().clear();
}

void testLifetimeAndEndpoints()
{
    reset();
    BW::ClientSpace * space = makeSpace( 10 );
    plane( space, 40.f, 3u );
    Harness h;
    Simulator s = h.make();
    // Authoritative endpoints still govern: explode before the wall stops there.
    std::uint32_t early = 0, late = 0, hidden = 0;
    CHECK( fire( s, early, 1 ) == AddResult::ADDED );
    CHECK( fire( s, late, 2 ) == AddResult::ADDED );
    CHECK( fire( s, hidden, 3 ) == AddResult::ADDED );
    Vec3 where, direction;
    CHECK( !s.explode( 1, Vec3{ 0.f, 1.f, 30.f }, where, direction ) );
    s.hide( 3, Vec3{ 0.f, 1.f, 25.f } );
    h.now = 10.1f; s.tick();
    CHECK( near( position( *s.lookup( early ) ).z, 29.99f, 0.02f ) );
    CHECK( near( position( *s.lookup( hidden ) ).z, 24.99f, 0.02f ) && s.lookup( hidden )->shotID == -3 );
    CHECK( near( position( *s.lookup( late ) ).z, 39.99f, 0.02f ) );
    // A verdict beyond the predicted wall reports the native terminal instead.
    CHECK( s.explode( 2, Vec3{ 0.f, 1.f, 60.f }, where, direction ) && near( where.z, 39.99f, 0.02f ) );

    // Prediction already made survives a later clear; new shots then miss the
    // cleared space and end at range. A missing space behaves the same.
    std::uint32_t before = 0;
    CHECK( fire( s, before, 4 ) == AddResult::ADDED );
    space->clear();
    std::uint32_t after = 0;
    CHECK( fire( s, after, 5, 100.f ) == AddResult::ADDED );
    CHECK( s.lookup( after )->points.size() == 1 && near( s.lookup( after )->points[0].position.z, 100.f, 0.1f ) );
    h.now = 10.2f; s.tick();
    CHECK( near( position( *s.lookup( before ) ).z, 39.99f, 0.02f ) );
    parameters().spaceID = 999;
    std::uint32_t missing = 0;
    CHECK( fire( s, missing, 6, 100.f ) == AddResult::ADDED );
    CHECK( near( s.lookup( missing )->points[0].position.z, 100.f, 0.1f ) );
    CHECK( !PyErr_Occurred() );

    // A kill callback that fires again runs a nested prediction inside the tick.
    reset();
    BW::ClientSpace * other = makeSpace( 11 );
    plane( other, 40.f, 3u );
    Harness n;
    Simulator t = n.make();
    std::uint32_t first = 0, nested = 0;
    n.onKill = [&]( std::int32_t ) {
        if (!nested) CHECK( fire( t, nested, 9 ) == AddResult::ADDED );
    };
    CHECK( fire( t, first, 8 ) == AddResult::ADDED );
    n.now = 10.1f; t.tick();
    CHECK( nested != 0 && t.lookup( nested ) && near( t.lookup( nested )->points[0].position.z, 40.f, 1e-3f ) );
    // Teardown: the space goes away while records remain; ticks stay safe.
    BW::SpaceManager::instance().spaces.clear();
    n.now = 13.f; t.tick();
    n.now = 16.f; t.tick();
    CHECK( t.projectiles().empty() );
    t.detach();
}
} // namespace

int main()
{
    Py_Initialize();
    testWallAndDestructible();
    testIdentities();
    testWater();
    testLifetimeAndEndpoints();
    reset();
    Py_Finalize();
    if (failures) { std::printf( "%d failures\n", failures ); return 1; }
    std::printf( "BQ native scene with BP presenter passed\n" );
    return 0;
}
