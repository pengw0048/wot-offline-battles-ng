#include "room_bot_rows.hpp"
#include "room_json.hpp"
#include <iostream>
#include <string>
int main() {
  using namespace wot::room;
  std::string line;
  while (std::getline(std::cin, line)) {
    auto response = object();
    try {
      const auto request = parseJson(line);
      if (request.get("operation").text == "snapshot") {
        auto result = encodeSnapshotEquipment(request.get("bots"));
        response.object["rows"] = std::move(result.first);
        response.object["contracts"] = std::move(result.second);
      } else
        response.object["decoded"] =
            decodeBotRow(request.get("row"), request.get("static"));
      response.object["ok"] = boolean(true);
    } catch (const std::invalid_argument &) {
      response.object["ok"] = boolean(false);
    }
    std::cout << writeJson(response) << '\n';
  }
}
