#if !defined(WOT_CLIENT_REPLICA_HOST_BUILD)
#include "pch.hpp"
#endif
#include "stock_native_client_state.hpp"
#include "stock_native_bot_python.hpp"
#include <algorithm>
#include <set>
#include <unordered_set>
#include <cctype>
namespace stock_native_client_state {
namespace p=stock_native_bot_python;
ObjectFields::ObjectFields(const ObjectFields&v):fields_(v.fields_?std::make_unique<Map>(*v.fields_):nullptr){}
ObjectFields& ObjectFields::operator=(const ObjectFields&v){if(this!=&v){ObjectFields copy(v);fields_.swap(copy.fields_);}return *this;}
ObjectFields::ObjectFields(ObjectFields&&)noexcept=default;
ObjectFields& ObjectFields::operator=(ObjectFields&&)noexcept=default;
ObjectFields::Map& ObjectFields::writable(){if(!fields_)fields_=std::make_unique<Map>();return *fields_;}
ObjectFields::Map& ObjectFields::emptyFields(){static Map empty;return empty;}
Value& ObjectFields::operator[](const std::string&key){return writable()[key];}
std::pair<ObjectFields::iterator,bool> ObjectFields::emplace(std::string key,Value value){return writable().emplace(std::move(key),std::move(value));}
ObjectFields::iterator ObjectFields::begin(){return fields_?fields_->begin():emptyFields().end();}
ObjectFields::iterator ObjectFields::end(){return fields_?fields_->end():emptyFields().end();}
ObjectFields::const_iterator ObjectFields::begin()const{return fields_?fields_->cbegin():emptyFields().cend();}
ObjectFields::const_iterator ObjectFields::end()const{return fields_?fields_->cend():emptyFields().cend();}
ObjectFields::iterator ObjectFields::find(const std::string&key){return fields_?fields_->find(key):emptyFields().end();}
ObjectFields::const_iterator ObjectFields::find(const std::string&key)const{return fields_?fields_->find(key):emptyFields().cend();}
std::size_t ObjectFields::count(const std::string&key)const{return fields_?fields_->count(key):0;}
std::size_t ObjectFields::size()const noexcept{return fields_?fields_->size():0;}
bool ObjectFields::empty()const noexcept{return !fields_||fields_->empty();}
std::size_t ObjectFields::erase(const std::string&key){return fields_?fields_->erase(key):0;}
void ObjectFields::clear()noexcept{if(fields_)fields_->clear();}
bool ObjectFields::operator==(const ObjectFields&v)const{if(empty()||v.empty())return empty()&&v.empty();return *fields_==*v.fields_;}
namespace {
const Value null;
Value boolean(bool v){Value x;x.kind=Value::Kind::Boolean;x.boolean=v;return x;}
Value number(double v){Value x;x.kind=Value::Kind::Number;x.number=v;return x;}
Value integer(std::int64_t v){Value x;x.kind=Value::Kind::Integer;x.integer=v;return x;}
Value text(std::string v){Value x;x.kind=Value::Kind::Text;x.text=std::move(v);return x;}
Value object(){Value x;x.kind=Value::Kind::Object;return x;}
Value poseValue(const Pose&v){auto out=object();unsigned i=0;for(const char*n:{"x","y","z","yaw","pitch","roll","aim_yaw","gun_pitch"})out.object[n]=number(v[i++]);return out;}
const std::vector<Value>& rows(const Value&v){static const std::vector<Value> empty;return v.kind==Value::Kind::Array?v.array:empty;}
std::optional<std::int64_t> exact(const Value&v,double maximum=1.e16){if(v.kind==Value::Kind::Text){try{std::size_t end=0;const auto n=std::stoll(v.text,&end);for(;end<v.text.size();++end)if(!std::isspace(static_cast<unsigned char>(v.text[end])))return {};if(n<0||double(n)>maximum)return {};return n;}catch(...){return {};}}if(v.kind!=Value::Kind::Integer&&v.kind!=Value::Kind::Number)return {};const double n=v.numeric(-1.);if(!std::isfinite(n)||n<0.||n>maximum||n>=9223372036854775808.||std::floor(n)!=n)return {};return std::int64_t(n);}
std::optional<Pose> pose(const Value&raw){const auto&s=raw.resolved();if(s.kind!=Value::Kind::Object||!s.object.count("x")||!s.object.count("y")||!s.object.count("z"))return {};Pose out{};unsigned i=0;for(const char*n:{"x","y","z","yaw","pitch","roll","aim_yaw","gun_pitch"})out[i++]=s.get(n).numeric();if(!s.object.count("aim_yaw"))out[6]=out[3];return out;}
std::string key(const std::string&kind,const Value&raw){const auto&s=raw.resolved();const auto&id=s.get("id");if(s.kind!=Value::Kind::Object||id.kind==Value::Kind::Null)return {};if(id.kind==Value::Kind::Text)return kind+":"+id.text;return kind+":"+std::to_string(std::int64_t(id.numeric()));}
Value base(const Actor&a,const char*kind){auto out=object();out.object["type"]=text(kind);out.object["entity"]=text(a.key);out.object["kind"]=text(a.kind);out.object["id"]=integer(a.id);return out;}
void emit(std::vector<Event>&out,Value v){out.push_back({std::move(v)});}
std::shared_ptr<Actor> install(State&s,const std::string&kind,const Value&state,std::vector<Event>&events){auto name=key(kind,state);if(name.empty())return {};auto&entry=s.actors[name];if(!entry){entry=std::make_shared<Actor>();entry->key=name;entry->kind=kind;entry->id=std::int64_t(state.get("id").numeric());entry->local=kind=="player"&&entry->id==s.local_player_id;auto event=base(*entry,"create");event.object["state"]=state;emit(events,std::move(event));}return entry;}
// Python materialization owns stock lifecycle/health/UI edges. Accepted motion,
// checkpoint/frontier data and running clocks remain canonical in Actor.state;
// their native consumers ingest every snapshot independently of this decision.
// Both field maps are sorted by name: walk them together instead of looking
// every name up in the other map. Missing fields compare as null, as get() does.
template<class Changed> bool anyFieldChanged(const ObjectFields& a, const ObjectFields& b, Changed changed){
    auto ia=a.begin(),ea=a.end(),ib=b.begin(),eb=b.end();
    while(ia!=ea||ib!=eb){
        if(ib==eb||(ia!=ea&&ia->first<ib->first)){if(changed(ia->first,ia->second,null))return true;++ia;}
        else if(ia==ea||ib->first<ia->first){if(changed(ib->first,null,ib->second))return true;++ib;}
        else{if(changed(ia->first,ia->second,ib->second))return true;++ia;++ib;}
    }
    return false;
}
bool presentationObjectChanged(const Value&first,const Value&second,const std::set<std::string>&clocks){
    const auto&a=first.resolved();const auto&b=second.resolved();
    if(a.kind!=Value::Kind::Object||b.kind!=Value::Kind::Object)return a!=b;
    return anyFieldChanged(a.object,b.object,[&](const std::string&name,const Value&x,const Value&y){return !clocks.count(name)&&x!=y;});
}
bool equipmentPresentationChanged(const Value&first,const Value&second){
    const auto&a=first.resolved();const auto&b=second.resolved();
    if(a.kind!=Value::Kind::Array||b.kind!=Value::Kind::Array)return a!=b;
    if(a.array.size()!=b.array.size())return true;
    static const std::set<std::string>clocks={"cooldownTimeLeft","autoPendingElapsed","aiPendingElapsed"};
    for(std::size_t i=0;i<a.array.size();++i)if(presentationObjectChanged(a.array[i],b.array[i],clocks))return true;
    return false;
}
bool presentationChanged(const Value&first,const Value&second){
    const auto&a=first.resolved();const auto&b=second.resolved();
    if(a.kind!=Value::Kind::Object||b.kind!=Value::Kind::Object)return a!=b;
    if(&a==&b)return false;
    static const std::unordered_set<std::string>native={
        "x","y","z","yaw","pitch","roll","aim_yaw","gun_pitch","speed","movement_dir","rotation_dir","airborne","vertical_speed","ram_vx","ram_vy","ram_vz","ram_motion","ram_motion_seq","contact_push_acks","push_x","push_z","push_yaw","input_seq","last_input_seq",
        "forward","turn","up_cosine","reload_time","siege_time_left_ms","combat_fire_elapsed","combat_fire_timer","gun_checkpoint","gun_checkpoint_seq",
        "input_processed_seq","landing_observation_seq","ram_contact_admitted_seq","ram_contact_resolved_seq","destructible_contact_admitted_seq","destructible_contact_resolved_seq","destructible_contact_resolved_seqs","destructible_contact_rejected_seqs","ram_contact","ram_contacts","destructible_contacts"};
    // Unknown fields stay conservative: only the reviewed native consumers
    // above bypass Python. Fire/death/critical/stock descriptor edges remain.
    return anyFieldChanged(a.object,b.object,[&](const std::string&name,const Value&x,const Value&y){
        if(native.count(name))return false;
        return name=="equipment_states"?equipmentPresentationChanged(x,y):x!=y;});
}
void liveAnchor(Actor&a,double time,double now){if(!a.has_target&&!a.has_current)return;const auto v=a.has_target?a.target:a.current;a.target=v;a.has_target=true;a.target_time=now;a.sample_time=time;a.previous_sample=time;a.anchor_time=time;a.anchor_local=now;a.has_anchor=true;a.snapshot_interval.reset();a.resume=false;a.has_resume_sample=false;a.render_delta=0.;a.has_observed=a.has_ideal=a.has_delay=false;a.warmup_intervals=0;a.warmup_active=true;a.samples={Sample{time,{},v,a.state}};a.cursor=time;a.teleport=false;a.timed=true;}
bool setTarget(Actor&a,const Pose&v,double now,std::optional<std::int64_t> sample,bool live,std::optional<std::int64_t> revision){
    const auto previous=a.target;const auto previous_sample=a.previous_sample;const bool had=a.has_target;const bool timed=bool(sample);double delta=0.;
    if(timed&&had&&previous_sample&&*sample>*previous_sample)delta=std::max(.000001,std::min((*sample-*previous_sample)/1000000.,.2));
    else if(!timed&&had&&now>a.target_time)delta=std::max(.01,std::min(now-a.target_time,.25));
    a.velocity={};if(delta>0.){double speed=0.;for(unsigned i=0;i<3;++i){a.velocity[i]=(v[i]-previous[i])/delta;speed+=a.velocity[i]*a.velocity[i];}speed=std::sqrt(speed);if(speed>80.)for(unsigned i=0;i<3;++i)a.velocity[i]*=80./speed;}
    const bool stale=live&&timed&&had&&previous_sample&&*sample-*previous_sample>200000.;a.teleport=false;
    if(timed&&had&&previous_sample&&*sample>*previous_sample&&!stale){double distance=0.;for(unsigned i=0;i<3;++i)distance+=std::pow(v[i]-previous[i],2.);distance=std::sqrt(distance);a.teleport=distance>25.&&distance/((*sample-*previous_sample)/1000000.)>80.;}
    if(timed){
        if(stale){a.has_ideal=a.has_delay=false;a.warmup_intervals=0;a.warmup_active=true;a.samples.clear();a.resume=false;a.has_resume_sample=false;a.cursor=double(*sample);}
        if(had&&previous_sample&&!stale){
            ++a.warmup_intervals;const double interval=*sample-*previous_sample;double exposure=a.snapshot_interval.value_or(0.);if(interval>100000.&&exposure>100000.)exposure=1000000./30.;
            double observed_interval=interval;if(a.coalesced_timing.kind==Value::Kind::Object){const double measured=a.coalesced_timing.get("source_interval_us").numeric(),measured_exposure=a.coalesced_timing.get("snapshot_interval_us").numeric();if(measured>0.&&measured<=interval){observed_interval=measured;if(measured_exposure>0.)exposure=std::min(exposure,measured_exposure);}}a.coalesced_timing=Value{};
            a.observed=observed_interval+exposure;if(a.warmup_active&&a.warmup_intervals<=3)a.observed+=std::min(20000.,observed_interval/3.);a.has_observed=true;
            const bool recovery=a.resume&&a.render_delta<.1;const double retained=a.has_ideal?std::max(60000.,a.ideal-interval*(recovery?.5:a.ideal>120000.?.06:.005)):90000.;a.ideal=std::max(a.observed,retained);a.has_ideal=true;
            if(!a.has_delay){a.delay=a.ideal;a.has_delay=true;}else {const double growth=a.ideal-a.delay;const bool warm=a.warmup_active&&a.warmup_intervals<=3;if(growth>0.&&(warm||growth>=15000.||observed_interval>=100000.))a.delay=a.ideal;}
        }else if(!had||!previous_sample)a.has_ideal=false;
        a.samples.push_back(Sample{double(*sample),revision,v,a.state});
        if(a.receipts.empty()||a.receipts.back().revision!=revision)a.receipts.push_back(a.samples.back());
    }
    a.target=v;a.has_target=true;a.target_time=now;a.sample_time=sample?double(*sample):0.;a.previous_sample=sample?std::optional<double>(double(*sample)):std::nullopt;a.sample_revision=revision;a.timed=timed;
    if(!a.has_current||stale){a.current=v;a.has_current=true;return true;}return false;
}
void upsert(State&s,const std::string&kind,const Value&state,double now,std::vector<Event>&events,bool update,std::optional<std::int64_t> sample,std::optional<std::int64_t> motion,double anchor,bool live,std::optional<std::int64_t> revision,const Value&cadence){
    auto entry=install(s,kind,state,events);if(!entry)return;auto&a=*entry;const bool dirty=presentationChanged(a.state,state);const auto v=pose(state);const bool alive=state.get("alive").truth(true);
    // Pure native callers may donate an ordinary owned Value. Retain a frozen
    // copy once; the Python admission bridge already donates shared rows.
    if(state.reference)a.state=state;else{Value accepted;accepted.kind=Value::Kind::Shared;accepted.reference=std::make_shared<const Value>(state);a.state=std::move(accepted);}a.revision=++s.revision;a.speed=state.get("speed").numeric();a.drive=state.get("movement_dir").numeric();a.rotation=state.get("rotation_dir").numeric();a.alive=alive&&state.get("health").numeric(1.)>0.;a.airborne=state.get("airborne").truth();a.siege_state=std::int64_t(state.get("siege_state").numeric());a.missing=false;
    if(!alive){if(!a.dead){a.dead=true;a.settled=true;if(v){a.current=a.target=*v;a.has_current=a.has_target=true;}auto event=base(a,"destroy");event.object["reason"]=text("dead");event.object["keep_corpse"]=boolean(true);event.object["state"]=state;emit(events,std::move(event));return;}
        if(v&&a.has_current){a.target=*v;a.has_target=true;a.target_time=now;a.timed=false;}if(dirty){auto event=base(a,"update");event.object["state"]=state;event.object["pose"]=Value{};event.object["remote"]=boolean(true);emit(events,std::move(event));}return;}
    if(a.dead)return;
    if(a.local){if(v&&!a.has_current){a.current=*v;a.has_current=true;}if(dirty){auto event=base(a,"update");event.object["state"]=state;event.object["pose"]=v?poseValue(*v):Value{};event.object["correction"]=boolean(true);emit(events,std::move(event));}return;}
    if(sample){if(a.has_anchor&&motion&&*motion>a.anchor_time)a.snapshot_interval=*motion-a.anchor_time;a.anchor_time=double(motion.value_or(*sample));a.anchor_local=anchor;a.has_anchor=true;}
    a.coalesced_timing=cadence;const bool snapped=v&&(update||!a.has_target)&&setTarget(a,*v,now,sample,live,revision);
    if(dirty||snapped){auto event=base(a,"update");event.object["state"]=state;event.object["pose"]=snapped?poseValue(a.current):Value{};event.object["target"]=a.has_target?poseValue(a.target):Value{};event.object["remote"]=boolean(true);event.object["snap"]=boolean(snapped);emit(events,std::move(event));}
}
constexpr const char* capsule_name="offline.client.state";
void destroy(PyObject*object){auto*value=static_cast<std::shared_ptr<State>*>(PyCapsule_GetPointer(object,capsule_name));if(value)delete value;else PyErr_Clear();}
}
bool Value::operator==(const Value&v)const{if(reference&&reference==v.reference)return true;if(reference||v.reference)return resolved()==v.resolved();if(kind!=v.kind){if((kind==Kind::Integer||kind==Kind::Number)&&(v.kind==Kind::Integer||v.kind==Kind::Number))return numeric()==v.numeric();return false;}switch(kind){case Kind::Null:return true;case Kind::Boolean:return boolean==v.boolean;case Kind::Integer:return integer==v.integer;case Kind::Number:return number==v.number;case Kind::Text:return text==v.text;case Kind::Array:return array==v.array;case Kind::Object:return object==v.object;case Kind::Shared:return false;}return false;}
const Value& Value::get(const std::string&key)const{if(reference)return reference->get(key);auto i=object.find(key);return i==object.end()?null:i->second;}
double Value::numeric(double fallback)const{if(reference)return reference->numeric(fallback);if(kind==Kind::Integer)return double(integer);if(kind==Kind::Number)return std::isfinite(number)?number:fallback;if(kind==Kind::Boolean)return boolean?1.:0.;if(kind==Kind::Text)try{std::size_t end=0;const double n=std::stod(text,&end);return end==text.size()&&std::isfinite(n)?n:fallback;}catch(...){return fallback;}return fallback;}
bool Value::truth(bool fallback)const{if(reference)return reference->truth(fallback);switch(kind){case Kind::Null:return fallback;case Kind::Boolean:return boolean;case Kind::Integer:return integer!=0;case Kind::Number:return number!=0.;case Kind::Text:return !text.empty();case Kind::Array:return !array.empty();case Kind::Object:return !object.empty();case Kind::Shared:return fallback;}return fallback;}
Value read(PyObject*raw){Value out;if(raw==Py_None)return out;if(PyBool_Check(raw)){out.kind=Value::Kind::Boolean;out.boolean=raw==Py_True;return out;}
#if PY_MAJOR_VERSION<3
    if(PyInt_Check(raw)){out.kind=Value::Kind::Integer;out.integer=PyInt_AS_LONG(raw);return out;}
#endif
    if(PyLong_Check(raw)){out.kind=Value::Kind::Integer;out.integer=p::wide(raw);return out;}if(PyFloat_Check(raw)){out.kind=Value::Kind::Number;out.number=PyFloat_AS_DOUBLE(raw);return out;}
    if(PyDict_Check(raw)){out.kind=Value::Kind::Object;Py_ssize_t cursor=0;PyObject*key=nullptr,*value=nullptr;while(PyDict_Next(raw,&cursor,&key,&value))out.object.emplace(p::string(key),read(value));return out;}
    if(PyList_Check(raw)||PyTuple_Check(raw)){out.kind=Value::Kind::Array;const auto count=PySequence_Size(raw);out.array.reserve(std::size_t(count));for(Py_ssize_t i=0;i<count;++i){p::Ref v(PySequence_GetItem(raw,i));out.array.push_back(read(v.get()));}return out;}
    out.kind=Value::Kind::Text;out.text=p::string(raw);return out;
}
PyObject* write(const Value&v){if(v.reference)return write(*v.reference);switch(v.kind){case Value::Kind::Null:return p::retain(Py_None).release();case Value::Kind::Boolean:return p::boolean(v.boolean).release();case Value::Kind::Integer:return p::integer(v.integer).release();case Value::Kind::Number:return p::number(v.number).release();case Value::Kind::Text:return p::text(v.text).release();case Value::Kind::Array:{auto out=p::list();for(const auto&x:v.array)p::append(out.get(),p::Ref(write(x)));return out.release();}case Value::Kind::Object:{auto out=p::dict();for(const auto&x:v.object)p::set(out.get(),x.first.c_str(),p::Ref(write(x.second)));return out.release();}case Value::Kind::Shared:return p::retain(Py_None).release();}return p::retain(Py_None).release();}
void State::reset(Value value){actors.clear();round=std::move(value);++epoch;++revision;sequence.reset();bot_revision.reset();motion_time.reset();bot_time.reset();order_revision.reset();server_time_ms.reset();received_at=0.;timed_bots.reset();timing_phase.clear();reset_pending=false;last_advance.reset();}
const Actor* State::find(const std::string&key)const{auto i=actors.find(key);return i==actors.end()?nullptr:i->second.get();}
Actor* State::find(const std::string&key){auto i=actors.find(key);return i==actors.end()?nullptr:i->second.get();}
bool State::updateLocal(const Pose&v,const Point&velocity,double speed){if(retired)return false;auto*a=find("player:"+std::to_string(local_player_id));if(!a||!a->local)return false;a->current=v;a->has_current=true;std::copy(velocity.begin(),velocity.end(),a->velocity.begin());a->speed=speed;++a->revision;return true;}
std::vector<Event> manifest(State&s,const Value&message){std::vector<Event> events;const auto&round=message.get("round_id");if(round.kind!=Value::Kind::Null&&round!=s.round)s.reset(round);for(const char*kind:{"player","bot"})for(const auto&row:rows(message.get(std::string(kind)=="player"?"players":"bots"))){auto a=install(s,kind,row,events);if(a)a->manifest=row;}return events;}
std::vector<Event> snapshot(State&s,const Value&message,double now){
    std::vector<Event> events;if(s.retired)return events;const auto round=message.object.count("round_id")?message.get("round_id"):s.round;if(s.round.kind!=Value::Kind::Null&&round!=s.round)return events;
    const auto seq=exact(message.object.count("sequence")?message.get("sequence"):message.get("server_tick"));if(seq&&s.sequence&&*seq<=*s.sequence)return events;
    const bool has_revision=message.object.count("bot_state_revision");auto revision=has_revision?exact(message.get("bot_state_revision"),9.223372036854775e18):std::optional<std::int64_t>{};if(has_revision&&!revision)throw p::Error("bot_state_revision is invalid");if(revision&&s.bot_revision&&*revision<*s.bot_revision)throw p::Error("bot_state_revision regressed");if(!has_revision&&s.bot_revision)throw p::Error("bot_state_revision disappeared");const bool update=!revision||!s.bot_revision||*revision>*s.bot_revision;
    const bool timed=message.object.count("motion_time_us"),has_bot=message.object.count("bot_state_time_us");if(timed!=has_bot)throw p::Error("bot pose timing is incomplete");auto motion=timed?exact(message.get("motion_time_us")):std::optional<std::int64_t>{},bot=timed?exact(message.get("bot_state_time_us")):std::optional<std::int64_t>{};
    if(timed){if(!motion||!bot||*bot>*motion)throw p::Error("bot pose timing is invalid");if(s.motion_time&&*motion<*s.motion_time)throw p::Error("motion time regressed");if(s.bot_time&&*bot<*s.bot_time)throw p::Error("bot state time regressed");}else if(s.timed_bots.value_or(false))throw p::Error("bot pose timing disappeared");
    if(seq)s.sequence=seq;if(revision)s.bot_revision=revision;if(timed){s.motion_time=motion;s.bot_time=bot;s.timed_bots=true;}else if(!s.timed_bots)s.timed_bots=false;
    const auto phase=message.get("timing").get("phase").text;const bool entered=phase=="battle"&&(s.timing_phase=="loading"||s.timing_phase=="prebattle");if(entered)s.reset_pending=true;bool live=s.reset_pending&&timed&&update;if(phase=="loading"||phase=="prebattle"||phase=="battle"||phase=="finished")s.timing_phase=phase;
    const double anchor=now-std::max(0.,message.get("_client_dispatch_delay").numeric());if(auto server=exact(message.get("server_time_ms"),2147483647.)){s.server_time_ms=server;s.received_at=anchor;}if(entered&&timed&&!update){bool anchored=false;for(auto&entry:s.actors){auto&a=*entry.second;if(a.kind=="bot"&&!a.dead&&(a.has_target||a.has_current)){liveAnchor(a,double(*motion),anchor);anchored=true;}}if(anchored){s.reset_pending=false;live=false;}}
    std::set<std::string> seen;for(const char*kind:{"player","bot"})for(const auto&row:rows(message.get(std::string(kind)=="player"?"players":"bots"))){const auto name=key(kind,row);if(!name.empty())seen.insert(name);const bool is_bot=std::string(kind)=="bot";upsert(s,kind,row,now,events,!is_bot||update,is_bot?bot:std::optional<std::int64_t>{},is_bot?motion:std::optional<std::int64_t>{},anchor,is_bot&&live,is_bot?revision:std::optional<std::int64_t>{},message.get("_client_coalesced_timing"));}
    if(live)s.reset_pending=false;for(auto&entry:s.actors){auto&a=*entry.second;if(!seen.count(entry.first)&&!a.dead){a.dead=true;a.missing=true;auto event=base(a,"destroy");event.object["reason"]=text("missing");event.object["keep_corpse"]=boolean(false);emit(events,std::move(event));}}
    const auto order_revision=exact(message.get("bot_order_revision"));const auto&orders=message.get("bot_orders");if(orders.kind!=Value::Kind::Null&&(!s.order_revision||!order_revision||*order_revision>*s.order_revision)){s.order_revision=order_revision;for(const auto&order:rows(orders))if(order.kind==Value::Kind::Object){auto event=object();event.object["type"]=text("order");event.object["order"]=order;event.object["revision"]=order_revision?integer(*order_revision):Value{};emit(events,std::move(event));}}
    return events;
}
PyObject* capsule(const std::shared_ptr<State>&value){return PyCapsule_New(new std::shared_ptr<State>(value),capsule_name,destroy);}
std::shared_ptr<State> shared(PyObject*object){if(!object||object==Py_None)return {};auto*value=static_cast<std::shared_ptr<State>*>(PyCapsule_GetPointer(object,capsule_name));if(!value)throw p::Error("Native client state handle is invalid");return *value;}
std::shared_ptr<State> fromBattle(PyObject*battle){auto value=p::value(battle,"_native_client_state");return shared(value.get());}
}
