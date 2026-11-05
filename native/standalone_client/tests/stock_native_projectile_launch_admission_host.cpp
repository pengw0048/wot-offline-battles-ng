// Host boundary for the actual Bot launch selection, freezing and confirmation methods.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <deque>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
using ID=std::int64_t;
using Point=std::array<double,3>;
struct Value {
    enum class Kind {Null,Integer,Number,Text,Object,Array};
    Kind kind=Kind::Null;ID integer=0;double number=0.;std::string text;
    std::map<std::string,Value> object;std::vector<Value> array;
    const Value& resolved()const{return *this;}
    const Value& get(const std::string& key)const{static Value none;auto i=object.find(key);return i==object.end()?none:i->second;}
};
using Kind=Value::Kind;
std::string roundText(const Value& v){return std::to_string(v.integer);}
namespace w {
Value object(){Value v;v.kind=Kind::Object;return v;}
Value array(){Value v;v.kind=Kind::Array;return v;}
Value integer(ID n){Value v;v.kind=Kind::Integer;v.integer=n;return v;}
Value number(double n){Value v;v.kind=Kind::Number;v.number=n;return v;}
Value text(std::string s){Value v;v.kind=Kind::Text;v.text=std::move(s);return v;}
Value boolean(bool v){return integer(v);}
ID wide(const Value& v){return v.integer;}
const std::string& string(const Value& v){return v.text;}
template<std::size_t N>Value numbers(const std::array<double,N>& ns){auto v=array();for(double n:ns)v.array.push_back(number(n));return v;}
}
namespace p {using Error=std::runtime_error;}
enum class ActorKind {Bot};
struct ActorKey {ActorKind kind=ActorKind::Bot;ID id=0;};
struct LaunchRecord {
    ActorKey actor;struct Edge {ID shot_seq=0,group_seq=0;int shell=0,index=0,count=1;} edge;
    Point origin{},velocity{};struct Pose {Point position{};double yaw=0.,pitch=0.,roll=0.;} pose;
    bool has_origin=true,artillery=false,has_shells_before=false;
    ID launch_time_us=0;int maximum_time_ms=20000,shells_before=0;double yaw=0.,pitch=0.,gravity=9.81,maximum_distance=1000.;
};
namespace damage {
struct LaunchDescriptor {double speed=100.,gravity=9.81,maximum=1000.;struct Shot {struct Shell {std::string kind="ARMOR_PIERCING";} shell;double explosion_radius=0.;} shot;};
double sampleShellValue(double,int& calls){return .75+.01*++calls;}
}
Value shotWire(const damage::LaunchDescriptor& d){auto v=w::object();v.object["speed"]=w::number(d.speed);return v;}
double length(const Point& a,const Point& b){double n=0.;for(unsigned i=0;i<3;++i)n+=(a[i]-b[i])*(a[i]-b[i]);return std::sqrt(n);}
struct Battle {struct Projectiles {std::map<ID,ID>bot_fire_seen;} storage;Projectiles*projectiles=&storage;};
namespace link {std::optional<ID>authorityEpoch(Battle&){return 1;}}
struct Runtime {
    std::deque<LaunchRecord> pending;std::map<ID,int>states;std::vector<std::pair<ID,ID>>acks;
    struct Actor {int mode=0;};std::map<ID,Actor> actors_;
    struct Weapons {Runtime& r;const std::deque<LaunchRecord>&pending()const{return r.pending;}};
    Weapons weapons(){return {*this};}const std::map<ID,Actor>& actors()const{return actors_;}
    bool acknowledgeLaunch(ID bot,ID seq){auto i=std::find_if(pending.begin(),pending.end(),[&](const auto& l){return l.actor.id==bot;});if(i==pending.end()||i->edge.shot_seq!=seq)return false;pending.erase(i);acks.push_back({bot,seq});return true;}
};
struct Service {bool available=true,record_ready=true;std::array<std::vector<damage::LaunchDescriptor>,2> launch_shots;std::array<std::map<int,std::array<std::optional<unsigned>,2>>,2>projectile_stickers;};
struct Services {std::map<ID,Service>rows;Service*find(ActorKey k){auto i=rows.find(k.id);return i==rows.end()?nullptr:&i->second;}};
struct Launch {ID shooter=0,sequence=0;std::string id;};
std::optional<ID>optionalInteger(const Value& v,const char* key){const auto& field=v.get(key);return field.kind==Kind::Null?std::nullopt:std::optional<ID>(w::wide(field));}
bool readLaunch(const Value&,Launch&,std::string*){return false;}
struct Owner {
    int launches=0;std::vector<std::string>terminals;std::optional<ID> epoch_ = 1;
    std::optional<ID>epoch()const{return epoch_;}
    bool terminalEvent(const std::string& id,std::optional<ID>){terminals.push_back(id);return true;}
    bool launch(const Launch&,double){++launches;return true;}
};
struct LaunchMessage {ID bot=0,sequence=0;Value message;};
struct State {
    struct FrozenLaunch {LaunchRecord source;damage::LaunchDescriptor descriptor;Point velocity{};double factor=1.;std::optional<ID>epoch;std::array<std::optional<unsigned>,2>stickers;};
    Runtime runtime;Services services;Battle storage;Battle*battle=&storage;Owner owner;std::string round="1";int random=0,errors=0,rejected=0;bool live=true;
    std::map<ID,ID>confirmed_sequences,transport_admitted_sequences,next_sequences;
    std::map<ID,std::set<ID>>confirmations;
    std::map<std::pair<ID,ID>,FrozenLaunch>launch_payloads;
    std::map<std::string,std::array<std::optional<unsigned>,2>>source_stickers;
    std::vector<LaunchRecord>launch_batch;std::set<ID>blocked_bots;
    std::optional<std::pair<ID,ID>>sending;std::size_t launch_cursor=0;
    void current(){if(!live)throw p::Error("stale test owner");}
    void error(const char*,const std::exception&){++errors;}
    void refresh(){}bool observe(const Value&,double){return true;}
    @PACKED@
    @LAUNCHES@
    @RESULT@
    @CONFIRM_SEQUENCE@
    @CONFIRMED@
    @MATCHING_ROUND@
    @EVENT@
};
void require(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
void add(State& s,ID bot,ID sequence,int count=1){
    s.runtime.actors_[bot]={};auto& service=s.services.rows[bot];for(auto& shots:service.launch_shots)shots.resize(5);
    LaunchRecord r;r.actor.id=bot;r.edge.shot_seq=sequence;r.edge.shell=int(sequence%5);r.edge.group_seq=count>1?1:sequence;r.edge.index=count>1?int(sequence-1):0;r.edge.count=count;r.launch_time_us=sequence*100000;r.origin={double(sequence),1.,2.};
    s.runtime.pending.push_back(r);
}
Value publication(const State& s){auto v=w::object();v.object["type"]=w::text("bot_state");auto rows=w::array();for(const auto&r:s.runtime.pending){auto row=w::object();row.object["id"]=w::integer(r.actor.id);row.object["fire_seq"]=w::integer(r.edge.shot_seq);rows.array.push_back(row);}v.object["launches"]=std::move(rows);return v;}
void onceAndCanonical(){
    State s;add(s,7,1);auto msg=publication(s);auto wave=s.launches(&msg);require(wave.size()==1,"first shot unavailable");
    require(!s.launchResult(8,1,true),"wrong identity accepted a transport result");
    require(s.sending.has_value(),"wrong identity consumed the pending result");
    require(s.launchResult(7,1,true),"transport result unavailable");
    for(unsigned i=0;i<30;++i)require(s.launches(&msg).empty(),"admitted launch duplicated before canonical confirmation");
    require(s.runtime.pending.size()==1&&s.runtime.acks.empty()&&s.confirmed_sequences[7]==0,"enqueue success acknowledged canonical weapon state");
    require(s.launch_payloads.size()==1&&s.random==1,"admitted launch lost its frozen source");
    s.confirmed({7,1,"1:b:7:1"});
    require(s.runtime.pending.empty()&&s.runtime.acks.size()==1&&s.confirmed_sequences[7]==1&&s.launch_payloads.empty(),"canonical confirmation did not retire the original pending source");
    s.confirmed({7,1,"1:b:7:1"});require(s.runtime.acks.size()==1,"duplicate canonical confirmation acknowledged twice");
    State next_round;add(next_round,7,1);auto next=publication(next_round);require(next_round.launches(&next).size()==1,"new owner inherited prior-round transport admission");
    next_round.live=false;bool stale=false;try{next_round.launches(nullptr);}catch(const p::Error&){stale=true;}require(stale,"stale owner selected a launch");
}
void refusedFrozenSource(){
    State s;add(s,1,1);add(s,2,1);auto msg=publication(s);auto a=s.launches(&msg);require(a.size()==1&&a[0].bot==1,"first Bot order changed");
    const double factor=a[0].message.get("penetration_factor").number;const auto velocity=a[0].message.get("velocity").array[2].number;
    s.launchResult(1,1,false);auto b=s.launches(nullptr);require(b.size()==1&&b[0].bot==2,"one refused Bot blocked a different Bot");s.launchResult(2,1,true);require(s.launches(nullptr).empty(),"finished refusal wave remained live");
    s.services.rows[1].launch_shots[0][1].speed=999.;
    auto retry=s.launches(&msg);require(retry.size()==1&&retry[0].bot==1,"refused launch could not retry");
    require(retry[0].message.get("penetration_factor").number==factor&&retry[0].message.get("velocity").array[2].number==velocity&&s.random==2,"refused launch rebound its frozen payload");
    s.launchResult(1,1,true);require(s.launches(&msg).empty(),"successfully retried launch duplicated");
}
void burstAndGap(){
    State s;for(ID i=1;i<=64;++i)add(s,9,i,64);auto msg=publication(s);std::vector<ID> admitted;
    auto wave=s.launches(&msg);
    while(!wave.empty()){const auto row=wave.front();require(row.message.get("shell_index").integer==row.sequence%5&&row.message.get("burst_index").integer==row.sequence-1&&row.message.get("burst_count").integer==64,"burst source shell or identity changed");const bool ok=row.sequence!=20;s.launchResult(9,row.sequence,ok);if(ok)admitted.push_back(row.sequence);wave=s.launches(nullptr);}
    require(admitted.size()==19&&s.runtime.acks.empty(),"mid-burst refusal crossed its admission boundary");
    wave=s.launches(&msg);while(!wave.empty()){require(wave[0].sequence==ID(admitted.size()+1),"mid-burst recovery repeated or skipped a shot");admitted.push_back(wave[0].sequence);s.launchResult(9,wave[0].sequence,true);wave=s.launches(nullptr);}
    require(admitted.size()==64&&s.launches(&msg).empty()&&s.runtime.pending.size()==64,"burst admission duplicated or prematurely acknowledged sources");
    s.confirmed({9,2,"1:b:9:2"});require(s.runtime.acks.empty(),"out-of-order canonical event skipped the first source");
    for(ID seq=1;seq<=64;++seq)s.confirmed({9,seq,"1:b:9:"+std::to_string(seq)});
    require(s.runtime.pending.empty()&&s.launch_payloads.empty()&&s.runtime.acks.size()==64,"burst canonical retirement did not clean frozen sources");
    State gap;add(gap,3,1);add(gap,3,3);auto g=publication(gap);auto first=gap.launches(&g);gap.launchResult(3,1,true);require(gap.launches(nullptr).empty(),"sequence gap admitted a later shot");
    add(gap,3,2);std::sort(gap.runtime.pending.begin(),gap.runtime.pending.end(),[](const auto&a,const auto&b){return a.edge.shot_seq<b.edge.shot_seq;});g=publication(gap);auto resumed=gap.launches(&g);require(resumed.size()==1&&resumed[0].sequence==2,"sequence gap did not resume at its missing shot");gap.launchResult(3,2,true);resumed=gap.launches(nullptr);require(resumed.size()==1&&resumed[0].sequence==3,"sequence gap did not retain its later shot");
    State restored;restored.confirmed_sequences[4]=7;add(restored,4,8);auto r=publication(restored);require(restored.launches(&r).size()==1,"canonical baseline was ignored by transport admission");
}
Value failure(ID bot,ID seq,ID round=1,ID epoch=1){auto v=w::object();v.object["kind"]=w::text("projectile_launch_failed");v.object["shooter_id"]=w::integer(bot);v.object["shot_seq"]=w::integer(seq);v.object["round_id"]=w::integer(round);v.object["authority_epoch"]=w::integer(epoch);v.object["reason"]=w::text("canonical_vehicle_dead");return v;}
void canonicalFailure(){
    State s;for(ID seq=1;seq<=3;++seq)add(s,7,seq);auto msg=publication(s);auto wave=s.launches(&msg);while(!wave.empty()){s.launchResult(wave[0].bot,wave[0].sequence,true);wave=s.launches(nullptr);}
    require(!s.event(failure(7,2,2),0.)&&!s.event(failure(7,2,1,2),0.),"failure crossed round or authority identity");
    auto missing=failure(7,2);missing.object.erase("round_id");require(!s.event(missing,0.),"failure without round identity was admitted");missing=failure(7,2);missing.object.erase("authority_epoch");require(!s.event(missing,0.),"failure without authority identity was admitted");
    require(!s.event(failure(8,1),0.)&&!s.event(failure(7,4),0.)&&!s.event(failure(7,0),0.),"unowned failure retired a source");
    require(s.runtime.acks.empty()&&s.errors==0&&s.owner.terminals.empty(),"invalid failure changed source state");
    require(s.event(failure(7,2),0.),"canonical failure refused");
    require(s.runtime.acks.empty()&&s.launch_payloads.size()==3&&s.errors==1&&s.owner.terminals==std::vector<std::string>{"1:b:7:2"}&&s.owner.launches==0,"out-of-order failure fabricated a launch or skipped an earlier source");
    require(s.event(failure(7,2),0.)&&s.errors==1&&s.owner.terminals.size()==1,"duplicate failure applied twice");
    s.confirmed({7,1,"1:b:7:1"});
    require(s.runtime.acks==std::vector<std::pair<ID,ID>>{{7,1},{7,2}}&&s.runtime.pending.size()==1&&s.launch_payloads.size()==1,"success followed by failure did not retire a contiguous source prefix");
    require(!s.source_stickers.count("1:b:7:2"),"failure fabricated launch presentation metadata");
    s.confirmed({7,3,"1:b:7:3"});
    require(s.runtime.pending.empty()&&s.launch_payloads.empty()&&s.runtime.acks.size()==3,"later success could not retire after failure");
    require(s.event(failure(7,2),0.)&&s.runtime.acks.size()==3&&s.errors==1,"late duplicate failure changed a retired source");
    require(s.launches(&msg).empty(),"canonical failure reopened reliable transport admission");
}
int main(){try{onceAndCanonical();refusedFrozenSource();burstAndGap();canonicalFailure();std::cout<<"Bot enqueue-once, refusal, 64-shot burst and canonical retirement passed\n";return 0;}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
