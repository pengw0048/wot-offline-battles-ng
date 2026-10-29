#pragma once
#include "stock_native_bot_combat.hpp"
#include "stock_native_bot_step_state.hpp"
#include <map>
#include <string>
#include <tuple>
#include <vector>

namespace wot::offline_bot_step {
struct ImpactHit {double distance=0;std::string name;};
struct ImpactContact {std::string name;double weight=0;};
class ImpactWorld {
public:
    virtual ~ImpactWorld()=default;
    virtual std::vector<Point> ground_witnesses()=0;
    virtual std::vector<ImpactHit> material_segment(const Point&,const Point&)=0;
    virtual std::vector<ImpactHit> interior_segment(const Point&,const Point&)=0;
};
struct EnvironmentTotals {
    std::map<std::int64_t,std::int64_t> fall_damage;
    std::map<std::tuple<std::int64_t,std::string,std::uint32_t>,std::int64_t> critical;
    std::set<std::int64_t> fire_provenance;
};
struct ImpactResult {
    int damage=0;
    std::vector<ImpactContact> contacts;
    std::vector<CriticalEvent> events;
};
std::vector<ImpactContact> collect_impact_contacts(const std::vector<Point>& witnesses,
    const Point& normal,double impact_speed,ImpactWorld&);
ImpactResult apply_environment_impact(std::int64_t actor,CombatState&,VerticalState&,
    double impact_speed,bool normal_impact,bool ground_observed,const std::vector<Point>& witnesses,
    const Point& normal,ImpactWorld&,EnvironmentTotals&);
ImpactResult apply_environment_fall_damage(std::int64_t actor,CombatState&,VerticalState&,
    double impact_speed,bool ground_observed,const std::vector<Point>& witnesses,
    const Point& normal,ImpactWorld&,EnvironmentTotals&);
std::uint32_t environment_critical_mask(const CriticalState&,const CriticalState&);
void accumulate_environment_critical(EnvironmentTotals&,std::int64_t actor,const std::string& cause,
    const CriticalState& before,const CriticalState& after,bool crew_only=false);
}
