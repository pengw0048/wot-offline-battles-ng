#pragma once

#include "stock_native_bot_turret_obstacles.hpp"

namespace stock_native_detached_turret {
using Point=stock_native_world::Point;
using Box=stock_native_world::Box;
using Owner=stock_native_bot_turret::Owner;
using Matrix=std::array<double,9>;
constexpr double epsilon=1.e-6,step_seconds=.01,flight_step_seconds=.04;
struct Component {stock_native_bot_turret::Component geometry;double mass=0.;};
struct Properties {double mass=0.;Point centre{};Matrix inertia{},inverse_inertia{};};
struct Impact {Point point{},normal{},velocity{};double energy=0.;};
struct Checkpoint {Owner player;std::int64_t sequence=0;Point linear{},angular{};};
struct Frame {
    Point position{},attitude{},centre{},velocity{},angular_velocity{};
    bool grounded=false,sleeping=false;
    std::int64_t impact_serial=0;
    std::optional<Impact> impact;
    std::vector<Checkpoint> acknowledgements;
};
struct Contact {Point normal{},point{},momentum{},angular_momentum{},delta{},body_correction{},vehicle_correction{};};
struct Vehicle {Owner owner;std::vector<Box> boxes;double mass=0.;Point velocity{};bool human=false,alive=true;};
struct VehicleGeometry {
    stock_native_bot_turret::Component chassis,hull,turret,gun;
    Point turret_mount{},gun_mount{};
};
struct VehiclePose {Point position{},attitude{},hull_position{},hull_attitude{};bool separate_hull=false;double aim_yaw=0.,gun_pitch=0.;};
std::vector<Box> vehicleSupportBoxes(const VehicleGeometry&,const VehiclePose&,bool attached);
struct Surface {Point point{},normal{};};
class World {
public:
    virtual ~World()=default;
    // This boundary is a native filtered scenery query, including the exact
    // Vector3 conversion and canonical broken-skin filtering.
    virtual std::optional<Surface> collide(const Point&,const Point&)=0;
    virtual void applyVehicle(Vehicle&,const Contact&,double)=0;
    virtual double clock()=0;
    // A failed body slice must restore external Bot pose/velocity with the
    // body and its cumulative-impulse acknowledgement cursor.
    virtual void begin(Owner){}
    virtual void commit(Owner){}
    virtual void rollback(Owner){}
};
class Body {
    std::vector<Component> components_;
    std::vector<Point> local_points_;
public:
    Properties properties;
    Matrix rotation{};
    Point com{},velocity{},angular{};
    bool grounded=false,sleeping=false;
    std::int64_t impact_serial=0;
    std::optional<Impact> impact;
    std::vector<Checkpoint> acknowledgements;
    std::vector<Point> support_points;
    double radius=0.;
    Body(std::vector<Component>,const Frame&);
    Point position()const;
    Frame frame()const;
    std::vector<Box> boxes()const;
    std::vector<Point> points()const;
    Point inverseInertia(const Point&)const;
    Point pointVelocity(const Point&)const;
    double denominator(const Point&,const Point&)const;
    void momentum(const Point&,const Point&);
    void impulse(const Point&,const Point&);
    double kineticEnergy()const;
    void integrate(double);
    const std::vector<Component>& components()const{return components_;}
};
struct BoxContact {Point normal{};double depth=0.;Point point{};};
Properties properties(const std::vector<Component>&);
std::optional<BoxContact> boxContact(const Box&,const Box&,bool horizontal=false);
std::optional<Contact> vehicleContact(const Body&,const std::vector<Box>&,double,const Point&,bool horizontal=false);
void sceneryStep(Body&,double,World&);
Point translate(Body&,const Point&,World&);
bool supportedCom(const Body&);
double advance(Body&,double,std::vector<Vehicle>&,World&,std::optional<double> budget={});
struct Segment {Point origin{},velocity{};double start=0.,duration=0.;};
struct Flight {
    Point origin{},velocity{},rest{},impact_velocity{};
    std::vector<Segment> segments;
    double duration=0.,energy=0.;
    bool landed=false;
    std::optional<Point> contact,rest_attitude;
    std::optional<Frame> body;
};
struct Row {
    Owner owner;
    Point attitude{},spin{};
    std::optional<Owner> support;
    std::int64_t motion_sequence=0,motion_time_ms=0,created_time_ms=0;
    Flight flight;
};
Frame initialFrame(const Row&);
Row revision(const Row&,const Body&,double now_ms);
stock_native_bot_turret::Record obstacleRecord(const Row&);
struct PlayerImpulse {Owner turret;Checkpoint checkpoint;};
struct Failure {Owner owner;std::string reason;};
class Runtime {
    struct Entry {Row accepted,proposal;std::vector<Component> components;std::optional<Body> body;std::optional<Frame> published;double simulation_ms=0.;bool available=true,simulated=false;};
    std::map<Owner,Entry> entries_;
    double next_ms_=0.;
public:
    bool accept(const Row&,std::vector<Component>);
    void components(Owner owner,const std::vector<Component>& value){const auto i=entries_.find(owner);if(i!=entries_.end()&&!i->second.body)i->second.components=value;}
    bool hasBodyRecords()const{for(const auto&entry:entries_)if(entry.second.accepted.flight.body)return true;return false;}
    bool due(double server_ms)const{return server_ms>=0.&&server_ms>=next_ms_&&hasBodyRecords();}
    void available(Owner owner,bool value){const auto i=entries_.find(owner);if(i!=entries_.end())i->second.available=value;}
    // Owner order is supplied by ingress in the same order as the original
    // accepted-row dictionary; map iteration is never a substitute for it.
    std::vector<Owner> order;
    std::vector<Failure> failures;
    std::vector<Row> update(double server_ms,std::vector<Vehicle>&,
        const std::vector<PlayerImpulse>&,World&);
    void clear(){entries_.clear();order.clear();failures.clear();next_ms_=0.;}
    const Body* body(Owner)const;
    double simulationTime(Owner)const;
};
}
