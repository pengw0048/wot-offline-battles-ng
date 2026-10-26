#pragma once

#include "moo/graphics_settings.hpp"

struct _object;

BW_BEGIN_NAMESPACE
extern int StockGraphicsSettings_token;

// These hooks retain the public device/renderer owners while restoring the
// original preference version, publication, Python view and status contracts.
bool prepareStockGraphicsPreferences(DataSectionPtr section,
    Moo::GraphicsSetting::SettingsVector& settings, int overrideGroup);
void writeStockGraphicsPreferencesMetadata(DataSectionPtr section);
_object* stockGraphicsSettingsView();
BW_END_NAMESPACE
