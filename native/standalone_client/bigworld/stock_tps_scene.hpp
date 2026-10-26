#pragma once
#include "particle/actions/particle_system_action.hpp"
#include "moo/draw_context.hpp"
#include <string>

BW_BEGIN_NAMESPACE
ParticleSystemActionPtr createStockInfluxPSA();
PyParticleSystemActionPtr wrapStockInfluxPSA( ParticleSystemAction * action );
void resetStockInfluxMotion( ParticleSystemAction * action );
const BW::string & stockInfluxActionName();
// Main-thread native one-shot owner used by InfluxEffect (fa51e0/fa16e0).
bool playStockTPSOneShot( const BW::string & resource, const Vector3 & position, std::string & error );
bool initialiseStockTPSRuntime();
void tickStockTPSRuntime( float elapsed );
bool drawStockTPSRuntime();
void finiStockTPSRuntime();
bool isStockTPSParticleItem( Moo::DrawContext::UserDrawItem * item );
void drawStockTPSParticleItem( Moo::DrawContext::UserDrawItem * item );
void finishStockTPSParticleItems();
BW_END_NAMESPACE
