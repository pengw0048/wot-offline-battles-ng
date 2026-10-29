#pragma once
#include <array>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace stock_native_projectile {
using Point = std::array<double, 3>;
struct Key {
    std::string projectile;
    int segment=0;
    bool operator<(const Key& v) const {return projectile<v.projectile || (projectile==v.projectile && segment<v.segment);}
    bool operator==(const Key& v) const {return projectile==v.projectile && segment==v.segment;}
    bool operator!=(const Key& v) const {return !(*this==v);}
};
struct Cursor {
    Key key;
    Point origin{},velocity{},gravity{},position{};
    double launch_time=0,max_time=0,max_distance=0,cursor_time=0,distance=0,maximum_step=.05;
};
struct Terminal {std::string reason;double fraction=1;};
struct Chord {Point start{},end{};double start_time=0,end_time=0;bool distance_terminal=false;};
struct CursorMetrics {std::size_t active=0,chords=0,terminals=0;double debt_before=0,debt_after=0;};
// These virtual calls stay inside the native owner; no Python object is retained.
struct CursorSink {
    virtual ~CursorSink()=default;
    virtual std::optional<Terminal> chord(const Cursor&,const Chord&)=0;
    virtual void terminal(const Cursor&,const Terminal&)=0;
};
Point trajectory(const Point&,const Point&,const Point&,double);
Point interpolate(const Point&,const Point&,double);
double length(const Point&,const Point&);
Chord prepare_chord(const Cursor&,double,double);
class Cursors {
public:
    explicit Cursors(std::size_t maximum=128,double now=0,double maximum_step=.05);
    bool launch(Cursor,std::optional<double> admission={});
    bool restore(Cursor,std::optional<double> admission={});
    bool replace(Cursor,std::optional<Key> old={},std::optional<double> admission={});
    bool rollback(const Key&);
    bool remove(const Key&);
    bool reset(std::optional<double> now={});
    bool advance(double,CursorSink&,std::optional<std::size_t> maximum_chords={});
    std::size_t sustainable(double) const;
    std::size_t size() const {return states_.size()+pending_launch_.size();}
    bool contains(const Key& k) const {return states_.count(k)||pending_launch_.count(k);}
    std::optional<Cursor> get(const Key&) const;
    std::vector<Cursor> snapshot() const;
    double now() const {return now_;}
    const CursorMetrics& metrics() const {return metrics_;}
private:
    using State=std::shared_ptr<Cursor>;
    struct Operation {bool launch=false;Key key;State state;};
    bool validate(Cursor&,std::optional<double>,bool restoring) const;
    void install(State);
    void erase(const Key&);
    void flush();
    std::size_t safely(State,double,CursorSink&,std::optional<std::size_t>);
    std::size_t one(State,double,CursorSink&,std::optional<std::size_t>);
    void finish(State,Terminal,CursorSink&);
    double debt(double) const;
    std::size_t maximum_;
    double now_,maximum_step_;
    bool advancing_=false;
    std::map<Key,State> states_;
    std::vector<Key> order_;
    std::deque<Key> round_robin_;
    std::set<Key> known_,pending_launch_,pending_remove_,cancel_current_;
    std::optional<Key> current_;
    std::vector<Operation> pending_;
    CursorMetrics metrics_{};
};
}
