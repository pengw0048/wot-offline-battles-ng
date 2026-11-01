#pragma once

#include <array>
#include <cmath>

namespace wot {

// #1513 a6f330: one HDR tonemapping profile, starting from the original defaults.
struct StockToneMappingProfile {
    float eyeDarkLimit = 0.05f;
    float eyeLightLimit = 1.5f;
    float middleGray = 0.5f;
    float whitePoint = 5.f;
    float blackPoint = 0.15f;
};

// a729d0 g_params: middle gray, white point and black point of the active profile.
inline std::array<float, 4> stock_tonemapping_params(const StockToneMappingProfile& profile) {
    return {{profile.middleGray, profile.whitePoint, profile.blackPoint, 0.f}};
}

// a72a33..a72a7f g_reinhardParams.
inline std::array<float, 4> stock_reinhard_params(const StockToneMappingProfile& profile) {
    const float black = std::pow(2.f, -1.f / profile.blackPoint);
    return {{black, (profile.whitePoint * 0.1f + 1.f) / (1.f - black), 0.f, 0.f}};
}

// a6eeb0 indexes the three loaded profiles; contain an authored index outside them.
inline int stock_active_tonemapping(int active) { return active >= 0 && active < 3 ? active : 1; }

} // namespace wot
