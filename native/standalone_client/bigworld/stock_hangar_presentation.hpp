#pragma once

#include "cstdmf/stdmf.hpp"

BW_BEGIN_NAMESPACE
extern int StockHangarPresentation_token;
// GUIApp::init/fini own the GPU resources; Python-visible state outlives them.
bool initialiseStockHangarPresentation();
void finaliseStockHangarPresentation();
// 006338e5: GUIApp::draw composes the background before SimpleGUI and Scaleform.
bool drawStockGUIBackground();
BW_END_NAMESPACE
