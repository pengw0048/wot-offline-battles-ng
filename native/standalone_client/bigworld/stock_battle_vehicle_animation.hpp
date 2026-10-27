#pragma once

#include "cstdmf/bw_namespace.hpp"
#include "pyscript/script_math.hpp"
#include "stock_battle_vehicle_animation_data.hpp"

BW_BEGIN_NAMESPACE
extern int StockBattleVehicleAnimation_token;

// D3DX/UCRT services of the original bodies (stock_battle_vehicle_animation_math.cpp).
const wot::StockAnimationMath & stockBattleAnimationMath();

// Current node-local transform without re-evaluating providers, as the
// original Svarog managers read the compound node array. Defined by the
// vehicle_appearance owner (private central patch).
bool stockVehicleCompoundNodeLocal( PyObject * compound, const BW::string & name, Matrix & local );

// SwingingAnimator is an auto-ticked MatrixProvider (ProviderStore). The
// Svarog managers below run in the original static factory order:
// phase 2 LeveredSuspension 014932bc then LinkedNodesPitchAnimator 014932dc;
// phase 3 SuspensionSound 014932c0 before VehicleAudition 01493424.
void tickStockLinkedNodesPitchAnimators();
void tickStockSuspensionSounds( float frameTime );
BW_END_NAMESPACE
