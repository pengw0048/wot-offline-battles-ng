#pragma once
#include <cmath>
#include <limits>

namespace wot
{
// 006b4a07..006b4a9f: stock falling-body spring equilibrium iteration.
// Preserve every binary32 store; do not introduce convergence clamps or a
// different root finder. The original CRT sinf is a transcendental boundary.
inline float solveStockDestructibleFallPitch(float weight,float angularStiffness,
    float basePitch,float initialPitch)
{
    volatile float ratio=weight/angularStiffness;
    float previous=initialPitch,previousError=std::numeric_limits<float>::max();
    for(unsigned iteration=0;iteration<15;++iteration)
    {
        volatile float sine=std::sin(previous);
        volatile float product=sine*ratio;
        volatile float candidate=product+basePitch;
        volatile float difference=candidate-previous;
        volatile float error=difference*difference;
        if(error>previousError)return previous;
        if(0.000001f>error)return candidate;
        previousError=error;
        previous=candidate;
    }
    return previous;
}
}
