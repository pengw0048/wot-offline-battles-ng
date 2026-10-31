#pragma once
struct _object;
namespace stock_native_bot {
// Returns a new list of filtered, decorated plain-data player dictionaries. `profiles` maps a Bot id
// to its static ram profile (mass, collision_shape, ram_profile, vehicle, team).
_object* prepareHumanInput(_object* raw, _object* battle, _object* profiles);
}
