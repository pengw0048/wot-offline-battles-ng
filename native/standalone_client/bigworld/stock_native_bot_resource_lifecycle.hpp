#pragma once
#include <cstdint>
#include <vector>
struct _object;
namespace stock_native_bot {
// Pointer identities are comparison tokens only; the stamp never owns or
// dereferences them. Resource owners are retained by the installed geometry.
struct ResourceStamp {
    std::vector<const void*> owners;
    std::vector<bool> flags;
    bool operator==(const ResourceStamp& other) const {
        return owners==other.owners && flags==other.flags;
    }
    bool operator!=(const ResourceStamp& other) const { return !(*this==other); }
};
// Returns a new reference, including Py_None when no entity exists.
_object* readNativeEntity(_object* battle,std::int64_t engine_id);
// Bypass the Python Vehicle attribute hook while retaining its stored overlay.
// The exact #1513 turret-detachment property is evaluated from its two fields.
_object* readNativeEntityField(_object* battle,_object* entity,const char* name);
// Missing entities, descriptors and unloaded resources yield empty tokens.
bool readResourceStamp(_object* entity,const std::vector<_object*>& descriptors,ResourceStamp&,
                      _object* battle=nullptr);
}
