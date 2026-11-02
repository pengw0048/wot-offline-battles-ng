#pragma once

#include "cstdmf/stdmf.hpp"

BW_BEGIN_NAMESPACE
class PyAttachment;
namespace Moo { class DrawContext; }
extern int StockHangarPresentation_token;
// GUIApp::init/fini own the GPU resources; Python-visible state outlives them.
bool initialiseStockHangarPresentation();
void finaliseStockHangarPresentation();
// 006338e5: GUIApp::draw composes the background before SimpleGUI and Scaleform.
bool drawStockGUIBackground();
// 00637193: prepare the mask before the opaque scene is completed. Mode-1/2
// embodiments draw their normal colour only after their occlusion mask.
bool prepareStockEdgeDetection();
bool stockDeferEdgeAttachment( const PyAttachment *, const Moo::DrawContext & );
// 00630e77: LensApp transfers the prepared mask after the semitransparent scene.
bool drawStockEdgeDetection();
BW_END_NAMESPACE
