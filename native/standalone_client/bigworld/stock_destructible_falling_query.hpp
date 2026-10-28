#pragma once
#include "math/vector3.hpp"
BW_BEGIN_NAMESPACE
class ClientSpace;
float queryStockDestructibleFallPitch(ClientSpace& space,const Vector3& origin,float height,float yaw);
BW_END_NAMESPACE
