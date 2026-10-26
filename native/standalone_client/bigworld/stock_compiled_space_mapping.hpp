#pragma once
#include "compiled_space/forward_declarations.hpp"
#include "resmgr/datasection.hpp"
#include "math/matrix.hpp"

BW_BEGIN_NAMESPACE
class AssetClient;
class Scene;
namespace CompiledSpace
{
class CompiledSpace;
class CompiledSpaceMapping;
CompiledSpaceMapping * createStockCompiledSpaceMapping(
    const BW::string & path, const Matrix & transform,
    const DataSectionPtr & settings, CompiledSpace & space,
    AssetClient * assetClient, uint32 visibilityMask );
void registerStockCompiledSpaceHandlers( Scene & scene );
}
BW_END_NAMESPACE
