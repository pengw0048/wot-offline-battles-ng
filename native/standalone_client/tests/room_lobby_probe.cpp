#include "../room/room_test_resources.hpp"
// Portable lobby component probe. Network, native engine and rendering are not
// exercised.
#include "../room/room_core.hpp"
#include "../room/room_json.hpp"
#include "../room/room_lobby_policy.hpp"
#include <fstream>
#include <iostream>
using namespace wot::room;
namespace wot::room {
struct LobbyProbe {
  static Value run(const Value &input, const Value &resources) {
    Value deliveries = array();
    Core core(
        input.get("config"), resources,
        [&](Core::Peer peer, std::shared_ptr<const Value> message,
            std::optional<std::string>) {
          Value row = array();
          row.array = {integer(static_cast<std::int64_t>(peer)), *message};
          deliveries.array.push_back(std::move(row));
          return true;
        },
        [](Core::Peer, bool) {}, [](Core::Peer, std::int64_t) {},
        [&](const Value &raw) -> std::optional<Value> {
          if (raw == input.get("valid_params"))
            return raw;
          for (const auto &variant : rows(input.get("valid_param_variants")))
            if (raw == variant)
              return raw;
          return std::nullopt;
        });
    core.random_.seed(123);
    Value out = array();
    for (const auto &op : rows(input.get("operations"))) {
      deliveries = array();
      const auto type = lobby::string(op, "type");
      const auto peer = static_cast<Core::Peer>(intField(op, "peer", 1));
      if (type == "join")
        core.receive(peer, op.get("message"),
                     lobby::string(op, "address", "127.0.0.1"));
      else if (type == "start")
        core.start(peer, op.get("message"));
      else if (type == "reset")
        core.resetRound();
      else if (type == "command") {
        const auto c = core.peers_.find(peer);
        if (c != core.peers_.end())
          core.lobbyCommand(peer, c->second, op.get("message"));
      } else if (type == "patch") {
        for (const auto &row : op.get("players").object) {
          auto &p = core.state_.players.at(std::stoll(row.first));
          for (const auto &kv : row.second.object)
            p.object[kv.first] = kv.second;
        }
        if (has(op, "phase"))
          core.state_.phase = lobby::string(op, "phase");
      }
      Value snapshot = core.lobby();
      snapshot.object["bots"] = array();
      for (const auto &bot : rows(core.roster_)) {
        Value row = object();
        for (const auto &key : {"id", "team", "slot"})
          row.object[key] = bot.get(key);
        snapshot.object["bots"].array.push_back(row);
      }
      snapshot.object["deliveries"] = deliveries;
      snapshot.object["config"] = core.state_.config;
      out.array.push_back(std::move(snapshot));
    }
    return out;
  }
};
} // namespace wot::room
int main(int argc, char **argv) {
  if (argc != 2)
    return 2;
  std::ifstream f(argv[1]);
  const auto resources =
      testResources(parseJson(std::string((std::istreambuf_iterator<char>(f)), {})));
  std::string line;
  while (std::getline(std::cin, line)) {
    try {
      std::cout << writeJson(LobbyProbe::run(parseJson(line), resources))
                << '\n';
    } catch (const std::exception &e) {
      std::cerr << e.what() << '\n';
      return 1;
    }
  }
}
