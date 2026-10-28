#pragma once
#include <cmath>

namespace wot
{
template<class Update>
void stockBoidSubsteps(float remaining, Update update)
{
    if (!(remaining > 0.f && remaining < 20.f)) return;
    do
    {
        const float step = remaining > .1f ? .1f : remaining;
        remaining -= .1f;
        update(step);
    } while (remaining > 0.f);
}

struct StockBoidVector { float x, y, z; };
struct StockBoidAngles { float yaw, pitch, roll, yawDelta, speed; };

// 65ec90 adds a tangent only when the forward horizontal ray intersects
// the authored dead-zone circle. Centre Y is deliberately not consumed.
template<class Normalize>
void stockBoidAvoidDeadZone(StockBoidVector& delta, const StockBoidVector& position,
    const StockBoidVector& direction, const StockBoidVector& centre,
    float radius, Normalize normalize)
{
    if (!(radius > 0.f)) return;
    const float directionSquared = (direction.x * direction.x + direction.y * direction.y) +
        direction.z * direction.z;
    if (!(directionSquared > 0.f)) return;
    const float horizontal = std::sqrt(direction.x * direction.x + direction.z * direction.z);
    if (!(horizontal > 0.f)) return;
    const float x = centre.x - position.x, z = centre.z - position.z;
    const float left = direction.z * x, right = z * direction.x;
    const float distance = std::fabs(left - right) / horizontal;
    if (!(radius >= distance)) return;
    if (!(direction.x * x + z * direction.z > 0.f)) return;
    StockBoidVector tangent = {-direction.z, 0.f, direction.x};
    if (right - left >= 0.f) { tangent.x = -tangent.x; tangent.z = -tangent.z; }
    normalize(tangent);
    const float length = std::sqrt((delta.x * delta.x + delta.y * delta.y) + delta.z * delta.z);
    delta.x += tangent.x * length;
    delta.y += tangent.y * length;
    delta.z += tangent.z * length;
}

// 65f9f6..65fc96, after the original steering-vector producer and before
// the existing native model-transform/movement consumer. DT is in 1/30 s.
template<class Normalize>
void stockBoidSteer(StockBoidAngles& state, StockBoidVector delta,
    const StockBoidVector& direction, const StockBoidVector& yprSpeed,
    float normalSpeed, float pitchToSpeedRatio, float dt, bool landing,
    Normalize normalize)
{
    if (delta.y > .01f)
    {
        state.pitch += yprSpeed.y * dt;
        if (state.pitch > .8f) state.pitch = .8f;
    }
    else if (-.01f > delta.y)
    {
        state.pitch -= yprSpeed.y * dt;
        if (-.8f > state.pitch) state.pitch = -.8f;
    }
    else state.pitch *= .98f;
    state.speed -= pitchToSpeedRatio * state.pitch;
    state.speed = (state.speed - normalSpeed) * .99f + normalSpeed;
    if (normalSpeed * .5f > state.speed) state.speed = normalSpeed * .5f;
    if (state.speed > normalSpeed * 5.f) state.speed = normalSpeed * 5.f;
    if (!landing) delta.y = 0.f;
    normalize(delta);
    const float dot = (direction.y * delta.y + direction.x * delta.x) + direction.z * delta.z;
    float adjusted = dot;
    if (dot > .7f) adjusted -= .7f;
    const float strength = (1.f - adjusted) * .035f;
    const float cross = direction.x * delta.z - direction.z * delta.x;
    if (cross > .05f) state.yawDelta = state.yawDelta * .95f + strength * .05f;
    else if (-.05f > cross) state.yawDelta = (state.yawDelta * 19.f - strength) * .05f;
    else state.yawDelta *= .98f;
    state.yaw += (dt * state.yawDelta) * yprSpeed.x;
    const float angle = std::acos(dot);
    float target = (.7853981852531433f * angle) * .31830987334251404f;
    if (0.f > cross) target = -target;
    const float difference = target - state.roll;
    float step = yprSpeed.z * dt;
    if (step >= std::fabs(difference)) state.roll = target;
    else
    {
        if (!(difference >= 0.f)) step = -step;
        state.roll += step;
    }
    if (.1f >= angle) state.roll *= .98f;
}
}
