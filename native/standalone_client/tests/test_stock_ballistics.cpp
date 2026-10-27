// PyBallisticsSimulator presentation core: records, handles, terminal timing,
// callback order and re-entrancy, with fake boundary and scene owners.
#include "wot/stock_ballistics.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace wot::stock_ballistics;

namespace
{
int failures = 0;
#define CHECK( condition ) do { if (!(condition)) { \
    std::printf( "FAIL %s:%d %s\n", __FILE__, __LINE__, #condition ); ++failures; } } while (0)

bool near( float a, float b, float tolerance = 1e-4f ) { return std::fabs( a - b ) <= tolerance; }
bool near( const Vec3 & a, const Vec3 & b, float tolerance = 1e-4f )
{
    return near( a.x, b.x, tolerance ) && near( a.y, b.y, tolerance ) && near( a.z, b.z, tolerance );
}
Vec3 position( const Projectile & p ) { return { p.matrix.m[3][0], p.matrix.m[3][1], p.matrix.m[3][2] }; }

struct Harness
{
    float now = 10.f;
    Vec3 camera = { 0.f, 2.f, -5.f };
    std::vector<std::string> events;
    std::function<void( std::int32_t )> onKill;
    std::function<void( std::int32_t )> onErase;
    bool boundaryHit = false;
    Vec3 boundaryPoint = { 0.f, 0.f, 0.f };
    int boundaryCalls = 0;

    Callbacks callbacks()
    {
        Callbacks result;
        result.boundary = [this]( Vec3 & hit, const Vec3 &, const Vec3 & ) {
            ++boundaryCalls;
            if (boundaryHit) hit = boundaryPoint;
            return boundaryHit;
        };
        result.kill = [this]( std::int32_t id, const Vec3 &, const Vec3 & ) {
            events.push_back( "kill " + std::to_string( id ) );
            if (onKill) onKill( id );
        };
        result.erase = [this]( std::int32_t id ) {
            events.push_back( "delete " + std::to_string( id ) );
            if (onErase) onErase( id );
        };
        return result;
    }
    Simulator make()
    {
        return Simulator( callbacks(), [this] { return now; }, [this] { return camera; } );
    }
};

AddResult fire( Simulator & simulator, std::uint32_t & handle, std::int32_t id,
    float maxDistance = 100.f, Vec3 velocity = { 0.f, 0.f, 500.f } )
{
    const Vec3 start = { 0.f, 1.f, 0.f };
    return simulator.add( handle, id, 9.81f, start, velocity, start, maxDistance, true,
        Vec3{ 0.f, 1.f, 0.f }, Vec3{ 0.f, 2.f, -5.f } );
}

void resetParameters()
{
    parameters() = Parameters();
    parameters().serverTickLength = 0.1f;
}

void testAddAndFlight()
{
    resetParameters();
    Harness h;
    Simulator s = h.make();
    std::uint32_t handle = 0;
    CHECK( fire( s, handle, 7 ) == AddResult::ADDED );
    CHECK( (handle & INVALID_HANDLE) != INVALID_HANDLE );
    const Projectile * p = s.lookup( handle );
    CHECK( p && p->shotID == 7 && !p->visible && p->holdTime == -1.f && p->deathTime == -1.f );
    // Prediction ends with the 100 m range clip as the only, final point.
    CHECK( p->points.size() == 1 && p->points.back().final );
    CHECK( near( p->points.back().position.z, 100.f, 0.05f ) );
    CHECK( near( p->points.back().time, p->points.back().position.z / 500.f ) );
    CHECK( h.boundaryCalls > 0 );
    // Duplicate active ID is refused.
    std::uint32_t second = 0;
    CHECK( fire( s, second, 7 ) == AddResult::REJECTED && second == INVALID_HANDLE );

    // Hidden until hidingTime has elapsed, then moves on the ballistic path.
    h.now = 10.04f; s.tick();
    CHECK( !s.lookup( handle )->visible );
    h.now = 10.1f; s.tick();
    p = s.lookup( handle );
    CHECK( p->visible && position( *p ).z > 40.f && position( *p ).z < 60.f );
    CHECK( h.events.empty() );
    // Reaching the final point kills the record, hides it and keeps its tail.
    h.now = 10.3f; s.tick();
    p = s.lookup( handle );
    CHECK( p && p->deathTime == 10.3f && !p->visible );
    CHECK( h.events.size() == 1 && h.events[0] == "kill 7" );
    CHECK( near( p->impactDirection, Vec3{ 0.f, 0.f, 1.f }, 1e-2f ) );
    // Deleted only after timeAfterDeath; the slot generation then advances.
    h.now = 12.29f; s.tick();
    CHECK( h.events.size() == 1 );
    h.now = 12.31f; s.tick();
    CHECK( h.events.size() == 2 && h.events[1] == "delete 7" );
    CHECK( s.lookup( handle ) == NULL && s.projectiles().empty() );
    std::uint32_t reused = 0;
    CHECK( fire( s, reused, 7 ) == AddResult::ADDED );
    CHECK( reused != handle && s.lookup( handle ) == NULL && s.lookup( reused ) != NULL );
}

void testHideExplodeHold()
{
    resetParameters();
    Harness h;
    Simulator s = h.make();
    std::uint32_t a = 0, b = 0, c = 0;
    CHECK( fire( s, a, 1, 1000.f ) == AddResult::ADDED );
    CHECK( fire( s, b, 2, 1000.f ) == AddResult::ADDED );
    CHECK( fire( s, c, 3, 1000.f ) == AddResult::ADDED );

    // hide renames to -id and stops at the endpoint plane.
    s.hide( 1, Vec3{ 0.f, 1.f, 30.f } );
    CHECK( s.lookup( a )->shotID == -1 );
    h.now = 10.05f; s.tick();
    CHECK( h.events.empty() );
    h.now = 10.1f; s.tick();
    CHECK( h.events.size() == 1 && h.events[0] == "kill -1" );
    CHECK( near( position( *s.lookup( a ) ).z, 29.99f, 0.02f ) );

    // explode on an alive record returns None and keeps only the nearest point.
    Vec3 where, direction;
    CHECK( !s.explode( 2, Vec3{ 0.f, 1.f, 80.f }, where, direction ) );
    CHECK( s.lookup( b )->points.size() == 1 );
    h.now = 10.2f; s.tick();
    CHECK( h.events.size() == 2 && h.events[1] == "kill 2" );
    // A dead record reports its terminal position and direction.
    CHECK( s.explode( 2, Vec3{ 0.f, 1.f, 80.f }, where, direction ) );
    CHECK( near( where.z, 79.99f, 0.02f ) && near( direction, Vec3{ 0.f, 0.f, 1.f }, 1e-2f ) );
    // An endpoint already behind the tracer stops it where it is.
    std::uint32_t late = 0;
    CHECK( fire( s, late, 8, 1000.f ) == AddResult::ADDED );
    h.now = 10.3f; s.tick();
    CHECK( !s.explode( 8, Vec3{ 0.f, 1.f, 10.f }, where, direction ) );
    h.now = 10.35f; s.tick();
    CHECK( h.events.back() == "kill 8" && near( position( *s.lookup( late ) ).z, 49.99f, 0.02f ) );
    // Unknown IDs are a None result without side effects.
    CHECK( !s.explode( 99, where, where, direction ) );
    s.hide( 99, where );
    s.hold( 99 );

    // hold delays the flight clock by hidingTime.
    resetParameters();
    Harness g;
    Simulator t = g.make();
    std::uint32_t held = 0, free = 0;
    CHECK( fire( t, held, 5, 1000.f ) == AddResult::ADDED );
    CHECK( fire( t, free, 6, 1000.f ) == AddResult::ADDED );
    t.hold( 5 );
    CHECK( t.lookup( held )->holdTime == 0.05f );
    g.now = 10.2f; t.tick();
    CHECK( near( position( *t.lookup( free ) ).z - position( *t.lookup( held ) ).z, 25.f, 0.05f ) );
}

void testReusedHiddenIdentity()
{
    resetParameters();
    Harness h;
    Simulator s = h.make();
    std::uint32_t first = 0, second = 0, third = 0;
    CHECK( fire( s, first, 4, 1000.f ) == AddResult::ADDED );
    s.hide( 4, Vec3{ 0.f, 1.f, 900.f } );
    // Re-adding the same ID ages the hidden predecessor by one second.
    CHECK( fire( s, second, 4, 1000.f ) == AddResult::ADDED );
    CHECK( s.lookup( first )->startTime == 9.f );
    // Hiding again discards the older hidden record without a delete callback.
    s.hide( 4, Vec3{ 0.f, 1.f, 900.f } );
    CHECK( s.lookup( first ) == NULL && s.lookup( second )->shotID == -4 );
    CHECK( h.events.empty() );
    // Shot 0 is its own negation and stays a single record.
    CHECK( fire( s, third, 0, 1000.f ) == AddResult::ADDED );
    s.hide( 0, Vec3{ 0.f, 1.f, 5.f } );
    CHECK( s.lookup( third ) && s.lookup( third )->shotID == 0 );
    std::uint32_t minimum = 0;
    CHECK( fire( s, minimum, -2147483647 - 1, 1000.f ) == AddResult::ADDED );
    s.hide( -2147483647 - 1, Vec3{ 0.f, 1.f, 5.f } );
    CHECK( s.lookup( minimum ) != NULL );
}

void testBoundaryAndDegenerate()
{
    resetParameters();
    Harness h;
    h.boundaryHit = true;
    h.boundaryPoint = Vec3{ 0.f, 1.f, 12.f };
    Simulator s = h.make();
    std::uint32_t handle = 0;
    CHECK( fire( s, handle, 1 ) == AddResult::ADDED );
    const Projectile * p = s.lookup( handle );
    CHECK( p->points.size() == 1 && near( p->points[0].position, h.boundaryPoint ) );
    CHECK( near( p->points[0].time, 12.f / 500.f ) );

    // A final point at the visual start leaves no velocity: rejected.
    h.boundaryPoint = Vec3{ 0.f, 1.f, 0.f };
    std::uint32_t degenerate = 0;
    CHECK( fire( s, degenerate, 2 ) == AddResult::REJECTED );
    CHECK( degenerate == INVALID_HANDLE && s.projectiles().size() == 1 );

    // Missing boundary owner is reported, not treated as no boundary.
    Simulator missing( Callbacks(), [] { return 1.f; }, [] { return Vec3{ 0.f, 0.f, 0.f }; } );
    std::uint32_t none = 0;
    CHECK( fire( missing, none, 3 ) == AddResult::NO_BOUNDARY && missing.projectiles().empty() );
    // Unless range ends inside the first chord, as in #1513.
    CHECK( fire( missing, none, 3, 0.5f ) == AddResult::ADDED );

    // A non-positive tick produces no prediction instead of spinning.
    parameters().serverTickLength = 0.f;
    std::uint32_t idle = 0;
    CHECK( fire( s, idle, 9 ) == AddResult::ADDED && s.lookup( idle )->points.empty() );
}

void testVelocityRecompute()
{
    resetParameters();
    Harness h;
    Simulator s = h.make();
    std::uint32_t handle = 0;
    const Vec3 ref = { 0.f, 10.f, 0.f };
    const Vec3 visual = { 1.f, 10.5f, -2.f };
    CHECK( s.add( handle, 1, 9.81f, ref, Vec3{ 0.f, 5.f, 300.f }, visual, 150.f, false,
        ref, ref ) == AddResult::ADDED );
    const Projectile * p = s.lookup( handle );
    const Point & last = p->points.back();
    const float t = last.time;
    // The visual start reaches the predicted end at the same time.
    const Vec3 end = { visual.x + p->velocity0.x * t,
        visual.y + p->velocity0.y * t - 0.5f * 9.81f * t * t, visual.z + p->velocity0.z * t };
    CHECK( near( end, last.position, 2e-3f ) );
    CHECK( near( p->velocity0, p->velocity ) );
}

void testSubdivision()
{
    std::vector<Vec3> chords;
    Vec3 endPosition, endVelocity;
    const Vec3 g = { 0.f, -9.81f, 0.f };
    Simulator::subdivide( endPosition, endVelocity, chords, Vec3{ 0.f, 0.f, 0.f },
        Vec3{ 0.f, 200.f, 200.f }, g, 0.1f, 0.03f );
    CHECK( near( endPosition, Vec3{ 0.f, 20.f - 0.04905f, 20.f } ) );
    CHECK( near( endVelocity, Vec3{ 0.f, 200.f - 0.981f, 200.f } ) );
    CHECK( !chords.empty() && near( chords.back(), endPosition ) );
    for (std::size_t i = 1; i < chords.size(); ++i) CHECK( chords[i].z > chords[i - 1].z );
    // A strongly curved slow tick splits into several chords within tolerance.
    chords.clear();
    Simulator::subdivide( endPosition, endVelocity, chords, Vec3{ 0.f, 0.f, 0.f },
        Vec3{ 0.f, 30.f, 5.f }, g, 6.f, 0.03f );
    CHECK( chords.size() > 8 );
    // Vertical chords are emitted without subdivision.
    chords.clear();
    Simulator::subdivide( endPosition, endVelocity, chords, Vec3{ 0.f, 0.f, 0.f },
        Vec3{ 0.f, 100.f, 0.f }, g, 0.5f, 0.03f );
    CHECK( chords.size() == 1 );
}

struct FakeScene : public Scene
{
    struct Surface { float z; std::uint32_t material; std::uint64_t item; bool alive; };
    std::vector<Surface> surfaces;
    float water = -1.f;
    int segmentQueries = 0;
    int pointQueries = 0;

    bool collideSegment( std::int32_t, const Vec3 & start, const Vec3 & end, std::uint16_t flags,
        const SceneFilter & filter, SceneHit & hit ) override
    {
        CHECK( flags == 0x80 );
        if (start.z == end.z && start.x == end.x && start.y == end.y)
        {
            ++pointQueries;
            for (const Surface & s : surfaces)
                if (s.alive && near( s.z, start.z, 0.01f ) && (!filter || filter( s.item )))
                {
                    hit = SceneHit{ start, s.material, s.item };
                    return true;
                }
            return false;
        }
        ++segmentQueries;
        const Surface * best = NULL;
        for (const Surface & s : surfaces)
        {
            if (!s.alive || s.z < std::fmin( start.z, end.z ) || s.z > std::fmax( start.z, end.z ))
                continue;
            if (filter && !filter( s.item )) continue;
            if (!best || std::fabs( s.z - start.z ) < std::fabs( best->z - start.z )) best = &s;
        }
        if (!best) return false;
        const float f = (best->z - start.z) / (end.z - start.z);
        hit = SceneHit{ Vec3{ start.x + (end.x - start.x) * f, start.y + (end.y - start.y) * f,
            best->z }, best->material, best->item };
        return true;
    }
    float collideWater( const Vec3 &, const Vec3 & ) override { return water; }
};

void testSceneCandidates()
{
    resetParameters();
    FakeScene scene;
    // A destructible fence (0x49) in front of a wall (material 3).
    scene.surfaces.push_back( { 20.f, 0x49u, 77u, true } );
    scene.surfaces.push_back( { 40.f, 3u, 0u, true } );
    setScene( &scene );
    Harness h;
    Simulator s = h.make();
    std::uint32_t handle = 0;
    CHECK( fire( s, handle, 1, 1000.f ) == AddResult::ADDED );
    const Projectile * p = s.lookup( handle );
    CHECK( p->points.size() == 2 );
    CHECK( !p->points[0].final && p->points[0].itemID == 77u && p->points[0].materialKind == 0x49u );
    CHECK( p->points[1].final && near( p->points[1].position.z, 40.f, 1e-3f ) );
    // The fence still stands when reached: the tracer ends there.
    h.now = 10.05f; s.tick();
    CHECK( h.events.size() == 1 && near( position( *s.lookup( handle ) ).z, 19.99f, 0.02f ) );

    // A fence destroyed before arrival lets the tracer pass to the wall.
    scene.surfaces[0].alive = true;
    std::uint32_t passing = 0;
    CHECK( fire( s, passing, 2, 1000.f ) == AddResult::ADDED );
    scene.surfaces[0].alive = false;
    h.now = 10.1f; s.tick();
    CHECK( h.events.size() == 1 );
    h.now = 10.2f; s.tick();
    CHECK( h.events.size() == 2 && near( position( *s.lookup( passing ) ).z, 39.99f, 0.02f ) );

    // Water in front of the wall replaces it as the final point (segment path).
    scene.surfaces[0].alive = false;
    scene.water = 1.5f;
    Harness dry;
    Simulator w( dry.callbacks(), [] { return 0.f; }, [] { return Vec3{ 0.f, 0.f, 0.f }; } );
    std::uint32_t wet = 0;
    CHECK( w.add( wet, 3, 9.81f, Vec3{ 0.f, 1.f, 0.f }, Vec3{ 0.f, 0.f, 500.f },
        Vec3{ 0.f, 1.f, 0.f }, 1000.f, false, Vec3{ 0.f, 1.f, 0.f }, Vec3{ 0.f, 2.f, -5.f } ) ==
        AddResult::ADDED );
    const Projectile * q = w.lookup( wet );
    CHECK( q->points.size() == 1 && q->points[0].final );
    CHECK( q->points[0].position.z > 0.f && q->points[0].position.z < 40.f );
    setScene( NULL );
}

void testReentrancyAndDetach()
{
    resetParameters();
    Harness h;
    Simulator s = h.make();
    std::uint32_t a = 0, b = 0;
    CHECK( fire( s, a, 1 ) == AddResult::ADDED );
    CHECK( fire( s, b, 2 ) == AddResult::ADDED );
    // A kill callback that fires new shots and hides another stays coherent.
    h.onKill = [&]( std::int32_t id ) {
        std::uint32_t extra = 0;
        CHECK( fire( s, extra, 100 + id ) == AddResult::ADDED );
        s.hide( 2, Vec3{ 0.f, 1.f, 1.f } );
    };
    h.now = 10.3f; s.tick();
    CHECK( h.events.size() == 2 && h.events[0] == "kill 1" && h.events[1] == "kill -2" );
    CHECK( s.projectiles().size() == 4 );
    // Shots added inside callbacks tick on the next frame. Kills precede
    // deletes; a delete callback that detaches stops all further callbacks.
    h.onKill = nullptr;
    h.onErase = [&]( std::int32_t ) { s.detach(); };
    h.now = 12.4f; s.tick();
    const std::vector<std::string> expected = { "kill 1", "kill -2", "kill 101", "kill 98",
        "delete 1" };
    CHECK( h.events == expected );
    CHECK( s.projectiles().empty() && s.lookup( a ) == NULL );
    s.tick();
    CHECK( h.events.size() == expected.size() );
}

void testMatrixAndScale()
{
    resetParameters();
    Projectile p = {};
    p.velocity = Vec3{ 0.f, 0.f, 10.f };
    p.autoScale = true;
    Simulator::setMatrix( p, Vec3{ 0.f, 0.f, 360.f }, parameters(), Vec3{ 0.f, 0.f, 0.f } );
    // Rows: right, up, forward; scaled by distance / autoScaleDistance.
    CHECK( near( p.matrix.m[2][2], 2.f ) && near( p.matrix.m[1][1], 2.f ) &&
        near( p.matrix.m[0][0], 2.f ) && p.matrix.m[3][2] == 360.f && p.matrix.m[3][3] == 1.f );
    parameters().autoScaleOff = true;
    Simulator::setMatrix( p, Vec3{ 0.f, 0.f, 360.f }, parameters(), Vec3{ 0.f, 0.f, 0.f } );
    CHECK( near( p.matrix.m[2][2], 1.f ) );
    // Near-vertical flight picks the x reference axis.
    p.velocity = Vec3{ 0.f, -10.f, 0.1f };
    p.autoScale = false;
    Simulator::setMatrix( p, Vec3{ 1.f, 2.f, 3.f }, parameters(), Vec3{ 0.f, 0.f, 0.f } );
    CHECK( near( p.matrix.m[2][1], -1.f, 1e-3f ) && std::isfinite( p.matrix.m[0][0] ) );
}

void testPlaneIntersection()
{
    const Plane plane = { 0.f, 0.f, 1.f, 5.f };
    Vec3 out;
    CHECK( Simulator::intersect( plane, out, Vec3{ 0.f, 0.f, 0.f }, Vec3{ 0.f, 0.f, 10.f } ) &&
        near( out.z, 5.f ) );
    CHECK( !Simulator::intersect( plane, out, Vec3{ 0.f, 0.f, 6.f }, Vec3{ 0.f, 0.f, 10.f } ) );
    CHECK( !Simulator::intersect( plane, out, Vec3{ 0.f, 0.f, 0.f }, Vec3{ 1.f, 0.f, 0.f } ) );
}
} // namespace

int main()
{
    testAddAndFlight();
    testHideExplodeHold();
    testReusedHiddenIdentity();
    testBoundaryAndDegenerate();
    testVelocityRecompute();
    testSubdivision();
    testSceneCandidates();
    testReentrancyAndDetach();
    testMatrixAndScale();
    testPlaneIntersection();
    if (failures) { std::printf( "%d failures\n", failures ); return 1; }
    std::printf( "stock ballistics core passed\n" );
    return 0;
}
