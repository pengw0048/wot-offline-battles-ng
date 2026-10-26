#pragma once
#include "cstdmf/bw_namespace.hpp"
#include "cstdmf/bw_string_ref.hpp"

BW_BEGIN_NAMESPACE
class IFileSystem;
// Original BWResourceImpl owns a snapshot, read before the resource mounts.
int loadStockResourceContentType(IFileSystem& files, const BW::StringRef& directory);
BW_END_NAMESPACE
