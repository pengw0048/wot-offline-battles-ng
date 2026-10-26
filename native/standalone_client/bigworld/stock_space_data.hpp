#pragma once
#include "space/forward_declarations.hpp"

BW_BEGIN_NAMESPACE
bool initialiseStockSpaceData();
void finaliseStockSpaceData();

// Called only after the real native space exists and its protocol SpaceTypeID
// is known. 0xffff is the original no-schema local-space branch.
bool publishStockSpaceData( SpaceID id, uint16 type );
void retireStockSpaceData( SpaceID id, const ClientSpace * expectedOwner );

// Read the actual typed Python value for a new network geometry mapping.
// Existing mappings retain their creation mask. Failure leaves mask unchanged.
bool stockSpaceItemsVisibilityMask( SpaceID id, uint32 & mask );
BW_END_NAMESPACE
