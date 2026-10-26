#include "pch.hpp"
#include "stock_resource_content.hpp"
#include "resmgr/file_system.hpp"
#include "resmgr/xml_section.hpp"

BW_BEGIN_NAMESPACE
int loadStockResourceContentType(IFileSystem& files, const BW::StringRef& directory)
{
    // 11a4bf0 appends the filename to its physical application directory;
    // resource overlays and archive members never participate in this lookup.
    BW::string filename(directory.data(),directory.size());
    filename += "app_type.xml";
    if (files.getFileType(filename)==IFileSystem::FT_NOT_FOUND) return 0;
    BinaryPtr bytes=files.readFile(filename);
    if (!bytes) return 0;
    DataSectionPtr protocol=XMLSection::createFromBinary("protocol",bytes);
    DataSectionPtr value=protocol?protocol->openSection("app_type"):NULL;
    if (!value) return 0;
    const BW::string name=value->asString();
    // 6aedf0 compares exact lengths and bytes. switch_to_type is not read.
    if (name=="full") return 0;
    if (name=="sd") return 1;
    if (name=="hd") return 2;
    if (name=="incomplete") return 3;
    if (name=="tutorial") return 4;
    if (name=="sandbox") return 5;
    return 0;
}
BW_END_NAMESPACE
