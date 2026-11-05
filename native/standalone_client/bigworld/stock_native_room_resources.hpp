#pragma once
// The current round's cold navigation and tactics handoff. No Python or engine
// object crosses into the room thread; geometry remains compact native arrays.
#include "offline_navigation.h"
#include "stock_native_client_state.hpp"
namespace wot::room::strategy {
struct GraphData;
}
namespace native_battle::room_resources {
using Value = stock_native_client_state::Value;
struct Prepared {
  Value resources;
  std::shared_ptr<const wot::room::strategy::GraphData> graph;
};
Prepared prepare(const std::string &map_name,
                 const std::map<int, std::vector<Value>> &actual_routes,
                 const offline_navigation::MapInput &,
                 const Value &graph_metadata, const Value &active_tactics,
                 const Value &immutable_archive);
// Pure admission surface used by the cold factory and differential probes.
std::string validateRoute(const offline_navigation::MapInput &,
                          const Value &graph_metadata, const Value &route);
// Once the native owner installs, move the cold cache rather than retain a
// second full worker grid. A retained const reader receives a safe copy.
inline std::optional<offline_navigation::MapInput>
takeMapInput(std::shared_ptr<offline_navigation::MapInput> &cache) {
  if (!cache)
    return {};
  auto input = std::move(cache);
  if (input.use_count() == 1)
    return std::move(*input);
  return *input;
}
} // namespace native_battle::room_resources
