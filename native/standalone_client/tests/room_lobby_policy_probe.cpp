#include "../room/room_test_resources.hpp"
#include "../room/room_json.hpp"
#include "../room/room_lobby_policy.hpp"
#include <fstream>
#include <iostream>
using namespace wot::room;
int main(int argc, char **argv) {
  if (argc != 2)
    return 2;
  std::ifstream f(argv[1]);
  const std::string bytes((std::istreambuf_iterator<char>(f)), {});
  const auto resources = testResources(parseJson(bytes));
  std::string line;
  while (std::getline(std::cin, line)) {
    try {
      const auto input = parseJson(line);
      Value out = array();
      for (const auto &op : rows(input)) {
        try {
          const auto type = lobby::string(op, "type");
          // Scalar and container aliases must obey the same admission laws.
          Value raw; raw.kind = Value::Kind::Shared;
          raw.reference = freeze(borrowedResourceTree(op.get("value")));
          Value value;
          if (type == "safe")
            value =
                text(lobby::safe(raw, lobby::string(op, "fallback"),
                                 resources, op.get("vehicle").truth()));
          else if (type == "lower")
            value = text(lobby::lower(lobby::string(op, "value"), resources));
          else if (type == "team")
            value = integer(lobby::requestedTeam(raw, resources));
          else if (type == "outfits")
            value = lobby::outfits(raw, resources);
          else if (type == "marks")
            value = lobby::marks(raw);
          else if (type == "descriptor")
            value = lobby::descriptor(raw);
          else if (type == "catalog")
            value = lobby::catalog(raw, resources);
          else if (type == "normalize") {
            value = raw.resolved();
            lobby::normalizeConfiguration(value, resources);
          } else if (type == "tiers") {
            std::set<int> available, humans;
            for (const auto &v : rows(op.get("available")))
              available.insert(static_cast<int>(v.integer));
            for (const auto &v : rows(op.get("humans")))
              humans.insert(static_cast<int>(v.integer));
            value = array();
            for (const auto t : lobby::matchTiers(
                     static_cast<int>(intField(op, "host", 1)),
                     lobby::string(op, "mode"), available, humans))
              value.array.push_back(integer(t));
          } else if (type == "policy") {
            auto row = op.get("row");
            std::vector<std::string> pinned;
            for (const auto &v : rows(op.get("pinned")))
              pinned.push_back(v.text);
            State state;
            state.resources = resources;
            state.config = object();
            state.config.object["team_classes"] = object();
            state.config.object["team_classes"].object["1"] = row;
            state.config.object["team1_size"] =
                integer(intField(op, "automatic"));
            int slot = 0;
            state.config.object["bot_lineup"] = array();
            for (const auto &tag : pinned) {
              Value bot = object();
              bot.object["team"] = integer(1);
              bot.object["slot"] = integer(slot++);
              const auto name = "ussr:Pinned" + std::to_string(slot);
              bot.object["vehicle"] = text(name);
              state.config.object["bot_lineup"].array.push_back(bot);
              state.resources.object["lobby"] = state.resources.get("lobby").resolved();
              state.resources.object["lobby"].object["vehicle_classes"] =
                  state.resources.get("lobby").get("vehicle_classes").resolved();
              state.resources.object["lobby"]
                  .object["vehicle_classes"]
                  .object[name] = text(tag);
            }
            state.config.object["team1_size"] =
                integer(intField(op, "automatic") + slot);
            const auto removed =
                lobby::trim(state, 1, lobby::string(op, "keep"));
            row = state.config.get("team_classes").get("1");
            std::set<std::string> candidates;
            for (const auto &v : rows(op.get("candidates")))
              candidates.insert(v.text);
            value = object();
            value.object["row"] = row;
            value.object["removed"] = removed;
            value.object["required"] = array();
            for (const auto &tag : lobby::required(row, pinned))
              value.object["required"].array.push_back(text(tag));
            const auto limit = lobby::artilleryLimit(
                static_cast<int>(intField(op, "size")),
                static_cast<int>(intField(op, "human_spgs")),
                static_cast<int>(
                    std::count(pinned.begin(), pinned.end(), "SPG")),
                row);
            value.object["limit"] = integer(limit);
            lobby::Context context;
            context.automatic = static_cast<int>(intField(op, "automatic"));
            context.pinned = pinned;
            const auto reason =
                lobby::validate(row, context, candidates, limit);
            value.object["reason"] = reason.empty() ? Value() : text(reason);
          } else if (type == "matching") {
            std::vector<std::pair<int, std::string>> candidates;
            std::set<std::string> needed;
            std::set<int> humans;
            std::vector<int> proposed;
            std::vector<std::set<std::string>> groups;
            std::vector<std::set<int>> slots;
            for (const auto &v : rows(op.get("candidates")))
              candidates.emplace_back(static_cast<int>(v.array[0].integer),
                                      v.array[1].text);
            for (const auto &v : rows(op.get("needed")))
              needed.insert(v.text);
            for (const auto &v : rows(op.get("humans")))
              humans.insert(static_cast<int>(v.integer));
            for (const auto &v : rows(op.get("proposed")))
              proposed.push_back(static_cast<int>(v.integer));
            for (const auto &group : rows(op.get("groups"))) {
              std::set<std::string> s;
              for (const auto &v : rows(group))
                s.insert(v.text);
              groups.push_back(s);
            }
            for (const auto &group : rows(op.get("slots"))) {
              std::set<int> s;
              for (const auto &v : rows(group))
                s.insert(static_cast<int>(v.integer));
              slots.push_back(s);
            }
            const auto tiers =
                lobby::matchingTiers(candidates, needed, humans, proposed,
                                     op.get("random").truth(), groups, slots);
            if (tiers) {
              value = array();
              for (const auto t : *tiers)
                value.array.push_back(integer(t));
            }
          }
          Value result = object();
          result.object["value"] = std::move(value);
          out.array.push_back(std::move(result));
        } catch (const std::exception &) {
          Value result = object();
          result.object["error"] = boolean(true);
          out.array.push_back(result);
        }
      }
      std::cout << writeJson(out) << '\n';
    } catch (const std::exception &e) {
      std::cerr << e.what() << '\n';
      return 1;
    }
  }
}
