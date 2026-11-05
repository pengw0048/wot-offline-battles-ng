#pragma once
// Room strategy owns only admitted observations and pure-data tactical state.
#include "room_state.hpp"
#include <set>
#include <tuple>

namespace wot::room {
namespace strategy {
using ID = std::int64_t;
using Identity = std::pair<std::string, ID>;
using Records = std::map<ID, Value>;
using Contacts = std::map<Identity, Value>;
struct GraphData;
class ShotPolicy;
std::vector<Value> orderedShells(const Value& profile,const Value& inventory=Value(),bool stages=false);
Value point(const Value&);
double distance(const Value&, const Value&);
bool inside(const Value& pose, const Value& area);
Value constrainCapture(const Value& order, const Value& pose);
bool emptyConfirmed(const Value& state, const Value& profile);
bool emptyActive(const Value& bot);
bool weaponCanRecover(const Value& bot);
bool weaponReady(const Value& bot);
bool magazineFollowup(const Value& bot);
Value normalizeCover(const Value&);
Value scoreCover(const Value&, const Value& weights = Value());
Value scoreCovers(const Value&, const Value& weights = Value());
Value personality(ID);
Value crossfireRisk(const Value& bot,const std::vector<Value>& contacts);
Value canonicalSpgPlan(const Value& raw,const Value& resources,const std::string& map,
                       const Value& vehicle,int team,bool enforce_identity=true);
bool capability(double rating,const std::string&,const std::vector<Value>& occasion);
bool diskReached(const Value& position, const Value& target,
                 const Value& following = Value(), const Value& previous = Value());
struct RoutePosition {
    std::string route;
    int index = 0;
    Value move, anchor;
    bool join = false;
};
}

class Planner {
public:
    explicit Planner(State& state) : state_(state) {}
    void reset();
    void clearObservations();
    Value debugSummary(double now) const;
    Outcome dispatch(Sender, const Value&);
    void admitDamage(std::int64_t victim, const std::string& attacker_kind,
                     std::int64_t attacker, int damage, double now);
    void build(double now);
    void setGraph(std::shared_ptr<const strategy::GraphData> graph) { graph_=std::move(graph); }
    void setTeamCommands(Value admitted) { team_commands_=std::move(admitted); }
    Outcome dispatchRadio(Sender,const Value&);
    void advanceRadio();
    void clearRadio(const std::string& code);
    void cancelRadio(const std::set<std::int64_t>& cancelled);
    void drainAmmo();
    void refreshAmmoOrders();
    // Cold resources.strategy: tactics, graph and class-specific validated
    // routes. All geometry/configuration is loaded before the round starts.
    // No strategy method opens a file or calls Python/BigWorld.
private:
    friend struct StrategyProbe;
    State& state_;
    std::map<strategy::ID,std::shared_ptr<strategy::ShotPolicy>> ammo_policies_;
    std::shared_ptr<const strategy::GraphData> graph_;
    std::map<int, strategy::Contacts> contacts_;
    strategy::Records affordances_, recent_hits_, route_states_, route_history_;
    strategy::Records route_recoveries_, recovery_route_visits_, wreck_progress_, wreck_avoid_;
    strategy::Records route_assignments_, assignment_receipts_, travel_tasks_;
    strategy::Records previous_player_, engage_anchors_, firing_holds_, cover_states_;
    strategy::Records cover_failures_, target_assignments_, target_decisions_, combat_states_;
    strategy::Records retreat_states_, capture_arrivals_, artillery_anchors_;
    std::set<strategy::ID> empty_bots_, previous_combat_;
    std::set<std::string> cover_reservations_;
    std::map<int, Value> base_defense_, balance_receipts_;
    std::map<int, double> next_rebalance_;
    std::map<std::tuple<int, std::string, int, int>, Value> wait_claims_;
    Value team_commands_ = array(), wait_bodies_ = array(), last_signature_;
    std::vector<Value> aliveBots() const;
    std::map<strategy::Identity, Value> knownTargets() const;
    std::size_t reportContacts(const Value&, double now, Value& accepted_visibility);
    std::size_t reportAffordances(const Value&, double now);
    Outcome admitObservation(Sender,const Value&);
    void prune(double now);
    std::vector<Value> contactsFor(const Value&, double now) const;
    const Value& route(const Value& bot) const;
    Value routeConfig(const Value& bot, const Value& route,bool check_geometry=false) const;
    std::map<std::string, Value> routeCatalog(const std::vector<Value>& bots) const;
    strategy::RoutePosition routePosition(const Value& bot, double now);
    bool routeMobile(const Value& bot) const;
    void dropParking(const Value& bot);
    void taskRejoin(const Value& bot);
    Value& taskBegin(const Value&, const std::string&, const Value&, const Value&, double now);
    void taskPublish(Value& order, const Value& bot, double now);
    void taskApply(Value& order, const Value& bot, double now,
                   const Value& defense, const Value& player_order, const Value& context);
    static bool taskCombat(const Value&);
    void taskCaptureHandoff(Value&, const Value&, const Value&);
    void emptyUpdate(std::vector<Value>&);
    void emptyOrder(Value&, const Value&, const std::vector<Value>&,
                    const Value& capture, double now);
    void cycleLeg(const Value&, Value&, const std::string&, int index, double now);
    void cycleFinish(const Value&);
    void cyclePublish(Value&, const Value&, double now);
    void cycleRestart(const Value&, Value&, double now);
    std::string cycleChangeRoute(Value&, const Value&, const std::vector<Value>&, Value&, double now);
    void recoverRoute(Value&, const Value&, const std::vector<Value>&, double now);
    void automaticGoal(Value&, const Value&, const std::vector<Value>&, double now);
    void authoredOrder(Value&, const Value&, const Value& route_point, double now);
    bool waitOccupied(strategy::ID,const Value&,double radius,double height) const;
    std::pair<bool,Value> waitParking(const Value&,Value&,const std::string&,int,const Value&,double now);
    void terminalOrder(Value&, const Value&, const Value& capture, double now);
    void recordAssignment(const Value&, double now, const std::string&, const std::string&,
                          const std::string& reason,const Value& evidence=Value());
    Value captureTarget(const Value& context, int team) const;
    static std::vector<Value> defensePoints(const Value&, int team);
    static bool insideBase(const Value& bot, const Value& base);
    void protectCapture(Value&, const Value&, const Value&);
    void captureOrder(Value&, const Value&, const Value&);
    bool capable(const Value& bot,const std::string&,std::vector<Value> occasion = {}) const;
    strategy::Records assignTargets(const std::vector<Value>&,const std::vector<Value>&,double now);
    static double engagementRange(const Value&,const Value&);
    static int selectedShell(const Value&,const Value&);
    static Value newTargetLease(const Value&,const strategy::Identity&,double now);
    static bool targetAimProgress(const Value&,Value&,double now);
    Value orderFor(const Value&,int index,int count,const Value& focus,const std::vector<Value>&,
                   double now,const Value& defense,const Value& axis,const std::vector<Value>& allies,
                   const Value& capture,bool no_enemies,bool active_capture);
    void stationaryAngle(Value&,const Value&);
    bool coverOrder(Value&,const Value&,const Value&,double now,bool urgent=false,bool hold_only=false);
    void artilleryOrder(Value&,const Value&,const Value& axis);
    void retreatOrder(Value&,const Value&,const Value& destination,const Value& face,double now,
                      const std::string& moving,const std::string& hold);
    bool leasedMovement(Value&,const Value&,const Value&);
    std::string rangeMode(const Value&,const Value&,double distance,double far_limit,
                          std::string far_mode,double fire_range,double now);
    void releaseFiringHold(Value&,const Value&,const Value& move,const Value& anchor,double now);
    void setTarget(Value&,const Value&,const Value&,bool fire);
    Value retreatPoint(const Value&,const Value& anchor) const;
    void baseDefenseOrder(Value&,const Value&,const Value& base);
    bool captureStaged(const Value&,int route_index,const Value& capture);
    static std::set<strategy::Identity> contributors(const Value& context,int team);
    std::map<int,strategy::Records> updateDefense(const std::vector<Value>&,const Value&,double now);
    void prioritizeInvaders(int team,const std::vector<Value>&,strategy::Records& assignments,
                            const strategy::Records& defenders,const Value& context,double now);
    bool defenseCombatProgress(const Value&,Value&,const std::set<strategy::Identity>&,double now);
    std::string taskFamily(const Value& route,int team) const;
    Value taskClassRoute(const Value&,const Value&,const std::map<std::string,Value>&) const;
    static std::string nearestRoute(const Value&,const std::map<std::string,Value>&);
    void rebalanceRoutes(int,const std::vector<Value>&,const std::vector<Value>&,double now,std::set<strategy::ID> protected_ids);
    void observeLeases(const std::vector<Value>&,double now,const std::string&,bool evaluation=true,const Value& extra=Value());
    void pruneTactics(const std::vector<Value>&,double now);
    strategy::Records teamOrders(const std::vector<Value>&) const;
    Value teamFocus(const Value&,const Value&,double now) const;
    void applyTeamOrder(Value&,const Value&,const Value&,const Value& context,
                        const Value& route_point,const Value& turnback_point);
    static Value teamAxis(int,const std::vector<Value>&);
};
}
