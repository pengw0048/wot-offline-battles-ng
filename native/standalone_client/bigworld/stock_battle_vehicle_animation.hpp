#pragma once

#include "cstdmf/bw_namespace.hpp"
#include "pyscript/script_math.hpp"
#include "stock_battle_vehicle_animation_data.hpp"
#include "stock_battle_vehicle_levered_data.hpp"

BW_BEGIN_NAMESPACE
extern int StockBattleVehicleAnimation_token;

// D3DX/UCRT services of the original bodies (stock_battle_vehicle_animation_math.cpp).
const wot::StockAnimationMath & stockBattleAnimationMath();
const wot::StockLeveredMath & stockBattleLeveredMath();

// SwingingAnimator is an auto-ticked MatrixProvider (ProviderStore). The
// Svarog managers below run in the original static factory order:
// phase 2 LeveredSuspension 014932bc then LinkedNodesPitchAnimator 014932dc;
// phase 3 SuspensionSound 014932c0 before VehicleAudition 01493424.
void tickStockLeveredSuspensions( float frameTime );
void tickStockLinkedNodesPitchAnimators();
void tickStockSuspensionSounds( float frameTime );
BW_END_NAMESPACE
