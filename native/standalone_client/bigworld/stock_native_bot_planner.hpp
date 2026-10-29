#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace stock_native_bot_planner {
using Point = std::array<double, 3>;
template<class T> struct Optional {
    bool present = false;
    T value{};
    void set(const T &v) { value = v; present = true; }
    void clear() { present = false; }
};
struct Body {
    std::int64_t id = 0;
    int team = 0;
    bool team_known = true, alive = true, velocity_known = true;
    Point position{}, velocity{};
    double yaw = 0., pitch = 0., roll = 0.;
    double half_length = 0., half_width = 0., lower_y = 0., upper_y = 0.;
};
struct Order {
    Optional<Point> aim, move, face;
    Optional<double> throttle, arrival_radius, parking_heading, hull_angle_degrees;
    Optional<std::int64_t> target;
    std::string mode = "route", parking_phase, target_kind;
    bool fire_allowed = false, stable_hull_face = false;
    int shell_index = 0;
    double fire_range = 0.;
};
struct Input {
    Body body;
    int slot = 0;
    double speed = 0., dt = 0., now = 0., decision_horizon = 0.;
    Optional<double> stopping_distance, turn_speed_limit, implicit_probe_distance;
    bool rotation_refused = false, navigation_recovery_allowed = false;
};
struct Navigation {
    Point target{};
    Optional<Point> progress_target;
    Optional<bool> stop_at_target;
    std::string status = "pending";
};
struct Command {
    std::int64_t bot_id = 0;
    Optional<std::int64_t> target, forward_blocker, reverse_blocker;
    Point aim{}, face{}, move{};
    double throttle = 0., turn = 0., target_yaw = 0., fire_range = 0.;
    bool brake = false, fire_allowed = false, movement_intent = false, stable_hull_face = false;
    bool withdrawal_aim = false, navigation_recovery = false, navigation_replan = false;
    int shell_index = 0;
    std::string mode = "route", recovery = "drive", parking_phase, traffic_mode;
    Optional<double> probe_distance, pivot_extent, hull_angle_degrees, navigation_probe_distance;
    Optional<Point> driver_progress_target;
};
// Implementations are synchronous C++ world/navigation owners. These methods
// may not invoke Python, access Python objects, or retain borrowed input views.
class World {
public:
    virtual ~World() = default;
    virtual bool direction(std::int64_t actor, double yaw,
        const Optional<double> &maximum_distance, double drive_direction,
        const Optional<double> &implicit_horizon) = 0;
    virtual bool pose(std::int64_t actor, double yaw, bool allow_wreck_push) = 0;
    virtual Navigation navigate(const Input &, const Order &, const Point &destination) = 0;
};
struct Escape {
    Point start{};
    double yaw = 0., sign = 1., distance = 0., until = 0.;
};
struct Objective {
    Point goal{};
    double best = 0., at = 0.;
    Optional<double> alignment_yaw, alignment_best;
};
struct Episode {
    Point origin{}, goal{};
    double at = 0., issued_at = 0.;
    int count = 0, issued_count = 0;
    bool issued = false;
};
struct NavigationWait {
    double age = 0., elapsed = 0.;
    bool active = false, completed = false;
    Point origin{};
};
struct DriverState {
    int slot = 0, recovery_count = 0;
    Point last_position{};
    double stuck_time = 0., recovery_time = 0., recovery_side = 0.;
    double clock = 0., steering_age = 999., plan_age = 999.;
    double escape_side = 0., escape_until = 0., traffic_wait_time = 0., last_step = 0.;
    bool traffic_waiting = false, avoiding = false, native_translation_stalled = false;
    Optional<double> steering_yaw, desired_yaw, progress_yaw, best_heading_error, last_clear_yaw;
    Optional<double> translation_progress_at;
    Optional<Point> alignment_target, braking_target;
    Optional<Escape> short_escape, translation_escape;
    Optional<Objective> objective;
    Optional<Episode> pocket, replan;
    Optional<NavigationWait> navigation_wait;
    std::map<int, double> failed_yaws, real_failed_yaws;
};
struct Contact {
    std::int64_t peer = 0;
    int team = 0;
    bool alive = true;
    Point position{};
    std::array<double, 2> normal{};
};
struct ContactMemory {
    Contact contact;
    Point position{};
    double yaw = 0.;
    bool failed_reverse = false, failed_forward = false;
};
struct ContactEscape {
    Contact contact;
    Point origin{}, target{};
    double yaw = 0., sign = 0., distance = 0., half_length = 0., half_width = 0.;
    double elapsed = 0., stalled = 0., best_progress = 0.;
};
struct WreckAttempt {
    std::int64_t key = 0;
    Point position{}, origin{}, own_origin{}, exhausted_position{};
    double elapsed = 0., heading = 0., best_progress = 0.;
    double own_heading = 0., own_best_progress = 0., own_elapsed = 0.;
    bool exhausted = false;
};
struct Withdrawal {
    Point goal{}, position{};
    double distance = 0., age = 0.;
    bool fallback = false;
};
struct ActorMemory {
    DriverState driver;
    Optional<std::int64_t> contact_peer, active_wreck;
    Optional<ContactMemory> contact_attempt;
    Optional<ContactEscape> contact_escape;
    std::map<std::int64_t, WreckAttempt> wreck_history;
    Optional<Withdrawal> withdrawal;
};
class Planner {
    std::map<std::int64_t, ActorMemory> actors_;
public:
    ActorMemory &actor(const Input &);
    void forget(std::int64_t);
    void remember_failure(std::int64_t, double yaw, double ttl = 2.);
    bool wait_for_traffic(std::int64_t, double elapsed);
    Command drive(const Input &, const Navigation &, const Point &destination,
        bool movement_intent, bool stop_at_target, const Optional<double> &arrival,
        const std::vector<Body> &, World &);
    Command decide(const Input &, const Order &, const std::vector<Body> &, World &);
    const std::map<std::int64_t, ActorMemory> &actors() const { return actors_; }
    void restore(std::map<std::int64_t, ActorMemory> values) { actors_ = std::move(values); }
};
struct TrafficLease {
    bool head_on = false;
    std::int64_t winner = 0;
    std::array<double, 2> axis{}, targets{};
    double until = 0., clear_after = 0.;
    std::set<std::int64_t> blocked;
};
struct ParkedLease {
    std::int64_t requester = 0;
    double until = 0., sign = 0., distance = 0.;
    Point origin{};
};
struct TrafficSnapshot {
    std::map<std::pair<std::int64_t,std::int64_t>,TrafficLease> pairs;
    std::map<std::int64_t,double> held;
    std::map<std::int64_t,std::pair<double,Command>> orders;
    std::map<std::pair<std::int64_t,std::int64_t>,double> jams;
    std::map<std::int64_t,ParkedLease> parked;
};
class Traffic {
    using Pair = std::pair<std::int64_t, std::int64_t>;
    std::map<Pair, TrafficLease> pairs_;
    std::map<std::int64_t, double> held_;
    std::map<std::int64_t, std::pair<double, Command>> orders_;
    std::map<Pair, double> jams_;
    std::map<std::int64_t, ParkedLease> parked_;
    bool parked_yield(const Input &, const Command &, const std::map<std::int64_t, Body> &,
        const std::vector<Body> &, World &, Command &);
public:
    void restore(TrafficSnapshot value) { pairs_=std::move(value.pairs);held_=std::move(value.held);orders_=std::move(value.orders);jams_=std::move(value.jams);parked_=std::move(value.parked); }
    void forget(std::int64_t);
    Optional<double> clearance_distance(std::int64_t, const Point &) const;
    Command adjust(const Input &, const Command &, const std::vector<Body> &, World &);
    // Called every physical slice after hull aiming, including reused decisions.
    // stopping_distance must be the exact installed-vehicle braking integral.
    Command safe_controls(const Input &, const Command &, const std::vector<Body> &,
        double stopping_distance);
};
}
