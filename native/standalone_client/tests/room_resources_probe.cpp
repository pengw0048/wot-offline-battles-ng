#include "../room/room_test_resources.hpp"
#include "../bigworld/stock_native_room_resources.hpp"
#include "../room/room_json.hpp"
#include "../room/room_strategy_graph.hpp"
#include <chrono>
#include <iostream>
using namespace wot::room;
namespace resource = native_battle::room_resources;
offline_navigation::MapInput mapInput(const Value &raw) {
  offline_navigation::MapInput m;
  m.width = static_cast<int>(intField(raw, "width"));
  m.height = static_cast<int>(intField(raw, "height"));
  m.cell_size = raw.get("cell_size").numeric();
  m.origin_x = raw.get("origin").array.at(0).numeric();
  m.origin_z = raw.get("origin").array.at(1).numeric();
  m.has_bounds = true;
  for (std::size_t n = 0; n < 4; ++n)
    m.bounds[n] = raw.get("bounds").array.at(n).numeric();
  for (const auto &v : rows(raw.get("heights_mm")))
    m.heights.push_back(v.kind == Value::Kind::Null ? NAN
                                                    : v.numeric() / 1000.);
  for (const auto &v : rows(raw.get("links")))
    m.links.push_back(static_cast<unsigned>(v.integer));
  for (const auto &v : rows(raw.get("hazards")))
    m.hazards.push_back(static_cast<unsigned>(v.integer));
  for (const auto &v : rows(raw.get("local_meshes"))) {
    offline_navigation::LocalMeshInput p;
    p.width = static_cast<int>(intField(v, "width"));
    p.height = static_cast<int>(intField(v, "height"));
    p.cell_size = v.get("cell_size").numeric();
    p.origin_x = v.get("origin").array.at(0).numeric();
    p.origin_z = v.get("origin").array.at(1).numeric();
    for (const auto &h : rows(v.get("heights_mm")))
      p.heights.push_back(h.kind == Value::Kind::Null ? NAN
                                                      : h.numeric() / 1000.);
    for (const auto &h : rows(v.get("links")))
      p.links.push_back(static_cast<unsigned>(h.integer));
    for (const auto &h : rows(v.get("hazards")))
      p.hazards.push_back(static_cast<unsigned>(h.integer));
    m.local_meshes.push_back(std::move(p));
  }
  return m;
}
int main() {
  std::string line;
  while (std::getline(std::cin, line)) {
    try {
      const auto request = parseJson(line);
      const auto map = mapInput(request.get("graph"));
      Value out = object();
      out.object["errors"] = array();
      for (const auto &route : rows(request.get("routes"))) {
        const auto error =
            resource::validateRoute(map, request.get("graph"), route);
        out.object["errors"].array.push_back(error.empty() ? Value()
                                                           : text(error));
      }
      if (has(request, "tactics")) {
        std::map<int, std::vector<Value>> actual;
        for (const auto &kv : request.get("actual_routes").object)
          actual[std::stoi(kv.first)] = rows(kv.second);
        const auto began = std::chrono::steady_clock::now();
        const auto prepared =
            resource::prepare(request.get("graph").get("map").text, actual, map,
                              testResources(request.get("graph")), testResources(request.get("tactics")),
                              testResources(request.get("archive")));
        out.object["prepare_us"] =
            integer(std::chrono::duration_cast<std::chrono::microseconds>(
                        std::chrono::steady_clock::now() - began)
                        .count());
        out.object["resources"] = prepared.resources;
        out.object["graph"] = object();
        auto &g = out.object["graph"];
        g.object["width"] = integer(prepared.graph->width);
        g.object["height"] = integer(prepared.graph->height);
        g.object["cell"] = number(prepared.graph->cell);
        g.object["heights_mm"] = array();
        for (const auto h : prepared.graph->heights_mm)
          g.object["heights_mm"].array.push_back(
              h == wot::room::strategy::GraphData::missing ? Value()
                                                           : integer(h));
        g.object["bytes"] = integer(static_cast<std::int64_t>(
            prepared.graph->heights_mm.size() * sizeof(std::int32_t) +
            prepared.graph->links.size() + prepared.graph->hazards.size()));
      }
      auto cache = std::make_shared<offline_navigation::MapInput>(map);
      const auto *storage = cache->heights.data();
      const auto taken = resource::takeMapInput(cache);
      if (!taken)
        throw std::logic_error("Missing cold cache result");
      out.object["cache_move"] =
          boolean(!cache && taken->heights.data() == storage);
      cache = std::make_shared<offline_navigation::MapInput>(map);
      std::shared_ptr<const offline_navigation::MapInput> reader = cache;
      const auto copied = resource::takeMapInput(cache);
      out.object["cache_retained"] =
          boolean(!cache && copied &&
                  copied->heights.data() != reader->heights.data() &&
                  copied->heights.size() == reader->heights.size());
      out.object["cache_empty"] = boolean(!resource::takeMapInput(cache));
      std::cout << writeJson(out) << '\n';
    } catch (const std::exception &e) {
      Value error = object();
      error.object["error"] = text(e.what());
      std::cout << writeJson(error) << '\n';
    }
  }
}
