#pragma once

#include "resmgr/datasection.hpp"

BW_BEGIN_NAMESPACE
extern int StockPreferencesCallbacks_token;
void stockRecordPreferencesLoaded(DataSectionPtr root);
void stockNotifySavePreferences(DataSectionPtr root);
void stockNotifyReadPreferences(int windowMode);
void finiStockPreferencesCallbacks();
BW_END_NAMESPACE
