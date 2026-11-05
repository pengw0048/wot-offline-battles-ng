#include "../room/room_test_resources.hpp"
#include "room_inputs.hpp"
#include "room_json.hpp"
#include <iostream>
int main() {
  using namespace wot::room;
  std::string line;
  while (std::getline(std::cin, line)) {
    try {
      const auto request = parseJson(line);
      State state;
      state.round_id = 1;
      state.phase = "battle";
      state.server_tick = 300;
      state.motion_time_us = 1000000;
      state.bot_state_time_us = 1000000;
      state.config.object["prebattle_seconds"] = number(10.);
      state.resources = testResources(request.get("resources"));
      state.players[1] = request.get("actor");
      for (const auto &v : rows(request.get("bots")))
        state.bots[intField(v, "id")] = v;
      Inputs input(state);
      for (const auto &v : rows(request.get("archive"))) {
        if (request.get("map_archive").truth()) {
          std::map<std::int64_t, Value> bots;
          for (const auto &bot : rows(v.get("bots"))) bots[intField(bot, "id")] = bot;
          input.rememberBots(intField(v, "revision"), intField(v, "stamp"), bots);
        } else input.rememberBots(intField(v, "revision"), intField(v, "stamp"), v.get("bots"));
      }
      auto output = array();
      for (const auto &command : rows(request.get("commands"))) {
        if (has(command, "actor_patch"))
          for (const auto &v : command.get("actor_patch").resolved().object)
            state.players[1].object[v.first] = v.second;
        if (has(command, "phase"))
          state.phase = command.get("phase").text;
        if (has(command, "tick"))
          state.server_tick = intField(command, "tick");
        if (has(command, "motion_time_us"))
          state.motion_time_us = intField(command, "motion_time_us");
        if (has(command, "bot_state_revision"))
          state.bot_state_revision = intField(command, "bot_state_revision");
        if (has(command, "bot_state_time_us"))
          state.bot_state_time_us = intField(command, "bot_state_time_us");
        if (has(command, "advance_ticks"))
          input.advanceTicks(intField(command, "advance_ticks"));
        const auto result = input.dispatch({1, false}, command.get("message"));
        auto row = object();
        row.object["accepted"] =
            boolean(result.status == Outcome::Status::accepted);
        row.object["actor"] = state.players[1];
        row.object["replies"] = array();
        for (const auto &v : result.replies)
          row.object["replies"].array.push_back(v);
        output.array.push_back(std::move(row));
      }
      std::cout << writeJson(output) << '\n';
    } catch (const std::exception &e) {
      std::cerr << e.what() << '\n';
      return 1;
    }
  }
}
