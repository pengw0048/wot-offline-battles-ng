#include "room_json.hpp"
#include "room_rules_achievements.hpp"
#include <iostream>
int main() {
  using namespace wot::room;
  std::string line;
  while (std::getline(std::cin, line)) {
    auto response = object();
    try {
      auto request = parseJson(line);
      auto operation = request.get("operation").text;
      if (operation == "award")
        response.object["value"] =
            awardBattleAchievements(request.get("battle"));
      else if (operation == "rewards")
        response.object["value"] =
            offlineRewards(request.get("stats"), request.get("won").truth(),
                           request.get("participated").truth(),
                           int(intField(request, "tier", 1)),
                           int(intField(request, "spotted_spgs")),
                           int(intField(request, "capture_participants")),
                           int(intField(request, "killed_durability")));
      else if (operation == "medals")
        response.object["value"] = medalRewards(request.get("achievements"),
                                                int(intField(request, "tier")));
      else
        response.object["value"] = integer(battleBondReward(
            intField(request, "xp"), request.get("eligible").truth()));
      response.object["ok"] = boolean(true);
    } catch (const std::exception &) {
      response.object["ok"] = boolean(false);
    }
    std::cout << writeJson(response) << '\n';
  }
}
