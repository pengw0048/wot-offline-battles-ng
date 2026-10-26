#pragma once
#include "cstdmf/bw_namespace.hpp"
#include "cstdmf/bw_string.hpp"
#include <string>

BW_BEGIN_NAMESPACE
class Vector2;
class Vector3;
bool initialiseStockDynamicDecals();
void finaliseStockDynamicDecals();
void tickStockDynamicDecals(float elapsed);
void setStockDynamicDecalQuality(int option);
bool drawStockDynamicDecals();
float stockDynamicDecalGroupLifetime(const BW::string& name);
bool addStockDynamicDecal(float lifetime, const Vector3& start, const Vector3& end,
    const Vector2& size, float yaw, const BW::string& diffuse,
    const BW::string& normal, const BW::string& specular,
    bool useSimpleCollision, std::string& error);
BW_END_NAMESPACE
