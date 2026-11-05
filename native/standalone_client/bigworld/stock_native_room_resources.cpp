#include "stock_native_room_resources.hpp"
#include "../room/room_strategy_graph.hpp"
#include <algorithm>
#include <cmath>
#include <map>
#include <queue>
#include <set>
#include <stdexcept>
#include <tuple>
namespace native_battle::room_resources {
namespace {
using namespace wot::room;
using Cell = offline_navigation::Cell;
using Point = offline_navigation::Point;
using MapInput = offline_navigation::MapInput;
using K = Value::Kind;
const std::array<Cell, 8> directions{
    {{-1, -1}, {0, -1}, {1, -1}, {-1, 0}, {1, 0}, {-1, 1}, {0, 1}, {1, 1}}};
const std::vector<std::string> classes{"lightTank", "mediumTank", "heavyTank",
                                       "AT-SPG", "SPG"};
void require(bool okay, const char *why) {
  if (!okay)
    throw std::invalid_argument(why);
}
std::string string(const Value &value, const char *key,
                   const std::string &fallback = {}) {
  const auto &v = value.get(key).resolved();
  return v.kind == K::Text ? v.text : fallback;
}
double scalar(const Value &v) {
  const auto &r = v.resolved();
  require(r.kind == K::Number || r.kind == K::Integer,
          "Cold route coordinate is not numeric");
  const auto n = r.numeric();
  require(std::isfinite(n), "Cold route coordinate is not finite");
  return n;
}
Cell direction(const Value &value) {
  const auto &row = rows(value);
  require(row.size() == 2, "Invalid cold route direction width");
  Cell out{};
  for (std::size_t i = 0; i < 2; ++i) {
    const double n = scalar(row[i]);
    require(n >= -1. && n <= 1. && std::floor(n) == n,
            "Invalid cold route direction component");
    out[i] = static_cast<int>(n);
  }
  require(out[0] != 0 || out[1] != 0, "Empty cold route direction");
  return out;
}
Point point(const Value &v) {
  const auto &r = rows(v);
  require(r.size() >= 2, "Cold route point is incomplete");
  return {scalar(r[0]), 0., scalar(r[1])};
}
std::array<double, 4> bounds(const MapInput &map, const Value &meta) {
  const auto &r = rows(meta.get("bounds"));
  if (r.empty()) {
    require(map.has_bounds, "Cold route bounds are missing");
    return map.bounds;
  }
  require(r.size() == 4, "Cold route bounds width is invalid");
  std::array<double, 4> out;
  for (std::size_t i = 0; i < 4; ++i)
    out[i] = scalar(r[i]);
  require(out[0] < out[2] && out[1] < out[3], "Cold route bounds are empty");
  return out;
}
bool inside(const Point &p, const std::array<double, 4> &b) {
  return p[0] >= b[0] && p[0] <= b[2] && p[2] >= b[1] && p[2] <= b[3];
}
// Additive keys preserve TerrainGrid/LocalMeshes identity and directed links.
// Coarse heights remain borrowed; only each local patch's key/edge index is
// built.
struct LocalGraph {
  const MapInput &map;
  std::array<double, 4> limits;
  std::vector<std::vector<Cell>> keys;
  std::map<Cell, Point> extra;
  std::map<Cell, std::set<Cell>> edges;
  explicit LocalGraph(const MapInput &m, const Value &meta)
      : map(m), limits(bounds(m, meta)) {
    for (std::size_t n = 0; n < map.local_meshes.size(); ++n) {
      const auto &patch = map.local_meshes[n];
      require(patch.width > 0 && patch.height > 0 && patch.cell_size > 0.,
              "Invalid additive route mesh");
      const auto count = static_cast<std::size_t>(patch.width) *
                         static_cast<std::size_t>(patch.height);
      require(patch.heights.size() == count && patch.links.size() == count &&
                  patch.hazards.size() == count,
              "Incomplete additive route mesh");
      std::vector<Cell> layer;
      layer.reserve(count);
      for (std::size_t i = 0; i < count; ++i) {
        Point p{
            patch.origin_x +
                static_cast<double>(i % static_cast<std::size_t>(patch.width)) *
                    patch.cell_size,
            patch.heights[i],
            patch.origin_z +
                static_cast<double>(i / static_cast<std::size_t>(patch.width)) *
                    patch.cell_size};
        auto key = coarse(p);
        const auto base = place(key);
        if (!(std::isfinite(p[1]) && valid(key) &&
              std::abs(base[0] - p[0]) < 1.e-6 &&
              std::abs(base[2] - p[2]) < 1.e-6 &&
              std::abs(height(key) - p[1]) < .01)) {
          key = {-static_cast<int>(n) - 1, static_cast<int>(i)};
          p[1] = std::isfinite(p[1]) ? p[1] : 0.;
          extra[key] = p;
        }
        layer.push_back(key);
      }
      keys.push_back(std::move(layer));
      for (std::size_t i = 0; i < count; ++i)
        if (std::isfinite(patch.heights[i]))
          for (std::size_t bit = 0; bit < 8; ++bit)
            if (patch.links[i] & (1u << bit)) {
              const int x = static_cast<int>(
                                i % static_cast<std::size_t>(patch.width)) +
                            directions[bit][0],
                        z = static_cast<int>(
                                i / static_cast<std::size_t>(patch.width)) +
                            directions[bit][1];
              require(x >= 0 && x < patch.width && z >= 0 && z < patch.height,
                      "Additive route edge leaves mesh");
              const auto next = static_cast<std::size_t>(z) *
                                    static_cast<std::size_t>(patch.width) +
                                static_cast<std::size_t>(x);
              require(std::isfinite(patch.heights[next]),
                      "Additive route edge has no support");
              edges[keys[n][i]].insert(keys[n][next]);
            }
    }
  }
  Cell coarse(const Point &p) const {
    return {static_cast<int>(
                std::floor((p[0] - map.origin_x) / map.cell_size + .5)),
            static_cast<int>(
                std::floor((p[2] - map.origin_z) / map.cell_size + .5))};
  }
  bool flat(Cell c) const {
    return c[0] < 0 ? extra.count(c) != 0
                    : c[0] < map.width && c[1] >= 0 && c[1] < map.height;
  }
  double height(Cell c) const {
    if (c[0] < 0) {
      const auto n = static_cast<std::size_t>(-c[0] - 1);
      return n < map.local_meshes.size() && c[1] >= 0 &&
                     static_cast<std::size_t>(c[1]) <
                         map.local_meshes[n].heights.size()
                 ? map.local_meshes[n].heights[static_cast<std::size_t>(c[1])]
                 : NAN;
    }
    return flat(c) ? map.heights[static_cast<std::size_t>(c[1]) *
                                     static_cast<std::size_t>(map.width) +
                                 static_cast<std::size_t>(c[0])]
                   : NAN;
  }
  bool valid(Cell c) const { return flat(c) && std::isfinite(height(c)); }
  unsigned hazards(Cell c) const {
    return c[0] < 0 ? map.local_meshes[static_cast<std::size_t>(-c[0] - 1)]
                          .hazards[static_cast<std::size_t>(c[1])]
                    : map.hazards[static_cast<std::size_t>(c[1]) *
                                      static_cast<std::size_t>(map.width) +
                                  static_cast<std::size_t>(c[0])];
  }
  Point place(Cell c) const {
    const auto found = extra.find(c);
    if (found != extra.end())
      return found->second;
    return {map.origin_x + c[0] * map.cell_size, 0.,
            map.origin_z + c[1] * map.cell_size};
  }
  bool supported(Cell c) const {
    return valid(c) && !(hazards(c) & 15u) && inside(place(c), limits);
  }
  std::optional<Cell> local(const Point &p) const {
    for (std::size_t n = 0; n < map.local_meshes.size(); ++n) {
      const auto &m = map.local_meshes[n];
      const double x = (p[0] - m.origin_x) / m.cell_size,
                   z = (p[2] - m.origin_z) / m.cell_size;
      if (x >= 0. && z >= 0. && x <= m.width - 1 && z <= m.height - 1)
        return keys[n][static_cast<std::size_t>(std::floor(z + .5)) *
                           static_cast<std::size_t>(m.width) +
                       static_cast<std::size_t>(std::floor(x + .5))];
    }
    return {};
  }
  std::optional<Cell> nearestLocal(Cell c, int radius) const {
    const auto n = static_cast<std::size_t>(-c[0] - 1);
    const auto &m = map.local_meshes[n];
    const int reach = static_cast<int>(
                  std::ceil(std::max(0, radius) * map.cell_size / m.cell_size)),
              x = c[1] % m.width, z = c[1] / m.width;
    std::optional<std::pair<int, Cell>> best;
    for (int dz = -reach; dz <= reach; ++dz)
      for (int dx = -reach; dx <= reach; ++dx)
        if (x + dx >= 0 && x + dx < m.width && z + dz >= 0 &&
            z + dz < m.height) {
          const auto key = keys[n][static_cast<std::size_t>(z + dz) *
                                       static_cast<std::size_t>(m.width) +
                                   static_cast<std::size_t>(x + dx)];
          if (valid(key)) {
            const auto score = std::make_pair(dx * dx + dz * dz, key);
            if (!best || score < *best)
              best = score;
          }
        }
    return best ? std::optional<Cell>(best->second) : std::nullopt;
  }
  std::optional<Cell> nearest(Cell c, int radius) const {
    if (valid(c))
      return c;
    if (extra.count(c))
      return nearestLocal(c, radius);
    if (const auto at = local(place(c))) {
      if (valid(*at))
        return at;
      if (extra.count(*at)) {
        const auto candidate = nearestLocal(*at, radius);
        if (candidate)
          return candidate;
      }
    }
    for (int reach = 1; reach <= radius; ++reach) {
      std::optional<Cell> best;
      int best_distance = 0;
      for (int z = c[1] - reach; z <= c[1] + reach; ++z)
        for (int x = c[0] - reach; x <= c[0] + reach; ++x) {
          if (std::max(std::abs(x - c[0]), std::abs(z - c[1])) != reach ||
              !valid({x, z}))
            continue;
          const int score = (x - c[0]) * (x - c[0]) + (z - c[1]) * (z - c[1]);
          if (!best || score < best_distance) {
            best = Cell{x, z};
            best_distance = score;
          }
        }
      if (best)
        return best;
    }
    return {};
  }
  std::vector<Cell> neighbours(Cell c) const {
    std::vector<Cell> out;
    const auto extra_edges = edges.find(c);
    if (extra_edges != edges.end())
      out.insert(out.end(), extra_edges->second.begin(),
                 extra_edges->second.end());
    if (c[0] >= 0) {
      const auto mask = map.links[static_cast<std::size_t>(c[1]) *
                                      static_cast<std::size_t>(map.width) +
                                  static_cast<std::size_t>(c[0])];
      for (std::size_t bit = 0; bit < 8; ++bit)
        if (mask & (1u << bit)) {
          Cell next{c[0] + directions[bit][0], c[1] + directions[bit][1]};
          if (valid(next))
            out.push_back(next);
        }
    }
    return out;
  }
};
struct Validator {
  const MapInput &map;
  const Value &meta;
  std::array<double, 4> limits;
  std::unique_ptr<LocalGraph> local;
  std::array<Cell, 8> dirs{};
  std::map<std::pair<Cell, Cell>, bool> cache;
  explicit Validator(const MapInput &m, const Value &metadata)
      : map(m), meta(metadata), limits(bounds(m, metadata)) {
    require(m.width > 0 && m.height > 0 && m.cell_size > 0. &&
                std::isfinite(m.cell_size),
            "Invalid cold route graph dimensions");
    const auto count =
        static_cast<std::size_t>(m.width) * static_cast<std::size_t>(m.height);
    require(m.heights.size() == count && m.links.size() == count &&
                m.hazards.size() == count,
            "Incomplete cold route graph");
    const auto &raw = rows(meta.get("directions"));
    require(raw.size() == 8, "Missing cold route directions");
    std::set<Cell> seen;
    for (std::size_t i = 0; i < 8; ++i) {
      require(rows(raw[i]).size() == 2, "Invalid cold route direction");
      dirs[i] = direction(raw[i]);
      require(std::max(std::abs(dirs[i][0]), std::abs(dirs[i][1])) == 1 &&
                  seen.insert(dirs[i]).second,
              "Invalid cold route direction set");
    }
    if (!m.local_meshes.empty())
      local = std::make_unique<LocalGraph>(m, meta);
  }
  bool supported(Cell c) const {
    if (local)
      return local->supported(c);
    if (c[0] < 0 || c[0] >= map.width || c[1] < 0 || c[1] >= map.height)
      return false;
    const auto i =
        static_cast<std::size_t>(c[1]) * static_cast<std::size_t>(map.width) +
        static_cast<std::size_t>(c[0]);
    return std::isfinite(map.heights[i]) && !(map.hazards[i] & 15u) &&
           inside(place(c), limits);
  }
  Point place(Cell c) const {
    return local ? local->place(c)
                 : Point{map.origin_x + c[0] * map.cell_size, 0.,
                         map.origin_z + c[1] * map.cell_size};
  }
  std::optional<Cell> direct(const Point &p) const {
    if (!inside(p, limits))
      return {};
    const auto c = local ? local->local(p).value_or(local->coarse(p))
                         : Cell{static_cast<int>(std::nearbyint(
                                    (p[0] - map.origin_x) / map.cell_size)),
                                static_cast<int>(std::nearbyint(
                                    (p[2] - map.origin_z) / map.cell_size))};
    return supported(c) ? std::optional<Cell>(c) : std::nullopt;
  }
  std::optional<Cell> target(const Value &v, const Value &route,
                             std::optional<std::size_t> index) {
    const auto p = point(v);
    if (const auto c = direct(p))
      return c;
    if (local && !inside(p, limits))
      return {};
    const auto &bases = rows(meta.get("objective_bases")),
               &radii = rows(meta.get("objective_base_radii"));
    if (bases.size() != 2 || radii.size() != 2)
      return {};
    const auto team = intField(route, "team");
    if (team != 1 && team != 2)
      return {};
    int side = -1;
    if (index && *index + 1 == rows(route.get("points")).size())
      side = static_cast<int>(2 - team);
    else if (index && *index == 0 && string(route, "purpose") == "spg_attack")
      side = static_cast<int>(team - 1);
    if (side < 0)
      return {};
    const auto base = point(bases[static_cast<std::size_t>(side)]);
    const auto radius = scalar(radii[static_cast<std::size_t>(side)]);
    if (std::hypot(p[0] - base[0], p[2] - base[2]) > radius)
      return {};
    if (local) {
      const auto c = local->local(p).value_or(local->coarse(p));
      const auto candidate = local->nearest(c, 3);
      if (candidate && supported(*candidate)) {
        const auto q = place(*candidate);
        if (std::hypot(q[0] - base[0], q[2] - base[2]) <= radius)
          return candidate;
      }
      return {};
    }
    const int col = static_cast<int>(
                  std::nearbyint((p[0] - map.origin_x) / map.cell_size)),
              row = static_cast<int>(
                  std::nearbyint((p[2] - map.origin_z) / map.cell_size));
    std::optional<std::pair<double, int>> best;
    for (int z = std::max(0, row - 3); z < std::min(map.height, row + 4); ++z)
      for (int x = std::max(0, col - 3); x < std::min(map.width, col + 4); ++x)
        if (supported({x, z})) {
          const auto q = place({x, z});
          if (std::hypot(q[0] - base[0], q[2] - base[2]) <= radius) {
            const auto score = std::make_pair((q[0] - p[0]) * (q[0] - p[0]) +
                                                  (q[2] - p[2]) * (q[2] - p[2]),
                                              z * map.width + x);
            if (!best || score < *best)
              best = score;
          }
        }
    return best ? std::optional<Cell>(
                      Cell{best->second % map.width, best->second / map.width})
                : std::nullopt;
  }
  bool reachable(Cell start, Cell goal) {
    const auto key = std::make_pair(start, goal);
    const auto known = cache.find(key);
    if (known != cache.end())
      return known->second;
    std::set<Cell> seen{start};
    std::vector<Cell> todo{start};
    for (std::size_t cursor = 0; cursor < todo.size(); ++cursor) {
      const auto cell = todo[cursor];
      if (cell == goal)
        return cache[key] = true;
      std::vector<Cell> next;
      if (local)
        next = local->neighbours(cell);
      else {
        const auto mask = map.links[static_cast<std::size_t>(cell[1]) *
                                        static_cast<std::size_t>(map.width) +
                                    static_cast<std::size_t>(cell[0])];
        for (std::size_t bit = 0; bit < 8; ++bit)
          if (mask & (1u << bit))
            next.push_back({cell[0] + dirs[bit][0], cell[1] + dirs[bit][1]});
      }
      for (const auto c : next)
        if (supported(c) && seen.insert(c).second)
          todo.push_back(c);
    }
    return cache[key] = false;
  }
  std::string validate(const Value &route) {
    const auto &points = rows(route.get("points"));
    std::optional<Cell> previous;
    for (std::size_t i = 0; i < points.size(); ++i) {
      const auto current = target(points[i], route, i);
      if (!current)
        return "waypoint_unusable";
      if (previous && !reachable(*previous, *current))
        return "waypoints_disconnected";
      const auto &p = rows(points[i]);
      const std::vector<Value> empty;
      const auto &waits = p.size() > 4 ? rows(p[4]) : empty;
      for (const auto &wait : waits) {
        const auto parked =
            local ? target(wait, route, {}) : direct(point(wait));
        if (!parked)
          return "wait_place_unusable";
        if (!reachable(*current, *parked))
          return "wait_place_disconnected";
        if (local && i + 1 < points.size()) {
          const auto following = target(points[i + 1], route, i + 1);
          if (following && !reachable(*parked, *following))
            return "wait_exit_disconnected";
        }
      }
      previous = current;
    }
    return {};
  }
};
std::string routeId(const Value &row) {
  const auto tag = string(row, "class_tag", "all"), id = string(row, "id");
  if (tag == "all")
    return id;
  const auto found = std::find(classes.begin(), classes.end(), tag);
  require(found != classes.end(), "Invalid cold route class");
  const std::array<const char *, 5> codes{{"lt", "mt", "ht", "td", "spg"}};
  return "class_" +
         std::string(codes[static_cast<std::size_t>(found - classes.begin())]) +
         "_" + id;
}
Value routeConfig(const Value &raw) {
  Value out = raw.resolved();
  const auto tag = string(raw, "class_tag", "all");
  out.object["id"] = text(routeId(raw));
  out.object["default"] = boolean(true);
  out.object["source_id"] = raw.get("id");
  out.object["classes"] = array();
  for (const auto &name : classes)
    if (tag == "all" || tag == name)
      out.object["classes"].array.push_back(text(name));
  out.object["slots"] = array();
  out.object["policy"] = text("preferred");
  return out;
}
Value classRoute(const Value &row, const std::string &tag) {
  Value route = object();
  route.object["id"] = text(routeId(row));
  route.object["capacity"] = integer(15);
  route.object["class_weights"] = object();
  route.object["class_weights"].object[tag] = number(1.);
  route.object["waypoints"] = array();
  for (const auto &p : rows(row.get("points"))) {
    const auto q = point(p);
    auto value = object();
    value.object["x"] = number(q[0]);
    value.object["y"] = number(0.);
    value.object["z"] = number(q[2]);
    route.object["waypoints"].array.push_back(std::move(value));
  }
  return route;
}
std::shared_ptr<const strategy::GraphData> compactGraph(const MapInput &map,
                                                        const Value &meta) {
  auto out = std::make_shared<strategy::GraphData>();
  const auto count = static_cast<std::size_t>(map.width) *
                     static_cast<std::size_t>(map.height);
  require(map.width > 0 && map.height > 0 && map.width <= 4096 &&
              map.height <= 4096 && count <= 1000000 && map.cell_size > 0. &&
              map.cell_size <= 32.,
          "Invalid cold strategy navigation graph");
  require(map.heights.size() == count && map.links.size() == count &&
              map.hazards.size() == count,
          "Incomplete cold strategy navigation graph");
  out->width = map.width;
  out->height = map.height;
  out->cell = map.cell_size;
  out->origin = {map.origin_x, map.origin_z};
  out->bounds = bounds(map, meta);
  const auto &dirs = rows(meta.get("directions"));
  require(dirs.size() == 8, "Missing cold strategy graph directions");
  std::set<Cell> unique;
  for (std::size_t i = 0; i < 8; ++i) {
    require(rows(dirs[i]).size() == 2, "Invalid cold strategy graph direction");
    out->directions[i] = direction(dirs[i]);
    const auto d = out->directions[i];
    require(std::max(std::abs(d[0]), std::abs(d[1])) == 1 &&
                unique.insert(d).second,
            "Invalid cold strategy graph direction set");
  }
  out->heights_mm.reserve(count);
  out->links.reserve(count);
  out->hazards.reserve(count);
  for (std::size_t i = 0; i < count; ++i) {
    const double h = map.heights[i] * 1000.;
    require(std::isnan(map.heights[i]) ||
                (std::isfinite(h) && h > INT32_MIN && h <= INT32_MAX),
            "Cold graph height exceeds millimetre domain");
    require(map.links[i] <= 255 && map.hazards[i] <= 255,
            "Cold graph byte column exceeds domain");
    out->heights_mm.push_back(std::isfinite(h)
                                  ? static_cast<std::int32_t>(std::llround(h))
                                  : strategy::GraphData::missing);
    out->links.push_back(static_cast<std::uint8_t>(map.links[i]));
    out->hazards.push_back(static_cast<std::uint8_t>(map.hazards[i]));
  }
  return out;
}
} // namespace
std::string validateRoute(const MapInput &map, const Value &meta,
                          const Value &route) {
  return Validator(map, meta).validate(route);
}
Prepared prepare(const std::string &map_name,
                 const std::map<int, std::vector<Value>> &actual_routes,
                 const MapInput &map, const Value &metadata,
                 const Value &active_tactics, const Value &archive) {
  require(string(metadata, "map") == map_name &&
              string(metadata, "game_version") == "0.9.22.0.1-cn-1513",
          "Cold room graph identity mismatch");
  const auto &static_map = archive.get("maps").get(map_name);
  require(static_map.resolved().kind == K::Object,
          "Cold room metadata does not contain current map");
  require(active_tactics.resolved().kind == K::Object,
          "Cold room tactics are missing");
  Value meta = metadata.resolved();
  const auto &canonical_bounds = static_map.get("tactical_map").get("bounds");
  if (canonical_bounds.resolved().kind == K::Array)
    meta.object["bounds"] = canonical_bounds;
  Prepared out;
  out.graph = compactGraph(map, meta);
  out.resources = object();
  out.resources.object["arena_bounds"] = meta.get("bounds");
  auto strategy = object();
  strategy.object["tactics"] = active_tactics;
  strategy.object["tactical_map"] = static_map.get("tactical_map");
  strategy.object["arena_bounds"] = meta.get("bounds");
  strategy.object["routes"] = object();
  for (const auto &[team, routes] : actual_routes) {
    auto list = array();
    list.array = routes;
    strategy.object["routes"].object[std::to_string(team)] = std::move(list);
  }
  const auto &expected = static_map.get("tactics");
  const auto &authored = static_map.get("default_tactics");
  if (active_tactics == expected)
    strategy.object["tactics_digest"] = static_map.get("tactics_digest");
  else if (active_tactics == authored)
    strategy.object["tactics_digest"] =
        static_map.get("default_tactics_digest");
  else
    throw std::invalid_argument("Cold tactics lack a reviewed identity");
  require(strategy.get("tactics_digest").resolved().kind == K::Text,
          "Cold tactics identity is missing");
  strategy.object["spg_catalog"] = archive.get("spg_catalog");
  strategy.object["route_configs"] = object();
  strategy.object["class_routes"] = object();
  for (int team : {1, 2}) {
    strategy.object["route_configs"].object[std::to_string(team)] = object();
    strategy.object["class_routes"].object[std::to_string(team)] = object();
  }
  const auto &settings = active_tactics.get("maps").get(map_name);
  for (const auto &row : rows(settings.get("routes"))) {
    const auto team = intField(row, "team");
    require(team == 1 || team == 2, "Invalid cold authored route team");
    strategy.object["route_configs"]
        .object[std::to_string(team)]
        .object["user_" + string(row, "id")] = row;
  }
  std::unique_ptr<Validator> validator;
  std::map<std::pair<int, std::string>, std::vector<Value>> valid;
  for (const auto &row : rows(settings.get("default_routes"))) {
    const auto team = static_cast<int>(intField(row, "team"));
    require(team == 1 || team == 2, "Invalid cold default route team");
    strategy.object["route_configs"]
        .object[std::to_string(team)]
        .object[routeId(row)] = routeConfig(row);
    if (row.get("disabled").truth())
      continue;
    if (!validator)
      validator = std::make_unique<Validator>(map, meta);
    if (validator->validate(row).empty())
      valid[{team, string(row, "id")}].push_back(row);
  }
  for (const auto &[key, candidates] : valid) {
    auto variants = array();
    for (const auto &tag : classes) {
      const Value *selected = nullptr;
      for (const auto &row : candidates)
        if (string(row, "class_tag", "all") == tag) {
          selected = &row;
          break;
        }
      if (!selected)
        for (const auto &row : candidates)
          if (string(row, "class_tag", "all") == "all") {
            selected = &row;
            break;
          }
      if (!selected)
        continue;
      auto variant = object();
      variant.object["class_tag"] = text(tag);
      variant.object["route"] = classRoute(*selected, tag);
      variants.array.push_back(std::move(variant));
    }
    strategy.object["class_routes"]
        .object[std::to_string(key.first)]
        .object[key.second] = std::move(variants);
  }
  out.resources.object["strategy"] = std::move(strategy);
  return out;
}
} // namespace native_battle::room_resources
