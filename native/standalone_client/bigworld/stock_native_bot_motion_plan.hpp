#pragma once
#include "stock_native_bot_candidate.hpp"

namespace stock_native_bot_navigation {
struct WorldReceipt {
    Point origin{};
    double yaw=0.,distance=0.,half_width=0.,leading=0.;
    int direction=1;
};
struct MotionSample:DirectionSample {
    std::optional<WorldReceipt> receipt;
    bool receipt_pending=false;
    MotionSample()=default;
    explicit MotionSample(const DirectionSample&sample):DirectionSample(sample){}
};
struct ProbeCache {
    MotionSample result;
    Point position{};
    double yaw=0.,distance=0.,leading=0.,deadline=0.;
    std::optional<double> maximum_distance;
};
struct ExactDescriptor {
    CandidateDescriptor candidate;
    Point hull_minimum{},hull_maximum{};
    double mass=0.,speed_forward=0.,speed_reverse=0.;
    bool valid=false;
};
enum class ReceiptStatus {Unavailable,Blocked,Deferred,Clear};
struct ReceiptResult {ReceiptStatus status=ReceiptStatus::Unavailable;WorldReceipt receipt;};
class MotionQueryWorld {
public:
    virtual ~MotionQueryWorld()=default;
    virtual std::optional<MotionSample> direction(std::int64_t,const Point&,double yaw,
        double speed,const ExactDescriptor&,std::optional<double>maximum)=0;
    virtual ReceiptResult receipt(std::int64_t,const Point&,double yaw,double speed,
        const ExactDescriptor&,std::optional<double>maximum,double now)=0;
};
struct MotionPlanInput {
    std::int64_t id=0;
    Point position{};
    double yaw=0.,speed=0.,turn_speed=0.,throttle=0.,turn=0.,step=0.,now=0.;
    double half_length=0.,terrain_pitch=0.;
    bool grounded=false,airborne=false,decision_due=false,refresh_control=true,exact_resolver=true;
    bool baked=true,movement_intent=true;
    double cell_size=0.;
    std::string recovery="drive";
    std::optional<Point> move_position;
    std::optional<double> recovery_probe_distance,clearance_distance;
    ExactDescriptor descriptor;
};
struct ProbePlan {
    double throttle=0.,turn=0.,travel_sign=1.,travel_yaw=0.,slope_pitch=0.;
    int movement_direction=0,rotation_direction=0;
    bool path_clear=true,pose_frozen=false,generic_collision=false,exact_collision=false;
    bool remember_failure=false,report_blocked_corridor=false;
    bool cache_reused=false,catchup_hold=false,catchup_reprobe=false;
    std::optional<double> maximum_distance;
    std::optional<MotionSample> sample;
};
bool receiptContains(const WorldReceipt&,const Point&,double yaw,double speed,double dt);
bool receiptRefreshDue(const WorldReceipt&,const Point&,double yaw,double speed,double dt);
bool cacheCovers(const ProbeCache&,std::optional<double>maximum);
bool cacheReusable(const ProbeCache&,const Point&,double yaw,double speed,double now,
    bool settled,double dt,bool ignore_deadline=false);
class MotionPlan {
    MotionQueryWorld& world_;
    std::vector<std::pair<std::int64_t,bool>> waiting_;
    struct Frame {
        std::vector<std::pair<std::int64_t,bool>> waiting;
        std::map<std::int64_t,bool> waiting_initial,requested_uncached;
        std::vector<std::int64_t> requested,deferred;
        std::set<std::int64_t> priority,requested_set,attempted;
        std::map<std::int64_t,ReceiptStatus> results;
        bool initial_first=false;
        unsigned budget=13;
    };
    std::optional<Frame> frame_;
    ReceiptResult receipt(const MotionPlanInput&,double travel_yaw,double signed_speed,bool uncached,std::optional<double>maximum);
public:
    std::map<std::int64_t,ProbeCache> cache;
    explicit MotionPlan(MotionQueryWorld&world):world_(world){}
    void beginFrame(const std::set<std::int64_t>& live);
    void endFrame();
    void clear();
    ProbePlan prepare(const MotionPlanInput&);
    bool worldReceiptReusable(std::int64_t,const Point&,double yaw,double speed,double now,double dt)const;
    bool worldCorridorReusable(std::int64_t,const Point&,double yaw,double speed,double now,double dt)const;
    const auto& waiting()const{return waiting_;}
};
}
