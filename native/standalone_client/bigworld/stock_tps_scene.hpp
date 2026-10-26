#pragma once
#include "particle/actions/particle_system_action.hpp"
#include "moo/draw_context.hpp"

BW_BEGIN_NAMESPACE
ParticleSystemActionPtr createStockInfluxPSA();
PyParticleSystemActionPtr wrapStockInfluxPSA( ParticleSystemAction * action );
void resetStockInfluxMotion( ParticleSystemAction * action );
const BW::string & stockInfluxActionName();
bool initialiseStockTPSRuntime();
void tickStockTPSRuntime( float elapsed );
bool drawStockTPSRuntime();
void finiStockTPSRuntime();
bool isStockTPSParticleItem( Moo::DrawContext::UserDrawItem * item );
void drawStockTPSParticleItem( Moo::DrawContext::UserDrawItem * item );
void finishStockTPSParticleItems();
BW_END_NAMESPACE
