// Host stand-ins for the D3DX/UCRT entry points of the battle animation
// kernels. The x86 oracle loads these same functions through ctypes, so a
// comparison tests the original instruction sequence, not D3DX itself.
#pragma once
#include "stock_battle_vehicle_animation_data.hpp"
#include <cmath>
#include <cstring>

extern "C" {
inline void refTransform(float* out, const float* v, const float* m) {
    for (int c = 0; c < 4; ++c) out[c] = v[0] * m[c] + v[1] * m[4 + c] + v[2] * m[8 + c] + m[12 + c];
}
inline void refTransformNormal(float* out, const float* v, const float* m) {
    float r[3];
    for (int c = 0; c < 3; ++c) r[c] = v[0] * m[c] + v[1] * m[4 + c] + v[2] * m[8 + c];
    std::memcpy(out, r, sizeof(r));
}
inline void refNormalize(float* out, const float* v) {
    const float length = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    float r[3] = {0.f, 0.f, 0.f};
    if (length != 0.f) for (int i = 0; i < 3; ++i) r[i] = v[i] / length;
    std::memcpy(out, r, sizeof(r));
}
inline void refMultiply(float* out, const float* a, const float* b) {
    float r[16];
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            r[i * 4 + j] = a[i * 4] * b[j] + a[i * 4 + 1] * b[4 + j] + a[i * 4 + 2] * b[8 + j] + a[i * 4 + 3] * b[12 + j];
    std::memcpy(out, r, sizeof(r));
}
// Gauss-Jordan with partial pivoting; a singular input is left unchanged.
inline void refInverse(float* m) {
    float a[4][8];
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 8; ++j) a[i][j] = j < 4 ? m[i * 4 + j] : (j - 4 == i ? 1.f : 0.f);
    for (int c = 0; c < 4; ++c) {
        int p = c;
        for (int r = c + 1; r < 4; ++r) if (std::fabs(a[r][c]) > std::fabs(a[p][c])) p = r;
        if (a[p][c] == 0.f) return;
        if (p != c) for (int j = 0; j < 8; ++j) { float t = a[p][j]; a[p][j] = a[c][j]; a[c][j] = t; }
        const float pivot = a[c][c];
        for (int j = 0; j < 8; ++j) a[c][j] = a[c][j] / pivot;
        for (int r = 0; r < 4; ++r) {
            if (r == c) continue;
            const float f = a[r][c];
            for (int j = 0; j < 8; ++j) a[r][j] = a[r][j] - f * a[c][j];
        }
    }
    for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j) m[i * 4 + j] = a[i][4 + j];
}
inline float refSin(float x) { return static_cast<float>(std::sin(static_cast<double>(x))); }
inline float refCos(float x) { return static_cast<float>(std::cos(static_cast<double>(x))); }
inline void refRotationYawPitchRoll(float* out, float yaw, float pitch, float roll) {
    const float sy = refSin(yaw), cy = refCos(yaw), sp = refSin(pitch), cp = refCos(pitch);
    const float sr = refSin(roll), cr = refCos(roll);
    const float r[16] = {
        cr * cy + sr * sp * sy, sr * cp, sr * sp * cy - cr * sy, 0.f,
        cr * sp * sy - sr * cy, cr * cp, sr * sy + cr * sp * cy, 0.f,
        cp * sy, -sp, cp * cy, 0.f,
        0.f, 0.f, 0.f, 1.f};
    std::memcpy(out, r, sizeof(r));
}
inline void refRotationX(float* out, float angle) {
    const float s = refSin(angle), c = refCos(angle);
    const float r[16] = {1.f, 0.f, 0.f, 0.f, 0.f, c, s, 0.f, 0.f, -s, c, 0.f, 0.f, 0.f, 0.f, 1.f};
    std::memcpy(out, r, sizeof(r));
}
inline float refAsin(float x) { return static_cast<float>(std::asin(static_cast<double>(x))); }
}

inline const wot::StockAnimationMath & referenceAnimationMath() {
    struct Bridge {
        static void transform(float out[4], const wot::StockAnimationVector& v, const wot::StockAnimationMatrix& m) {
            refTransform(out, v.data(), m.data()); }
        static wot::StockAnimationVector normal(const wot::StockAnimationVector& v, const wot::StockAnimationMatrix& m) {
            wot::StockAnimationVector r; refTransformNormal(r.data(), v.data(), m.data()); return r; }
        static void inverse(wot::StockAnimationMatrix& m) { refInverse(m.data()); }
        static wot::StockAnimationVector normalize(const wot::StockAnimationVector& v) {
            wot::StockAnimationVector r; refNormalize(r.data(), v.data()); return r; }
        static wot::StockAnimationMatrix ypr(float y, float p, float r) {
            wot::StockAnimationMatrix m; refRotationYawPitchRoll(m.data(), y, p, r); return m; }
        static wot::StockAnimationMatrix rx(float a) { wot::StockAnimationMatrix m; refRotationX(m.data(), a); return m; }
        static wot::StockAnimationMatrix multiply(const wot::StockAnimationMatrix& a, const wot::StockAnimationMatrix& b) {
            wot::StockAnimationMatrix m; refMultiply(m.data(), a.data(), b.data()); return m; }
    };
    static const wot::StockAnimationMath math = {&Bridge::transform, &Bridge::normal, &Bridge::inverse,
        &Bridge::normalize, &Bridge::ypr, &Bridge::rx, &Bridge::multiply, &refAsin};
    return math;
}
