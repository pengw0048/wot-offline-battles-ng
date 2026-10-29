#pragma once
struct _object;
namespace stock_native_bot {
// Returns a new list of filtered, decorated plain-data player dictionaries.
_object* prepareHumanInput(_object* raw, _object* battle, _object* bot_runtime);
}
