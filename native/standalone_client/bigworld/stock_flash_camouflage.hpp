#ifndef STOCK_FLASH_CAMOUFLAGE_HPP
#define STOCK_FLASH_CAMOUFLAGE_HPP

#include <cstdint>
#include <string>

namespace wot { namespace flash_camouflage {

struct Parameters
{
    std::uint32_t width = 0, height = 0;
    std::string texture, background;
    std::uint32_t colors[4] = {};
    float weights[4] = {};
};

bool hasProtocol(const char* value);
bool parse(const char* value, Parameters& output);
std::uint32_t pixel(std::uint32_t mask, std::uint32_t background, const Parameters& params);

}} // namespace wot::flash_camouflage
#endif
