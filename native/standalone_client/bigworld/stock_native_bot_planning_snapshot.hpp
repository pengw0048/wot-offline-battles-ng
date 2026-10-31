#pragma once
#include "stock_native_bot_navigation_runtime.hpp"
#include "stock_native_bot_motion_plan.hpp"
#include "stock_native_bot_planner.hpp"
#include "stock_native_bot_strategy.hpp"
#include "stock_native_bot_perception.hpp"
#include "stock_native_bot_parking.hpp"
#include "stock_native_bot_seed.hpp"
struct _object;

namespace stock_native_bot_planning {
struct StrategicOrder {
    stock_native_bot_planner::Order drive;
    stock_native_bot_navigation::Strategy navigation;
    std::string signature;
};
struct PlanningSnapshot {
    offline_navigation::MapInput map;
    double grade_up=0.,grade_down=0.;
    stock_native_bot_navigation::BakeProof bake;
    stock_native_bot_navigation::TimedPenalties failed_edges;
    stock_native_bot_navigation::Penalties static_hull_edges;
    std::set<stock_native_bot_navigation::Cell> review_cells;
    std::uint64_t hull_revision=0,proof_revision=0;
    std::int64_t order_revision=-1;
    stock_native_bot_strategy::Director director;
    std::map<std::int64_t,stock_native_bot_planner::ActorMemory> planner;
    stock_native_bot_planner::TrafficSnapshot traffic;
    std::map<std::int64_t,stock_native_bot_navigation::NavigationActor> actors;
    std::map<std::int64_t,stock_native_bot_navigation::ExactDescriptor> descriptors;
    std::map<std::int64_t,StrategicOrder> orders;
    std::map<std::int64_t,std::uint64_t> order_tokens;
    std::vector<stock_native_bot_perception::Actor> perception;
    std::array<std::array<int,2>,8> directions{};
    std::map<std::int64_t,stock_native_bot_parking::Catalog> parking;
    std::map<std::int64_t,stock_native_bot_parking::Plan> initial_parking;
    std::map<std::int64_t,stock_native_bot_navigation::Point> parking_origins;
    std::map<int,std::map<std::string,std::vector<std::array<double,2>>>> routes;
};
// Cold startup only: the native round start's manifest seed and the accepted server orders. No
// borrowed Python reference survives success.
bool readPlanningSnapshot(const stock_native_bot::RoundSeed & seed,const stock_native_bot::ServerOrders & orders,
    PlanningSnapshot & result);
// The round's baked grid: its validated graph with the shore-classified hazards.
bool readMapInput(const stock_native_bot::RoundSeed & seed,offline_navigation::MapInput & result);
bool readNavigationMode(const stock_native_client_state::Value & state,_object * descriptor,
    stock_native_bot_navigation::ExactDescriptor & result);
// Undamaged visibility/radio coefficients and descriptor checkpoints for one
// vehicle mode. Apply live critical vision/signal factors exactly once later.
bool readPerceptionMode(const stock_native_bot::BotSeed & bot,_object * descriptor,
    stock_native_bot_perception::Actor & result);
// The same plain parser handles accepted server order replacement at ingress;
// no Python strategy, geometry or simulation callback is invoked here.
bool readPlanningOrders(const std::map<std::int64_t,stock_native_client_state::Value> & orders,std::map<std::int64_t,StrategicOrder> & result);
}
