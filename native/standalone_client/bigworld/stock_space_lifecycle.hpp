#pragma once
#include "cstdmf/bw_namespace.hpp"

BW_BEGIN_NAMESPACE
extern int StockSpaceLifecycle_token;
void clearStockEntitiesAndSpaces(bool keepClientOnlySpaces);
bool stockClientSpacesOpen();
void finiStockClientSpaces();
BW_END_NAMESPACE
