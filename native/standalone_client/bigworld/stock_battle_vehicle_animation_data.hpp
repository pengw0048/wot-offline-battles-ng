#pragma once

// Exact #1513 battle vehicle animation kernels: SwingingAnimator (0077f7b0),
// LinkedNodesPitchAnimator (0078c070) and SuspensionSound (00789630).
// Header-only and engine-free so host probes execute the client arithmetic.
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace wot
{
typedef std::array<float, 16> StockAnimationMatrix; // D3DX row-major
typedef std::array<float, 3> StockAnimationVector;

// D3DX entry points used by the original bodies, injected so Windows binds
// d3dx9_43 while host probes bind recorded references.
struct StockAnimationMath
{
    void (*transform)( float out[4], const StockAnimationVector & value, const StockAnimationMatrix & matrix );
    StockAnimationVector (*transformNormal)( const StockAnimationVector & value, const StockAnimationMatrix & matrix );
    void (*inverse)( StockAnimationMatrix & value );
    StockAnimationVector (*normalize)( const StockAnimationVector & value );
    StockAnimationMatrix (*rotationYawPitchRoll)( float yaw, float pitch, float roll );
    StockAnimationMatrix (*rotationX)( float angle );
    StockAnimationMatrix (*multiply)( const StockAnimationMatrix & first, const StockAnimationMatrix & second );
    float (*asin)( float value );
};

inline StockAnimationMatrix stockAnimationIdentity()
{
    StockAnimationMatrix value = {};
    value[0] = value[5] = value[10] = value[15] = 1.f;
    return value;
}

// ---------------------------------------------------------------------------
// SwingingAnimator: native object 0x148 bytes, constructor 0077f360.
struct StockSwingingState
{
    StockAnimationMatrix basis = stockAnimationIdentity();  // +0x04
    StockAnimationMatrix output = stockAnimationIdentity(); // +0x44
    float accelPeriod = 0.f;      // +0xb8
    float accelDirection = 0.f;   // +0xbc
    float lodSetting = 0.f;       // +0xc0
    float maxMovementSpeed = 0.f; // +0xc4
    float roll[7] = {};           // +0xc8, +0xe8..+0xfc
    float shotSensitivity = 0.f;  // +0xcc
    float pitch[6] = {};          // +0xd0..+0xe4
    bool reset = true;            // +0x100
    StockAnimationVector position = {};     // +0x104
    StockAnimationVector velocity = {};     // +0x110
    StockAnimationVector acceleration = {}; // +0x11c
    StockAnimationVector impulse = {};      // +0x128
    float pitchAngle = 0.f;    // +0x134
    float rollAngle = 0.f;     // +0x138
    float pitchVelocity = 0.f; // +0x13c
    float rollVelocity = 0.f;  // +0x140
};

// 0077f650: impulses accumulate until the next normal update consumes them.
inline void receiveStockSwingingImpulse( StockSwingingState & state,
    const StockAnimationVector & direction, float impulse )
{
    for (int axis = 0; axis < 3; ++axis)
        state.impulse[axis] += direction[axis] * impulse;
}

namespace detail
{
inline float stockSwingSign( float velocity )
{
    if (velocity > 0.f) return -1.f;
    if (0.f > velocity) return 1.f;
    return -0.f;
}

// One damped spring leg of the 0077fd00 substep loop.
inline void stepStockSwingAxis( float & angle, float & velocity, float force,
    float friction, float damping, float limit, float step )
{
    float drag = (friction * stockSwingSign( velocity ) - damping * velocity) * step;
    if (std::fabs( drag ) > std::fabs( velocity ))
        drag = -velocity;
    float next = drag + force + velocity;
    angle += (next + velocity) * 0.5f * step;
    if (std::fabs( angle ) > limit)
    {
        if (angle > 0.f) angle = limit;
        else if (0.f > angle) angle = -limit;
        else angle = 0.f;
        next = 0.f;
    }
    velocity = next;
}
}

// Exact 0077f7b0 after its lodLink sample. `world` and `compensation` read
// their MatrixProviders into a zeroed matrix and return false when unbound.
// Returns false when the original returns before touching its output.
template <class World, class Compensation>
bool advanceStockSwinging( StockSwingingState & state, float elapsed, float lod,
    World world, Compensation compensation, const StockAnimationMath & math )
{
    const bool outside = lod > state.lodSetting;
    if (elapsed == 0.f || outside)
    {
        state.reset = true;
        if (!outside)
            return false;
        state.output = stockAnimationIdentity();
    }
    else
    {
        StockAnimationMatrix worldMatrix = {};
        if (!world( worldMatrix ))
            worldMatrix = stockAnimationIdentity();
        const StockAnimationVector probe = {{ 0.f, 0.f, state.roll[0] }};
        float sampled[4];
        math.transform( sampled, probe, worldMatrix );
        const StockAnimationVector position = {{ sampled[0], sampled[1], sampled[2] }};
        if (state.reset)
        {
            state.position = position;
            state.velocity = StockAnimationVector();
            state.acceleration = StockAnimationVector();
            state.impulse = StockAnimationVector();
            state.pitchAngle = state.rollAngle = 0.f;
            state.pitchVelocity = state.rollVelocity = 0.f;
            state.output = stockAnimationIdentity();
            state.reset = false;
        }
        StockAnimationMatrix inverse = worldMatrix;
        math.inverse( inverse );
        const float reciprocal = 1.f / elapsed;
        StockAnimationVector velocity;
        for (int axis = 0; axis < 3; ++axis)
            velocity[axis] = (position[axis] - state.position[axis]) * reciprocal;
        float drive = 0.f;
        const float travelled = (velocity[1] * velocity[1] + velocity[0] * velocity[0] +
            velocity[2] * velocity[2]) * elapsed * elapsed;
        const float maxSpeed = state.maxMovementSpeed * state.maxMovementSpeed;
        if (!(maxSpeed < travelled) && maxSpeed == maxSpeed && travelled == travelled &&
            0.3f > elapsed)
        {
            const float keep = 0.3f - elapsed;
            StockAnimationVector & acceleration = state.acceleration;
            acceleration[0] = ((velocity[0] - state.velocity[0]) + keep * acceleration[0]) * 3.3333333f;
            acceleration[1] = (acceleration[1] * keep + (velocity[1] - state.velocity[1])) * 3.3333333f;
            acceleration[2] = (acceleration[2] * keep + (velocity[2] - state.velocity[2])) * 3.3333333f;
            const float along = (acceleration[1] * worldMatrix[9] + worldMatrix[8] * acceleration[0] +
                acceleration[2] * worldMatrix[10]) * state.accelDirection;
            if (along > 0.f)
                acceleration = StockAnimationVector();
            const float magnitude = std::sqrt( acceleration[0] * acceleration[0] +
                acceleration[1] * acceleration[1] + acceleration[2] * acceleration[2] );
            if (magnitude > 15.f)
            {
                const float scale = 15.f / magnitude;
                acceleration[1] *= scale;
                acceleration[0] *= scale;
                acceleration[2] *= scale;
            }
            if (state.accelPeriod > 0.f)
                drive = -(state.pitch[0] * math.transformNormal( acceleration, inverse )[2]);
            else
                acceleration = StockAnimationVector();
        }
        const float period = state.accelPeriod - elapsed;
        state.accelPeriod = period > 0.f ? period : 0.f;
        const StockAnimationVector gravityWorld = {{ 0.f, -9.81f, 0.f }};
        const float gravity = state.pitch[5] * math.transformNormal( gravityWorld, inverse )[2];
        if (0.f > gravity * drive)
        {
            const float combined = gravity + drive;
            drive = 0.f > combined * drive ? 0.f : combined;
        }
        const StockAnimationVector impulse = math.transformNormal( state.impulse, inverse );
        state.impulse = StockAnimationVector();
        float rollForce = -(state.shotSensitivity * impulse[0]);
        float pitchForce = state.shotSensitivity * impulse[2];
        float remaining = elapsed;
        while (static_cast<double>( remaining ) > 1e-05)
        {
            const float step = remaining < 0.05f ? remaining : 0.05f;
            remaining -= step;
            const float pitchSpring = step * drive + pitchForce -
                state.pitch[1] * state.pitchAngle * step;
            pitchForce = 0.f;
            detail::stepStockSwingAxis( state.pitchAngle, state.pitchVelocity, pitchSpring,
                state.pitch[2], state.pitch[3], state.pitch[4], step );
            const float rollSpring = rollForce - state.rollAngle * state.roll[2] * step;
            rollForce = 0.f;
            detail::stepStockSwingAxis( state.rollAngle, state.rollVelocity, rollSpring,
                state.roll[3], state.roll[4], state.roll[5], step );
        }
        state.position = position;
        state.velocity = velocity;
        state.output = math.rotationYawPitchRoll( 0.f, state.pitchAngle, state.rollAngle );
    }
    StockAnimationMatrix placing = {};
    if (compensation( placing ))
        state.output = math.multiply( state.output, placing );
    state.output = math.multiply( state.output, state.basis );
    return true;
}

// ---------------------------------------------------------------------------
// LinkedNodesPitchAnimator: one master node row plus slave rows sharing the
// first `knots` angles. Table rows are (knots + 1) floats; the extra column
// holds the current interpolated angle (0078ba80/0078c070).
struct StockLinkedNodesGroup
{
    std::size_t knots = 0;
    std::size_t rows = 0;
    std::vector<float> table;
};

// Builds the native table from authored rows. A row whose width differs from
// the group's shortest row keeps its zero-initialised entries.
inline StockLinkedNodesGroup makeStockLinkedNodesGroup( const std::vector<std::vector<float> > & rows )
{
    StockLinkedNodesGroup group;
    if (rows.empty())
        return group;
    std::size_t knots = rows[0].size();
    for (std::size_t index = 1; index < rows.size(); ++index)
        if (rows[index].size() < knots)
            knots = rows[index].size();
    group.knots = knots;
    group.rows = rows.size();
    group.table.assign( (knots + 1) * rows.size(), 0.f );
    for (std::size_t row = 0; row < rows.size(); ++row)
        if (rows[row].size() == knots)
            for (std::size_t knot = 0; knot < knots; ++knot)
                group.table[(knots + 1) * row + knot] = rows[row][knot];
    return group;
}

inline float stockLinkedNodesMasterAngle( const StockAnimationMatrix & local, const StockAnimationMath & math )
{
    const StockAnimationVector axis = math.normalize( {{ local[8], local[9], local[10] }} );
    float value = axis[1];
    if (-1.f <= value)
    {
        if (1.f <= value)
            value = 1.f;
    }
    else
        value = -1.f;
    return -math.asin( value );
}

// 0078c070 interpolation. Returns whether the master angle changed.
inline bool interpolateStockLinkedNodes( StockLinkedNodesGroup & group, float angle )
{
    const std::size_t knots = group.knots;
    float * table = group.table.data();
    if (group.table.empty() || angle == table[knots])
        return false;
    table[knots] = angle;
    std::size_t index = 0;
    while (index != knots && table[index] < angle)
        ++index;
    if (index == 0 || index == knots)
    {
        for (std::size_t row = 1; row < group.rows; ++row)
        {
            const std::size_t base = (knots + 1) * row;
            table[base + knots] = table[index != 0 ? base + knots - 1 : base];
        }
        return true;
    }
    const float upper = table[index];
    const float lower = table[index - 1];
    const float span = upper - lower;
    if (span != 0.f)
        for (std::size_t row = 1; row < group.rows; ++row)
        {
            const std::size_t base = (knots + 1) * row + index;
            table[(knots + 1) * row + knots] =
                (table[base - 1] * (upper - angle) + table[base] * (angle - lower)) * (1.f / span);
        }
    return true;
}

inline float stockLinkedNodesSlaveAngle( const StockLinkedNodesGroup & group, std::size_t slave )
{
    return slave < group.rows ? group.table[(group.knots + 1) * slave + group.knots] : 0.f;
}

// ---------------------------------------------------------------------------
// SuspensionSound: events are slots 0..7 in the 01d32460 name order; slot 8
// is the never-assigned zero event that 0124ee30 ignores.
static const char * const STOCK_SUSPENSION_SOUND_SLOTS[8] = {
    "upStart_underLimit", "upStop_underLimit", "downStart_underLimit", "downStop_underLimit",
    "upStart_overLimit", "upStop_overLimit", "downStart_overLimit", "downStop_overLimit" };
// 01a12e98: {start, stop} slots for motion indices 0..4.
static const int STOCK_SUSPENSION_SOUND_TRANSITIONS[5][2] = {
    { 8, 8 }, { 0, 1 }, { 4, 5 }, { 2, 3 }, { 6, 7 } };

struct StockSuspensionSoundState
{
    float lodSetting = 0.f;     // +0x28
    float angleLimit = 0.f;     // +0x2c
    float angle = 0.f;          // +0x68
    float angularSpeed = 0.f;   // +0x6c
    float startAngle = 0.f;     // +0x70
    int motion = 0;             // +0x74
    std::uint16_t vehicleState = 0; // +0x78
};

// 0078a310: pitch(vehicleMatrix) - pitch(bodyMatrix) from their Z axes.
inline float stockSuspensionSoundAngle( const StockAnimationMatrix & body,
    const StockAnimationMatrix & vehicle, const StockAnimationMath & math )
{
    float pitch[2];
    const StockAnimationMatrix * sources[2] = { &body, &vehicle };
    for (int index = 0; index < 2; ++index)
    {
        const StockAnimationMatrix & matrix = *sources[index];
        float value = math.normalize( {{ matrix[8], matrix[9], matrix[10] }} )[1];
        value = -1.f > value ? -1.f : (value < 1.f ? value : 1.f);
        pitch[index] = math.asin( value );
    }
    return pitch[1] - pitch[0];
}

struct StockSuspensionSoundTransition
{
    int stop = 8;
    int start = 8;
    bool changed = false;
};

// Exact 00789630 body for a record in vehicleState 2 that passed its LOD
// sample. `angle` returns false when either provider is unbound.
template <class Angle>
StockSuspensionSoundTransition advanceStockSuspensionSound( StockSuspensionSoundState & state,
    float elapsed, Angle angle )
{
    float speed = 0.f;
    float current;
    if (elapsed != 0.f && angle( current ))
    {
        float weight = elapsed * 10.f;
        const float previous = state.angularSpeed;
        if (1.f <= weight)
            weight = 1.f;
        speed = ((current - state.angle) / elapsed - previous) * weight + previous;
        if (previous * speed <= 0.f)
            state.startAngle = current;
        state.angle = current;
    }
    state.angularSpeed = speed;
    int motion = 0;
    if (0.05f <= std::fabs( speed ))
    {
        const int over = state.angleLimit <= std::fabs( state.angle - state.startAngle ) ? 1 : 0;
        motion = 0.f <= speed ? over + 3 : over + 1;
    }
    StockSuspensionSoundTransition result;
    if (state.motion != motion)
    {
        result.stop = STOCK_SUSPENSION_SOUND_TRANSITIONS[state.motion][1];
        result.start = STOCK_SUSPENSION_SOUND_TRANSITIONS[motion][0];
        result.changed = true;
        state.motion = motion;
    }
    return result;
}

inline bool stockSuspensionSoundLodAllows( float lod, float setting )
{
    return lod < setting || lod == setting;
}
}
