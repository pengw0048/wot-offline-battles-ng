#include "stock_native_bot_combat_snapshot.hpp"
namespace wot::offline_bot_step {
bool adopt_combat_mode(offline_simulation::weapons::Store& store,offline_simulation::ActorKey key,const CombatModeSnapshot& mode){
    auto& actor=store.at(key);const bool changed=actor.gun.adopt(mode.gun_config);
    actor.aim_config=mode.aim_config;actor.has_aim=true;return changed;
}
}
