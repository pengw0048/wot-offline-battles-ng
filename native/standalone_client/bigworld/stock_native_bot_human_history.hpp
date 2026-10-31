#pragma once
struct _object;
namespace native_battle {struct Battle;}
namespace stock_native_bot {
// Returns a new list of filtered, decorated plain-data player dictionaries. `profiles` maps a Bot id
// to its static ram profile (mass, collision_shape, ram_profile, vehicle, team). A receipt naming a past
// snapshot revision takes its Bot body from the world's ram history (world::ramBotState).
_object* prepareHumanInput(_object* raw, const native_battle::Battle& battle, _object* profiles);
}
