#include "stock_native_bot_publication_python.hpp"
#include "stock_native_bot_python.hpp"
#include "stock_native_bot_combat_snapshot.hpp"

namespace stock_native_bot {
namespace {
namespace p=stock_native_bot_python;
const char* kind(Key k){return p::kind(k.kind==offline_simulation::ActorKind::Human);}
p::Ref identity(Key k){auto out=p::dict();p::set(out.get(),"kind",p::text(kind(k)));p::set(out.get(),"id",p::integer(k.id));return out;}
p::Ref proof(const std::vector<double>&v){
    if(v.size()!=14)throw p::Error("Native artillery launch proof width mismatch");
    const char* kinds[]={"","bot","human"};const int target=int(v[1]);if(target<0||target>2)throw p::Error("Native artillery target kind invalid");
    p::Ref out(PyTuple_New(13));
    auto put=[&](int i,p::Ref value){PyTuple_SET_ITEM(out.get(),i,value.release());};
    put(0,p::text("launch"));put(1,p::integer(std::int64_t(v[0])));put(2,p::text(kinds[target]));
    for(int i=2;i<5;++i)put(i+1,p::integer(std::int64_t(v[i])));
    p::Ref origin(PyTuple_New(3));for(int i=0;i<3;++i)PyTuple_SET_ITEM(origin.get(),i,p::number(v[i+5]).release());put(6,std::move(origin));
    for(int i=8;i<14;++i)put(i-1,p::number(v[i]));return out;
}
p::Ref launch(const offline_simulation::weapons::LaunchRecord&v){
    auto out=p::dict();auto*d=out.get();const auto&e=v.edge;
    p::set(d,"id",p::integer(v.actor.id));p::set(d,"fire_seq",p::integer(e.shot_seq));p::set(d,"shell_index",p::integer(e.shell));
    p::set(d,"shot_yaw",p::number(v.yaw));p::set(d,"shot_pitch",p::number(v.pitch));p::set(d,"class_tag",p::text(v.class_tag));
    p::set(d,"burst_group_seq",p::integer(e.group_seq));p::set(d,"burst_index",p::integer(e.index));p::set(d,"burst_count",p::integer(e.count));
    p::set(d,"launch_time_us",p::integer(v.launch_time_us));p::set(d,"launch_pose",p::tupleNumbers(std::array<double,6>{{v.pose.position[0],v.pose.position[1],v.pose.position[2],v.pose.yaw,v.pose.pitch,v.pose.roll}}));
    if(v.has_origin)p::set(d,"shot_origin",p::tupleNumbers(v.origin));
    if(v.artillery){p::set(d,"shot_velocity",p::tupleNumbers(v.velocity));p::set(d,"shot_gravity",p::number(v.gravity));p::set(d,"shot_max_distance",p::number(v.maximum_distance));p::set(d,"shot_max_time_ms",p::integer(v.maximum_time_ms));p::set(d,"shot_proof_key",proof(v.proof));}
    if(v.has_shells_before)p::set(d,"shells_before_shot",p::integer(v.shells_before));return out;
}
p::Ref ram(const contacts::Report&v){
    auto out=p::dict();auto*d=out.get();p::set(d,"type",p::text("bot_ram"));p::set(d,"bot_id",p::integer(v.bot));
    p::set(d,"target_kind",p::text(p::kind(v.target_human)));p::set(d,"target_id",p::integer(v.target));p::set(d,"ram_seq",p::integer(v.sequence));
    p::set(d,"damage_to_bot",p::integer(v.damage_bot));p::set(d,"damage_to_target",p::integer(v.damage_target));
    if(!v.bot_tracks.empty()||!v.target_tracks.empty()){p::set(d,"track_contacts_bot",p::strings(v.bot_tracks));p::set(d,"track_contacts_target",p::strings(v.target_tracks));p::set(d,"track_impact_speed",p::number(std::max(0.,v.closing_speed)));}
    if(v.target_human){p::set(d,"ram_contact_player_id",p::integer(v.target));p::set(d,"ram_contact_seq",p::integer(v.player_receipt));}
    else p::set(d,"contact_positions",p::numbers(v.positions));return out;
}
p::Ref packObservation(const observation::Observation&v){
    auto out=p::dict();auto*d=out.get();p::set(d,"type",p::text("bot_observation"));auto contacts=p::list();
    for(const auto&c:v.contacts){auto row=p::dict();auto*r=row.get();
        p::set(r,"observing_team",p::integer(c.observing_team));p::set(r,"target_kind",p::text(kind(c.target)));p::set(r,"target_id",p::integer(c.target.id));p::set(r,"target_team",p::integer(c.target_team));
        p::set(r,"visible",p::boolean(c.visible));p::set(r,"fresh",p::boolean(c.fresh));p::set(r,"time_left",p::number(c.time_left));auto recipients=p::list();
        for(const auto&a:c.radio_recipients){auto x=identity(a.actor);p::set(x.get(),"time_left",p::number(a.time_left));p::append(recipients.get(),x);}p::set(r,"radio_recipients",recipients);
        p::set(r,"visible_by_player_ids",p::integers(c.visible_by_player_ids));p::set(r,"visible_by_bot_ids",p::integers(c.visible_by_bot_ids));
        p::set(r,"shootable_by_bot_ids",p::integers(c.shootable_by_bot_ids));p::set(r,"threatened_bot_ids",p::integers(c.threatened_bot_ids));
        p::set(r,"x",p::number(c.position[0]));p::set(r,"y",p::number(c.position[1]));p::set(r,"z",p::number(c.position[2]));
        p::set(r,"health",p::integer(c.health));p::set(r,"max_health",p::integer(c.max_health));p::set(r,"class_tag",p::text(c.class_tag));p::set(r,"armor",p::number(c.armor));p::append(contacts.get(),row);
    }p::set(d,"contacts",contacts);auto links=p::list();
    for(const auto&l:v.radio_links){auto row=identity(l.actor),allies=p::list();for(const auto&k:l.allies)p::append(allies.get(),identity(k));p::set(row.get(),"allies",allies);p::append(links.get(),row);}p::set(d,"radio_links",links);
    auto ranges=p::list();for(const auto&r:v.player_vision_ranges){auto row=p::dict();p::set(row.get(),"id",p::integer(r.first));p::set(row.get(),"radius",p::number(r.second));p::append(ranges.get(),row);}p::set(d,"player_vision_ranges",ranges);
    auto affordances=p::list();for(const auto&a:v.affordances){auto row=p::dict();p::set(row.get(),"bot_id",p::integer(a.bot_id));p::set(row.get(),"target_kind",p::text(a.target_kind));p::set(row.get(),"target_id",p::integer(a.target_id));auto candidates=p::list();
        for(const auto&c:a.candidates){auto x=p::dict();p::set(x.get(),"id",p::text(c.id));p::set(x.get(),"position",p::numbers(c.position));p::set(x.get(),"peek_position",p::numbers(c.peek_position));
            const char*names[]={"travel_distance","route_alignment","enemy_occlusion","exposure","slope","water","ally_congestion"};const double values[]={c.travel_distance,c.route_alignment,c.enemy_occlusion,c.exposure,c.slope,c.water,c.ally_congestion};
            for(unsigned i=0;i<7;++i)p::set(x.get(),names[i],p::number(values[i]));p::set(x.get(),"peek_feasible",p::boolean(c.peek_feasible));p::set(x.get(),"escape_feasible",p::boolean(c.escape_feasible));p::append(candidates.get(),x);
        }p::set(row.get(),"candidates",candidates);p::append(affordances.get(),row);
    }p::set(d,"affordances",affordances);return out;
}
}
PyObject* packNativeOutputs(const std::vector<Output>&values,std::int64_t horizon){try{
    auto out=p::list();for(const auto&v:values){const auto&s=v.state;auto row=p::dict();auto*d=row.get();p::set(d,"type",p::text("bot_state"));auto rows=p::list();
        for(const auto&r:s.rows)p::append(rows.get(),p::integers(r));p::set(d,"rows",rows);p::set(d,"sample_time_us",p::integer(s.sample_us));p::set(d,"edge_sample_time_us",p::integer(s.edge_sample_us));p::set(d,"edge_revision",p::integer(s.edge_revision));p::set(d,"source_batch_horizon_us",p::integer(horizon));
        auto push=p::dict(),contacts=p::list();for(const auto&c:v.pushes){auto x=p::dict();p::set(x.get(),"seq",p::integer(c.sequence));p::set(x.get(),"attacker_kind",p::text(p::kind(c.attacker_human)));p::set(x.get(),"attacker_id",p::integer(c.attacker));p::set(x.get(),"target_kind",p::text(p::kind(c.target_human)));p::set(x.get(),"target_id",p::integer(c.target));p::append(contacts.get(),x);}p::set(push.get(),"contacts",contacts);p::set(push.get(),"hazards",p::integers(v.hazards));p::set(push.get(),"environment_fires",p::integers(v.environment_fires));
        auto falls=p::list();for(const auto&f:v.falls)p::append(falls.get(),p::integers(f));p::set(push.get(),"falls",falls);auto critical=p::list();
        for(const auto&c:v.critical){auto x=p::list();p::append(x.get(),p::integer(c.actor));p::append(x.get(),p::text(c.cause));p::append(x.get(),p::integer(c.mask));p::append(x.get(),p::integer(c.count));p::append(critical.get(),x);}p::set(push.get(),"critical_totals",critical);p::set(d,"push_contacts",push);
        if(!s.launches.empty()){auto launches=p::list();for(const auto&l:s.launches)p::append(launches.get(),launch(l));p::set(d,"launches",launches);}p::append(out.get(),row);
        for(const auto&r:s.reports)p::append(out.get(),ram(r));if(v.observation)p::append(out.get(),packObservation(*v.observation));
    }return out.release();
}catch(const std::exception&e){if(!PyErr_Occurred())PyErr_SetString(PyExc_ValueError,e.what());return nullptr;}}
}
