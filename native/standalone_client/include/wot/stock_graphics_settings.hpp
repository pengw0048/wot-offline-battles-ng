#pragma once

#include <cstdint>

namespace wot
{
struct StockGraphicsPreferencesVersion
{
    int major = 0;
    int minor = 0;
    int maintenance = 0;
    int status = 0;
};

struct StockGraphicsPreferencesMigration
{
    StockGraphicsPreferencesVersion result;
    bool detect = false;
    bool migrateShadows = false;
    bool refreshDetector = false;
};

// #1513 91d670 uses 4.2.1 as the shadow-option migration threshold and
// normalizes the stored version to 4.2.2. The persisted status is independent
// of the restart comparison performed against live graphics settings.
inline StockGraphicsPreferencesMigration stockGraphicsPreferencesMigration(
    bool present, StockGraphicsPreferencesVersion input)
{
    StockGraphicsPreferencesMigration output;
    output.result = input;
    if (!present)
    {
        output.result = {4, 2, 2, 3};
        output.detect = true;
        return output;
    }
    output.migrateShadows = input.major < 4 ||
        (input.major == 4 && (input.minor < 2 ||
            (input.minor == 2 && input.maintenance < 1)));
    if (input.major != 4)
    {
        output.result = {4, 2, 2, 1};
        output.detect = true;
    }
    else if (input.minor != 2)
    {
        output.refreshDetector = input.minor < 2;
        output.result = {4, 2, 2, 2};
    }
    else
    {
        output.refreshDetector = input.maintenance < 2;
        output.result.maintenance = 2;
    }
    return output;
}

inline std::uint32_t stockMigrateShadowOption(std::uint32_t option)
{
    // 0091ab60 is an unsigned comparison, including negative stored indices.
    return option <= 2 ? option + 1 : option;
}
}
