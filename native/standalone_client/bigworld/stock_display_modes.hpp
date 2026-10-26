#pragma once

#include "cstdmf/stdmf.hpp"
#include "cstdmf/cstdmf_windows.hpp"

BW_BEGIN_NAMESPACE
extern int StockDisplayModes_token;
void stockRememberDisplayWindow();
void stockDisplayWindowMessage( UINT message, WPARAM wParam, LPARAM lParam );
bool stockDisplayDeviceLost();
bool stockChangeVideoMode( int modeIndex, int windowMode );
void stockResizeWindow( int width, int height );
BW_END_NAMESPACE
