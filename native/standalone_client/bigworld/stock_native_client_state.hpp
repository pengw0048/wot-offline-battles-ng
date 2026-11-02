#pragma once
#include <array>
#include <cstdint>
#include <initializer_list>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
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
    // An Integer above int64 (wire u64, e.g. damage stickers): `integer` holds its bits.
    bool unsigned_integer=false;
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
// Accepted contact history owns exactly the newest 512 revisions. Physical
// slots stay fixed; chronological indexes follow head across wraparound.
class ReceiptHistory {
    static constexpr std::size_t limit=512;
    using Storage=std::array<Sample,limit>;
    std::unique_ptr<Storage> slots_;
    std::size_t head_=0,count_=0;
    std::size_t bound(double time,std::optional<std::int64_t> revision,bool upper)const{
        std::size_t first=0,length=count_;
        while(length){const auto step=length/2,index=first+step;const auto&row=(*this)[index];
            const bool less=revision?std::make_pair(row.time,row.revision.value_or(-1))<std::make_pair(time,*revision):row.time<time;
            const bool equal=revision?row.time==time&&row.revision.value_or(-1)==*revision:row.time==time;
            if(less||(upper&&equal)){first=index+1;length-=step+1;}else length=step;
        }return first;
    }
public:
    ReceiptHistory()noexcept=default;
    ReceiptHistory(std::initializer_list<Sample> rows){for(const auto&row:rows)push_back(row);}
    ReceiptHistory(const ReceiptHistory&v):slots_(v.slots_?std::make_unique<Storage>(*v.slots_):nullptr),head_(v.head_),count_(v.count_){}
    ReceiptHistory& operator=(const ReceiptHistory&v){if(this!=&v){ReceiptHistory copy(v);slots_.swap(copy.slots_);std::swap(head_,copy.head_);std::swap(count_,copy.count_);}return *this;}
    ReceiptHistory(ReceiptHistory&&v)noexcept:slots_(std::move(v.slots_)),head_(std::exchange(v.head_,0)),count_(std::exchange(v.count_,0)){}
    ReceiptHistory& operator=(ReceiptHistory&&v)noexcept{if(this!=&v){slots_=std::move(v.slots_);head_=std::exchange(v.head_,0);count_=std::exchange(v.count_,0);}return *this;}
    std::size_t size()const noexcept{return count_;}
    bool empty()const noexcept{return count_==0;}
    Sample& operator[](std::size_t index){return (*slots_)[(head_+index)%limit];}
    const Sample& operator[](std::size_t index)const{return (*slots_)[(head_+index)%limit];}
    Sample& front(){return (*this)[0];}const Sample& front()const{return (*this)[0];}
    Sample& back(){return (*this)[count_-1];}const Sample& back()const{return (*this)[count_-1];}
    void push_back(Sample row){if(!slots_)slots_=std::make_unique<Storage>();if(count_<limit){(*this)[count_]=std::move(row);++count_;}else{(*slots_)[head_]=std::move(row);head_=(head_+1)%limit;}}
    void clear()noexcept{for(std::size_t i=0;i<count_;++i)(*this)[i]=Sample{};head_=count_=0;}
    std::size_t lowerBound(double time)const{return bound(time,{},false);}
    std::size_t lowerBound(double time,std::int64_t revision)const{return bound(time,revision,false);}
    std::size_t upperBound(double time,std::int64_t revision)const{return bound(time,revision,true);}
};
struct Bounds {Point minimum{},maximum{};};
// An owned reference to a stock Python object (arena properties, track parameters) kept in battle state.
class Handle {
    _object* object_=nullptr;
public:
    Handle()noexcept=default;
    explicit Handle(_object* owned)noexcept:object_(owned){}
    Handle(const Handle&);
    Handle& operator=(const Handle&);
    Handle(Handle&&v)noexcept:object_(std::exchange(v.object_,nullptr)){}
    Handle& operator=(Handle&&v)noexcept{if(this!=&v){Handle old(std::move(*this));object_=std::exchange(v.object_,nullptr);}return *this;}
    ~Handle();
    _object* get()const noexcept{return object_;}
    explicit operator bool()const noexcept{return object_!=nullptr;}
};
// What the battle has presented to #1513 for one vehicle, beside its accepted authority state.
struct Presentation {
    std::int64_t network_id=0;
    bool remote=false;              // created by the remote factory, not a server-entered stock Vehicle
    bool initialized=false,simulation_entity=false,arena_added=false,destroy_requested=false;
    double ready_deadline=0.;
    Handle properties;              // stock arena properties for arena_vehicle_added
    Value state;                    // presented state: accepted state plus local death and critical edges
    std::optional<Pose> pending_pose,presented_pose;
    std::optional<std::int64_t> presented_time_us;
    // Spotting gates and HUD components.
    bool spot_visible=true,spot_marker_visible=true,world_marker_visible=false,direct_spot_visible=false;
    bool world_marker_started=false,minimap_started=false,dead_marker_known=false,minimap_rebound=false,deferred_health=false;
    double spot_until=0.,radio_spot_until=0.,spot_next=0.,shot_penalty_until=0.;
    std::optional<double> still_since;
    std::optional<std::array<bool,4>> spot_signature;
    // Authority edges already shown through stock callbacks.
    std::optional<std::int64_t> frags,siege_state;
    bool team_killer=false;
    std::optional<std::string> stun_signature;
    std::optional<std::array<std::int64_t,4>> health;   // health, display health, crew active, death reason
    Value critical;
    std::int64_t critical_revision=-1,critical_base_revision=0,critical_ack_seq=0;
    // Accepted simulation state is independent of successful stock HUD delivery.
    struct CriticalNotification {
        Value event;
        std::int64_t attacker=0,index=0,extra=0;
    };
    std::map<std::string,CriticalNotification> critical_hud_pending;
    std::vector<std::string> critical_hud_order;
    Handle critical_hud_entity;
    std::int64_t critical_hud_engine_id=0;
    std::uint64_t critical_hud_epoch=0;
    bool critical_layout_prewarmed=false;
    std::string ammo_death_cause;
    // Pose, aim, settle and track writes already made, so unchanged samples cost no engine call.
    std::optional<std::array<double,8>> pose_signature,settled_signature;
    std::optional<std::array<double,5>> aim_signature;
    std::uint64_t pose_generation=0;
    bool track_pending=false;
    double track_next_update=0.;
    std::optional<std::array<double,3>> track_signature;
    std::optional<std::array<double,2>> track_turn_sample;   // time, yaw
    Handle track_params;
    Value collision_pose;           // the projectile collision pose of the latest presented sample
};
struct Actor {
    std::string key,kind;std::int64_t id=0,engine_id=0;
    std::uint64_t revision=0,descriptor_revision=0;
    bool local=false,has_current=false,has_target=false,missing=false;
    Pose current{},target{},velocity{};
    std::vector<Sample> samples;ReceiptHistory receipts;
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
    Presentation presented;
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
