#pragma once
#include "stock_native_projectile_chord.hpp"
#include "stock_native_world_pipeline.hpp"
#include <deque>
namespace stock_native_projectile {
struct Launch {
    std::string id,vehicle,source_signature;
    std::int64_t shooter=0,sequence=0,group_sequence=0,burst_index=0,burst_count=0,shell_index=0;
    Point origin{},velocity{},range_origin{},segment_origin{},segment_velocity{};
    double gravity=0,maximum_distance=0,splash_radius=0,penetration_factor=0,multiplier=1,checked_distance=0,piercing_loss=0;
    std::int64_t maximum_ms=0,launch_ms=0,segment_ms=0,checked_ms=0;
    int segment=0;bool he=false;
    std::optional<std::int64_t> presentation_us;
    damage::FrozenShot shot;
};
bool valid(const Launch&);
bool same_launch(const Launch&,const Launch&);
struct Frontier {
    std::string projectile;
    std::int64_t base_ms=0,checked_ms=0;
    double distance=0,loss=0,penetration_factor=0;
    std::vector<stock_native_world::Destruction> destructibles;
};
struct Proposal {
    Frontier frontier;
    std::string outcome;
    Point impact{},segment_origin{},segment_velocity{};
    std::optional<damage::Effect> direct;
    std::vector<damage::Effect> splash;
    std::optional<damage::TargetIdentity> wreck;
    bool hit_vehicle=false,ricochet=false;
    double multiplier=1;
};
struct Dispatch {
    std::uint64_t serial=0;
    std::int64_t epoch=0;
    std::string kind;
    std::vector<Frontier> progress;
    std::optional<Proposal> proposal;
};
struct Flight {
    Launch canonical;
    FlightMeta meta;
    std::optional<double> anchor;
    double acked_distance=0,acked_loss=0;
    std::vector<stock_native_world::Destruction> destructibles;
    std::optional<Frontier> progress_pending;
    std::optional<Proposal> terminal;
    std::optional<Cursor> terminal_cursor;
    bool awaiting=false,wire_frozen=false;
};
class OwnerWorld:public ChordWorld {
public:
    virtual std::vector<Record> records()=0;
    virtual damage::Resolution resolve(Flight&,const Cursor&,const TerminalData&)=0;
    virtual void confirmed(const Launch&){}
    virtual std::map<std::string,double> presentationOffsets(const Launch&,double){return {};}
    virtual std::vector<stock_native_world::Destruction> takeDestructions()=0;
};
struct OwnerMetrics {
    std::uint64_t launches=0,restores=0,rejected=0,local_failures=0,terminal_proposals=0;
    // Owner-lifetime work counters; active and debt remain current gauges.
    // Cursors and ChordResolver retain their per-advance internal metrics.
    CursorMetrics cursor;
    ChordMetrics chord;
};
struct AdvanceMetrics {
    CursorMetrics cursor;
    ChordMetrics chord;
};
class Owner:private CursorSink {
    OwnerWorld& world_;
    History history_;
    ChordResolver resolver_;
    Cursors cursors_;
    std::map<std::string,Flight> flights_;
    std::map<std::string,TerminalData> terminals_;
    std::set<std::string> terminal_fence_,resolution_fence_;
    std::deque<std::string> terminal_fifo_,resolution_fifo_;
    std::map<std::uint64_t,Dispatch> outbox_;
    std::uint64_t next_serial_=1;
    std::optional<std::int64_t> epoch_,server_ms_;
    double server_local_=0,next_progress_=0;
    OwnerMetrics metrics_;
    AdvanceMetrics last_advance_;
    bool setEpoch(std::int64_t,double);
    std::optional<Terminal> chord(const Cursor&,const Chord&)override;
    void terminal(const Cursor&,const Terminal&)override;
    Flight* install(const Launch&);
    Cursor snapshot(Flight&,double);
    void fence(const std::string&,bool resolution);
    Frontier frontier(const Flight&,const Cursor&)const;
    void pending();
    void progress();
    bool admit(const Launch&,double,bool restore);
public:
    explicit Owner(OwnerWorld&w):world_(w),resolver_(history_,w),cursors_(128){}
    bool observe(std::optional<std::int64_t> epoch,std::optional<std::int64_t> server_ms,double local_anchor,double now);
    bool launch(const Launch&,double now);
    bool reconcile(const std::vector<Launch>&,const std::set<std::string>& reported,double now);
    bool terminalEvent(const std::string&,std::optional<std::int64_t> epoch={});
    bool advance(double,bool force=false);
    std::vector<Dispatch> dispatches()const;
    bool acknowledge(std::uint64_t,bool sent);
    const OwnerMetrics& metrics()const{return metrics_;}
    const AdvanceMetrics& lastAdvanceMetrics()const{return last_advance_;}
    std::optional<Pose> historic(const std::string&key,double at)const{return history_.at(key,at);}
    const std::map<std::string,Flight>& flights()const{return flights_;}
    std::optional<std::int64_t> epoch()const{return epoch_;}
    std::optional<std::int64_t> serverTime(double)const;
};
}
