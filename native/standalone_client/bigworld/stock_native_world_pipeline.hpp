#pragma once

#include "stock_motion_rotation_batch.hpp"
#include "stock_offline_query_skin.hpp"

#include <functional>
#include <optional>
#include <string>

namespace stock_native_world {
using Point = stock_offline_query::Point;
using Box = stock_offline_query::Box;
using Key = stock_offline_query::Key;
using MaterialKey = stock_offline_query::MaterialKey;
using Surface = stock_offline_query::Surface;
using LocatorSignature = std::array<std::int64_t,12>;
LocatorSignature locatorSignature(const Point & local_origin,
    const Point & chunk_translation,const std::array<Point,3> & basis,
    double quantization);
bool equivalentLocator(const LocatorSignature & live,
    const LocatorSignature & canonical,bool canonical_ambiguous);
enum class QueryStatus { Miss, Found, Failed };
enum class Kind { Tree, Falling, Fragile, Structure, Other };
enum class Admission { Pending, Admitted, Isolated };
enum class TreeIdentity {Tree,Pending,Other,Hard};
struct Hit {
    Point point{}, normal{}, translation{};
    Surface surface{};
};
struct MaterialHit {
    Hit hit;
    std::string filename;
};
struct Instance {
    Kind kind = Kind::Other;
    Admission admission = Admission::Pending;
    std::string filename;
    std::vector<Box> boxes;
    Point origin{};
    // Frozen exact DestructiblesCache.scaledDestructibleHealth outputs.
    // Structure keys are material IDs; other kinds use -1.
    std::map<std::int64_t, double> scaled_health;
    std::map<std::int64_t, double> reference_health;
    double kinetic_correction = 0.;
    double item_scale = 0.;
    double broad_phase_margin = 0.;
    double tree_health = 0.;
    bool descriptor_valid = false, native_name_exact = false;
    bool retained_collision = false;
    std::map<std::int64_t, std::vector<std::int64_t>> dependencies;
    std::map<std::int64_t, std::vector<std::int64_t>> inverse_dependencies;
};
struct Destruction {
    Kind kind = Kind::Other;
    Key key{};
    std::int64_t material = -1;
    Point position{};
    double yaw = 0., speed = 0., now = 0.;
    bool shot = false;
};
// Native acceptance updates shared collision state synchronously. The journal
// retains every accepted event until the external transport acknowledges it.
struct Event { std::uint64_t sequence = 0; Destruction destruction; };
struct Registry {
    stock_offline_query::SkinView skin;
    std::map<Key, Instance> instances;
    std::map<Key, Instance> baked_instances;
    offline_destructible::Store geometry;
    std::set<std::uint32_t> ready_tree_chunks;
    std::map<MaterialKey, double> pending;
    std::vector<Event> journal;
    std::uint64_t generation = 0, next_sequence = 1, epoch = 1;
    bool catalog_available = false;
    bool baked_dirty = false;
    double unit_vehicle_mass = 0.;
    void put(const Key &, Instance);
    void putBaked(const Key &, Instance);
    void syncBaked();
    void remove(const Key &);
    void clear();
    bool destroyed(const MaterialKey &) const;
    bool isolated(const Key &) const;
    void acknowledge(std::uint64_t sequence);
};
using Filter = std::function<bool(const Surface &)>;
struct TurretPose {
    Point position{}, hull_position{};
    double yaw=0.,pitch=0.,roll=0.,hull_yaw=0.,hull_pitch=0.,hull_roll=0.;
    bool separate_hull=false;
};
struct TurretSweep {
    std::uint64_t actor=0;
    TurretPose start,end;
    bool human=false;
};
class Scene {
public:
    virtual ~Scene() = default;
    virtual QueryStatus segment(const Point &, const Point &, std::uint16_t skip,
        const Filter &, Hit &) = 0;
    virtual QueryStatus material(const Point & start, const Point & end,
        const Point & probe, MaterialHit &) = 0;
    // Same keep polarity as segment. Implementations must not silently ignore
    // a nonempty predicate: a just-broken skin may precede an intact wall.
    virtual QueryStatus materialFiltered(const Point & start,const Point & end,
        const Point & probe,const Filter & keep,MaterialHit & out) {
        return keep?QueryStatus::Failed:material(start,end,probe,out);
    }
    // Return the exact native distance, including -1 for an absent water hit.
    virtual QueryStatus water(const Point &, const Point &, double & distance) = 0;
    // Populate a typed live snapshot from the actual native mapping. A pending
    // or contradictory native identity cannot become an admitted instance.
    virtual Admission refresh(const Key &, Registry &) = 0;
    // A streamed unresolved non-falling item may block only when its exact
    // live locator still proves these baked world boxes. Failure is no proof.
    virtual bool unresolved(const Key &, std::vector<Box> &) = 0;
    virtual bool destroy(const Destruction &) = 0;
    virtual bool treeChunks(const std::vector<Box> &,
        std::vector<std::uint32_t> & chunks) = 0;
    virtual bool warmTreeChunks(const std::vector<std::uint32_t>&){return false;}
    virtual TreeIdentity trustedTree(const Key&){return TreeIdentity::Pending;}
    virtual std::uint64_t streamRevision()const{return 0;}
    virtual bool turretSweep(const TurretSweep &) = 0;
};
struct ActorInputs {
    double mass = 0., now = 0., yaw = 0., speed = 0.;
    double kinetic_speed = 0.;
    bool has_kinetic_speed = false, allow_kinetic = false;
    bool commit_enabled = true;
};
enum class ContactStatus { Clear, Crushed, Approach, Kinetic, Hard, Pending, Failed };
enum class SoftStatus {Hard,Clear,Kinetic,Deferred,PendingHard,Failed};
struct ContactResult {
    ContactStatus status = ContactStatus::Clear;
    std::set<MaterialKey> contacts;
    bool accepted_now = false, used_kinetic_speed = false;
    bool requires_commit = false;
    bool pending = false;
    double impact_speed = 0.;
    std::set<Kind> kinds;
};
struct RotationRequest {
    stock_motion_rotation::Snapshot world;
    Point hull_minimum{},hull_maximum{};
    double dt=0.,rotation_speed_cap=0.,drive_speed_cap=0.;
    bool has_rotation_speed_cap=false,has_drive_speed_cap=false;
    const TurretSweep * turret=nullptr;
    std::optional<Point> geometry_end;
};
struct MotionRequest {
    WorldSnapshot world{};
    Point body_minimum{},body_maximum{},hull_minimum{},hull_maximum{};
    bool allow_crush_drive=false,commit_enabled=true;
    const TurretSweep * turret=nullptr;
    // The caller's Python geometry position precedes Vector3 rounding. Tree
    // endpoint construction uses it before rounding the complete endpoint.
    std::optional<Point> geometry_position;
};
struct Resolution {
    ContactStatus status=ContactStatus::Clear;
    ContactResult contact;
    stock_motion_rotation::Trace trace;
    bool world_blocked=false,turret_blocked=false;
};
class Pipeline final : public stock_motion_rotation::WorldFrameFactory {
    Registry & registry_;
    Scene & scene_;
    ActorInputs actor_;
    std::uint16_t skip_flags_;
    unsigned recast_budget_;
    double epsilon_;
    stock_offline_query::SkinTuning skin_tuning_;
public:
    Pipeline(Registry &, Scene &, std::uint16_t skip_flags,
        unsigned recast_budget = 4, double epsilon = .001,
        stock_offline_query::SkinTuning tuning = {.001, .075, 8.});
    Registry & registry() { return registry_; }
    Scene & scene() { return scene_; }
    void actor(const ActorInputs & value) { actor_ = value; }
    const ActorInputs & actor() const { return actor_; }
    QueryStatus ground(const Point &, const Point &, Hit &,
        const std::set<Key> * members = nullptr);
    QueryStatus water(const Point &, const Point &, double &);
    QueryStatus horizontal(const Point &, const Point &, Hit &,
        const std::set<Key> * members = nullptr);
    QueryStatus navigation(const Point &,const Point &,Hit &,
        bool descriptor_present=true);
    SoftStatus softStatic(const Point &,const Point &,const Hit &,
        bool pending_first,bool allow_kinetic,unsigned & budget);
    stock_motion_rotation::WorldResult world(const WorldSnapshot &,
        stock_motion_rotation::DeparturePolicy * = nullptr);
    stock_motion_rotation::Result rotation(const stock_motion_rotation::Snapshot &);
    ContactResult catalogMotion(const offline_destructible::Pose & sweep,
        double speed, double dt, bool kinetic_commit,
        const offline_destructible::Pose * physical_contact = nullptr,
        bool spawn_overlap = false, bool proposal_only = false);
    ContactResult poseSweep(const RotationRequest &, bool commit_enabled);
    Resolution resolveRotation(const RotationRequest &);
    Resolution resolveMotion(const MotionRequest &);
    ContactResult trees(const Point & start, double start_yaw, const Point & end,
        double end_yaw, const Point & minimum, const Point & maximum,
        double speed, double dt, double pivot = 0., bool commit = true,
        const std::set<MaterialKey> * requested = nullptr);
    ContactResult occupiedScan(const offline_destructible::Pose&,double speed,
        const std::vector<std::uint32_t>& chunks);
    void retryTreeContacts(double now);
    void beginSlice(double now);
    bool crushable(const MaterialKey &, double speed) const;
    bool commit(const MaterialKey &, const Point &, double speed);
    bool commit(const MaterialKey &, const Point &, double speed, double yaw);
    // 0 hard, 1 clear, 2 kinetic; -1 query/identity failure.
    int resolve(const Point &, const Point &, const Hit &, bool & crush_state,
        bool commit_enabled);
    std::unique_ptr<stock_motion_rotation::WorldFrame> begin(const WorldSnapshot &,
        stock_motion_rotation::DeparturePolicy *) override;
private:
    QueryStatus ray(const Point &, const Point &, Hit &, const Filter &, bool skin);
    std::vector<MaterialKey> candidates(const Point &, const Point &, const Point &,
        const std::set<MaterialKey> &, bool prefer_destroyed);
    int softPath(const Point &, const Point &, const Hit &, bool pending_first,
        bool allow_kinetic, unsigned & budget);
    bool commitOne(const MaterialKey &, const Point &, double speed, double yaw);
    struct TreeRetry {
        Point start,end,minimum,maximum;
        double start_yaw,end_yaw,speed,dt,pivot,stamp;
        ActorInputs actor;
        std::uint64_t epoch;
    };
    std::map<std::set<MaterialKey>,TreeRetry> tree_retries_;
    double synchronized_time_=0.;
    std::uint64_t synchronized_epoch_=0;
    ContactStatus completeTrees(const ContactResult &, const Point &, double,
        const Point &, double, const Point &, const Point &, double, double,
        double, bool, ContactStatus);
};
}
