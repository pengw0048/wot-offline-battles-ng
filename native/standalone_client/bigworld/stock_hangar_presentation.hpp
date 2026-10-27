#pragma once

#include "cstdmf/stdmf.hpp"

BW_BEGIN_NAMESPACE
extern int StockHangarPresentation_token;
// GUIApp::init/fini own the GPU resources; Python-visible state outlives them.
bool initialiseStockHangarPresentation();
void finaliseStockHangarPresentation();
// 006338e5: GUIApp::draw composes the background before SimpleGUI and Scaleform.
bool drawStockGUIBackground();
// 00630e2b/00630e77: LensApp draws the edge z-pass and transfer after the
// semitransparent scene.
bool drawStockEdgeDetection();
BW_END_NAMESPACE
