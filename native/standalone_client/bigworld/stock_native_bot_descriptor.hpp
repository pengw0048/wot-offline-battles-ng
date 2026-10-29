#pragma once
#include "stock_native_bot_weapon.hpp"
#include <optional>
#include <string>
#include <vector>
struct _object;
namespace stock_native_bot_descriptor {
using Point=wot::offline_bot_step::Point;
struct Bounds {Point minimum{},maximum{};};
Bounds hullBounds(_object* descriptor);
Bounds bodyBounds(_object* descriptor);
std::array<double,4> chassisShape(_object* descriptor);
std::optional<std::array<Point,6>> checkpoints(_object* descriptor);
struct DevicePool {std::optional<double> maximum,regeneration;};
double botRating(_object* runtime,_object* id);
int botCrewLevel(_object* runtime,_object* id);
wot::offline_bot_step::Point spawnAnchor(_object* battle,int team,int slot);
double driveCap(_object* descriptor,double forward,double reverse,double speed,
                _object* travel_descriptor=nullptr);
DevicePool devicePool(_object* descriptor,const std::string& name);
std::vector<std::string> crewRoster(_object* descriptor);
// New reference. Only the exact stock #1513 crew/attribute producers are
// invoked; all offline modifier mathematics remains native.
_object* defaultCrewFactors(_object* descriptor,std::optional<int> level={});
bool aimSamples(_object* descriptor,std::vector<wot::offline_bot_step::AimSample>&);
}
