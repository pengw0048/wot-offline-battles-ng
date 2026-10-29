#pragma once
#include "stock_native_client_drive_presentation.hpp"
struct _object;
namespace stock_native_client_drive {
// Exact #1513 CompoundAppearance producer, implemented through raw fields and
// native controller calls. This never calls its Python business methods.
void applyTrackMode(_object* entity,EngineMode);
void applyAppearanceTrackMode(_object* appearance,EngineMode);
void applyTrackScroll(_object* appearance,double left,double right);
}
