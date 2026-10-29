#pragma once
#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>
struct _object;
namespace stock_native_client_state {
using Point=std::array<double,3>;
// x, y, z, yaw, pitch, roll, absolute aim yaw, gun pitch.
using Pose=std::array<double,8>;
struct Value;
// Empty/primitive Values must not construct a tree (MSVC allocates its head).
// Copies own their fields; moves transfer the tree without allocating a head.
class ObjectFields {
    using Map=std::map<std::string,Value>;
    std::unique_ptr<Map> fields_;
    Map& writable();
    static Map& emptyFields();
public:
    using iterator=Map::iterator;
    using const_iterator=Map::const_iterator;
    ObjectFields()noexcept=default;
    ~ObjectFields();
    ObjectFields(const ObjectFields&);
    ObjectFields& operator=(const ObjectFields&);
    ObjectFields(ObjectFields&&)noexcept;
    ObjectFields& operator=(ObjectFields&&)noexcept;
    Value& operator[](const std::string&);
    std::pair<iterator,bool> emplace(std::string,Value);
    iterator begin();iterator end();
    const_iterator begin()const;const_iterator end()const;
    iterator find(const std::string&);const_iterator find(const std::string&)const;
    std::size_t count(const std::string&)const;
    std::size_t size()const noexcept;
    bool empty()const noexcept;
    std::size_t erase(const std::string&);
    void clear()noexcept;
    bool operator==(const ObjectFields&)const;
};
// Owned protocol data. No Python object is retained by the battle state.
struct Value {
    enum class Kind {Null,Boolean,Integer,Number,Text,Array,Object,Shared};
    Kind kind=Kind::Null;bool boolean=false;std::int64_t integer=0;double number=0.;
    std::string text;std::vector<Value> array;ObjectFields object;
    std::shared_ptr<const Value> reference;
    const Value& resolved()const{return reference?reference->resolved():*this;}
    bool operator==(const Value&)const;
    bool operator!=(const Value&v)const{return !(*this==v);}
    const Value& get(const std::string&)const;
    double numeric(double fallback=0.)const;
    bool truth(bool fallback=false)const;
};
// Keep passive Actor/State consumers independent of the Python data decoder.
inline ObjectFields::~ObjectFields()=default;
struct Sample {double time=0.;std::optional<std::int64_t> revision;Pose value{};Value state;};
struct Bounds {Point minimum{},maximum{};};
struct Actor {
    std::string key,kind;std::int64_t id=0,engine_id=0;
    std::uint64_t revision=0,descriptor_revision=0;
    bool local=false,has_current=false,has_target=false,missing=false;
    Pose current{},target{},velocity{};
    std::vector<Sample> samples,receipts;
    double target_time=0.,sample_time=0.,anchor_time=0.,anchor_local=0.,ideal=0.,delay=0.,observed=0.;
    std::optional<double> cursor,snapshot_interval,previous_sample;
    std::optional<std::int64_t> sample_revision;
    std::optional<std::array<std::int64_t,4>> presentation_bracket;
    bool dead=false,settled=false,timed=false,teleport=false,resume=false;
    bool has_observed=false,has_ideal=false,has_delay=false,has_anchor=false,has_resume_sample=false;
    bool warmup_active=false;unsigned warmup_intervals=0;
    double resume_sample=0.,render_delta=0.,speed=0.,drive=0.,rotation=0.;
    bool alive=true,airborne=false,visible=true,ready=false,tombstone=false,detached=false,native_remote=false;std::int64_t siege_state=0;
    // Accepted state may be an immutable shared Value; use get()/resolved().
    Value state,manifest,coalesced_timing;
    std::optional<Bounds> bounds;
    // Resource owners may attach their immutable native geometry without
    // copying it through Python. Its concrete type remains adapter-owned.
    std::shared_ptr<const void> geometry;
    std::uint64_t probe_frames=0,probe_held=0,probe_moving_held=0;
    double probe_max_gap_ms=0.;
};
struct Event {Value payload;};
static_assert(std::is_nothrow_move_constructible<Value>::value,"Value relocation must not copy nested data");
static_assert(std::is_nothrow_move_constructible<Sample>::value,"Sample relocation must not copy accepted state");
static_assert(std::is_nothrow_move_constructible<Event>::value,"Event relocation must not copy payloads");
struct State {
    std::map<std::string,std::shared_ptr<Actor>> actors;
    Value round;
    std::uint64_t epoch=1,revision=0;
    std::int64_t local_player_id=0;
    std::optional<std::int64_t> sequence,bot_revision,motion_time,bot_time,order_revision;
    std::optional<std::int64_t> server_time_ms;
    double received_at=0.;
    std::optional<bool> timed_bots;
    std::string timing_phase;
    bool reset_pending=false,retired=false;
    std::optional<double> last_advance;
    void reset(Value);
    const Actor* find(const std::string&)const;
    Actor* find(const std::string&);
    bool updateLocal(const Pose&,const Point& velocity={},double speed=0.);
};
Value read(_object*);
_object* write(const Value&);
std::vector<Event> manifest(State&,const Value&);
std::vector<Event> snapshot(State&,const Value&,double now);
// Shared handle installed on battle._native_client_state. The handle and all
// consumers retain State independently; retirement fences every consumer.
_object* capsule(const std::shared_ptr<State>&);
std::shared_ptr<State> shared(_object* capsule);
std::shared_ptr<State> fromBattle(_object* battle);
}
