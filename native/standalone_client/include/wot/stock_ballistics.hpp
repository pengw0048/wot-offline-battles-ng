#pragma once

// #1513 PyBallisticsSimulator presentation core (0x8aa260 class, 0xd8 bytes).
// It owns tracer records, handles and terminal timing; it is not a damage
// authority. Scene and water queries come from the scene-query owner.

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace wot {
namespace stock_ballistics {

struct Vec3 { float x, y, z; };
struct Mat4 { float m[4][4]; };
struct Plane { float x, y, z, d; };

// Process-wide statics written by the Python setters.
struct Parameters
{
    float hidingTime = 0.05f;          // 01a17264
    float timeAfterDeath = 2.f;        // 01a17268
    float autoScaleDistance = 180.f;   // 01a1726c
    float serverTickLength = 0.03f;    // 01a17270
    std::int32_t spaceID = 0;          // 01d350ac
    bool autoScaleOff = false;         // 01d350b0
};
Parameters & parameters();

const std::uint32_t INVALID_HANDLE = 0x00ffffffu;

// One f642b0 hit: result+4 point, +0x1c material kind, +0x30 identity.
// The identity is 0 when absent, else chunkID | itemIndex16 << 32 | 1 << 48
// (f63ac0); it is compared whole and never unpacked here.
struct SceneHit
{
    Vec3 point;
    std::uint32_t materialKind;
    std::uint64_t identity;
};
// ClosestCcbNative surface filter; this owner reads only the identity. An
// empty filter accepts every surface.
typedef std::function<bool( std::uint64_t identity )> SceneFilter;

// The battle scene-query owner's native f642b0 and f645e0 (false mode).
class Scene
{
public:
    virtual ~Scene() {}
    // start == end asks whether a surface is still present at that point.
    virtual bool collideSegment( std::int32_t spaceID, const Vec3 & start,
        const Vec3 & end, std::uint16_t flags, const SceneFilter & filter,
        SceneHit & hit ) = 0;
    // Distance from start to the first water surface, or a negative value.
    virtual float collideWater( const Vec3 & start, const Vec3 & end ) = 0;
};

struct Point
{
    Vec3 position;
    float time;
    std::uint64_t identity;
    std::uint32_t materialKind;
    bool final;
};

struct Projectile
{
    std::int32_t shotID;
    std::uint32_t handle;
    std::vector<Point> points;
    Plane plane;
    Mat4 matrix;
    Vec3 refStart;
    Vec3 refVelocity;
    Vec3 velocity0;
    Vec3 start;
    Vec3 impactDirection;
    Vec3 velocity;
    float gravity;
    float startTime;
    float maxDistance;
    float holdTime;
    float deathTime;
    bool visible;
    bool autoScale;
};

struct Callbacks
{
    // ClientArena.collideWithSpaceBB(start, end); true writes the hit point.
    std::function<bool( Vec3 & hit, const Vec3 & start, const Vec3 & end )> boundary;
    std::function<void( std::int32_t, const Vec3 &, const Vec3 & )> kill;
    std::function<void( std::int32_t )> erase;
};

enum class AddResult { ADDED, REJECTED, NO_BOUNDARY };

class Simulator
{
public:
    Simulator( Callbacks callbacks, std::function<float()> clock,
        std::function<Vec3()> cameraPosition, Scene & scene );

    AddResult add( std::uint32_t & handle, std::int32_t shotID, float gravity,
        const Vec3 & refStart, const Vec3 & refVelocity, const Vec3 & start,
        float maxDistance, bool autoScale, const Vec3 & gunPosition,
        const Vec3 & tracerCameraPosition );
    void hide( std::int32_t shotID, const Vec3 & end );
    // True with the dead record's position and impact direction.
    bool explode( std::int32_t shotID, const Vec3 & end, Vec3 & position,
        Vec3 & direction );
    void hold( std::int32_t shotID );
    void tick();
    // Drops callbacks and records; later motor lookups find nothing.
    void detach();

    const Projectile * lookup( std::uint32_t handle ) const;
    const std::vector<Projectile> & projectiles() const { return projectiles_; }

    // Exposed for exact-instruction comparison.
    static bool update( Projectile & projectile, float now, const Parameters & parameters,
        Scene & scene, const Vec3 & camera );
    static void setMatrix( Projectile & projectile, const Vec3 & position,
        const Parameters & parameters, const Vec3 & camera );
    static void subdivide( Vec3 & endPosition, Vec3 & endVelocity, std::vector<Vec3> & chords,
        const Vec3 & position, const Vec3 & velocity, const Vec3 & gravity,
        float tickLength, float tolerance );
    static bool intersect( const Plane & plane, Vec3 & out, const Vec3 & a, const Vec3 & b );
    static Plane terminalPlane( const Projectile & projectile, const Vec3 & end );

private:
    struct Slot { std::uint32_t generation; std::uint32_t next; };
    void growSlots( std::uint32_t count );
    std::uint32_t allocateHandle();
    std::size_t remove( std::uint32_t handle );
    Projectile * find( std::int32_t shotID );
    bool predict( Projectile & projectile, const Vec3 & gunPosition,
        const Vec3 & tracerCameraPosition );

    std::vector<Projectile> projectiles_;
    Callbacks callbacks_;
    std::function<float()> clock_;
    std::function<Vec3()> cameraPosition_;
    Scene * scene_;
    std::vector<Slot> slots_;
    std::uint32_t freeHead_;
    std::uint32_t freeTail_;
    std::uint32_t growth_;
    std::vector<std::int32_t> indices_;
};

} // namespace stock_ballistics
} // namespace wot
