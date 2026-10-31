#pragma once
// The round's Bot manifest as the native round start built it (the pinned BotRuntime.battle_start
// state): the input of the native owner's cold installation. Descriptors stay #1513 handles.
#include "stock_native_client_state.hpp"
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace stock_native_bot {
// One consumable of a Bot's fixed ledger (equipment_mechanics.EquipmentState) at the round start.
struct SeedEquipment {
    std::string kind;
    std::int64_t uses_left = 0;
    double ready_at = 0.;
    bool active = false, autoactivate = false, repair_all = false;
    double cooldown_seconds = 0., auto_reaction_seconds = 0., bonus_value = 0., crew_level_increase = 0.;
    double fire_starting_chance_factor = 1., engine_power_factor = 1., turret_rotation_speed_factor = 1.;
    double engine_hp_loss_per_second = 0.;
};
// One Bot: its BotRuntime.states row and the per-Bot values the cold readers took from BotRuntime.
struct BotSeed {
    stock_native_client_state::Value state;                 // the row: pose, combat, siege, profile, route, SPG plan
    stock_native_client_state::Handle travel, siege;        // the #1513 descriptor pair; siege is empty without Siege mode
    stock_native_client_state::Point spawn{};               // the formation spawn of its (team, slot)
    double rating = .34;
    int crew_level = 100;
    std::vector<SeedEquipment> equipment;
};
// The descriptor of the Bot's installed Siege state (siege_mechanics.active_descriptor).
inline _object* activeDescriptor(const BotSeed& bot) {
    const auto& mode = bot.state.get("siege_state");
    return mode.kind == stock_native_client_state::Value::Kind::Integer && mode.integer == 2 && bot.siege ? bot.siege.get() : bot.travel.get();
}
// The accepted server orders (BotRuntime._apply_orders); they change after the manifest is built.
struct ServerOrders {
    std::map<std::int64_t, stock_native_client_state::Value> orders;
    std::map<std::int64_t, std::uint64_t> tokens;
    std::int64_t revision = -1;
};
struct RoundSeed {
    std::int64_t round = 0, generation = 0;
    double control_seconds = 0.;
    std::string map;
    stock_native_client_state::Handle graph;                // the validated baked navigation graph
    std::vector<unsigned> hazards;                          // its cell hazards after with_shore_hazards
    std::map<std::int64_t, BotSeed> bots;
    std::map<std::int64_t, stock_native_client_state::Value> agents;          // BattleDirector.agents
    std::map<int, std::vector<stock_native_client_state::Value>> routes;      // director.map_data routes per team
};
}
