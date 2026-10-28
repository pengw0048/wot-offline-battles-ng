// Host stand-ins for the D3DX/UCRT entry points of 007348e0. The x86 oracle
// loads these same functions, so comparisons test the original sequence.
#pragma once
#include "stock_timed_warplane_motor_data.hpp"
#include "stock_battle_vehicle_animation_reference.hpp"
#include <cmath>
#include <cstring>

extern "C" {
inline void refLookAtLH(float* out, const float* eye, const float* at, const float* up) {
    float z[3] = {at[0] - eye[0], at[1] - eye[1], at[2] - eye[2]};
    refNormalize(z, z);
    float x[3] = {up[1] * z[2] - up[2] * z[1], up[2] * z[0] - up[0] * z[2], up[0] * z[1] - up[1] * z[0]};
    refNormalize(x, x);
    const float y[3] = {z[1] * x[2] - z[2] * x[1], z[2] * x[0] - z[0] * x[2], z[0] * x[1] - z[1] * x[0]};
    const float r[16] = {x[0], y[0], z[0], 0.f, x[1], y[1], z[1], 0.f, x[2], y[2], z[2], 0.f,
        -(x[0] * eye[0] + x[1] * eye[1] + x[2] * eye[2]), -(y[0] * eye[0] + y[1] * eye[1] + y[2] * eye[2]),
        -(z[0] * eye[0] + z[1] * eye[1] + z[2] * eye[2]), 1.f};
    std::memcpy(out, r, sizeof(r));
}
inline void refNormalize2(float* out, const float* v) {
    const float length = std::sqrt(v[0] * v[0] + v[1] * v[1]);
    float r[2] = {0.f, 0.f};
    if (length != 0.f) { r[0] = v[0] / length; r[1] = v[1] / length; }
    std::memcpy(out, r, sizeof(r));
}
inline float refAtan(float x) { return static_cast<float>(std::atan(static_cast<double>(x))); }
inline void refRotationZ(float* out, float angle) {
    const float s = refSin(angle), c = refCos(angle);
    const float r[16] = {c, s, 0.f, 0.f, -s, c, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 1.f};
    std::memcpy(out, r, sizeof(r));
}
}

inline const wot::StockWarplaneMath & referenceWarplaneMath() {
    struct Bridge {
        static wot::StockWarplaneMatrix look(const wot::StockWarplaneVector& e, const wot::StockWarplaneVector& a,
            const wot::StockWarplaneVector& u) { wot::StockWarplaneMatrix m; refLookAtLH(m.data(), e.data(), a.data(), u.data()); return m; }
        static void inverse(wot::StockWarplaneMatrix& m) { refInverse(m.data()); }
        static std::array<float, 2> normalize2(float x, float y) {
            const float v[2] = {x, y}; std::array<float, 2> r; refNormalize2(r.data(), v); return r; }
        static wot::StockWarplaneMatrix rz(float a) { wot::StockWarplaneMatrix m; refRotationZ(m.data(), a); return m; }
        static wot::StockWarplaneMatrix multiply(const wot::StockWarplaneMatrix& a, const wot::StockWarplaneMatrix& b) {
            wot::StockWarplaneMatrix m; refMultiply(m.data(), a.data(), b.data()); return m; }
    };
    static const wot::StockWarplaneMath math = {&Bridge::look, &Bridge::inverse, &Bridge::normalize2, &refAtan,
        &Bridge::rz, &Bridge::multiply};
    return math;
}
